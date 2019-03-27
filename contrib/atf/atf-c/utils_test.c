/* Copyright (c) 2010 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND
 * CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.  */

#include "atf-c/utils.h"

#include <sys/stat.h>
#include <sys/wait.h>

#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <atf-c.h>

#include "atf-c/detail/dynstr.h"
#include "atf-c/detail/test_helpers.h"

/** Reads the contents of a file into a buffer.
 *
 * Up to buflen-1 characters are read into buffer.  If this function returns,
 * the contents read into the buffer are guaranteed to be nul-terminated.
 * Note, however, that if the file contains any nul characters itself,
 * comparing it "as a string" will not work.
 *
 * \param path The file to be read, which must exist.
 * \param buffer Buffer into which to store the file contents.
 * \param buflen Size of the target buffer.
 *
 * \return The count of bytes read. */
static ssize_t
read_file(const char *path, void *const buffer, const size_t buflen)
{
    const int fd = open(path, O_RDONLY);
    ATF_REQUIRE_MSG(fd != -1, "Cannot open %s", path);
    const ssize_t length = read(fd, buffer, buflen - 1);
    close(fd);
    ATF_REQUIRE(length != -1);
    ((char *)buffer)[length] = '\0';
    return length;
}

ATF_TC_WITHOUT_HEAD(cat_file__empty);
ATF_TC_BODY(cat_file__empty, tc)
{
    atf_utils_create_file("file.txt", "%s", "");
    atf_utils_redirect(STDOUT_FILENO, "captured.txt");
    atf_utils_cat_file("file.txt", "PREFIX");
    fflush(stdout);
    close(STDOUT_FILENO);

    char buffer[1024];
    read_file("captured.txt", buffer, sizeof(buffer));
    ATF_REQUIRE_STREQ("", buffer);
}

ATF_TC_WITHOUT_HEAD(cat_file__one_line);
ATF_TC_BODY(cat_file__one_line, tc)
{
    atf_utils_create_file("file.txt", "This is a single line\n");
    atf_utils_redirect(STDOUT_FILENO, "captured.txt");
    atf_utils_cat_file("file.txt", "PREFIX");
    fflush(stdout);
    close(STDOUT_FILENO);

    char buffer[1024];
    read_file("captured.txt", buffer, sizeof(buffer));
    ATF_REQUIRE_STREQ("PREFIXThis is a single line\n", buffer);
}

ATF_TC_WITHOUT_HEAD(cat_file__several_lines);
ATF_TC_BODY(cat_file__several_lines, tc)
{
    atf_utils_create_file("file.txt", "First\nSecond line\nAnd third\n");
    atf_utils_redirect(STDOUT_FILENO, "captured.txt");
    atf_utils_cat_file("file.txt", ">");
    fflush(stdout);
    close(STDOUT_FILENO);

    char buffer[1024];
    read_file("captured.txt", buffer, sizeof(buffer));
    ATF_REQUIRE_STREQ(">First\n>Second line\n>And third\n", buffer);
}

ATF_TC_WITHOUT_HEAD(cat_file__no_newline_eof);
ATF_TC_BODY(cat_file__no_newline_eof, tc)
{
    atf_utils_create_file("file.txt", "Foo\n bar baz");
    atf_utils_redirect(STDOUT_FILENO, "captured.txt");
    atf_utils_cat_file("file.txt", "PREFIX");
    fflush(stdout);
    close(STDOUT_FILENO);

    char buffer[1024];
    read_file("captured.txt", buffer, sizeof(buffer));
    ATF_REQUIRE_STREQ("PREFIXFoo\nPREFIX bar baz", buffer);
}

ATF_TC_WITHOUT_HEAD(compare_file__empty__match);
ATF_TC_BODY(compare_file__empty__match, tc)
{
    atf_utils_create_file("test.txt", "%s", "");
    ATF_REQUIRE(atf_utils_compare_file("test.txt", ""));
}

