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

#include <sys/types.h>
#include <sys/stat.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <atf-c.h>

#include "atf-c/detail/test_helpers.h"
#include "atf-c/detail/user.h"

/* ---------------------------------------------------------------------
 * Auxiliary functions.
 * --------------------------------------------------------------------- */

static
void
create_dir(const char *p, int mode)
{
    int ret;

    ret = mkdir(p, mode);
    if (ret == -1)
        atf_tc_fail("Could not create helper directory %s", p);
}

static
void
create_file(const char *p, int mode)
{
    int fd;

    fd = open(p, O_CREAT | O_WRONLY | O_TRUNC, mode);
    if (fd == -1)
        atf_tc_fail("Could not create helper file %s", p);
    close(fd);
}

static
bool
exists(const atf_fs_path_t *p)
{
    return access(atf_fs_path_cstring(p), F_OK) == 0;
}

static
atf_error_t
mkstemp_discard_fd(atf_fs_path_t *p)
{
    int fd;
    atf_error_t err = atf_fs_mkstemp(p, &fd);
    if (!atf_is_error(err))
        close(fd);
    return err;
}

/* ---------------------------------------------------------------------
 * Test cases for the "atf_fs_path" type.
 * --------------------------------------------------------------------- */

ATF_TC(path_normalize);
ATF_TC_HEAD(path_normalize, tc)
{
    atf_tc_set_md_var(tc, "descr", "Tests the path's normalization");
}
ATF_TC_BODY(path_normalize, tc)
{
    struct test {
        const char *in;
        const char *out;
    } tests[] = {
        { ".", ".", },
        { "..", "..", },

        { "/", "/", },
        { "//", "/", }, /* NO_CHECK_STYLE */
        { "///", "/", }, /* NO_CHECK_STYLE */

        { "foo", "foo", },
        { "foo/", "foo", },
        { "foo/bar", "foo/bar", },
        { "foo/bar/", "foo/bar", },

        { "/foo", "/foo", },
        { "/foo/bar", "/foo/bar", },
        { "/foo/bar/", "/foo/bar", },

        { "///foo", "/foo", }, /* NO_CHECK_STYLE */
        { "///foo///bar", "/foo/bar", }, /* NO_CHECK_STYLE */
        { "///foo///bar///", "/foo/bar", }, /* NO_CHECK_STYLE */

        { NULL, NULL }
    };
    struct test *t;

    for (t = &tests[0]; t->in != NULL; t++) {
        atf_fs_path_t p;

        printf("Input          : >%s<\n", t->in);
        printf("Expected output: >%s<\n", t->out);

        RE(atf_fs_path_init_fmt(&p, "%s", t->in));
        printf("Output         : >%s<\n", atf_fs_path_cstring(&p));
        ATF_REQUIRE(strcmp(atf_fs_path_cstring(&p), t->out) == 0);
        atf_fs_path_fini(&p);

        printf("\n");
    }
}

ATF_TC(path_copy);
ATF_TC_HEAD(path_copy, tc)
{
    atf_tc_set_md_var(tc, "descr", "Tests the atf_fs_path_copy constructor");
}
ATF_TC_BODY(path_copy, tc)
{
    atf_fs_path_t str, str2;

    RE(atf_fs_path_init_fmt(&str, "foo"));
    RE(atf_fs_path_copy(&str2, &str));

    ATF_REQUIRE(atf_equal_fs_path_fs_path(&str, &str2));

    RE(atf_fs_path_append_fmt(&str2, "bar"));

    ATF_REQUIRE(!atf_equal_fs_path_fs_path(&str, &str2));

    atf_fs_path_fini(&str2);
    atf_fs_path_fini(&str);
}

ATF_TC(path_is_absolute);
ATF_TC_HEAD(path_is_absolute, tc)
{
    atf_tc_set_md_var(tc, "descr", "Tests the path::is_absolute function");
}
ATF_TC_BODY(path_is_absolute, tc)
{
    struct test {
        const char *in;
        bool abs;
    } tests[] = {
        { "/", true },
        { "////", true }, /* NO_CHECK_STYLE */
        { "////a", true }, /* NO_CHECK_STYLE */
        { "//a//", true }, /* NO_CHECK_STYLE */
        { "a////", false }, /* NO_CHECK_STYLE */
        { "../foo", false },
        { NULL, false },
    };
    struct test *t;

    for (t = &tests[0]; t->in != NULL; t++) {
        atf_fs_path_t p;

        printf("Input          : %s\n", t->in);
        printf("Expected result: %s\n", t->abs ? "true" : "false");

        RE(atf_fs_path_init_fmt(&p, "%s", t->in));
        printf("Result         : %s\n",
               atf_fs_path_is_absolute(&p) ? "true" : "false");
        if (t->abs)
            ATF_REQUIRE(atf_fs_path_is_absolute(&p));
        else
            ATF_REQUIRE(!atf_fs_path_is_absolute(&p));
        atf_fs_path_fini(&p);

        printf("\n");
    }
}

