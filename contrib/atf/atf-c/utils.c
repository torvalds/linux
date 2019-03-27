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

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <atf-c.h>

#include "atf-c/detail/dynstr.h"

/** Allocate a filename to be used by atf_utils_{fork,wait}.
 *
 * In case of a failure, marks the calling test as failed when in_parent is
 * true, else terminates execution.
 *
 * \param [out] name String to contain the generated file.
 * \param pid PID of the process that will write to the file.
 * \param suffix Either "out" or "err".
 * \param in_parent If true, fail with atf_tc_fail; else use err(3). */
static void
init_out_filename(atf_dynstr_t *name, const pid_t pid, const char *suffix,
                  const bool in_parent)
{
    atf_error_t error;

    error = atf_dynstr_init_fmt(name, "atf_utils_fork_%d_%s.txt",
                                (int)pid, suffix);
    if (atf_is_error(error)) {
        char buffer[1024];
        atf_error_format(error, buffer, sizeof(buffer));
        if (in_parent) {
            atf_tc_fail("Failed to create output file: %s", buffer);
        } else {
            err(EXIT_FAILURE, "Failed to create output file: %s", buffer);
        }
    }
}

/** Searches for a regexp in a string.
 *
 * \param regex The regexp to look for.
 * \param str The string in which to look for the expression.
 *
 * \return True if there is a match; false otherwise. */
static
bool
grep_string(const char *regex, const char *str)
{
    int res;
    regex_t preg;

    printf("Looking for '%s' in '%s'\n", regex, str);
    ATF_REQUIRE(regcomp(&preg, regex, REG_EXTENDED) == 0);

    res = regexec(&preg, str, 0, NULL, 0);
    ATF_REQUIRE(res == 0 || res == REG_NOMATCH);

    regfree(&preg);

    return res == 0;
}

/** Prints the contents of a file to stdout.
 *
 * \param name The name of the file to be printed.
 * \param prefix An string to be prepended to every line of the printed
 *     file. */
void
atf_utils_cat_file(const char *name, const char *prefix)
{
    const int fd = open(name, O_RDONLY);
    ATF_REQUIRE_MSG(fd != -1, "Cannot open %s", name);

    char buffer[1024];
    ssize_t count;
    bool continued = false;
    while ((count = read(fd, buffer, sizeof(buffer) - 1)) > 0) {
        buffer[count] = '\0';

        if (!continued)
            printf("%s", prefix);

        char *iter = buffer;
        char *end;
        while ((end = strchr(iter, '\n')) != NULL) {
            *end = '\0';
            printf("%s\n", iter);

            iter = end + 1;
            if (iter != buffer + count)
                printf("%s", prefix);
            else
                continued = false;
        }
        if (iter < buffer + count) {
            printf("%s", iter);
            continued = true;
        }
    }
    ATF_REQUIRE(count == 0);
}

/** Compares a file against the given golden contents.
 *
 * \param name Name of the file to be compared.
 * \param contents Expected contents of the file.
 *
 * \return True if the file matches the contents; false otherwise. */
bool
atf_utils_compare_file(const char *name, const char *contents)
{
    const int fd = open(name, O_RDONLY);
    ATF_REQUIRE_MSG(fd != -1, "Cannot open %s", name);

    const char *pos = contents;
    ssize_t remaining = strlen(contents);

    char buffer[1024];
    ssize_t count;
    while ((count = read(fd, buffer, sizeof(buffer))) > 0 &&
           count <= remaining) {
        if (memcmp(pos, buffer, count) != 0) {
            close(fd);
            return false;
        }
        remaining -= count;
        pos += count;
    }
    close(fd);
    return count == 0 && remaining == 0;
}

/** Copies a file.
 *
 * \param source Path to the source file.
 * \param destination Path to the destination file. */
void
atf_utils_copy_file(const char *source, const char *destination)
{
    const int input = open(source, O_RDONLY);
    ATF_REQUIRE_MSG(input != -1, "Failed to open source file during "
                    "copy (%s)", source);

    const int output = open(destination, O_WRONLY | O_CREAT | O_TRUNC, 0777);
    ATF_REQUIRE_MSG(output != -1, "Failed to open destination file during "
                    "copy (%s)", destination);

    char buffer[1024];
    ssize_t length;
    while ((length = read(input, buffer, sizeof(buffer))) > 0)
        ATF_REQUIRE_MSG(write(output, buffer, length) == length,
                        "Failed to write to %s during copy", destination);
    ATF_REQUIRE_MSG(length != -1, "Failed to read from %s during copy", source);

    struct stat sb;
    ATF_REQUIRE_MSG(fstat(input, &sb) != -1,
                    "Failed to stat source file %s during copy", source);
    ATF_REQUIRE_MSG(fchmod(output, sb.st_mode) != -1,
                    "Failed to chmod destination file %s during copy",
                    destination);

    close(output);
    close(input);
}

