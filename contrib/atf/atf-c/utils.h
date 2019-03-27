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

#if !defined(ATF_C_UTILS_H)
#define ATF_C_UTILS_H

#include <stdbool.h>
#include <unistd.h>

#include <atf-c/defs.h>

void atf_utils_cat_file(const char *, const char *);
bool atf_utils_compare_file(const char *, const char *);
void atf_utils_copy_file(const char *, const char *);
void atf_utils_create_file(const char *, const char *, ...)
    ATF_DEFS_ATTRIBUTE_FORMAT_PRINTF(2, 3);
bool atf_utils_file_exists(const char *);
pid_t atf_utils_fork(void);
void atf_utils_free_charpp(char **);
bool atf_utils_grep_file(const char *, const char *, ...)
    ATF_DEFS_ATTRIBUTE_FORMAT_PRINTF(1, 3);
bool atf_utils_grep_string(const char *, const char *, ...)
    ATF_DEFS_ATTRIBUTE_FORMAT_PRINTF(1, 3);
char *atf_utils_readline(int);
void atf_utils_redirect(const int, const char *);
void atf_utils_wait(const pid_t, const int, const char *, const char *);

#endif /* !defined(ATF_C_UTILS_H) */
