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
#include <sys/stat.h>
#include <sys/wait.h>

#include <fcntl.h>
#include <unistd.h>
}

#include <cstdlib>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <atf-c++.hpp>

static std::string
read_file(const std::string& path)
{
    char buffer[1024];

    const int fd = open(path.c_str(), O_RDONLY);
    if (fd == -1)
        ATF_FAIL("Cannot open " + path);
    const ssize_t length = read(fd, buffer, sizeof(buffer) - 1);
    close(fd);
    ATF_REQUIRE(length != -1);
    if (length == sizeof(buffer) - 1)
        ATF_FAIL("Internal buffer not long enough to read temporary file");
    ((char *)buffer)[length] = '\0';

    return buffer;
}

// ------------------------------------------------------------------------
// Tests cases for the free functions.
// ------------------------------------------------------------------------

ATF_TEST_CASE_WITHOUT_HEAD(cat_file__empty);
ATF_TEST_CASE_BODY(cat_file__empty)
{
    atf::utils::create_file("file.txt", "");
    atf::utils::redirect(STDOUT_FILENO, "captured.txt");
    atf::utils::cat_file("file.txt", "PREFIX");
    std::cout.flush();
    close(STDOUT_FILENO);

    ATF_REQUIRE_EQ("", read_file("captured.txt"));
}

ATF_TEST_CASE_WITHOUT_HEAD(cat_file__one_line);
ATF_TEST_CASE_BODY(cat_file__one_line)
{
    atf::utils::create_file("file.txt", "This is a single line\n");
    atf::utils::redirect(STDOUT_FILENO, "captured.txt");
    atf::utils::cat_file("file.txt", "PREFIX");
    std::cout.flush();
    close(STDOUT_FILENO);

    ATF_REQUIRE_EQ("PREFIXThis is a single line\n", read_file("captured.txt"));
}

ATF_TEST_CASE_WITHOUT_HEAD(cat_file__several_lines);
ATF_TEST_CASE_BODY(cat_file__several_lines)
{
    atf::utils::create_file("file.txt", "First\nSecond line\nAnd third\n");
    atf::utils::redirect(STDOUT_FILENO, "captured.txt");
    atf::utils::cat_file("file.txt", ">");
    std::cout.flush();
    close(STDOUT_FILENO);

    ATF_REQUIRE_EQ(">First\n>Second line\n>And third\n",
                   read_file("captured.txt"));
}

ATF_TEST_CASE_WITHOUT_HEAD(cat_file__no_newline_eof);
ATF_TEST_CASE_BODY(cat_file__no_newline_eof)
{
    atf::utils::create_file("file.txt", "Foo\n bar baz");
    atf::utils::redirect(STDOUT_FILENO, "captured.txt");
    atf::utils::cat_file("file.txt", "PREFIX");
    std::cout.flush();
    close(STDOUT_FILENO);

    ATF_REQUIRE_EQ("PREFIXFoo\nPREFIX bar baz", read_file("captured.txt"));
}

ATF_TEST_CASE_WITHOUT_HEAD(compare_file__empty__match);
ATF_TEST_CASE_BODY(compare_file__empty__match)
{
    atf::utils::create_file("test.txt", "");
    ATF_REQUIRE(atf::utils::compare_file("test.txt", ""));
}

ATF_TEST_CASE_WITHOUT_HEAD(compare_file__empty__not_match);
ATF_TEST_CASE_BODY(compare_file__empty__not_match)
{
    atf::utils::create_file("test.txt", "");
    ATF_REQUIRE(!atf::utils::compare_file("test.txt", "\n"));
    ATF_REQUIRE(!atf::utils::compare_file("test.txt", "foo"));
    ATF_REQUIRE(!atf::utils::compare_file("test.txt", " "));
}

ATF_TEST_CASE_WITHOUT_HEAD(compare_file__short__match);
ATF_TEST_CASE_BODY(compare_file__short__match)
{
    atf::utils::create_file("test.txt", "this is a short file");
    ATF_REQUIRE(atf::utils::compare_file("test.txt", "this is a short file"));
}