ATF_TC(path_is_root);
ATF_TC_HEAD(path_is_root, tc)
{
    atf_tc_set_md_var(tc, "descr", "Tests the path::is_root function");
}
ATF_TC_BODY(path_is_root, tc)
{
    struct test {
        const char *in;
        bool root;
    } tests[] = {
        { "/", true },
        { "////", true }, /* NO_CHECK_STYLE */
        { "////a", false }, /* NO_CHECK_STYLE */
        { "//a//", false }, /* NO_CHECK_STYLE */
        { "a////", false }, /* NO_CHECK_STYLE */
        { "../foo", false },
        { NULL, false },
    };
    struct test *t;

    for (t = &tests[0]; t->in != NULL; t++) {
        atf_fs_path_t p;

        printf("Input          : %s\n", t->in);
        printf("Expected result: %s\n", t->root ? "true" : "false");

        RE(atf_fs_path_init_fmt(&p, "%s", t->in));
        printf("Result         : %s\n",
               atf_fs_path_is_root(&p) ? "true" : "false");
        if (t->root)
            ATF_REQUIRE(atf_fs_path_is_root(&p));
        else
            ATF_REQUIRE(!atf_fs_path_is_root(&p));
        atf_fs_path_fini(&p);

        printf("\n");
    }
}

ATF_TC(path_branch_path);
ATF_TC_HEAD(path_branch_path, tc)
{
    atf_tc_set_md_var(tc, "descr", "Tests the atf_fs_path_branch_path "
                      "function");
}
ATF_TC_BODY(path_branch_path, tc)
{
    struct test {
        const char *in;
        const char *branch;
    } tests[] = {
        { ".", "." },
        { "foo", "." },
        { "foo/bar", "foo" },
        { "/foo", "/" },
        { "/foo/bar", "/foo" },
        { NULL, NULL },
    };
    struct test *t;

    for (t = &tests[0]; t->in != NULL; t++) {
        atf_fs_path_t p, bp;

        printf("Input          : %s\n", t->in);
        printf("Expected output: %s\n", t->branch);

        RE(atf_fs_path_init_fmt(&p, "%s", t->in));
        RE(atf_fs_path_branch_path(&p, &bp));
        printf("Output         : %s\n", atf_fs_path_cstring(&bp));
        ATF_REQUIRE(strcmp(atf_fs_path_cstring(&bp), t->branch) == 0);
        atf_fs_path_fini(&bp);
        atf_fs_path_fini(&p);

        printf("\n");
    }
}

ATF_TC(path_leaf_name);
ATF_TC_HEAD(path_leaf_name, tc)
{
    atf_tc_set_md_var(tc, "descr", "Tests the atf_fs_path_leaf_name "
                      "function");
}
ATF_TC_BODY(path_leaf_name, tc)
{
    struct test {
        const char *in;
        const char *leaf;
    } tests[] = {
        { ".", "." },
        { "foo", "foo" },
        { "foo/bar", "bar" },
        { "/foo", "foo" },
        { "/foo/bar", "bar" },
        { NULL, NULL },
    };
    struct test *t;

    for (t = &tests[0]; t->in != NULL; t++) {
        atf_fs_path_t p;
        atf_dynstr_t ln;

        printf("Input          : %s\n", t->in);
        printf("Expected output: %s\n", t->leaf);

        RE(atf_fs_path_init_fmt(&p, "%s", t->in));
        RE(atf_fs_path_leaf_name(&p, &ln));
        printf("Output         : %s\n", atf_dynstr_cstring(&ln));
        ATF_REQUIRE(atf_equal_dynstr_cstring(&ln, t->leaf));
        atf_dynstr_fini(&ln);
        atf_fs_path_fini(&p);

        printf("\n");
    }
}

ATF_TC(path_append);
ATF_TC_HEAD(path_append, tc)
{
    atf_tc_set_md_var(tc, "descr", "Tests the concatenation of multiple "
                      "paths");
}
ATF_TC_BODY(path_append, tc)
{
    struct test {
        const char *in;
        const char *ap;
        const char *out;
    } tests[] = {
        { "foo", "bar", "foo/bar" },
        { "foo/", "/bar", "foo/bar" },
        { "foo/", "/bar/baz", "foo/bar/baz" },
        { "foo/", "///bar///baz", "foo/bar/baz" }, /* NO_CHECK_STYLE */

        { NULL, NULL, NULL }
    };
    struct test *t;

    for (t = &tests[0]; t->in != NULL; t++) {
        atf_fs_path_t p;

        printf("Input          : >%s<\n", t->in);
        printf("Append         : >%s<\n", t->ap);
        printf("Expected output: >%s<\n", t->out);

        RE(atf_fs_path_init_fmt(&p, "%s", t->in));

        RE(atf_fs_path_append_fmt(&p, "%s", t->ap));

        printf("Output         : >%s<\n", atf_fs_path_cstring(&p));
        ATF_REQUIRE(strcmp(atf_fs_path_cstring(&p), t->out) == 0);

        atf_fs_path_fini(&p);

        printf("\n");
    }
}

