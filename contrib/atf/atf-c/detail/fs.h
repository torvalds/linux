/* Copyright (c) 2008 The NetBSD Foundation, Inc.
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

#if !defined(ATF_C_DETAIL_FS_H)
#define ATF_C_DETAIL_FS_H

#include <sys/types.h>
#include <sys/stat.h>

#include <stdarg.h>
#include <stdbool.h>

#include <atf-c/detail/dynstr.h>
#include <atf-c/error_fwd.h>

/* ---------------------------------------------------------------------
 * The "atf_fs_path" type.
 * --------------------------------------------------------------------- */

struct atf_fs_path {
    atf_dynstr_t m_data;
};
typedef struct atf_fs_path atf_fs_path_t;

/* Constructors/destructors. */
atf_error_t atf_fs_path_init_ap(atf_fs_path_t *, const char *, va_list);
atf_error_t atf_fs_path_init_fmt(atf_fs_path_t *, const char *, ...);
atf_error_t atf_fs_path_copy(atf_fs_path_t *, const atf_fs_path_t *);
void atf_fs_path_fini(atf_fs_path_t *);

/* Getters. */
atf_error_t atf_fs_path_branch_path(const atf_fs_path_t *, atf_fs_path_t *);
const char *atf_fs_path_cstring(const atf_fs_path_t *);
atf_error_t atf_fs_path_leaf_name(const atf_fs_path_t *, atf_dynstr_t *);
bool atf_fs_path_is_absolute(const atf_fs_path_t *);
bool atf_fs_path_is_root(const atf_fs_path_t *);

/* Modifiers. */
atf_error_t atf_fs_path_append_ap(atf_fs_path_t *, const char *, va_list);
atf_error_t atf_fs_path_append_fmt(atf_fs_path_t *, const char *, ...);
atf_error_t atf_fs_path_append_path(atf_fs_path_t *, const atf_fs_path_t *);
atf_error_t atf_fs_path_to_absolute(const atf_fs_path_t *, atf_fs_path_t *);

/* Operators. */
bool atf_equal_fs_path_fs_path(const atf_fs_path_t *,
                               const atf_fs_path_t *);

/* ---------------------------------------------------------------------
 * The "atf_fs_stat" type.
 * --------------------------------------------------------------------- */

struct atf_fs_stat {
    int m_type;
    struct stat m_sb;
};
typedef struct atf_fs_stat atf_fs_stat_t;

/* Constants. */
extern const int atf_fs_stat_blk_type;
extern const int atf_fs_stat_chr_type;
extern const int atf_fs_stat_dir_type;
extern const int atf_fs_stat_fifo_type;
extern const int atf_fs_stat_lnk_type;
extern const int atf_fs_stat_reg_type;
extern const int atf_fs_stat_sock_type;
extern const int atf_fs_stat_wht_type;

/* Constructors/destructors. */
atf_error_t atf_fs_stat_init(atf_fs_stat_t *, const atf_fs_path_t *);
void atf_fs_stat_copy(atf_fs_stat_t *, const atf_fs_stat_t *);
void atf_fs_stat_fini(atf_fs_stat_t *);

/* Getters. */
dev_t atf_fs_stat_get_device(const atf_fs_stat_t *);
ino_t atf_fs_stat_get_inode(const atf_fs_stat_t *);
mode_t atf_fs_stat_get_mode(const atf_fs_stat_t *);
off_t atf_fs_stat_get_size(const atf_fs_stat_t *);
int atf_fs_stat_get_type(const atf_fs_stat_t *);
bool atf_fs_stat_is_owner_readable(const atf_fs_stat_t *);
bool atf_fs_stat_is_owner_writable(const atf_fs_stat_t *);
bool atf_fs_stat_is_owner_executable(const atf_fs_stat_t *);
bool atf_fs_stat_is_group_readable(const atf_fs_stat_t *);
bool atf_fs_stat_is_group_writable(const atf_fs_stat_t *);
bool atf_fs_stat_is_group_executable(const atf_fs_stat_t *);
bool atf_fs_stat_is_other_readable(const atf_fs_stat_t *);
bool atf_fs_stat_is_other_writable(const atf_fs_stat_t *);
bool atf_fs_stat_is_other_executable(const atf_fs_stat_t *);

/* ---------------------------------------------------------------------
 * Free functions.
 * --------------------------------------------------------------------- */

extern const int atf_fs_access_f;
extern const int atf_fs_access_r;
extern const int atf_fs_access_w;
extern const int atf_fs_access_x;

atf_error_t atf_fs_eaccess(const atf_fs_path_t *, int);
atf_error_t atf_fs_exists(const atf_fs_path_t *, bool *);
atf_error_t atf_fs_getcwd(atf_fs_path_t *);
atf_error_t atf_fs_mkdtemp(atf_fs_path_t *);
atf_error_t atf_fs_mkstemp(atf_fs_path_t *, int *);
atf_error_t atf_fs_rmdir(const atf_fs_path_t *);
atf_error_t atf_fs_unlink(const atf_fs_path_t *);

#endif /* !defined(ATF_C_DETAIL_FS_H) */
