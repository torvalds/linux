/* Copyright (c) 2007 The NetBSD Foundation, Inc.
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

#include "atf-c/detail/fs.h"

#if defined(HAVE_CONFIG_H)
#include "config.h"
#endif

#include <sys/types.h>
#include <sys/param.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <dirent.h>
#include <errno.h>
#include <libgen.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "atf-c/defs.h"
#include "atf-c/detail/sanity.h"
#include "atf-c/detail/text.h"
#include "atf-c/detail/user.h"
#include "atf-c/error.h"

/* ---------------------------------------------------------------------
 * Prototypes for auxiliary functions.
 * --------------------------------------------------------------------- */

static bool check_umask(const mode_t, const mode_t);
static atf_error_t copy_contents(const atf_fs_path_t *, char **);
static mode_t current_umask(void);
static atf_error_t do_mkdtemp(char *);
static atf_error_t normalize(atf_dynstr_t *, char *);
static atf_error_t normalize_ap(atf_dynstr_t *, const char *, va_list);
static void replace_contents(atf_fs_path_t *, const char *);
static const char *stat_type_to_string(const int);

/* ---------------------------------------------------------------------
 * The "invalid_umask" error type.
 * --------------------------------------------------------------------- */

struct invalid_umask_error_data {
    /* One of atf_fs_stat_*_type. */
    int m_type;

    /* The original path causing the error. */
    /* XXX: Ideally this would be an atf_fs_path_t, but if we create it
     * from the error constructor, we cannot delete the path later on.
     * Can't remember why atf_error_new does not take a hook for
     * deletion. */
    char m_path[1024];

    /* The umask that caused the error. */
    mode_t m_umask;
};
typedef struct invalid_umask_error_data invalid_umask_error_data_t;

static
void
invalid_umask_format(const atf_error_t err, char *buf, size_t buflen)
{
    const invalid_umask_error_data_t *data;

    PRE(atf_error_is(err, "invalid_umask"));

    data = atf_error_data(err);
    snprintf(buf, buflen, "Could not create the temporary %s %s because "
             "it will not have enough access rights due to the current "
             "umask %05o", stat_type_to_string(data->m_type),
             data->m_path, (unsigned int)data->m_umask);
}

static
atf_error_t
invalid_umask_error(const atf_fs_path_t *path, const int type,
                    const mode_t failing_mask)
{
    atf_error_t err;
    invalid_umask_error_data_t data;

    data.m_type = type;

    strncpy(data.m_path, atf_fs_path_cstring(path), sizeof(data.m_path));
    data.m_path[sizeof(data.m_path) - 1] = '\0';

    data.m_umask = failing_mask;

    err = atf_error_new("invalid_umask", &data, sizeof(data),
                        invalid_umask_format);

    return err;
}

/* ---------------------------------------------------------------------
 * The "unknown_file_type" error type.
 * --------------------------------------------------------------------- */

struct unknown_type_error_data {
    const char *m_path;
    int m_type;
};
typedef struct unknown_type_error_data unknown_type_error_data_t;

static
void
unknown_type_format(const atf_error_t err, char *buf, size_t buflen)
{
    const unknown_type_error_data_t *data;

    PRE(atf_error_is(err, "unknown_type"));

    data = atf_error_data(err);
    snprintf(buf, buflen, "Unknown file type %d of %s", data->m_type,
             data->m_path);
}

static
atf_error_t
unknown_type_error(const char *path, int type)
{
    atf_error_t err;
    unknown_type_error_data_t data;

    data.m_path = path;
    data.m_type = type;

    err = atf_error_new("unknown_type", &data, sizeof(data),
                        unknown_type_format);

    return err;
}

/* ---------------------------------------------------------------------
 * Auxiliary functions.
 * --------------------------------------------------------------------- */

static
bool
check_umask(const mode_t exp_mode, const mode_t min_mode)
{
    const mode_t actual_mode = (~current_umask() & exp_mode);
    return (actual_mode & min_mode) == min_mode;
}

static
atf_error_t
copy_contents(const atf_fs_path_t *p, char **buf)
{
    atf_error_t err;
    char *str;

    str = (char *)malloc(atf_dynstr_length(&p->m_data) + 1);
    if (str == NULL)
        err = atf_no_memory_error();
    else {
        strcpy(str, atf_dynstr_cstring(&p->m_data));
        *buf = str;
        err = atf_no_error();
    }

    return err;
}

static
mode_t
current_umask(void)
{
    const mode_t current = umask(0);
    (void)umask(current);
    return current;
}