ATF_TC(path_to_absolute);
ATF_TC_HEAD(path_to_absolute, tc)
{
    atf_tc_set_md_var(tc, "descr", "Tests the atf_fs_path_to_absolute "
                      "function");
}
ATF_TC_BODY(path_to_absolute, tc)
{
    const char *names[] = { ".", "dir", NULL };
    const char **n;

    ATF_REQUIRE(mkdir("dir", 0755) != -1);

    for (n = names; *n != NULL; n++) {
        atf_fs_path_t p, p2;
        atf_fs_stat_t st1, st2;

        RE(atf_fs_path_init_fmt(&p, "%s", *n));
        RE(atf_fs_stat_init(&st1, &p));
        printf("Relative path: %s\n", atf_fs_path_cstring(&p));

        RE(atf_fs_path_to_absolute(&p, &p2));
        printf("Absolute path: %s\n", atf_fs_path_cstring(&p2));

        ATF_REQUIRE(atf_fs_path_is_absolute(&p2));
        RE(atf_fs_stat_init(&st2, &p2));

        ATF_REQUIRE_EQ(atf_fs_stat_get_device(&st1),
                        atf_fs_stat_get_device(&st2));
        ATF_REQUIRE_EQ(atf_fs_stat_get_inode(&st1),
                        atf_fs_stat_get_inode(&st2));

        atf_fs_stat_fini(&st2);
        atf_fs_stat_fini(&st1);
        atf_fs_path_fini(&p2);
        atf_fs_path_fini(&p);

        printf("\n");
    }
}

ATF_TC(path_equal);
ATF_TC_HEAD(path_equal, tc)
{
    atf_tc_set_md_var(tc, "descr", "Tests the equality operators for paths");
}
ATF_TC_BODY(path_equal, tc)
{
    atf_fs_path_t p1, p2;

    RE(atf_fs_path_init_fmt(&p1, "foo"));

    RE(atf_fs_path_init_fmt(&p2, "foo"));
    ATF_REQUIRE(atf_equal_fs_path_fs_path(&p1, &p2));
    atf_fs_path_fini(&p2);

    RE(atf_fs_path_init_fmt(&p2, "bar"));
    ATF_REQUIRE(!atf_equal_fs_path_fs_path(&p1, &p2));
    atf_fs_path_fini(&p2);

    atf_fs_path_fini(&p1);
}

/* ---------------------------------------------------------------------
 * Test cases for the "atf_fs_stat" type.
 * --------------------------------------------------------------------- */

ATF_TC(stat_mode);
ATF_TC_HEAD(stat_mode, tc)
{
    atf_tc_set_md_var(tc, "descr", "Tests the atf_fs_stat_get_mode function "
                      "and, indirectly, the constructor");
}
ATF_TC_BODY(stat_mode, tc)
{
    atf_fs_path_t p;
    atf_fs_stat_t st;

    create_file("f1", 0400);
    create_file("f2", 0644);

    RE(atf_fs_path_init_fmt(&p, "f1"));
    RE(atf_fs_stat_init(&st, &p));
    ATF_CHECK_EQ(0400, atf_fs_stat_get_mode(&st));
    atf_fs_stat_fini(&st);
    atf_fs_path_fini(&p);

    RE(atf_fs_path_init_fmt(&p, "f2"));
    RE(atf_fs_stat_init(&st, &p));
    ATF_CHECK_EQ(0644, atf_fs_stat_get_mode(&st));
    atf_fs_stat_fini(&st);
    atf_fs_path_fini(&p);
}

ATF_TC(stat_type);
ATF_TC_HEAD(stat_type, tc)
{
    atf_tc_set_md_var(tc, "descr", "Tests the atf_fs_stat_get_type function "
                      "and, indirectly, the constructor");
}
ATF_TC_BODY(stat_type, tc)
{
    atf_fs_path_t p;
    atf_fs_stat_t st;

    create_dir("dir", 0755);
    create_file("reg", 0644);

    RE(atf_fs_path_init_fmt(&p, "dir"));
    RE(atf_fs_stat_init(&st, &p));
    ATF_REQUIRE_EQ(atf_fs_stat_get_type(&st), atf_fs_stat_dir_type);
    atf_fs_stat_fini(&st);
    atf_fs_path_fini(&p);

    RE(atf_fs_path_init_fmt(&p, "reg"));
    RE(atf_fs_stat_init(&st, &p));
    ATF_REQUIRE_EQ(atf_fs_stat_get_type(&st), atf_fs_stat_reg_type);
    atf_fs_stat_fini(&st);
    atf_fs_path_fini(&p);
}