ATF_TC_WITHOUT_HEAD(compare_file__empty__not_match);
ATF_TC_BODY(compare_file__empty__not_match, tc)
{
    atf_utils_create_file("test.txt", "%s", "");
    ATF_REQUIRE(!atf_utils_compare_file("test.txt", "\n"));
    ATF_REQUIRE(!atf_utils_compare_file("test.txt", "foo"));
    ATF_REQUIRE(!atf_utils_compare_file("test.txt", " "));
}

ATF_TC_WITHOUT_HEAD(compare_file__short__match);
ATF_TC_BODY(compare_file__short__match, tc)
{
    atf_utils_create_file("test.txt", "this is a short file");
    ATF_REQUIRE(atf_utils_compare_file("test.txt", "this is a short file"));
}

ATF_TC_WITHOUT_HEAD(compare_file__short__not_match);
ATF_TC_BODY(compare_file__short__not_match, tc)
{
    atf_utils_create_file("test.txt", "this is a short file");
    ATF_REQUIRE(!atf_utils_compare_file("test.txt", ""));
    ATF_REQUIRE(!atf_utils_compare_file("test.txt", "\n"));
    ATF_REQUIRE(!atf_utils_compare_file("test.txt", "this is a Short file"));
    ATF_REQUIRE(!atf_utils_compare_file("test.txt", "this is a short fil"));
    ATF_REQUIRE(!atf_utils_compare_file("test.txt", "this is a short file "));
}

ATF_TC_WITHOUT_HEAD(compare_file__long__match);
ATF_TC_BODY(compare_file__long__match, tc)
{
    char long_contents[3456];
    size_t i = 0;
    for (; i < sizeof(long_contents) - 1; i++)
        long_contents[i] = '0' + (i % 10);
    long_contents[i] = '\0';
    atf_utils_create_file("test.txt", "%s", long_contents);

    ATF_REQUIRE(atf_utils_compare_file("test.txt", long_contents));
}

ATF_TC_WITHOUT_HEAD(compare_file__long__not_match);
ATF_TC_BODY(compare_file__long__not_match, tc)
{
    char long_contents[3456];
    size_t i = 0;
    for (; i < sizeof(long_contents) - 1; i++)
        long_contents[i] = '0' + (i % 10);
    long_contents[i] = '\0';
    atf_utils_create_file("test.txt", "%s", long_contents);

    ATF_REQUIRE(!atf_utils_compare_file("test.txt", ""));
    ATF_REQUIRE(!atf_utils_compare_file("test.txt", "\n"));
    ATF_REQUIRE(!atf_utils_compare_file("test.txt", "0123456789"));
    long_contents[i - 1] = 'Z';
    ATF_REQUIRE(!atf_utils_compare_file("test.txt", long_contents));
}

ATF_TC_WITHOUT_HEAD(copy_file__empty);
ATF_TC_BODY(copy_file__empty, tc)
{
    atf_utils_create_file("src.txt", "%s", "");
    ATF_REQUIRE(chmod("src.txt", 0520) != -1);

    atf_utils_copy_file("src.txt", "dest.txt");
    ATF_REQUIRE(atf_utils_compare_file("dest.txt", ""));
    struct stat sb;
    ATF_REQUIRE(stat("dest.txt", &sb) != -1);
    ATF_REQUIRE_EQ(0520, sb.st_mode & 0xfff);
}

ATF_TC_WITHOUT_HEAD(copy_file__some_contents);
ATF_TC_BODY(copy_file__some_contents, tc)
{
    atf_utils_create_file("src.txt", "This is a\ntest file\n");
    atf_utils_copy_file("src.txt", "dest.txt");
    ATF_REQUIRE(atf_utils_compare_file("dest.txt", "This is a\ntest file\n"));
}