static
atf_error_t
do_mkdtemp(char *tmpl)
{
    atf_error_t err;

    PRE(strstr(tmpl, "XXXXXX") != NULL);

    if (mkdtemp(tmpl) == NULL)
        err = atf_libc_error(errno, "Cannot create temporary directory "
                             "with template '%s'", tmpl);
    else
        err = atf_no_error();

    return err;
}

static
atf_error_t
do_mkstemp(char *tmpl, int *fdout)
{
    atf_error_t err;

    PRE(strstr(tmpl, "XXXXXX") != NULL);

    *fdout = mkstemp(tmpl);
    if (*fdout == -1)
        err = atf_libc_error(errno, "Cannot create temporary file "
                             "with template '%s'", tmpl);

    else
        err = atf_no_error();

    return err;
}

static
atf_error_t
normalize(atf_dynstr_t *d, char *p)
{
    const char *ptr;
    char *last;
    atf_error_t err;
    bool first;

    PRE(strlen(p) > 0);
    PRE(atf_dynstr_length(d) == 0);

    if (p[0] == '/')
        err = atf_dynstr_append_fmt(d, "/");
    else
        err = atf_no_error();

    first = true;
    last = NULL; /* Silence GCC warning. */
    ptr = strtok_r(p, "/", &last);
    while (!atf_is_error(err) && ptr != NULL) {
        if (strlen(ptr) > 0) {
            err = atf_dynstr_append_fmt(d, "%s%s", first ? "" : "/", ptr);
            first = false;
        }

        ptr = strtok_r(NULL, "/", &last);
    }

    return err;
}

static
atf_error_t
normalize_ap(atf_dynstr_t *d, const char *p, va_list ap)
{
    char *str;
    atf_error_t err;
    va_list ap2;

    err = atf_dynstr_init(d);
    if (atf_is_error(err))
        goto out;

    va_copy(ap2, ap);
    err = atf_text_format_ap(&str, p, ap2);
    va_end(ap2);
    if (atf_is_error(err))
        atf_dynstr_fini(d);
    else {
        err = normalize(d, str);
        free(str);
    }

out:
    return err;
}

static
void
replace_contents(atf_fs_path_t *p, const char *buf)
{
    atf_error_t err;

    PRE(atf_dynstr_length(&p->m_data) == strlen(buf));

    atf_dynstr_clear(&p->m_data);
    err = atf_dynstr_append_fmt(&p->m_data, "%s", buf);

    INV(!atf_is_error(err));
}

static
const char *
stat_type_to_string(const int type)
{
    const char *str;

    if (type == atf_fs_stat_blk_type)
        str = "block device";
    else if (type == atf_fs_stat_chr_type)
        str = "character device";
    else if (type == atf_fs_stat_dir_type)
        str = "directory";
    else if (type == atf_fs_stat_fifo_type)
        str = "named pipe";
    else if (type == atf_fs_stat_lnk_type)
        str = "symbolic link";
    else if (type == atf_fs_stat_reg_type)
        str = "regular file";
    else if (type == atf_fs_stat_sock_type)
        str = "socket";
    else if (type == atf_fs_stat_wht_type)
        str = "whiteout";
    else {
        UNREACHABLE;
        str = NULL;
    }

    return str;
}

/* ---------------------------------------------------------------------
 * The "atf_fs_path" type.
 * --------------------------------------------------------------------- */

/*
 * Constructors/destructors.
 */

atf_error_t
atf_fs_path_init_ap(atf_fs_path_t *p, const char *fmt, va_list ap)
{
    atf_error_t err;
    va_list ap2;

    va_copy(ap2, ap);
    err = normalize_ap(&p->m_data, fmt, ap2);
    va_end(ap2);

    return err;
}

atf_error_t
atf_fs_path_init_fmt(atf_fs_path_t *p, const char *fmt, ...)
{
    va_list ap;
    atf_error_t err;

    va_start(ap, fmt);
    err = atf_fs_path_init_ap(p, fmt, ap);
    va_end(ap);

    return err;
}

atf_error_t
atf_fs_path_copy(atf_fs_path_t *dest, const atf_fs_path_t *src)
{
    return atf_dynstr_copy(&dest->m_data, &src->m_data);
}

void
atf_fs_path_fini(atf_fs_path_t *p)
{
    atf_dynstr_fini(&p->m_data);
}

/*
 * Getters.
 */