ATF_TC(stat_perms);
ATF_TC_HEAD(stat_perms, tc)
{
    atf_tc_set_md_var(tc, "descr", "Tests the atf_fs_stat_is_* functions");
}
ATF_TC_BODY(stat_perms, tc)
{
    atf_fs_path_t p;
    atf_fs_stat_t st;

    create_file("reg", 0);

    RE(atf_fs_path_init_fmt(&p, "reg"));

#define perms(ur, uw, ux, gr, gw, gx, othr, othw, othx) \
    { \
        RE(atf_fs_stat_init(&st, &p)); \
        ATF_REQUIRE(atf_fs_stat_is_owner_readable(&st) == ur); \
        ATF_REQUIRE(atf_fs_stat_is_owner_writable(&st) == uw); \
        ATF_REQUIRE(atf_fs_stat_is_owner_executable(&st) == ux); \
        ATF_REQUIRE(atf_fs_stat_is_group_readable(&st) == gr); \
        ATF_REQUIRE(atf_fs_stat_is_group_writable(&st) == gw); \
        ATF_REQUIRE(atf_fs_stat_is_group_executable(&st) == gx); \
        ATF_REQUIRE(atf_fs_stat_is_other_readable(&st) == othr); \
        ATF_REQUIRE(atf_fs_stat_is_other_writable(&st) == othw); \
        ATF_REQUIRE(atf_fs_stat_is_other_executable(&st) == othx); \
        atf_fs_stat_fini(&st); \
    }

    chmod("reg", 0000);
    perms(false, false, false, false, false, false, false, false, false);

    chmod("reg", 0001);
    perms(false, false, false, false, false, false, false, false, true);

    chmod("reg", 0010);
    perms(false, false, false, false, false, true, false, false, false);

    chmod("reg", 0100);
    perms(false, false, true, false, false, false, false, false, false);

    chmod("reg", 0002);
    perms(false, false, false, false, false, false, false, true, false);

    chmod("reg", 0020);
    perms(false, false, false, false, true, false, false, false, false);

    chmod("reg", 0200);
    perms(false, true, false, false, false, false, false, false, false);

    chmod("reg", 0004);
    perms(false, false, false, false, false, false, true, false, false);

    chmod("reg", 0040);
    perms(false, false, false, true, false, false, false, false, false);

    chmod("reg", 0400);
    perms(true, false, false, false, false, false, false, false, false);

    chmod("reg", 0644);
    perms(true, true, false, true, false, false, true, false, false);

    chmod("reg", 0755);
    perms(true, true, true, true, false, true, true, false, true);

    chmod("reg", 0777);
    perms(true, true, true, true, true, true, true, true, true);

#undef perms

    atf_fs_path_fini(&p);
}

/* ---------------------------------------------------------------------
 * Test cases for the free functions.
 * --------------------------------------------------------------------- */

ATF_TC(exists);
ATF_TC_HEAD(exists, tc)
{
    atf_tc_set_md_var(tc, "descr", "Tests the atf_fs_exists function");
}
ATF_TC_BODY(exists, tc)
{
    atf_error_t err;
    atf_fs_path_t pdir, pfile;
    bool b;

    RE(atf_fs_path_init_fmt(&pdir, "dir"));
    RE(atf_fs_path_init_fmt(&pfile, "dir/file"));

    create_dir(atf_fs_path_cstring(&pdir), 0755);
    create_file(atf_fs_path_cstring(&pfile), 0644);

    printf("Checking existence of a directory\n");
    RE(atf_fs_exists(&pdir, &b));
    ATF_REQUIRE(b);

    printf("Checking existence of a file\n");
    RE(atf_fs_exists(&pfile, &b));
    ATF_REQUIRE(b);

    /* XXX: This should probably be a separate test case to let the user
     * be aware that some tests were skipped because privileges were not
     * correct. */
    if (!atf_user_is_root()) {
        printf("Checking existence of a file inside a directory without "
               "permissions\n");
        ATF_REQUIRE(chmod(atf_fs_path_cstring(&pdir), 0000) != -1);
        err = atf_fs_exists(&pfile, &b);
        ATF_REQUIRE(atf_is_error(err));
        ATF_REQUIRE(atf_error_is(err, "libc"));
        ATF_REQUIRE(chmod(atf_fs_path_cstring(&pdir), 0755) != -1);
        atf_error_free(err);
    }

    printf("Checking existence of a non-existent file\n");
    ATF_REQUIRE(unlink(atf_fs_path_cstring(&pfile)) != -1);
    RE(atf_fs_exists(&pfile, &b));
    ATF_REQUIRE(!b);

    atf_fs_path_fini(&pfile);
    atf_fs_path_fini(&pdir);
}

