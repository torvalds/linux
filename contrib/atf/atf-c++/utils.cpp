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

#include "atf-c++/utils.hpp"

extern "C" {
#include "atf-c/utils.h"
}

#include <cstdlib>
#include <iostream>

void
atf::utils::cat_file(const std::string& path, const std::string& prefix)
{
    atf_utils_cat_file(path.c_str(), prefix.c_str());
}

void
atf::utils::copy_file(const std::string& source, const std::string& destination)
{
    atf_utils_copy_file(source.c_str(), destination.c_str());
}

bool
atf::utils::compare_file(const std::string& path, const std::string& contents)
{
    return atf_utils_compare_file(path.c_str(), contents.c_str());
}

void
atf::utils::create_file(const std::string& path, const std::string& contents)
{
    atf_utils_create_file(path.c_str(), "%s", contents.c_str());
}

bool
atf::utils::file_exists(const std::string& path)
{
    return atf_utils_file_exists(path.c_str());
}

pid_t
atf::utils::fork(void)
{
    std::cout.flush();
    std::cerr.flush();
    return atf_utils_fork();
}

bool
atf::utils::grep_file(const std::string& regex, const std::string& path)
{
    return atf_utils_grep_file("%s", path.c_str(), regex.c_str());
}

bool
atf::utils::grep_string(const std::string& regex, const std::string& str)
{
    return atf_utils_grep_string("%s", str.c_str(), regex.c_str());
}

void
atf::utils::redirect(const int fd, const std::string& path)
{
    if (fd == STDOUT_FILENO)
        std::cout.flush();
    else if (fd == STDERR_FILENO)
        std::cerr.flush();
    atf_utils_redirect(fd, path.c_str());
}

void
atf::utils::wait(const pid_t pid, const int exitstatus,
                 const std::string& expout, const std::string& experr)
{
    atf_utils_wait(pid, exitstatus, expout.c_str(), experr.c_str());
}