atf_error_t
atf_fs_path_branch_path(const atf_fs_path_t *p, atf_fs_path_t *bp)
{
    const size_t endpos = atf_dynstr_rfind_ch(&p->m_data, '/');
    atf_error_t err;

    if (endpos == atf_dynstr_npos)
        err = atf_fs_path_init_fmt(bp, ".");
    else if (endpos == 0)
        err = atf_fs_path_init_fmt(bp, "/");
    else
        err = atf_dynstr_init_substr(&bp->m_data, &p->m_data, 0, endpos);

#if defined(HAVE_CONST_DIRNAME)
    INV(atf_equal_dynstr_cstring(&bp->m_data,
                                 dirname(atf_dynstr_cstring(&p->m_data))));
#endif /* defined(HAVE_CONST_DIRNAME) */

    return err;
}

const char *
atf_fs_path_cstring(const atf_fs_path_t *p)
{
    return atf_dynstr_cstring(&p->m_data);
}

atf_error_t
atf_fs_path_leaf_name(const atf_fs_path_t *p, atf_dynstr_t *ln)
{
    size_t begpos = atf_dynstr_rfind_ch(&p->m_data, '/');
    atf_error_t err;

    if (begpos == atf_dynstr_npos)
        begpos = 0;
    else
        begpos++;

    err = atf_dynstr_init_substr(ln, &p->m_data, begpos, atf_dynstr_npos);

#if defined(HAVE_CONST_BASENAME)
    INV(atf_equal_dynstr_cstring(ln,
                                 basename(atf_dynstr_cstring(&p->m_data))));
#endif /* defined(HAVE_CONST_BASENAME) */

    return err;
}

bool
atf_fs_path_is_absolute(const atf_fs_path_t *p)
{
    return atf_dynstr_cstring(&p->m_data)[0] == '/';
}

bool
atf_fs_path_is_root(const atf_fs_path_t *p)
{
    return atf_equal_dynstr_cstring(&p->m_data, "/");
}

/*
 * Modifiers.
 */

atf_error_t
atf_fs_path_append_ap(atf_fs_path_t *p, const char *fmt, va_list ap)
{
    atf_dynstr_t aux;
    atf_error_t err;
    va_list ap2;

    va_copy(ap2, ap);
    err = normalize_ap(&aux, fmt, ap2);
    va_end(ap2);
    if (!atf_is_error(err)) {
        const char *auxstr = atf_dynstr_cstring(&aux);
        const bool needslash = auxstr[0] != '/';

        err = atf_dynstr_append_fmt(&p->m_data, "%s%s",
                                    needslash ? "/" : "", auxstr);

        atf_dynstr_fini(&aux);
    }

    return err;
}

atf_error_t
atf_fs_path_append_fmt(atf_fs_path_t *p, const char *fmt, ...)
{
    va_list ap;
    atf_error_t err;

    va_start(ap, fmt);
    err = atf_fs_path_append_ap(p, fmt, ap);
    va_end(ap);

    return err;
}

atf_error_t
atf_fs_path_append_path(atf_fs_path_t *p, const atf_fs_path_t *p2)
{
    return atf_fs_path_append_fmt(p, "%s", atf_dynstr_cstring(&p2->m_data));
}

atf_error_t
atf_fs_path_to_absolute(const atf_fs_path_t *p, atf_fs_path_t *pa)
{
    atf_error_t err;

    PRE(!atf_fs_path_is_absolute(p));

    err = atf_fs_getcwd(pa);
    if (atf_is_error(err))
        goto out;

    err = atf_fs_path_append_path(pa, p);
    if (atf_is_error(err))
        atf_fs_path_fini(pa);

out:
    return err;
}

/*
 * Operators.
 */

bool atf_equal_fs_path_fs_path(const atf_fs_path_t *p1,
                               const atf_fs_path_t *p2)
{
    return atf_equal_dynstr_dynstr(&p1->m_data, &p2->m_data);
}

/* ---------------------------------------------------------------------
 * The "atf_fs_path" type.
 * --------------------------------------------------------------------- */

/*
 * Constants.
 */

const int atf_fs_stat_blk_type  = 1;
const int atf_fs_stat_chr_type  = 2;
const int atf_fs_stat_dir_type  = 3;
const int atf_fs_stat_fifo_type = 4;
const int atf_fs_stat_lnk_type  = 5;
const int atf_fs_stat_reg_type  = 6;
const int atf_fs_stat_sock_type = 7;
const int atf_fs_stat_wht_type  = 8;

/*
 * Constructors/destructors.
 */