ATF_TC(eaccess);
ATF_TC_HEAD(eaccess, tc)
{
    atf_tc_set_md_var(tc, "descr", "Tests the atf_fs_eaccess function");
}
ATF_TC_BODY(eaccess, tc)
{
    const int modes[] = { atf_fs_access_f, atf_fs_access_r, atf_fs_access_w,
                          atf_fs_access_x, 0 };
    const int *m;
    struct tests {
        mode_t fmode;
        int amode;
        int uerror;
        int rerror;
    } tests[] = {
        { 0000, atf_fs_access_r, EACCES, 0 },
        { 0000, atf_fs_access_w, EACCES, 0 },
        { 0000, atf_fs_access_x, EACCES, EACCES },

        { 0001, atf_fs_access_r, EACCES, 0 },
        { 0001, atf_fs_access_w, EACCES, 0 },
        { 0001, atf_fs_access_x, EACCES, 0 },
        { 0002, atf_fs_access_r, EACCES, 0 },
        { 0002, atf_fs_access_w, EACCES, 0 },
        { 0002, atf_fs_access_x, EACCES, EACCES },
        { 0004, atf_fs_access_r, EACCES, 0 },
        { 0004, atf_fs_access_w, EACCES, 0 },
        { 0004, atf_fs_access_x, EACCES, EACCES },

        { 0010, atf_fs_access_r, EACCES, 0 },
        { 0010, atf_fs_access_w, EACCES, 0 },
        { 0010, atf_fs_access_x, 0,      0 },
        { 0020, atf_fs_access_r, EACCES, 0 },
        { 0020, atf_fs_access_w, 0,      0 },
        { 0020, atf_fs_access_x, EACCES, EACCES },
        { 0040, atf_fs_access_r, 0,      0 },
        { 0040, atf_fs_access_w, EACCES, 0 },
        { 0040, atf_fs_access_x, EACCES, EACCES },

        { 0100, atf_fs_access_r, EACCES, 0 },
        { 0100, atf_fs_access_w, EACCES, 0 },
        { 0100, atf_fs_access_x, 0,      0 },
        { 0200, atf_fs_access_r, EACCES, 0 },
        { 0200, atf_fs_access_w, 0,      0 },
        { 0200, atf_fs_access_x, EACCES, EACCES },
        { 0400, atf_fs_access_r, 0,      0 },
        { 0400, atf_fs_access_w, EACCES, 0 },
        { 0400, atf_fs_access_x, EACCES, EACCES },

        { 0, 0, 0, 0 }
    };
    struct tests *t;
    atf_fs_path_t p;
    atf_error_t err;

    RE(atf_fs_path_init_fmt(&p, "the-file"));

    printf("Non-existent file checks\n");
    for (m = &modes[0]; *m != 0; m++) {
        err = atf_fs_eaccess(&p, *m);
        ATF_REQUIRE(atf_is_error(err));
        ATF_REQUIRE(atf_error_is(err, "libc"));
        ATF_REQUIRE_EQ(atf_libc_error_code(err), ENOENT);
        atf_error_free(err);
    }

    create_file(atf_fs_path_cstring(&p), 0000);
    ATF_REQUIRE(chown(atf_fs_path_cstring(&p), geteuid(), getegid()) != -1);

    for (t = &tests[0]; t->amode != 0; t++) {
        const int experr = atf_user_is_root() ? t->rerror : t->uerror;

        printf("\n");
        printf("File mode     : %04o\n", (unsigned int)t->fmode);
        printf("Access mode   : 0x%02x\n", t->amode);

        ATF_REQUIRE(chmod(atf_fs_path_cstring(&p), t->fmode) != -1);

        /* First, existence check. */
        err = atf_fs_eaccess(&p, atf_fs_access_f);
        ATF_REQUIRE(!atf_is_error(err));

        /* Now do the specific test case. */
        printf("Expected error: %d\n", experr);
        err = atf_fs_eaccess(&p, t->amode);
        if (atf_is_error(err)) {
            if (atf_error_is(err, "libc"))
                printf("Error         : %d\n", atf_libc_error_code(err));
            else
                printf("Error         : Non-libc error\n");
        } else
                printf("Error         : None\n");
        if (experr == 0) {
            ATF_REQUIRE(!atf_is_error(err));
        } else {
            ATF_REQUIRE(atf_is_error(err));
            ATF_REQUIRE(atf_error_is(err, "libc"));
            ATF_REQUIRE_EQ(atf_libc_error_code(err), experr);
            atf_error_free(err);
        }
    }

    atf_fs_path_fini(&p);
}

ATF_TC(getcwd);
ATF_TC_HEAD(getcwd, tc)
{
    atf_tc_set_md_var(tc, "descr", "Tests the atf_fs_getcwd function");
}
ATF_TC_BODY(getcwd, tc)
{
    atf_fs_path_t cwd1, cwd2;

    create_dir ("root", 0755);

    RE(atf_fs_getcwd(&cwd1));
    ATF_REQUIRE(chdir("root") != -1);
    RE(atf_fs_getcwd(&cwd2));

    RE(atf_fs_path_append_fmt(&cwd1, "root"));

    ATF_REQUIRE(atf_equal_fs_path_fs_path(&cwd1, &cwd2));

    atf_fs_path_fini(&cwd2);
    atf_fs_path_fini(&cwd1);
}

ATF_TC(rmdir_empty);
ATF_TC_HEAD(rmdir_empty, tc)
{
    atf_tc_set_md_var(tc, "descr", "Tests the atf_fs_rmdir function");
}
ATF_TC_BODY(rmdir_empty, tc)
{
    atf_fs_path_t p;

    RE(atf_fs_path_init_fmt(&p, "test-dir"));

    ATF_REQUIRE(mkdir("test-dir", 0755) != -1);
    ATF_REQUIRE(exists(&p));
    RE(atf_fs_rmdir(&p));
    ATF_REQUIRE(!exists(&p));

    atf_fs_path_fini(&p);
}

