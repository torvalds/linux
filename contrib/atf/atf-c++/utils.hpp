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

#if !defined(ATF_CXX_UTILS_HPP)
#define ATF_CXX_UTILS_HPP

extern "C" {
#include <unistd.h>
}

#include <string>

namespace atf {
namespace utils {

void cat_file(const std::string&, const std::string&);
bool compare_file(const std::string&, const std::string&);
void copy_file(const std::string&, const std::string&);
void create_file(const std::string&, const std::string&);
bool file_exists(const std::string&);
pid_t fork(void);
bool grep_file(const std::string&, const std::string&);
bool grep_string(const std::string&, const std::string&);
void redirect(const int, const std::string&);
void wait(const pid_t, const int, const std::string&, const std::string&);

template< typename Collection >
bool
grep_collection(const std::string& regexp, const Collection& collection)
{
    for (typename Collection::const_iterator iter = collection.begin();
         iter != collection.end(); ++iter) {
        if (grep_string(regexp, *iter))
            return true;
    }
    return false;
}

} // namespace utils
} // namespace atf

#endif // !defined(ATF_CXX_UTILS_HPP)