ATF_TC_WITHOUT_HEAD(create_file);
ATF_TC_BODY(create_file, tc)
{
    atf_utils_create_file("test.txt", "This is a test with %d", 12345);

    char buffer[128];
    read_file("test.txt", buffer, sizeof(buffer));
    ATF_REQUIRE_STREQ("This is a test with 12345", buffer);
}

ATF_TC_WITHOUT_HEAD(file_exists);
ATF_TC_BODY(file_exists, tc)
{
    atf_utils_create_file("test.txt", "foo");

    ATF_REQUIRE( atf_utils_file_exists("test.txt"));
    ATF_REQUIRE( atf_utils_file_exists("./test.txt"));
    ATF_REQUIRE(!atf_utils_file_exists("./test.tx"));
    ATF_REQUIRE(!atf_utils_file_exists("test.txt2"));
}

ATF_TC_WITHOUT_HEAD(fork);
ATF_TC_BODY(fork, tc)
{
    fprintf(stdout, "Should not get into child\n");
    fprintf(stderr, "Should not get into child\n");
    pid_t pid = atf_utils_fork();
    if (pid == 0) {
        fprintf(stdout, "Child stdout\n");
        fprintf(stderr, "Child stderr\n");
        exit(EXIT_SUCCESS);
    }

    int status;
    ATF_REQUIRE(waitpid(pid, &status, 0) != -1);
    ATF_REQUIRE(WIFEXITED(status));
    ATF_REQUIRE_EQ(EXIT_SUCCESS, WEXITSTATUS(status));

    atf_dynstr_t out_name;
    RE(atf_dynstr_init_fmt(&out_name, "atf_utils_fork_%d_out.txt", (int)pid));
    atf_dynstr_t err_name;
    RE(atf_dynstr_init_fmt(&err_name, "atf_utils_fork_%d_err.txt", (int)pid));

    char buffer[1024];
    read_file(atf_dynstr_cstring(&out_name), buffer, sizeof(buffer));
    ATF_REQUIRE_STREQ("Child stdout\n", buffer);
    read_file(atf_dynstr_cstring(&err_name), buffer, sizeof(buffer));
    ATF_REQUIRE_STREQ("Child stderr\n", buffer);

    atf_dynstr_fini(&err_name);
    atf_dynstr_fini(&out_name);
}

ATF_TC_WITHOUT_HEAD(free_charpp__empty);
ATF_TC_BODY(free_charpp__empty, tc)
{
    char **array = malloc(sizeof(char *) * 1);
    array[0] = NULL;

    atf_utils_free_charpp(array);
}

ATF_TC_WITHOUT_HEAD(free_charpp__some);
ATF_TC_BODY(free_charpp__some, tc)
{
    char **array = malloc(sizeof(char *) * 4);
    array[0] = strdup("first");
    array[1] = strdup("second");
    array[2] = strdup("third");
    array[3] = NULL;

    atf_utils_free_charpp(array);
}

ATF_TC_WITHOUT_HEAD(grep_file);
ATF_TC_BODY(grep_file, tc)
{
    atf_utils_create_file("test.txt", "line1\nthe second line\naaaabbbb\n");

    ATF_CHECK(atf_utils_grep_file("line1", "test.txt"));
    ATF_CHECK(atf_utils_grep_file("line%d", "test.txt", 1));
    ATF_CHECK(atf_utils_grep_file("second line", "test.txt"));
    ATF_CHECK(atf_utils_grep_file("aa.*bb", "test.txt"));
    ATF_CHECK(!atf_utils_grep_file("foo", "test.txt"));
    ATF_CHECK(!atf_utils_grep_file("bar", "test.txt"));
    ATF_CHECK(!atf_utils_grep_file("aaaaa", "test.txt"));
}