ATF_TC(rmdir_enotempty);
ATF_TC_HEAD(rmdir_enotempty, tc)
{
    atf_tc_set_md_var(tc, "descr", "Tests the atf_fs_rmdir function");
}
ATF_TC_BODY(rmdir_enotempty, tc)
{
    atf_fs_path_t p;
    atf_error_t err;

    RE(atf_fs_path_init_fmt(&p, "test-dir"));

    ATF_REQUIRE(mkdir("test-dir", 0755) != -1);
    ATF_REQUIRE(exists(&p));
    create_file("test-dir/foo", 0644);

    err = atf_fs_rmdir(&p);
    ATF_REQUIRE(atf_is_error(err));
    ATF_REQUIRE(atf_error_is(err, "libc"));
    ATF_REQUIRE_EQ(atf_libc_error_code(err), ENOTEMPTY);
    atf_error_free(err);

    atf_fs_path_fini(&p);
}

ATF_TC(rmdir_eperm);
ATF_TC_HEAD(rmdir_eperm, tc)
{
    atf_tc_set_md_var(tc, "descr", "Tests the atf_fs_rmdir function");
}
ATF_TC_BODY(rmdir_eperm, tc)
{
    atf_fs_path_t p;
    atf_error_t err;

    RE(atf_fs_path_init_fmt(&p, "test-dir/foo"));

    ATF_REQUIRE(mkdir("test-dir", 0755) != -1);
    ATF_REQUIRE(mkdir("test-dir/foo", 0755) != -1);
    ATF_REQUIRE(chmod("test-dir", 0555) != -1);
    ATF_REQUIRE(exists(&p));

    err = atf_fs_rmdir(&p);
    if (atf_user_is_root()) {
        ATF_REQUIRE(!atf_is_error(err));
    } else {
        ATF_REQUIRE(atf_is_error(err));
        ATF_REQUIRE(atf_error_is(err, "libc"));
        ATF_REQUIRE_EQ(atf_libc_error_code(err), EACCES);
        atf_error_free(err);
    }

    atf_fs_path_fini(&p);
}

ATF_TC(mkdtemp_ok);
ATF_TC_HEAD(mkdtemp_ok, tc)
{
    atf_tc_set_md_var(tc, "descr", "Tests the atf_fs_mkdtemp function, "
                      "successful execution");
}
ATF_TC_BODY(mkdtemp_ok, tc)
{
    atf_fs_path_t p1, p2;
    atf_fs_stat_t s1, s2;

    RE(atf_fs_path_init_fmt(&p1, "testdir.XXXXXX"));
    RE(atf_fs_path_init_fmt(&p2, "testdir.XXXXXX"));
    RE(atf_fs_mkdtemp(&p1));
    RE(atf_fs_mkdtemp(&p2));
    ATF_REQUIRE(!atf_equal_fs_path_fs_path(&p1, &p2));
    ATF_REQUIRE(exists(&p1));
    ATF_REQUIRE(exists(&p2));

    RE(atf_fs_stat_init(&s1, &p1));
    ATF_REQUIRE_EQ(atf_fs_stat_get_type(&s1), atf_fs_stat_dir_type);
    ATF_REQUIRE( atf_fs_stat_is_owner_readable(&s1));
    ATF_REQUIRE( atf_fs_stat_is_owner_writable(&s1));
    ATF_REQUIRE( atf_fs_stat_is_owner_executable(&s1));
    ATF_REQUIRE(!atf_fs_stat_is_group_readable(&s1));
    ATF_REQUIRE(!atf_fs_stat_is_group_writable(&s1));
    ATF_REQUIRE(!atf_fs_stat_is_group_executable(&s1));
    ATF_REQUIRE(!atf_fs_stat_is_other_readable(&s1));
    ATF_REQUIRE(!atf_fs_stat_is_other_writable(&s1));
    ATF_REQUIRE(!atf_fs_stat_is_other_executable(&s1));

    RE(atf_fs_stat_init(&s2, &p2));
    ATF_REQUIRE_EQ(atf_fs_stat_get_type(&s2), atf_fs_stat_dir_type);
    ATF_REQUIRE( atf_fs_stat_is_owner_readable(&s2));
    ATF_REQUIRE( atf_fs_stat_is_owner_writable(&s2));
    ATF_REQUIRE( atf_fs_stat_is_owner_executable(&s2));
    ATF_REQUIRE(!atf_fs_stat_is_group_readable(&s2));
    ATF_REQUIRE(!atf_fs_stat_is_group_writable(&s2));
    ATF_REQUIRE(!atf_fs_stat_is_group_executable(&s2));
    ATF_REQUIRE(!atf_fs_stat_is_other_readable(&s2));
    ATF_REQUIRE(!atf_fs_stat_is_other_writable(&s2));
    ATF_REQUIRE(!atf_fs_stat_is_other_executable(&s2));

    atf_fs_stat_fini(&s2);
    atf_fs_stat_fini(&s1);
    atf_fs_path_fini(&p2);
    atf_fs_path_fini(&p1);
}