atf_error_t
atf_fs_stat_init(atf_fs_stat_t *st, const atf_fs_path_t *p)
{
    atf_error_t err;
    const char *pstr = atf_fs_path_cstring(p);

    if (lstat(pstr, &st->m_sb) == -1) {
        err = atf_libc_error(errno, "Cannot get information of %s; "
                             "lstat(2) failed", pstr);
    } else {
        int type = st->m_sb.st_mode & S_IFMT;
        err = atf_no_error();
        switch (type) {
            case S_IFBLK:  st->m_type = atf_fs_stat_blk_type;  break;
            case S_IFCHR:  st->m_type = atf_fs_stat_chr_type;  break;
            case S_IFDIR:  st->m_type = atf_fs_stat_dir_type;  break;
            case S_IFIFO:  st->m_type = atf_fs_stat_fifo_type; break;
            case S_IFLNK:  st->m_type = atf_fs_stat_lnk_type;  break;
            case S_IFREG:  st->m_type = atf_fs_stat_reg_type;  break;
            case S_IFSOCK: st->m_type = atf_fs_stat_sock_type; break;
#if defined(S_IFWHT)
            case S_IFWHT:  st->m_type = atf_fs_stat_wht_type;  break;
#endif
            default:
                err = unknown_type_error(pstr, type);
        }
    }

    return err;
}

void
atf_fs_stat_copy(atf_fs_stat_t *dest, const atf_fs_stat_t *src)
{
    dest->m_type = src->m_type;
    dest->m_sb = src->m_sb;
}

void
atf_fs_stat_fini(atf_fs_stat_t *st ATF_DEFS_ATTRIBUTE_UNUSED)
{
}

/*
 * Getters.
 */

dev_t
atf_fs_stat_get_device(const atf_fs_stat_t *st)
{
    return st->m_sb.st_dev;
}

ino_t
atf_fs_stat_get_inode(const atf_fs_stat_t *st)
{
    return st->m_sb.st_ino;
}

mode_t
atf_fs_stat_get_mode(const atf_fs_stat_t *st)
{
    return st->m_sb.st_mode & ~S_IFMT;
}

off_t
atf_fs_stat_get_size(const atf_fs_stat_t *st)
{
    return st->m_sb.st_size;
}

int
atf_fs_stat_get_type(const atf_fs_stat_t *st)
{
    return st->m_type;
}

bool
atf_fs_stat_is_owner_readable(const atf_fs_stat_t *st)
{
    return st->m_sb.st_mode & S_IRUSR;
}

bool
atf_fs_stat_is_owner_writable(const atf_fs_stat_t *st)
{
    return st->m_sb.st_mode & S_IWUSR;
}

bool
atf_fs_stat_is_owner_executable(const atf_fs_stat_t *st)
{
    return st->m_sb.st_mode & S_IXUSR;
}

bool
atf_fs_stat_is_group_readable(const atf_fs_stat_t *st)
{
    return st->m_sb.st_mode & S_IRGRP;
}

bool
atf_fs_stat_is_group_writable(const atf_fs_stat_t *st)
{
    return st->m_sb.st_mode & S_IWGRP;
}

bool
atf_fs_stat_is_group_executable(const atf_fs_stat_t *st)
{
    return st->m_sb.st_mode & S_IXGRP;
}

bool
atf_fs_stat_is_other_readable(const atf_fs_stat_t *st)
{
    return st->m_sb.st_mode & S_IROTH;
}

bool
atf_fs_stat_is_other_writable(const atf_fs_stat_t *st)
{
    return st->m_sb.st_mode & S_IWOTH;
}

bool
atf_fs_stat_is_other_executable(const atf_fs_stat_t *st)
{
    return st->m_sb.st_mode & S_IXOTH;
}

/* ---------------------------------------------------------------------
 * Free functions.
 * --------------------------------------------------------------------- */

const int atf_fs_access_f = 1 << 0;
const int atf_fs_access_r = 1 << 1;
const int atf_fs_access_w = 1 << 2;
const int atf_fs_access_x = 1 << 3;

/*
 * An implementation of access(2) but using the effective user value
 * instead of the real one.  Also avoids false positives for root when
 * asking for execute permissions, which appear in SunOS.
 */