ATF_TEST_CASE_WITHOUT_HEAD(compare_file__short__not_match);
ATF_TEST_CASE_BODY(compare_file__short__not_match)
{
    atf::utils::create_file("test.txt", "this is a short file");
    ATF_REQUIRE(!atf::utils::compare_file("test.txt", ""));
    ATF_REQUIRE(!atf::utils::compare_file("test.txt", "\n"));
    ATF_REQUIRE(!atf::utils::compare_file("test.txt", "this is a Short file"));
    ATF_REQUIRE(!atf::utils::compare_file("test.txt", "this is a short fil"));
    ATF_REQUIRE(!atf::utils::compare_file("test.txt", "this is a short file "));
}

ATF_TEST_CASE_WITHOUT_HEAD(compare_file__long__match);
ATF_TEST_CASE_BODY(compare_file__long__match)
{
    char long_contents[3456];
    size_t i = 0;
    for (; i < sizeof(long_contents) - 1; i++)
        long_contents[i] = '0' + (i % 10);
    long_contents[i] = '\0';
    atf::utils::create_file("test.txt", long_contents);

    ATF_REQUIRE(atf::utils::compare_file("test.txt", long_contents));
}

ATF_TEST_CASE_WITHOUT_HEAD(compare_file__long__not_match);
ATF_TEST_CASE_BODY(compare_file__long__not_match)
{
    char long_contents[3456];
    size_t i = 0;
    for (; i < sizeof(long_contents) - 1; i++)
        long_contents[i] = '0' + (i % 10);
    long_contents[i] = '\0';
    atf::utils::create_file("test.txt", long_contents);

    ATF_REQUIRE(!atf::utils::compare_file("test.txt", ""));
    ATF_REQUIRE(!atf::utils::compare_file("test.txt", "\n"));
    ATF_REQUIRE(!atf::utils::compare_file("test.txt", "0123456789"));
    long_contents[i - 1] = 'Z';
    ATF_REQUIRE(!atf::utils::compare_file("test.txt", long_contents));
}

ATF_TEST_CASE_WITHOUT_HEAD(copy_file__empty);
ATF_TEST_CASE_BODY(copy_file__empty)
{
    atf::utils::create_file("src.txt", "");
    ATF_REQUIRE(chmod("src.txt", 0520) != -1);

    atf::utils::copy_file("src.txt", "dest.txt");
    ATF_REQUIRE(atf::utils::compare_file("dest.txt", ""));
    struct stat sb;
    ATF_REQUIRE(stat("dest.txt", &sb) != -1);
    ATF_REQUIRE_EQ(0520, sb.st_mode & 0xfff);
}

ATF_TEST_CASE_WITHOUT_HEAD(copy_file__some_contents);
ATF_TEST_CASE_BODY(copy_file__some_contents)
{
    atf::utils::create_file("src.txt", "This is a\ntest file\n");
    atf::utils::copy_file("src.txt", "dest.txt");
    ATF_REQUIRE(atf::utils::compare_file("dest.txt", "This is a\ntest file\n"));
}

ATF_TEST_CASE_WITHOUT_HEAD(create_file);
ATF_TEST_CASE_BODY(create_file)
{
    atf::utils::create_file("test.txt", "This is a %d test");

    ATF_REQUIRE_EQ("This is a %d test", read_file("test.txt"));
}

ATF_TEST_CASE_WITHOUT_HEAD(file_exists);
ATF_TEST_CASE_BODY(file_exists)
{
    atf::utils::create_file("test.txt", "foo");

    ATF_REQUIRE( atf::utils::file_exists("test.txt"));
    ATF_REQUIRE( atf::utils::file_exists("./test.txt"));
    ATF_REQUIRE(!atf::utils::file_exists("./test.tx"));
    ATF_REQUIRE(!atf::utils::file_exists("test.txt2"));
}