ATF_TC(mkdtemp_err);
ATF_TC_HEAD(mkdtemp_err, tc)
{
    atf_tc_set_md_var(tc, "descr", "Tests the atf_fs_mkdtemp function, "
                      "error conditions");
    atf_tc_set_md_var(tc, "require.user", "unprivileged");
}
ATF_TC_BODY(mkdtemp_err, tc)
{
    atf_error_t err;
    atf_fs_path_t p;

    ATF_REQUIRE(mkdir("dir", 0555) != -1);

    RE(atf_fs_path_init_fmt(&p, "dir/testdir.XXXXXX"));

    err = atf_fs_mkdtemp(&p);
    ATF_REQUIRE(atf_is_error(err));
    ATF_REQUIRE(atf_error_is(err, "libc"));
    ATF_CHECK_EQ(atf_libc_error_code(err), EACCES);
    atf_error_free(err);

    ATF_CHECK(!exists(&p));
    ATF_CHECK(strcmp(atf_fs_path_cstring(&p), "dir/testdir.XXXXXX") == 0);

    atf_fs_path_fini(&p);
}

static
void
do_umask_check(atf_error_t (*const mk_func)(atf_fs_path_t *),
               atf_fs_path_t *path, const mode_t test_mask,
               const char *str_mask, const char *exp_name)
{
    char buf[1024];
    int old_umask;
    atf_error_t err;

    printf("Creating temporary %s with umask %s\n", exp_name, str_mask);

    old_umask = umask(test_mask);
    err = mk_func(path);
    (void)umask(old_umask);

    ATF_REQUIRE(atf_is_error(err));
    ATF_REQUIRE(atf_error_is(err, "invalid_umask"));
    atf_error_format(err, buf, sizeof(buf));
    ATF_CHECK(strstr(buf, exp_name) != NULL);
    ATF_CHECK(strstr(buf, str_mask) != NULL);
    atf_error_free(err);
}

ATF_TC(mkdtemp_umask);
ATF_TC_HEAD(mkdtemp_umask, tc)
{
    atf_tc_set_md_var(tc, "descr", "Tests the atf_fs_mkdtemp function "
                      "causing an error due to a too strict umask");
}
ATF_TC_BODY(mkdtemp_umask, tc)
{
    atf_fs_path_t p;

    RE(atf_fs_path_init_fmt(&p, "testdir.XXXXXX"));

    do_umask_check(atf_fs_mkdtemp, &p, 00100, "00100", "directory");
    do_umask_check(atf_fs_mkdtemp, &p, 00200, "00200", "directory");
    do_umask_check(atf_fs_mkdtemp, &p, 00400, "00400", "directory");
    do_umask_check(atf_fs_mkdtemp, &p, 00500, "00500", "directory");
    do_umask_check(atf_fs_mkdtemp, &p, 00600, "00600", "directory");

    atf_fs_path_fini(&p);
}

ATF_TC(mkstemp_ok);
ATF_TC_HEAD(mkstemp_ok, tc)
{
    atf_tc_set_md_var(tc, "descr", "Tests the atf_fs_mkstemp function, "
                      "successful execution");
}
ATF_TC_BODY(mkstemp_ok, tc)
{
    int fd1, fd2;
    atf_fs_path_t p1, p2;
    atf_fs_stat_t s1, s2;

    RE(atf_fs_path_init_fmt(&p1, "testfile.XXXXXX"));
    RE(atf_fs_path_init_fmt(&p2, "testfile.XXXXXX"));
    fd1 = fd2 = -1;
    RE(atf_fs_mkstemp(&p1, &fd1));
    RE(atf_fs_mkstemp(&p2, &fd2));
    ATF_REQUIRE(!atf_equal_fs_path_fs_path(&p1, &p2));
    ATF_REQUIRE(exists(&p1));
    ATF_REQUIRE(exists(&p2));

    ATF_CHECK(fd1 != -1);
    ATF_CHECK(fd2 != -1);
    ATF_CHECK(write(fd1, "foo", 3) == 3);
    ATF_CHECK(write(fd2, "bar", 3) == 3);
    close(fd1);
    close(fd2);

    RE(atf_fs_stat_init(&s1, &p1));
    ATF_CHECK_EQ(atf_fs_stat_get_type(&s1), atf_fs_stat_reg_type);
    ATF_CHECK( atf_fs_stat_is_owner_readable(&s1));
    ATF_CHECK( atf_fs_stat_is_owner_writable(&s1));
    ATF_CHECK(!atf_fs_stat_is_owner_executable(&s1));
    ATF_CHECK(!atf_fs_stat_is_group_readable(&s1));
    ATF_CHECK(!atf_fs_stat_is_group_writable(&s1));
    ATF_CHECK(!atf_fs_stat_is_group_executable(&s1));
    ATF_CHECK(!atf_fs_stat_is_other_readable(&s1));
    ATF_CHECK(!atf_fs_stat_is_other_writable(&s1));
    ATF_CHECK(!atf_fs_stat_is_other_executable(&s1));

    RE(atf_fs_stat_init(&s2, &p2));
    ATF_CHECK_EQ(atf_fs_stat_get_type(&s2), atf_fs_stat_reg_type);
    ATF_CHECK( atf_fs_stat_is_owner_readable(&s2));
    ATF_CHECK( atf_fs_stat_is_owner_writable(&s2));
    ATF_CHECK(!atf_fs_stat_is_owner_executable(&s2));
    ATF_CHECK(!atf_fs_stat_is_group_readable(&s2));
    ATF_CHECK(!atf_fs_stat_is_group_writable(&s2));
    ATF_CHECK(!atf_fs_stat_is_group_executable(&s2));
    ATF_CHECK(!atf_fs_stat_is_other_readable(&s2));
    ATF_CHECK(!atf_fs_stat_is_other_writable(&s2));
    ATF_CHECK(!atf_fs_stat_is_other_executable(&s2));

    atf_fs_stat_fini(&s2);
    atf_fs_stat_fini(&s1);
    atf_fs_path_fini(&p2);
    atf_fs_path_fini(&p1);
}

