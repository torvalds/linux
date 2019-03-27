// Copyright (c) 2007 The NetBSD Foundation, Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
// 1. Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND
// CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
// INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
// IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY
// DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
// GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
// IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
// OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
// IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "atf-c++/detail/text.hpp"

extern "C" {
#include <regex.h>
}

#include <cctype>
#include <cstring>

extern "C" {
#include "atf-c/detail/text.h"
#include "atf-c/error.h"
}

#include "atf-c++/detail/exceptions.hpp"

namespace impl = atf::text;
#define IMPL_NAME "atf::text"

char*
impl::duplicate(const char* str)
{
    char* copy = new char[std::strlen(str) + 1];
    std::strcpy(copy, str);
    return copy;
}

bool
impl::match(const std::string& str, const std::string& regex)
{
    bool found;

    // Special case: regcomp does not like empty regular expressions.
    if (regex.empty()) {
        found = str.empty();
    } else {
        ::regex_t preg;

        if (::regcomp(&preg, regex.c_str(), REG_EXTENDED) != 0)
            throw std::runtime_error("Invalid regular expression '" + regex +
                                     "'");

        const int res = ::regexec(&preg, str.c_str(), 0, NULL, 0);
        regfree(&preg);
        if (res != 0 && res != REG_NOMATCH)
            throw std::runtime_error("Invalid regular expression " + regex);

        found = res == 0;
    }

    return found;
}

std::string
impl::to_lower(const std::string& str)
{
    std::string lc;
    for (std::string::const_iterator iter = str.begin(); iter != str.end();
         iter++)
        lc += std::tolower(*iter);
    return lc;
}

std::vector< std::string >
impl::split(const std::string& str, const std::string& delim)
{
    std::vector< std::string > words;

    std::string::size_type pos = 0, newpos = 0;
    while (pos < str.length() && newpos != std::string::npos) {
        newpos = str.find(delim, pos);
        if (newpos != pos)
            words.push_back(str.substr(pos, newpos - pos));
        pos = newpos + delim.length();
    }

    return words;
}

std::string
impl::trim(const std::string& str)
{
    std::string::size_type pos1 = str.find_first_not_of(" \t");
    std::string::size_type pos2 = str.find_last_not_of(" \t");

    if (pos1 == std::string::npos && pos2 == std::string::npos)
        return "";
    else if (pos1 == std::string::npos)
        return str.substr(0, str.length() - pos2);
    else if (pos2 == std::string::npos)
        return str.substr(pos1);
    else
        return str.substr(pos1, pos2 - pos1 + 1);
}

bool
impl::to_bool(const std::string& str)
{
    bool b;

    atf_error_t err = atf_text_to_bool(str.c_str(), &b);
    if (atf_is_error(err))
        throw_atf_error(err);

    return b;
}

int64_t
impl::to_bytes(std::string str)
{
    if (str.empty())
        throw std::runtime_error("Empty value");

    const char unit = str[str.length() - 1];
    int64_t multiplier;
    switch (unit) {
    case 'k': case 'K': multiplier = 1 << 10; break;
    case 'm': case 'M': multiplier = 1 << 20; break;
    case 'g': case 'G': multiplier = 1 << 30; break;
    case 't': case 'T': multiplier = int64_t(1) << 40; break;
    default:
        if (!std::isdigit(unit))
            throw std::runtime_error(std::string("Unknown size unit '") + unit
                                     + "'");
        multiplier = 1;
    }
    if (multiplier != 1)
        str.erase(str.length() - 1);

    return to_type< int64_t >(str) * multiplier;
}