ATF_TEST_CASE_WITHOUT_HEAD(fork);
ATF_TEST_CASE_BODY(fork)
{
    std::cout << "Should not get into child\n";
    std::cerr << "Should not get into child\n";
    pid_t pid = atf::utils::fork();
    if (pid == 0) {
        std::cout << "Child stdout\n";
        std::cerr << "Child stderr\n";
        exit(EXIT_SUCCESS);
    }

    int status;
    ATF_REQUIRE(waitpid(pid, &status, 0) != -1);
    ATF_REQUIRE(WIFEXITED(status));
    ATF_REQUIRE_EQ(EXIT_SUCCESS, WEXITSTATUS(status));

    std::ostringstream out_name;
    out_name << "atf_utils_fork_" << pid << "_out.txt";
    std::ostringstream err_name;
    err_name << "atf_utils_fork_" << pid << "_err.txt";

    ATF_REQUIRE_EQ("Child stdout\n", read_file(out_name.str()));
    ATF_REQUIRE_EQ("Child stderr\n", read_file(err_name.str()));
}

ATF_TEST_CASE_WITHOUT_HEAD(grep_collection__set);
ATF_TEST_CASE_BODY(grep_collection__set)
{
    std::set< std::string > strings;
    strings.insert("First");
    strings.insert("Second");

    ATF_REQUIRE( atf::utils::grep_collection("irs", strings));
    ATF_REQUIRE( atf::utils::grep_collection("cond", strings));
    ATF_REQUIRE(!atf::utils::grep_collection("Third", strings));
}

ATF_TEST_CASE_WITHOUT_HEAD(grep_collection__vector);
ATF_TEST_CASE_BODY(grep_collection__vector)
{
    std::vector< std::string > strings;
    strings.push_back("First");
    strings.push_back("Second");

    ATF_REQUIRE( atf::utils::grep_collection("irs", strings));
    ATF_REQUIRE( atf::utils::grep_collection("cond", strings));
    ATF_REQUIRE(!atf::utils::grep_collection("Third", strings));
}

ATF_TEST_CASE_WITHOUT_HEAD(grep_file);
ATF_TEST_CASE_BODY(grep_file)
{
    atf::utils::create_file("test.txt", "line1\nthe second line\naaaabbbb\n");

    ATF_REQUIRE(atf::utils::grep_file("line1", "test.txt"));
    ATF_REQUIRE(atf::utils::grep_file("second line", "test.txt"));
    ATF_REQUIRE(atf::utils::grep_file("aa.*bb", "test.txt"));
    ATF_REQUIRE(!atf::utils::grep_file("foo", "test.txt"));
    ATF_REQUIRE(!atf::utils::grep_file("bar", "test.txt"));
    ATF_REQUIRE(!atf::utils::grep_file("aaaaa", "test.txt"));
}

ATF_TEST_CASE_WITHOUT_HEAD(grep_string);
ATF_TEST_CASE_BODY(grep_string)
{
    const char *str = "a string - aaaabbbb";
    ATF_REQUIRE(atf::utils::grep_string("a string", str));
    ATF_REQUIRE(atf::utils::grep_string("^a string", str));
    ATF_REQUIRE(atf::utils::grep_string("aaaabbbb$", str));
    ATF_REQUIRE(atf::utils::grep_string("aa.*bb", str));
    ATF_REQUIRE(!atf::utils::grep_string("foo", str));
    ATF_REQUIRE(!atf::utils::grep_string("bar", str));
    ATF_REQUIRE(!atf::utils::grep_string("aaaaa", str));
}

ATF_TEST_CASE_WITHOUT_HEAD(redirect__stdout);
ATF_TEST_CASE_BODY(redirect__stdout)
{
    std::cout << "Buffer this";
    atf::utils::redirect(STDOUT_FILENO, "captured.txt");
    std::cout << "The printed message";
    std::cout.flush();

    ATF_REQUIRE_EQ("The printed message", read_file("captured.txt"));
}

ATF_TEST_CASE_WITHOUT_HEAD(redirect__stderr);
ATF_TEST_CASE_BODY(redirect__stderr)
{
    std::cerr << "Buffer this";
    atf::utils::redirect(STDERR_FILENO, "captured.txt");
    std::cerr << "The printed message";
    std::cerr.flush();

    ATF_REQUIRE_EQ("The printed message", read_file("captured.txt"));
}