atf_error_t
atf_fs_eaccess(const atf_fs_path_t *p, int mode)
{
    atf_error_t err;
    struct stat st;
    bool ok;

    PRE(mode & atf_fs_access_f || mode & atf_fs_access_r ||
        mode & atf_fs_access_w || mode & atf_fs_access_x);

    if (lstat(atf_fs_path_cstring(p), &st) == -1) {
        err = atf_libc_error(errno, "Cannot get information from file %s",
                             atf_fs_path_cstring(p));
        goto out;
    }

    err = atf_no_error();

    /* Early return if we are only checking for existence and the file
     * exists (stat call returned). */
    if (mode & atf_fs_access_f)
        goto out;

    ok = false;
    if (atf_user_is_root()) {
        if (!ok && !(mode & atf_fs_access_x)) {
            /* Allow root to read/write any file. */
            ok = true;
        }

        if (!ok && (st.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH))) {
            /* Allow root to execute the file if any of its execution bits
             * are set. */
            ok = true;
        }
    } else {
        if (!ok && (atf_user_euid() == st.st_uid)) {
            ok = ((mode & atf_fs_access_r) && (st.st_mode & S_IRUSR)) ||
                 ((mode & atf_fs_access_w) && (st.st_mode & S_IWUSR)) ||
                 ((mode & atf_fs_access_x) && (st.st_mode & S_IXUSR));
        }
        if (!ok && atf_user_is_member_of_group(st.st_gid)) {
            ok = ((mode & atf_fs_access_r) && (st.st_mode & S_IRGRP)) ||
                 ((mode & atf_fs_access_w) && (st.st_mode & S_IWGRP)) ||
                 ((mode & atf_fs_access_x) && (st.st_mode & S_IXGRP));
        }
        if (!ok && ((atf_user_euid() != st.st_uid) &&
                    !atf_user_is_member_of_group(st.st_gid))) {
            ok = ((mode & atf_fs_access_r) && (st.st_mode & S_IROTH)) ||
                 ((mode & atf_fs_access_w) && (st.st_mode & S_IWOTH)) ||
                 ((mode & atf_fs_access_x) && (st.st_mode & S_IXOTH));
        }
    }

    if (!ok)
        err = atf_libc_error(EACCES, "Access check failed");

out:
    return err;
}

atf_error_t
atf_fs_exists(const atf_fs_path_t *p, bool *b)
{
    atf_error_t err;

    err = atf_fs_eaccess(p, atf_fs_access_f);
    if (atf_is_error(err)) {
        if (atf_error_is(err, "libc") && atf_libc_error_code(err) == ENOENT) {
            atf_error_free(err);
            err = atf_no_error();
            *b = false;
        }
    } else
        *b = true;

    return err;
}

atf_error_t
atf_fs_getcwd(atf_fs_path_t *p)
{
    atf_error_t err;
    char *cwd;

#if defined(HAVE_GETCWD_DYN)
    cwd = getcwd(NULL, 0);
#else
    cwd = getcwd(NULL, MAXPATHLEN);
#endif
    if (cwd == NULL) {
        err = atf_libc_error(errno, "Cannot determine current directory");
        goto out;
    }

    err = atf_fs_path_init_fmt(p, "%s", cwd);
    free(cwd);

out:
    return err;
}

atf_error_t
atf_fs_mkdtemp(atf_fs_path_t *p)
{
    atf_error_t err;
    char *buf;

    if (!check_umask(S_IRWXU, S_IRWXU)) {
        err = invalid_umask_error(p, atf_fs_stat_dir_type, current_umask());
        goto out;
    }

    err = copy_contents(p, &buf);
    if (atf_is_error(err))
        goto out;

    err = do_mkdtemp(buf);
    if (atf_is_error(err))
        goto out_buf;

    replace_contents(p, buf);

    INV(!atf_is_error(err));
out_buf:
    free(buf);
out:
    return err;
}

atf_error_t
atf_fs_mkstemp(atf_fs_path_t *p, int *fdout)
{
    atf_error_t err;
    char *buf;
    int fd;

    if (!check_umask(S_IRWXU, S_IRWXU)) {
        err = invalid_umask_error(p, atf_fs_stat_reg_type, current_umask());
        goto out;
    }

    err = copy_contents(p, &buf);
    if (atf_is_error(err))
        goto out;

    err = do_mkstemp(buf, &fd);
    if (atf_is_error(err))
        goto out_buf;

    replace_contents(p, buf);
    *fdout = fd;

    INV(!atf_is_error(err));
out_buf:
    free(buf);
out:
    return err;
}

atf_error_t
atf_fs_rmdir(const atf_fs_path_t *p)
{
    atf_error_t err;

    if (rmdir(atf_fs_path_cstring(p))) {
        if (errno == EEXIST) {
            /* Some operating systems (e.g. OpenSolaris 200906) return
             * EEXIST instead of ENOTEMPTY for non-empty directories.
             * Homogenize the return value so that callers don't need
             * to bother about differences in operating systems. */
            errno = ENOTEMPTY;
        }
        err = atf_libc_error(errno, "Cannot remove directory");
    } else
        err = atf_no_error();

    return err;
}

atf_error_t
atf_fs_unlink(const atf_fs_path_t *p)
{
    atf_error_t err;
    const char *path;

    path = atf_fs_path_cstring(p);

    if (unlink(path) != 0)
        err = atf_libc_error(errno, "Cannot unlink file: '%s'", path);
    else
        err = atf_no_error();

    return err;
}