ATF_TC(mkstemp_err);
ATF_TC_HEAD(mkstemp_err, tc)
{
    atf_tc_set_md_var(tc, "descr", "Tests the atf_fs_mkstemp function, "
                      "error conditions");
    atf_tc_set_md_var(tc, "require.user", "unprivileged");
}
ATF_TC_BODY(mkstemp_err, tc)
{
    int fd;
    atf_error_t err;
    atf_fs_path_t p;

    ATF_REQUIRE(mkdir("dir", 0555) != -1);

    RE(atf_fs_path_init_fmt(&p, "dir/testfile.XXXXXX"));
    fd = 1234;

    err = atf_fs_mkstemp(&p, &fd);
    ATF_REQUIRE(atf_is_error(err));
    ATF_REQUIRE(atf_error_is(err, "libc"));
    ATF_CHECK_EQ(atf_libc_error_code(err), EACCES);
    atf_error_free(err);

    ATF_CHECK(!exists(&p));
    ATF_CHECK(strcmp(atf_fs_path_cstring(&p), "dir/testfile.XXXXXX") == 0);
    ATF_CHECK_EQ(fd, 1234);

    atf_fs_path_fini(&p);
}

ATF_TC(mkstemp_umask);
ATF_TC_HEAD(mkstemp_umask, tc)
{
    atf_tc_set_md_var(tc, "descr", "Tests the atf_fs_mkstemp function "
                      "causing an error due to a too strict umask");
}
ATF_TC_BODY(mkstemp_umask, tc)
{
    atf_fs_path_t p;

    RE(atf_fs_path_init_fmt(&p, "testfile.XXXXXX"));

    do_umask_check(mkstemp_discard_fd, &p, 00100, "00100", "regular file");
    do_umask_check(mkstemp_discard_fd, &p, 00200, "00200", "regular file");
    do_umask_check(mkstemp_discard_fd, &p, 00400, "00400", "regular file");

    atf_fs_path_fini(&p);
}

/* ---------------------------------------------------------------------
 * Main.
 * --------------------------------------------------------------------- */

ATF_TP_ADD_TCS(tp)
{
    /* Add the tests for the "atf_fs_path" type. */
    ATF_TP_ADD_TC(tp, path_normalize);
    ATF_TP_ADD_TC(tp, path_copy);
    ATF_TP_ADD_TC(tp, path_is_absolute);
    ATF_TP_ADD_TC(tp, path_is_root);
    ATF_TP_ADD_TC(tp, path_branch_path);
    ATF_TP_ADD_TC(tp, path_leaf_name);
    ATF_TP_ADD_TC(tp, path_append);
    ATF_TP_ADD_TC(tp, path_to_absolute);
    ATF_TP_ADD_TC(tp, path_equal);

    /* Add the tests for the "atf_fs_stat" type. */
    ATF_TP_ADD_TC(tp, stat_mode);
    ATF_TP_ADD_TC(tp, stat_type);
    ATF_TP_ADD_TC(tp, stat_perms);

    /* Add the tests for the free functions. */
    ATF_TP_ADD_TC(tp, eaccess);
    ATF_TP_ADD_TC(tp, exists);
    ATF_TP_ADD_TC(tp, getcwd);
    ATF_TP_ADD_TC(tp, rmdir_empty);
    ATF_TP_ADD_TC(tp, rmdir_enotempty);
    ATF_TP_ADD_TC(tp, rmdir_eperm);
    ATF_TP_ADD_TC(tp, mkdtemp_ok);
    ATF_TP_ADD_TC(tp, mkdtemp_err);
    ATF_TP_ADD_TC(tp, mkdtemp_umask);
    ATF_TP_ADD_TC(tp, mkstemp_ok);
    ATF_TP_ADD_TC(tp, mkstemp_err);
    ATF_TP_ADD_TC(tp, mkstemp_umask);

    return atf_no_error();
}
