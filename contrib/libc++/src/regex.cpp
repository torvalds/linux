//===-------------------------- regex.cpp ---------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "regex"
#include "algorithm"
#include "iterator"

_LIBCPP_BEGIN_NAMESPACE_STD

static
const char*
make_error_type_string(regex_constants::error_type ecode)
{
    switch (ecode)
    {
    case regex_constants::error_collate:
        return "The expression contained an invalid collating element name.";
    case regex_constants::error_ctype:
        return "The expression contained an invalid character class name.";
    case regex_constants::error_escape:
        return "The expression contained an invalid escaped character, or a "
               "trailing escape.";
    case regex_constants::error_backref:
        return "The expression contained an invalid back reference.";
    case regex_constants::error_brack:
        return "The expression contained mismatched [ and ].";
    case regex_constants::error_paren:
        return "The expression contained mismatched ( and ).";
    case regex_constants::error_brace:
        return "The expression contained mismatched { and }.";
    case regex_constants::error_badbrace:
        return "The expression contained an invalid range in a {} expression.";
    case regex_constants::error_range:
        return "The expression contained an invalid character range, "
               "such as [b-a] in most encodings.";
    case regex_constants::error_space:
        return "There was insufficient memory to convert the expression into "
               "a finite state machine.";
    case regex_constants::error_badrepeat:
        return "One of *?+{ was not preceded by a valid regular expression.";
    case regex_constants::error_complexity:
        return "The complexity of an attempted match against a regular "
               "expression exceeded a pre-set level.";
    case regex_constants::error_stack:
        return "There was insufficient memory to determine whether the regular "
               "expression could match the specified character sequence.";
    case regex_constants::__re_err_grammar:
        return "An invalid regex grammar has been requested.";
    case regex_constants::__re_err_empty:
        return "An empty regex is not allowed in the POSIX grammar.";
    default:
        break;
    }
    return "Unknown error type";
}

regex_error::regex_error(regex_constants::error_type ecode)
    : runtime_error(make_error_type_string(ecode)),
      __code_(ecode)
{}

regex_error::~regex_error() throw() {}

namespace {

struct collationnames
{
    const char* elem_;
    char char_;
};

const collationnames collatenames[] =
{
    {"A", 0x41},
    {"B", 0x42},
    {"C", 0x43},
    {"D", 0x44},
    {"E", 0x45},
    {"F", 0x46},
    {"G", 0x47},
    {"H", 0x48},
    {"I", 0x49},
    {"J", 0x4a},
    {"K", 0x4b},
    {"L", 0x4c},
    {"M", 0x4d},
    {"N", 0x4e},
    {"NUL", 0x00},
    {"O", 0x4f},
    {"P", 0x50},
    {"Q", 0x51},
    {"R", 0x52},
    {"S", 0x53},
    {"T", 0x54},
    {"U", 0x55},
    {"V", 0x56},
    {"W", 0x57},
    {"X", 0x58},
    {"Y", 0x59},
    {"Z", 0x5a},
    {"a", 0x61},
    {"alert", 0x07},
    {"ampersand", 0x26},
    {"apostrophe", 0x27},
    {"asterisk", 0x2a},
    {"b", 0x62},
    {"backslash", 0x5c},
    {"backspace", 0x08},
    {"c", 0x63},
    {"carriage-return", 0x0d},
    {"circumflex", 0x5e},
    {"circumflex-accent", 0x5e},
    {"colon", 0x3a},
    {"comma", 0x2c},
    {"commercial-at", 0x40},
    {"d", 0x64},
    {"dollar-sign", 0x24},
    {"e", 0x65},
    {"eight", 0x38},
    {"equals-sign", 0x3d},
    {"exclamation-mark", 0x21},
    {"f", 0x66},
    {"five", 0x35},
    {"form-feed", 0x0c},
    {"four", 0x34},
    {"full-stop", 0x2e},
    {"g", 0x67},
    {"grave-accent", 0x60},
    {"greater-than-sign", 0x3e},
    {"h", 0x68},
    {"hyphen", 0x2d},
    {"hyphen-minus", 0x2d},
    {"i", 0x69},
    {"j", 0x6a},
    {"k", 0x6b},
    {"l", 0x6c},
    {"left-brace", 0x7b},
    {"left-curly-bracket", 0x7b},
    {"left-parenthesis", 0x28},
    {"left-square-bracket", 0x5b},
    {"less-than-sign", 0x3c},
    {"low-line", 0x5f},
    {"m", 0x6d},
    {"n", 0x6e},
    {"newline", 0x0a},
    {"nine", 0x39},
    {"number-sign", 0x23},
    {"o", 0x6f},
    {"one", 0x31},
    {"p", 0x70},
    {"percent-sign", 0x25},
    {"period", 0x2e},
    {"plus-sign", 0x2b},
    {"q", 0x71},
    {"question-mark", 0x3f},
    {"quotation-mark", 0x22},
    {"r", 0x72},
    {"reverse-solidus", 0x5c},
    {"right-brace", 0x7d},
    {"right-curly-bracket", 0x7d},
    {"right-parenthesis", 0x29},
    {"right-square-bracket", 0x5d},
    {"s", 0x73},
    {"semicolon", 0x3b},
    {"seven", 0x37},
    {"six", 0x36},
    {"slash", 0x2f},
    {"solidus", 0x2f},
    {"space", 0x20},
    {"t", 0x74},
    {"tab", 0x09},
    {"three", 0x33},
    {"tilde", 0x7e},
    {"two", 0x32},
    {"u", 0x75},
    {"underscore", 0x5f},
    {"v", 0x76},
    {"vertical-line", 0x7c},
    {"vertical-tab", 0x0b},
    {"w", 0x77},
    {"x", 0x78},
    {"y", 0x79},
    {"z", 0x7a},
    {"zero", 0x30}
};

struct classnames
{
    const char* elem_;
    regex_traits<char>::char_class_type mask_;
};

const classnames ClassNames[] =
{
    {"alnum",  ctype_base::alnum},
    {"alpha",  ctype_base::alpha},
    {"blank",  ctype_base::blank},
    {"cntrl",  ctype_base::cntrl},
    {"d",      ctype_base::digit},
    {"digit",  ctype_base::digit},
    {"graph",  ctype_base::graph},
    {"lower",  ctype_base::lower},
    {"print",  ctype_base::print},
    {"punct",  ctype_base::punct},
    {"s",      ctype_base::space},
    {"space",  ctype_base::space},
    {"upper",  ctype_base::upper},
    {"w",      regex_traits<char>::__regex_word},
    {"xdigit", ctype_base::xdigit}
};

struct use_strcmp
{
    bool operator()(const collationnames& x, const char* y)
        {return strcmp(x.elem_, y) < 0;}
    bool operator()(const classnames& x, const char* y)
        {return strcmp(x.elem_, y) < 0;}
};

}