/** Creates a file.
 *
 * \param name Name of the file to create.
 * \param contents Text to write into the created file.
 * \param ... Positional parameters to the contents. */
void
atf_utils_create_file(const char *name, const char *contents, ...)
{
    va_list ap;
    atf_dynstr_t formatted;
    atf_error_t error;

    va_start(ap, contents);
    error = atf_dynstr_init_ap(&formatted, contents, ap);
    va_end(ap);
    ATF_REQUIRE(!atf_is_error(error));

    const int fd = open(name, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    ATF_REQUIRE_MSG(fd != -1, "Cannot create file %s", name);
    ATF_REQUIRE(write(fd, atf_dynstr_cstring(&formatted),
                      atf_dynstr_length(&formatted)) != -1);
    close(fd);

    atf_dynstr_fini(&formatted);
}

/** Checks if a file exists.
 *
 * \param path Location of the file to check for.
 *
 * \return True if the file exists, false otherwise. */
bool
atf_utils_file_exists(const char *path)
{
    const int ret = access(path, F_OK);
    if (ret == -1) {
        if (errno != ENOENT)
            atf_tc_fail("Failed to check the existence of %s: %s", path,
                        strerror(errno));
        else
            return false;
    } else
        return true;
}

/** Spawns a subprocess and redirects its output to files.
 *
 * Use the atf_utils_wait() function to wait for the completion of the spawned
 * subprocess and validate its exit conditions.
 *
 * \return 0 in the new child; the PID of the new child in the parent.  Does
 * not return in error conditions. */
pid_t
atf_utils_fork(void)
{
    const pid_t pid = fork();
    if (pid == -1)
        atf_tc_fail("fork failed");

    if (pid == 0) {
        atf_dynstr_t out_name;
        init_out_filename(&out_name, getpid(), "out", false);

        atf_dynstr_t err_name;
        init_out_filename(&err_name, getpid(), "err", false);

        atf_utils_redirect(STDOUT_FILENO, atf_dynstr_cstring(&out_name));
        atf_utils_redirect(STDERR_FILENO, atf_dynstr_cstring(&err_name));

        atf_dynstr_fini(&err_name);
        atf_dynstr_fini(&out_name);
    }
    return pid;
}

/** Frees an dynamically-allocated "argv" array.
 *
 * \param argv A dynamically-allocated array of dynamically-allocated
 *     strings. */
void
atf_utils_free_charpp(char **argv)
{
    char **ptr;

    for (ptr = argv; *ptr != NULL; ptr++)
        free(*ptr);

    free(argv);
}

/** Searches for a regexp in a file.
 *
 * \param regex The regexp to look for.
 * \param file The file in which to look for the expression.
 * \param ... Positional parameters to the regex.
 *
 * \return True if there is a match; false otherwise. */
bool
atf_utils_grep_file(const char *regex, const char *file, ...)
{
    int fd;
    va_list ap;
    atf_dynstr_t formatted;
    atf_error_t error;

    va_start(ap, file);
    error = atf_dynstr_init_ap(&formatted, regex, ap);
    va_end(ap);
    ATF_REQUIRE(!atf_is_error(error));

    ATF_REQUIRE((fd = open(file, O_RDONLY)) != -1);
    bool found = false;
    char *line = NULL;
    while (!found && (line = atf_utils_readline(fd)) != NULL) {
        found = grep_string(atf_dynstr_cstring(&formatted), line);
        free(line);
    }
    close(fd);

    atf_dynstr_fini(&formatted);

    return found;
}

/** Searches for a regexp in a string.
 *
 * \param regex The regexp to look for.
 * \param str The string in which to look for the expression.
 * \param ... Positional parameters to the regex.
 *
 * \return True if there is a match; false otherwise. */
bool
atf_utils_grep_string(const char *regex, const char *str, ...)
{
    bool res;
    va_list ap;
    atf_dynstr_t formatted;
    atf_error_t error;

    va_start(ap, str);
    error = atf_dynstr_init_ap(&formatted, regex, ap);
    va_end(ap);
    ATF_REQUIRE(!atf_is_error(error));

    res = grep_string(atf_dynstr_cstring(&formatted), str);

    atf_dynstr_fini(&formatted);

    return res;
}

/** Reads a line of arbitrary length.
 *
 * \param fd The descriptor from which to read the line.
 *
 * \return A pointer to the read line, which must be released with free(), or
 * NULL if there was nothing to read from the file. */
char *
atf_utils_readline(const int fd)
{
    char ch;
    ssize_t cnt;
    atf_dynstr_t temp;
    atf_error_t error;

    error = atf_dynstr_init(&temp);
    ATF_REQUIRE(!atf_is_error(error));

    while ((cnt = read(fd, &ch, sizeof(ch))) == sizeof(ch) &&
           ch != '\n') {
        error = atf_dynstr_append_fmt(&temp, "%c", ch);
        ATF_REQUIRE(!atf_is_error(error));
    }
    ATF_REQUIRE(cnt != -1);

    if (cnt == 0 && atf_dynstr_length(&temp) == 0) {
        atf_dynstr_fini(&temp);
        return NULL;
    } else
        return atf_dynstr_fini_disown(&temp);
}

/** Redirects a file descriptor to a file.
 *
 * \param target_fd The file descriptor to be replaced.
 * \param name The name of the file to direct the descriptor to.
 *
 * \pre Should only be called from the process spawned by fork_for_testing
 * because this exits uncontrolledly.
 * \post Terminates execution if the redirection fails. */
void
atf_utils_redirect(const int target_fd, const char *name)
{
    if (target_fd == STDOUT_FILENO)
        fflush(stdout);
    else if (target_fd == STDERR_FILENO)
        fflush(stderr);

    const int new_fd = open(name, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (new_fd == -1)
        err(EXIT_FAILURE, "Cannot create %s", name);
    if (new_fd != target_fd) {
        if (dup2(new_fd, target_fd) == -1)
            err(EXIT_FAILURE, "Cannot redirect to fd %d", target_fd);
    }
    close(new_fd);
}

/** Waits for a subprocess and validates its exit condition.
 *
 * \param pid The process to be waited for.  Must have been started by
 *     testutils_fork().
 * \param exitstatus Expected exit status.
 * \param expout Expected contents of stdout.
 * \param experr Expected contents of stderr. */
void
atf_utils_wait(const pid_t pid, const int exitstatus, const char *expout,
               const char *experr)
{
    int status;
    ATF_REQUIRE(waitpid(pid, &status, 0) != -1);

    atf_dynstr_t out_name;
    init_out_filename(&out_name, pid, "out", true);

    atf_dynstr_t err_name;
    init_out_filename(&err_name, pid, "err", true);

    atf_utils_cat_file(atf_dynstr_cstring(&out_name), "subprocess stdout: ");
    atf_utils_cat_file(atf_dynstr_cstring(&err_name), "subprocess stderr: ");

    ATF_REQUIRE(WIFEXITED(status));
    ATF_REQUIRE_EQ(exitstatus, WEXITSTATUS(status));

    const char *save_prefix = "save:";
    const size_t save_prefix_length = strlen(save_prefix);

    if (strlen(expout) > save_prefix_length &&
        strncmp(expout, save_prefix, save_prefix_length) == 0) {
        atf_utils_copy_file(atf_dynstr_cstring(&out_name),
                            expout + save_prefix_length);
    } else {
        ATF_REQUIRE(atf_utils_compare_file(atf_dynstr_cstring(&out_name),
                                           expout));
    }

    if (strlen(experr) > save_prefix_length &&
        strncmp(experr, save_prefix, save_prefix_length) == 0) {
        atf_utils_copy_file(atf_dynstr_cstring(&err_name),
                            experr + save_prefix_length);
    } else {
        ATF_REQUIRE(atf_utils_compare_file(atf_dynstr_cstring(&err_name),
                                           experr));
    }

    ATF_REQUIRE(unlink(atf_dynstr_cstring(&out_name)) != -1);
    ATF_REQUIRE(unlink(atf_dynstr_cstring(&err_name)) != -1);
}