ATF_TC_WITHOUT_HEAD(grep_string);
ATF_TC_BODY(grep_string, tc)
{
    const char *str = "a string - aaaabbbb";
    ATF_CHECK(atf_utils_grep_string("a string", str));
    ATF_CHECK(atf_utils_grep_string("^a string", str));
    ATF_CHECK(atf_utils_grep_string("aaaabbbb$", str));
    ATF_CHECK(atf_utils_grep_string("a%s*bb", str, "a."));
    ATF_CHECK(!atf_utils_grep_string("foo", str));
    ATF_CHECK(!atf_utils_grep_string("bar", str));
    ATF_CHECK(!atf_utils_grep_string("aaaaa", str));
}

ATF_TC_WITHOUT_HEAD(readline__none);
ATF_TC_BODY(readline__none, tc)
{
    atf_utils_create_file("empty.txt", "%s", "");

    const int fd = open("empty.txt", O_RDONLY);
    ATF_REQUIRE(fd != -1);
    ATF_REQUIRE(atf_utils_readline(fd) == NULL);
    close(fd);
}

ATF_TC_WITHOUT_HEAD(readline__some);
ATF_TC_BODY(readline__some, tc)
{
    const char *l1 = "First line with % formatting % characters %";
    const char *l2 = "Second line; much longer than the first one";
    const char *l3 = "Last line, without terminator";

    atf_utils_create_file("test.txt", "%s\n%s\n%s", l1, l2, l3);

    const int fd = open("test.txt", O_RDONLY);
    ATF_REQUIRE(fd != -1);

    char *line;

    line = atf_utils_readline(fd);
    ATF_REQUIRE_STREQ(l1, line);
    free(line);

    line = atf_utils_readline(fd);
    ATF_REQUIRE_STREQ(l2, line);
    free(line);

    line = atf_utils_readline(fd);
    ATF_REQUIRE_STREQ(l3, line);
    free(line);

    close(fd);
}

ATF_TC_WITHOUT_HEAD(redirect__stdout);
ATF_TC_BODY(redirect__stdout, tc)
{
    printf("Buffer this");
    atf_utils_redirect(STDOUT_FILENO, "captured.txt");
    printf("The printed message");
    fflush(stdout);

    char buffer[1024];
    read_file("captured.txt", buffer, sizeof(buffer));
    ATF_REQUIRE_STREQ("The printed message", buffer);
}

ATF_TC_WITHOUT_HEAD(redirect__stderr);
ATF_TC_BODY(redirect__stderr, tc)
{
    fprintf(stderr, "Buffer this");
    atf_utils_redirect(STDERR_FILENO, "captured.txt");
    fprintf(stderr, "The printed message");
    fflush(stderr);

    char buffer[1024];
    read_file("captured.txt", buffer, sizeof(buffer));
    ATF_REQUIRE_STREQ("The printed message", buffer);
}

ATF_TC_WITHOUT_HEAD(redirect__other);
ATF_TC_BODY(redirect__other, tc)
{
    const char *message = "Foo bar\nbaz\n";
    atf_utils_redirect(15, "captured.txt");
    ATF_REQUIRE(write(15, message, strlen(message)) != -1);
    close(15);

    char buffer[1024];
    read_file("captured.txt", buffer, sizeof(buffer));
    ATF_REQUIRE_STREQ(message, buffer);
}

static void
fork_and_wait(const int exitstatus, const char* expout, const char* experr)
{
    const pid_t pid = atf_utils_fork();
    ATF_REQUIRE(pid != -1);
    if (pid == 0) {
        fprintf(stdout, "Some output\n");
        fprintf(stderr, "Some error\n");
        exit(123);
    }
    atf_utils_wait(pid, exitstatus, expout, experr);
    exit(EXIT_SUCCESS);
}

ATF_TC_WITHOUT_HEAD(wait__ok);
ATF_TC_BODY(wait__ok, tc)
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