string
__get_collation_name(const char* s)
{
    const collationnames* i =
            _VSTD::lower_bound(begin(collatenames), end(collatenames), s, use_strcmp());
    string r;
    if (i != end(collatenames) && strcmp(s, i->elem_) == 0)
        r = char(i->char_);
    return r;
}

regex_traits<char>::char_class_type
__get_classname(const char* s, bool __icase)
{
    const classnames* i =
            _VSTD::lower_bound(begin(ClassNames), end(ClassNames), s, use_strcmp());
    regex_traits<char>::char_class_type r = 0;
    if (i != end(ClassNames) && strcmp(s, i->elem_) == 0)
    {
        r = i->mask_;
        if (r == regex_traits<char>::__regex_word)
            r |= ctype_base::alnum | ctype_base::upper | ctype_base::lower;
        else if (__icase)
        {
            if (r & (ctype_base::lower | ctype_base::upper))
                r |= ctype_base::alpha;
        }
    }
    return r;
}

template <>
void
__match_any_but_newline<char>::__exec(__state& __s) const
{
    if (__s.__current_ != __s.__last_)
    {
        switch (*__s.__current_)
        {
        case '\r':
        case '\n':
            __s.__do_ = __state::__reject;
            __s.__node_ = nullptr;
            break;
        default:
            __s.__do_ = __state::__accept_and_consume;
            ++__s.__current_;
            __s.__node_ = this->first();
            break;
        }
    }
    else
    {
        __s.__do_ = __state::__reject;
        __s.__node_ = nullptr;
    }
}

template <>
void
__match_any_but_newline<wchar_t>::__exec(__state& __s) const
{
    if (__s.__current_ != __s.__last_)
    {
        switch (*__s.__current_)
        {
        case '\r':
        case '\n':
        case 0x2028:
        case 0x2029:
            __s.__do_ = __state::__reject;
            __s.__node_ = nullptr;
            break;
        default:
            __s.__do_ = __state::__accept_and_consume;
            ++__s.__current_;
            __s.__node_ = this->first();
            break;
        }
    }
    else
    {
        __s.__do_ = __state::__reject;
        __s.__node_ = nullptr;
    }
}

_LIBCPP_END_NAMESPACE_STD
