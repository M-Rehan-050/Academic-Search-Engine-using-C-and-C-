#include <cpprest/http_listener.h>
#include <cpprest/json.h>
#include <cpprest/http_client.h>
#include <iostream>
#include <string>
#include <locale>
#include <codecvt>
#include <sstream>
#include <iomanip>
#include <vector>
#include <chrono>
#include <stack>

using namespace web::http::client;
using namespace web;
using namespace http;
using namespace http::experimental::listener;
using namespace std;
using namespace chrono;

const string GOOGLE_API_KEY = "AIzaSyCH6pdeifuA2mMQKx2FfdgUquTZV5vTTSk";
const string SEARCH_ENGINE_ID = "e5061dffdf8f24c60";

// Structure to store article data
struct Article {
    string title;
    string link;
    string snippet;
    system_clock::time_point publishDate;
    string type;

    Article(const string& t, const string& l, const string& s,
        const system_clock::time_point& p, const string& tp)
        : title(t), link(l), snippet(s), publishDate(p), type(tp) {
    }
};

// Global stack to store search history
stack<string> searchHistory;

// Utility Functions
string trim(const string& str) {
    size_t start = str.find_first_not_of(" \t\n\r");
    size_t end = str.find_last_not_of(" \t\n\r");
    return (start == string::npos || end == string::npos) ? "" : str.substr(start, end - start + 1);
}

string toStdString(const utility::string_t& wstr) {
    wstring_convert<codecvt_utf8_utf16<wchar_t>> conv;
    return conv.to_bytes(wstr);
}

utility::string_t toUtilityString(const string& str) {
    wstring_convert<codecvt_utf8_utf16<wchar_t>> conv;
    return conv.from_bytes(str);
}

system_clock::time_point parseDate(const string& dateString) {
    try {
        tm t = {};
        istringstream ss(dateString);
        ss >> get_time(&t, "%Y-%m-%dT%H:%M:%S");
        return system_clock::from_time_t(mktime(&t));
    }
    catch (...) {
        return {};
    }
}

vector<Article> fetchArticlesFromAPI(const string& query) {
    vector<Article> articles;

    string apiEndpoint = "https://www.googleapis.com/customsearch/v1";
    http_client client(toUtilityString(apiEndpoint));
    uri_builder builder(U(""));
    builder.append_query(U("q"), toUtilityString(query));
    builder.append_query(U("key"), toUtilityString(GOOGLE_API_KEY));
    builder.append_query(U("cx"), toUtilityString(SEARCH_ENGINE_ID));

    try {
        auto response = client.request(methods::GET, builder.to_string()).get();
        if (response.status_code() == status_codes::OK) {
            auto jsonResponse = response.extract_json().get();

            if (jsonResponse.has_field(U("items"))) {
                auto items = jsonResponse.at(U("items")).as_array();
                for (const auto& item : items) {
                    string title = toStdString(item.at(U("title")).as_string());
                    string link = toStdString(item.at(U("link")).as_string());
                    string snippet = item.has_field(U("snippet"))
                        ? toStdString(item.at(U("snippet")).as_string())
                        : "No snippet available";

                    system_clock::time_point publishDate = {};
                    if (item.has_field(U("pagemap")) &&
                        item.at(U("pagemap")).has_field(U("metatags"))) {
                        auto metaTags = item.at(U("pagemap")).at(U("metatags")).as_array();
                        for (const auto& meta : metaTags) {
                            if (meta.has_field(U("og:updated_time"))) {
                                publishDate = parseDate(toStdString(meta.at(U("og:updated_time")).as_string()));
                                break;
                            }
                        }
                    }

                    articles.emplace_back(title, link, snippet, publishDate, "document");
                }
            }
        }
    }
    catch (const exception& e) {
        cerr << "Error fetching data from API: " << e.what() << endl;
    }

    return articles;
}

vector<Article> filterArticles(const vector<Article>& articles, const string& filter) {
    vector<Article> filteredArticles;
    auto now = system_clock::now();

    for (const auto& article : articles) {
        auto duration = now - article.publishDate;

        if ((filter == "week" && duration <= hours(24 * 7)) ||
            (filter == "month" && duration <= hours(24 * 30)) ||
            (filter == "year" && duration <= hours(24 * 365)) ||
            (filter == "anytime") || article.publishDate == system_clock::time_point()) {
            filteredArticles.push_back(article);
        }
    }

    return filteredArticles;
}

json::value articlesToJson(const vector<Article>& articles) {
    json::value result = json::value::array();
    for (size_t i = 0; i < articles.size(); ++i) {
        json::value articleJson;
        articleJson[U("title")] = json::value::string(toUtilityString(articles[i].title));
        articleJson[U("link")] = json::value::string(toUtilityString(articles[i].link));
        articleJson[U("snippet")] = json::value::string(toUtilityString(articles[i].snippet));

        if (articles[i].publishDate.time_since_epoch().count() > 0) {
            auto time_t = system_clock::to_time_t(articles[i].publishDate);
            tm tm;
            gmtime_s(&tm, &time_t);
            stringstream ss;
            ss << put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
            articleJson[U("publishDate")] = json::value::string(toUtilityString(ss.str()));
        }
        else {
            articleJson[U("publishDate")] = json::value::string(U("Unknown Date"));
        }

        result[i] = articleJson;
    }
    return result;
}

void handleRequest(http_request request) {
    auto queryParams = uri::split_query(request.request_uri().query());
    string query = toStdString(queryParams[U("query")]);
    string filter = toStdString(queryParams[U("filter")]);

    if (!query.empty()) {
        searchHistory.push(query);
        vector<Article> articles = fetchArticlesFromAPI(query);
        vector<Article> filteredArticles = filterArticles(articles, filter);
        auto responseJson = articlesToJson(filteredArticles);
        request.reply(status_codes::OK, responseJson);
    }
    else {
        request.reply(status_codes::BadRequest, U("Missing query parameter"));
    }
}

void handleHistoryRequest(http_request request) {
    json::value historyJson = json::value::array();
    stack<string> tempStack = searchHistory; // Copy the stack to avoid modifying it

    size_t index = 0;
    while (!tempStack.empty()) {
        historyJson[index++] = json::value::string(toUtilityString(tempStack.top()));
        tempStack.pop();
    }

    request.reply(status_codes::OK, historyJson);
}

int main() {
    try {
        http_listener searchListener(U("http://localhost:8080/search"));
        searchListener.support(methods::GET, handleRequest);

        http_listener historyListener(U("http://localhost:8080/history"));
        historyListener.support(methods::GET, handleHistoryRequest);

        searchListener.open().then([]() {
            cout << "Search API running on http://localhost:8080/search" << endl;
            }).wait();

        historyListener.open().then([]() {
            cout << "History API running on http://localhost:8080/history" << endl;
            }).wait();

        string line;
        getline(cin, line);
    }
    catch (const exception& e) {
        cerr << "Error: " << e.what() << endl;
    }

    return 0;
}