ATF_TC_WITHOUT_HEAD(wait__ok_nested);
ATF_TC_BODY(wait__ok_nested, tc)
{
    const pid_t parent = atf_utils_fork();
    ATF_REQUIRE(parent != -1);
    if (parent == 0) {
        const pid_t child = atf_utils_fork();
        ATF_REQUIRE(child != -1);
        if (child == 0) {
            fflush(stderr);
            fprintf(stdout, "Child output\n");
            fflush(stdout);
            fprintf(stderr, "Child error\n");
            exit(50);
        } else {
            fprintf(stdout, "Parent output\n");
            fprintf(stderr, "Parent error\n");
            atf_utils_wait(child, 50, "Child output\n", "Child error\n");
            exit(40);
        }
    } else {
        atf_utils_wait(parent, 40,
                       "Parent output\n"
                       "subprocess stdout: Child output\n"
                       "subprocess stderr: Child error\n",
                       "Parent error\n");
    }
}

ATF_TC_WITHOUT_HEAD(wait__invalid_exitstatus);
ATF_TC_BODY(wait__invalid_exitstatus, tc)
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

ATF_TC_WITHOUT_HEAD(wait__invalid_stdout);
ATF_TC_BODY(wait__invalid_stdout, tc)
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

ATF_TC_WITHOUT_HEAD(wait__invalid_stderr);
ATF_TC_BODY(wait__invalid_stderr, tc)
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

ATF_TC_WITHOUT_HEAD(wait__save_stdout);
ATF_TC_BODY(wait__save_stdout, tc)
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

        ATF_REQUIRE(atf_utils_compare_file("my-output.txt", "Some output\n"));
    }
}

ATF_TC_WITHOUT_HEAD(wait__save_stderr);
ATF_TC_BODY(wait__save_stderr, tc)
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

        ATF_REQUIRE(atf_utils_compare_file("my-output.txt", "Some error\n"));
    }
}

ATF_TP_ADD_TCS(tp)
{
    ATF_TP_ADD_TC(tp, cat_file__empty);
    ATF_TP_ADD_TC(tp, cat_file__one_line);
    ATF_TP_ADD_TC(tp, cat_file__several_lines);
    ATF_TP_ADD_TC(tp, cat_file__no_newline_eof);

    ATF_TP_ADD_TC(tp, compare_file__empty__match);
    ATF_TP_ADD_TC(tp, compare_file__empty__not_match);
    ATF_TP_ADD_TC(tp, compare_file__short__match);
    ATF_TP_ADD_TC(tp, compare_file__short__not_match);
    ATF_TP_ADD_TC(tp, compare_file__long__match);
    ATF_TP_ADD_TC(tp, compare_file__long__not_match);

    ATF_TP_ADD_TC(tp, copy_file__empty);
    ATF_TP_ADD_TC(tp, copy_file__some_contents);

    ATF_TP_ADD_TC(tp, create_file);

    ATF_TP_ADD_TC(tp, file_exists);

    ATF_TP_ADD_TC(tp, fork);

    ATF_TP_ADD_TC(tp, free_charpp__empty);
    ATF_TP_ADD_TC(tp, free_charpp__some);

    ATF_TP_ADD_TC(tp, grep_file);
    ATF_TP_ADD_TC(tp, grep_string);

    ATF_TP_ADD_TC(tp, readline__none);
    ATF_TP_ADD_TC(tp, readline__some);

    ATF_TP_ADD_TC(tp, redirect__stdout);
    ATF_TP_ADD_TC(tp, redirect__stderr);
    ATF_TP_ADD_TC(tp, redirect__other);

    ATF_TP_ADD_TC(tp, wait__ok);
    ATF_TP_ADD_TC(tp, wait__ok_nested);
    ATF_TP_ADD_TC(tp, wait__save_stdout);
    ATF_TP_ADD_TC(tp, wait__save_stderr);
    ATF_TP_ADD_TC(tp, wait__invalid_exitstatus);
    ATF_TP_ADD_TC(tp, wait__invalid_stdout);
    ATF_TP_ADD_TC(tp, wait__invalid_stderr);

    return atf_no_error();
}