ATF_TEST_CASE_WITHOUT_HEAD(redirect__other);
ATF_TEST_CASE_BODY(redirect__other)
{
    const std::string message = "Foo bar\nbaz\n";
    atf::utils::redirect(15, "captured.txt");
    ATF_REQUIRE(write(15, message.c_str(), message.length()) != -1);
    close(15);

    ATF_REQUIRE_EQ(message, read_file("captured.txt"));
}

static void
fork_and_wait(const int exitstatus, const char* expout, const char* experr)
{
    const pid_t pid = atf::utils::fork();
    if (pid == 0) {
        std::cout << "Some output\n";
        std::cerr << "Some error\n";
        exit(123);
    }
    atf::utils::wait(pid, exitstatus, expout, experr);
    exit(EXIT_SUCCESS);
}

ATF_TEST_CASE_WITHOUT_HEAD(wait__ok);
ATF_TEST_CASE_BODY(wait__ok)
{
    const pid_t control = fork();
    ATF_REQUIRE(control != -1);
    if (control == 0)
        fork_and_wait(123, "Some output\n", "Some error\n");
    else {
        int status;
        ATF_REQUIRE(waitpid(control, &status, 0) != -1);
        ATF_REQUIRE(WIFEXITED(status));
        ATF_REQUIRE_EQ(EXIT_SUCCESS, WEXITSTATUS(status));
    }
}

ATF_TEST_CASE_WITHOUT_HEAD(wait__ok_nested);
ATF_TEST_CASE_BODY(wait__ok_nested)
{
    const pid_t parent = atf::utils::fork();
    ATF_REQUIRE(parent != -1);
    if (parent == 0) {
        const pid_t child = atf::utils::fork();
        ATF_REQUIRE(child != -1);
        if (child == 0) {
            std::cerr.flush();
            std::cout << "Child output\n";
            std::cout.flush();
            std::cerr << "Child error\n";
            std::exit(50);
        } else {
            std::cout << "Parent output\n";
            std::cerr << "Parent error\n";
            atf::utils::wait(child, 50, "Child output\n", "Child error\n");
            std::exit(40);
        }
    } else {
        atf::utils::wait(parent, 40,
                         "Parent output\n"
                         "subprocess stdout: Child output\n"
                         "subprocess stderr: Child error\n",
                         "Parent error\n");
    }
}

ATF_TEST_CASE_WITHOUT_HEAD(wait__invalid_exitstatus);
ATF_TEST_CASE_BODY(wait__invalid_exitstatus)
{
    const pid_t control = fork();
    ATF_REQUIRE(control != -1);
    if (control == 0)
        fork_and_wait(120, "Some output\n", "Some error\n");
    else {
        int status;
        ATF_REQUIRE(waitpid(control, &status, 0) != -1);
        ATF_REQUIRE(WIFEXITED(status));
        ATF_REQUIRE_EQ(EXIT_FAILURE, WEXITSTATUS(status));
    }
}

ATF_TEST_CASE_WITHOUT_HEAD(wait__invalid_stdout);
ATF_TEST_CASE_BODY(wait__invalid_stdout)
{
    const pid_t control = fork();
    ATF_REQUIRE(control != -1);
    if (control == 0)
        fork_and_wait(123, "Some output foo\n", "Some error\n");
    else {
        int status;
        ATF_REQUIRE(waitpid(control, &status, 0) != -1);
        ATF_REQUIRE(WIFEXITED(status));
        ATF_REQUIRE_EQ(EXIT_FAILURE, WEXITSTATUS(status));
    }
}

ATF_TEST_CASE_WITHOUT_HEAD(wait__invalid_stderr);
ATF_TEST_CASE_BODY(wait__invalid_stderr)
{
    const pid_t control = fork();
    ATF_REQUIRE(control != -1);
    if (control == 0)
        fork_and_wait(123, "Some output\n", "Some error foo\n");
    else {
        int status;
        ATF_REQUIRE(waitpid(control, &status, 0) != -1);
        ATF_REQUIRE(WIFEXITED(status));
        ATF_REQUIRE_EQ(EXIT_FAILURE, WEXITSTATUS(status));
    }
}

ATF_TEST_CASE_WITHOUT_HEAD(wait__save_stdout);
ATF_TEST_CASE_BODY(wait__save_stdout)
{
    const pid_t control = fork();
    ATF_REQUIRE(control != -1);
    if (control == 0)
        fork_and_wait(123, "save:my-output.txt", "Some error\n");
    else {
        int status;
        ATF_REQUIRE(waitpid(control, &status, 0) != -1);
        ATF_REQUIRE(WIFEXITED(status));
        ATF_REQUIRE_EQ(EXIT_SUCCESS, WEXITSTATUS(status));

        ATF_REQUIRE(atf::utils::compare_file("my-output.txt", "Some output\n"));
    }
}

ATF_TEST_CASE_WITHOUT_HEAD(wait__save_stderr);
ATF_TEST_CASE_BODY(wait__save_stderr)
{
    const pid_t control = fork();
    ATF_REQUIRE(control != -1);
    if (control == 0)
        fork_and_wait(123, "Some output\n", "save:my-output.txt");
    else {
        int status;
        ATF_REQUIRE(waitpid(control, &status, 0) != -1);
        ATF_REQUIRE(WIFEXITED(status));
        ATF_REQUIRE_EQ(EXIT_SUCCESS, WEXITSTATUS(status));

        ATF_REQUIRE(atf::utils::compare_file("my-output.txt", "Some error\n"));
    }
}

// ------------------------------------------------------------------------
// Main.
// ------------------------------------------------------------------------

ATF_INIT_TEST_CASES(tcs)
{
    // Add the test for the free functions.
    ATF_ADD_TEST_CASE(tcs, cat_file__empty);
    ATF_ADD_TEST_CASE(tcs, cat_file__one_line);
    ATF_ADD_TEST_CASE(tcs, cat_file__several_lines);
    ATF_ADD_TEST_CASE(tcs, cat_file__no_newline_eof);

    ATF_ADD_TEST_CASE(tcs, compare_file__empty__match);
    ATF_ADD_TEST_CASE(tcs, compare_file__empty__not_match);
    ATF_ADD_TEST_CASE(tcs, compare_file__short__match);
    ATF_ADD_TEST_CASE(tcs, compare_file__short__not_match);
    ATF_ADD_TEST_CASE(tcs, compare_file__long__match);
    ATF_ADD_TEST_CASE(tcs, compare_file__long__not_match);

    ATF_ADD_TEST_CASE(tcs, copy_file__empty);
    ATF_ADD_TEST_CASE(tcs, copy_file__some_contents);

    ATF_ADD_TEST_CASE(tcs, create_file);

    ATF_ADD_TEST_CASE(tcs, file_exists);

    ATF_ADD_TEST_CASE(tcs, fork);

    ATF_ADD_TEST_CASE(tcs, grep_collection__set);
    ATF_ADD_TEST_CASE(tcs, grep_collection__vector);
    ATF_ADD_TEST_CASE(tcs, grep_file);
    ATF_ADD_TEST_CASE(tcs, grep_string);

    ATF_ADD_TEST_CASE(tcs, redirect__stdout);
    ATF_ADD_TEST_CASE(tcs, redirect__stderr);
    ATF_ADD_TEST_CASE(tcs, redirect__other);

    ATF_ADD_TEST_CASE(tcs, wait__ok);
    ATF_ADD_TEST_CASE(tcs, wait__ok_nested);
    ATF_ADD_TEST_CASE(tcs, wait__invalid_exitstatus);
    ATF_ADD_TEST_CASE(tcs, wait__invalid_stdout);
    ATF_ADD_TEST_CASE(tcs, wait__invalid_stderr);
    ATF_ADD_TEST_CASE(tcs, wait__save_stdout);
    ATF_ADD_TEST_CASE(tcs, wait__save_stderr);
}
