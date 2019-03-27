/*
 * Copyright 1995-2019 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

/* Part of the code in here was originally in conf.c, which is now removed */

#include <stdio.h>
#include <string.h>
#include "internal/cryptlib.h"
#include "internal/o_dir.h"
#include <openssl/lhash.h>
#include <openssl/conf.h>
#include <openssl/conf_api.h>
#include "conf_def.h"
#include <openssl/buffer.h>
#include <openssl/err.h>
#ifndef OPENSSL_NO_POSIX_IO
# include <sys/stat.h>
# ifdef _WIN32
#  define stat    _stat
#  define strcasecmp _stricmp
# endif
#endif

#ifndef S_ISDIR
# define S_ISDIR(a) (((a) & S_IFMT) == S_IFDIR)
#endif

/*
 * The maximum length we can grow a value to after variable expansion. 64k
 * should be more than enough for all reasonable uses.
 */
#define MAX_CONF_VALUE_LENGTH       65536

static int is_keytype(const CONF *conf, char c, unsigned short type);
static char *eat_ws(CONF *conf, char *p);
static void trim_ws(CONF *conf, char *start);
static char *eat_alpha_numeric(CONF *conf, char *p);
static void clear_comments(CONF *conf, char *p);
static int str_copy(CONF *conf, char *section, char **to, char *from);
static char *scan_quote(CONF *conf, char *p);
static char *scan_dquote(CONF *conf, char *p);
#define scan_esc(conf,p)        (((IS_EOF((conf),(p)[1]))?((p)+1):((p)+2)))
#ifndef OPENSSL_NO_POSIX_IO
static BIO *process_include(char *include, OPENSSL_DIR_CTX **dirctx,
                            char **dirpath);
static BIO *get_next_file(const char *path, OPENSSL_DIR_CTX **dirctx);
#endif

static CONF *def_create(CONF_METHOD *meth);
static int def_init_default(CONF *conf);
static int def_init_WIN32(CONF *conf);
static int def_destroy(CONF *conf);
static int def_destroy_data(CONF *conf);
static int def_load(CONF *conf, const char *name, long *eline);
static int def_load_bio(CONF *conf, BIO *bp, long *eline);
static int def_dump(const CONF *conf, BIO *bp);
static int def_is_number(const CONF *conf, char c);
static int def_to_int(const CONF *conf, char c);

static CONF_METHOD default_method = {
    "OpenSSL default",
    def_create,
    def_init_default,
    def_destroy,
    def_destroy_data,
    def_load_bio,
    def_dump,
    def_is_number,
    def_to_int,
    def_load
};

static CONF_METHOD WIN32_method = {
    "WIN32",
    def_create,
    def_init_WIN32,
    def_destroy,
    def_destroy_data,
    def_load_bio,
    def_dump,
    def_is_number,
    def_to_int,
    def_load
};

CONF_METHOD *NCONF_default(void)
{
    return &default_method;
}

CONF_METHOD *NCONF_WIN32(void)
{
    return &WIN32_method;
}

static CONF *def_create(CONF_METHOD *meth)
{
    CONF *ret;

    ret = OPENSSL_malloc(sizeof(*ret));
    if (ret != NULL)
        if (meth->init(ret) == 0) {
            OPENSSL_free(ret);
            ret = NULL;
        }
    return ret;
}

static int def_init_default(CONF *conf)
{
    if (conf == NULL)
        return 0;

    conf->meth = &default_method;
    conf->meth_data = (void *)CONF_type_default;
    conf->data = NULL;

    return 1;
}

static int def_init_WIN32(CONF *conf)
{
    if (conf == NULL)
        return 0;

    conf->meth = &WIN32_method;
    conf->meth_data = (void *)CONF_type_win32;
    conf->data = NULL;

    return 1;
}

static int def_destroy(CONF *conf)
{
    if (def_destroy_data(conf)) {
        OPENSSL_free(conf);
        return 1;
    }
    return 0;
}

static int def_destroy_data(CONF *conf)
{
    if (conf == NULL)
        return 0;
    _CONF_free_data(conf);
    return 1;
}

static int def_load(CONF *conf, const char *name, long *line)
{
    int ret;
    BIO *in = NULL;

#ifdef OPENSSL_SYS_VMS
    in = BIO_new_file(name, "r");
#else
    in = BIO_new_file(name, "rb");
#endif
    if (in == NULL) {
        if (ERR_GET_REASON(ERR_peek_last_error()) == BIO_R_NO_SUCH_FILE)
            CONFerr(CONF_F_DEF_LOAD, CONF_R_NO_SUCH_FILE);
        else
            CONFerr(CONF_F_DEF_LOAD, ERR_R_SYS_LIB);
        return 0;
    }

    ret = def_load_bio(conf, in, line);
    BIO_free(in);

    return ret;
}

static int def_load_bio(CONF *conf, BIO *in, long *line)
{
/* The macro BUFSIZE conflicts with a system macro in VxWorks */
#define CONFBUFSIZE     512
    int bufnum = 0, i, ii;
    BUF_MEM *buff = NULL;
    char *s, *p, *end;
    int again;
    long eline = 0;
    char btmp[DECIMAL_SIZE(eline) + 1];
    CONF_VALUE *v = NULL, *tv;
    CONF_VALUE *sv = NULL;
    char *section = NULL, *buf;
    char *start, *psection, *pname;
    void *h = (void *)(conf->data);
    STACK_OF(BIO) *biosk = NULL;
#ifndef OPENSSL_NO_POSIX_IO
    char *dirpath = NULL;
    OPENSSL_DIR_CTX *dirctx = NULL;
#endif

    if ((buff = BUF_MEM_new()) == NULL) {
        CONFerr(CONF_F_DEF_LOAD_BIO, ERR_R_BUF_LIB);
        goto err;
    }

    section = OPENSSL_strdup("default");
    if (section == NULL) {
        CONFerr(CONF_F_DEF_LOAD_BIO, ERR_R_MALLOC_FAILURE);
        goto err;
    }

    if (_CONF_new_data(conf) == 0) {
        CONFerr(CONF_F_DEF_LOAD_BIO, ERR_R_MALLOC_FAILURE);
        goto err;
    }

    sv = _CONF_new_section(conf, section);
    if (sv == NULL) {
        CONFerr(CONF_F_DEF_LOAD_BIO, CONF_R_UNABLE_TO_CREATE_NEW_SECTION);
        goto err;
    }

    bufnum = 0;
    again = 0;
    for (;;) {
        if (!BUF_MEM_grow(buff, bufnum + CONFBUFSIZE)) {
            CONFerr(CONF_F_DEF_LOAD_BIO, ERR_R_BUF_LIB);
            goto err;
        }
        p = &(buff->data[bufnum]);
        *p = '\0';
 read_retry:
        BIO_gets(in, p, CONFBUFSIZE - 1);
        p[CONFBUFSIZE - 1] = '\0';
        ii = i = strlen(p);
        if (i == 0 && !again) {
            /* the currently processed BIO is at EOF */
            BIO *parent;

#ifndef OPENSSL_NO_POSIX_IO
            /* continue processing with the next file from directory */
            if (dirctx != NULL) {
                BIO *next;

                if ((next = get_next_file(dirpath, &dirctx)) != NULL) {
                    BIO_vfree(in);
                    in = next;
                    goto read_retry;
                } else {
                    OPENSSL_free(dirpath);
                    dirpath = NULL;
                }
            }
#endif
            /* no more files in directory, continue with processing parent */
            if ((parent = sk_BIO_pop(biosk)) == NULL) {
                /* everything processed get out of the loop */
                break;
            } else {
                BIO_vfree(in);
                in = parent;
                goto read_retry;
            }
        }
        again = 0;
        while (i > 0) {
            if ((p[i - 1] != '\r') && (p[i - 1] != '\n'))
                break;
            else
                i--;
        }
        /*
         * we removed some trailing stuff so there is a new line on the end.
         */
        if (ii && i == ii)
            again = 1;          /* long line */
        else {
            p[i] = '\0';
            eline++;            /* another input line */
        }

        /* we now have a line with trailing \r\n removed */

        /* i is the number of bytes */
        bufnum += i;

        v = NULL;
        /* check for line continuation */
        if (bufnum >= 1) {
            /*
             * If we have bytes and the last char '\\' and second last char
             * is not '\\'
             */
            p = &(buff->data[bufnum - 1]);
            if (IS_ESC(conf, p[0]) && ((bufnum <= 1) || !IS_ESC(conf, p[-1]))) {
                bufnum--;
                again = 1;
            }
        }
        if (again)
            continue;
        bufnum = 0;
        buf = buff->data;

        clear_comments(conf, buf);
        s = eat_ws(conf, buf);
        if (IS_EOF(conf, *s))
            continue;           /* blank line */
        if (*s == '[') {
            char *ss;

            s++;
            start = eat_ws(conf, s);
            ss = start;
 again:
            end = eat_alpha_numeric(conf, ss);
            p = eat_ws(conf, end);
            if (*p != ']') {
                if (*p != '\0' && ss != p) {
                    ss = p;
                    goto again;
                }
                CONFerr(CONF_F_DEF_LOAD_BIO,
                        CONF_R_MISSING_CLOSE_SQUARE_BRACKET);
                goto err;
            }
            *end = '\0';
            if (!str_copy(conf, NULL, &section, start))
                goto err;
            if ((sv = _CONF_get_section(conf, section)) == NULL)
                sv = _CONF_new_section(conf, section);
            if (sv == NULL) {
                CONFerr(CONF_F_DEF_LOAD_BIO,
                        CONF_R_UNABLE_TO_CREATE_NEW_SECTION);
                goto err;
            }
            continue;
        } else {
            pname = s;
            end = eat_alpha_numeric(conf, s);
            if ((end[0] == ':') && (end[1] == ':')) {
                *end = '\0';
                end += 2;
                psection = pname;
                pname = end;
                end = eat_alpha_numeric(conf, end);
            } else {
                psection = section;
            }
            p = eat_ws(conf, end);
            if (strncmp(pname, ".include", 8) == 0
                && (p != pname + 8 || *p == '=')) {
                char *include = NULL;
                BIO *next;

                if (*p == '=') {
                    p++;
                    p = eat_ws(conf, p);
                }
                trim_ws(conf, p);
                if (!str_copy(conf, psection, &include, p))
                    goto err;
                /* get the BIO of the included file */
#ifndef OPENSSL_NO_POSIX_IO
                next = process_include(include, &dirctx, &dirpath);
                if (include != dirpath) {
                    /* dirpath will contain include in case of a directory */
                    OPENSSL_free(include);
                }
#else
                next = BIO_new_file(include, "r");
                OPENSSL_free(include);
#endif
                if (next != NULL) {
                    /* push the currently processing BIO onto stack */
                    if (biosk == NULL) {
                        if ((biosk = sk_BIO_new_null()) == NULL) {
                            CONFerr(CONF_F_DEF_LOAD_BIO, ERR_R_MALLOC_FAILURE);
                            goto err;
                        }
                    }
                    if (!sk_BIO_push(biosk, in)) {
                        CONFerr(CONF_F_DEF_LOAD_BIO, ERR_R_MALLOC_FAILURE);
                        goto err;
                    }
                    /* continue with reading from the included BIO */
                    in = next;
                }
                continue;
            } else if (*p != '=') {
                CONFerr(CONF_F_DEF_LOAD_BIO, CONF_R_MISSING_EQUAL_SIGN);
                goto err;
            }
            *end = '\0';
            p++;
            start = eat_ws(conf, p);
            trim_ws(conf, start);

            if ((v = OPENSSL_malloc(sizeof(*v))) == NULL) {
                CONFerr(CONF_F_DEF_LOAD_BIO, ERR_R_MALLOC_FAILURE);
                goto err;
            }
            v->name = OPENSSL_strdup(pname);
            v->value = NULL;
            if (v->name == NULL) {
                CONFerr(CONF_F_DEF_LOAD_BIO, ERR_R_MALLOC_FAILURE);
                goto err;
            }
            if (!str_copy(conf, psection, &(v->value), start))
                goto err;

            if (strcmp(psection, section) != 0) {
                if ((tv = _CONF_get_section(conf, psection))
                    == NULL)
                    tv = _CONF_new_section(conf, psection);
                if (tv == NULL) {
                    CONFerr(CONF_F_DEF_LOAD_BIO,
                            CONF_R_UNABLE_TO_CREATE_NEW_SECTION);
                    goto err;
                }
            } else
                tv = sv;
            if (_CONF_add_string(conf, tv, v) == 0) {
                CONFerr(CONF_F_DEF_LOAD_BIO, ERR_R_MALLOC_FAILURE);
                goto err;
            }
            v = NULL;
        }
    }
    BUF_MEM_free(buff);
    OPENSSL_free(section);
    /*
     * No need to pop, since we only get here if the stack is empty.
     * If this causes a BIO leak, THE ISSUE IS SOMEWHERE ELSE!
     */
    sk_BIO_free(biosk);
    return 1;
 err:
    BUF_MEM_free(buff);
    OPENSSL_free(section);
    /*
     * Since |in| is the first element of the stack and should NOT be freed
     * here, we cannot use sk_BIO_pop_free().  Instead, we pop and free one
     * BIO at a time, making sure that the last one popped isn't.
     */
    while (sk_BIO_num(biosk) > 0) {
        BIO *popped = sk_BIO_pop(biosk);
        BIO_vfree(in);
        in = popped;
    }
    sk_BIO_free(biosk);
#ifndef OPENSSL_NO_POSIX_IO
    OPENSSL_free(dirpath);
    if (dirctx != NULL)
        OPENSSL_DIR_end(&dirctx);
#endif
    if (line != NULL)
        *line = eline;
    BIO_snprintf(btmp, sizeof(btmp), "%ld", eline);
    ERR_add_error_data(2, "line ", btmp);
    if (h != conf->data) {
        CONF_free(conf->data);
        conf->data = NULL;
    }
    if (v != NULL) {
        OPENSSL_free(v->name);
        OPENSSL_free(v->value);
        OPENSSL_free(v);
    }
    return 0;
}

static void clear_comments(CONF *conf, char *p)
{
    for (;;) {
        if (IS_FCOMMENT(conf, *p)) {
            *p = '\0';
            return;
        }
        if (!IS_WS(conf, *p)) {
            break;
        }
        p++;
    }

    for (;;) {
        if (IS_COMMENT(conf, *p)) {
            *p = '\0';
            return;
        }
        if (IS_DQUOTE(conf, *p)) {
            p = scan_dquote(conf, p);
            continue;
        }
        if (IS_QUOTE(conf, *p)) {
            p = scan_quote(conf, p);
            continue;
        }
        if (IS_ESC(conf, *p)) {
            p = scan_esc(conf, p);
            continue;
        }
        if (IS_EOF(conf, *p))
            return;
        else
            p++;
    }
}

static int str_copy(CONF *conf, char *section, char **pto, char *from)
{
    int q, r, rr = 0, to = 0, len = 0;
    char *s, *e, *rp, *p, *rrp, *np, *cp, v;
    BUF_MEM *buf;

    if ((buf = BUF_MEM_new()) == NULL)
        return 0;

    len = strlen(from) + 1;
    if (!BUF_MEM_grow(buf, len))
        goto err;

    for (;;) {
        if (IS_QUOTE(conf, *from)) {
            q = *from;
            from++;
            while (!IS_EOF(conf, *from) && (*from != q)) {
                if (IS_ESC(conf, *from)) {
                    from++;
                    if (IS_EOF(conf, *from))
                        break;
                }
                buf->data[to++] = *(from++);
            }
            if (*from == q)
                from++;
        } else if (IS_DQUOTE(conf, *from)) {
            q = *from;
            from++;
            while (!IS_EOF(conf, *from)) {
                if (*from == q) {
                    if (*(from + 1) == q) {
                        from++;
                    } else {
                        break;
                    }
                }
                buf->data[to++] = *(from++);
            }
            if (*from == q)
                from++;
        } else if (IS_ESC(conf, *from)) {
            from++;
            v = *(from++);
            if (IS_EOF(conf, v))
                break;
            else if (v == 'r')
                v = '\r';
            else if (v == 'n')
                v = '\n';
            else if (v == 'b')
                v = '\b';
            else if (v == 't')
                v = '\t';
            buf->data[to++] = v;
        } else if (IS_EOF(conf, *from))
            break;
        else if (*from == '$') {
            size_t newsize;

            /* try to expand it */
            rrp = NULL;
            s = &(from[1]);
            if (*s == '{')
                q = '}';
            else if (*s == '(')
                q = ')';
            else
                q = 0;

            if (q)
                s++;
            cp = section;
            e = np = s;
            while (IS_ALNUM(conf, *e))
                e++;
            if ((e[0] == ':') && (e[1] == ':')) {
                cp = np;
                rrp = e;
                rr = *e;
                *rrp = '\0';
                e += 2;
                np = e;
                while (IS_ALNUM(conf, *e))
                    e++;
            }
            r = *e;
            *e = '\0';
            rp = e;
            if (q) {
                if (r != q) {
                    CONFerr(CONF_F_STR_COPY, CONF_R_NO_CLOSE_BRACE);
                    goto err;
                }
                e++;
            }
            /*-
             * So at this point we have
             * np which is the start of the name string which is
             *   '\0' terminated.
             * cp which is the start of the section string which is
             *   '\0' terminated.
             * e is the 'next point after'.
             * r and rr are the chars replaced by the '\0'
             * rp and rrp is where 'r' and 'rr' came from.
             */
            p = _CONF_get_string(conf, cp, np);
            if (rrp != NULL)
                *rrp = rr;
            *rp = r;
            if (p == NULL) {
                CONFerr(CONF_F_STR_COPY, CONF_R_VARIABLE_HAS_NO_VALUE);
                goto err;
            }
            newsize = strlen(p) + buf->length - (e - from);
            if (newsize > MAX_CONF_VALUE_LENGTH) {
                CONFerr(CONF_F_STR_COPY, CONF_R_VARIABLE_EXPANSION_TOO_LONG);
                goto err;
            }
            if (!BUF_MEM_grow_clean(buf, newsize)) {
                CONFerr(CONF_F_STR_COPY, ERR_R_MALLOC_FAILURE);
                goto err;
            }
            while (*p)
                buf->data[to++] = *(p++);

            /*
             * Since we change the pointer 'from', we also have to change the
             * perceived length of the string it points at.  /RL
             */
            len -= e - from;
            from = e;

            /*
             * In case there were no braces or parenthesis around the
             * variable reference, we have to put back the character that was
             * replaced with a '\0'.  /RL
             */
            *rp = r;
        } else
            buf->data[to++] = *(from++);
    }
    buf->data[to] = '\0';
    OPENSSL_free(*pto);
    *pto = buf->data;
    OPENSSL_free(buf);
    return 1;
 err:
    BUF_MEM_free(buf);
    return 0;
}

#ifndef OPENSSL_NO_POSIX_IO
/*
 * Check whether included path is a directory.
 * Returns next BIO to process and in case of a directory
 * also an opened directory context and the include path.
 */
static BIO *process_include(char *include, OPENSSL_DIR_CTX **dirctx,
                            char **dirpath)
{
    struct stat st = { 0 };
    BIO *next;

    if (stat(include, &st) < 0) {
        SYSerr(SYS_F_STAT, errno);
        ERR_add_error_data(1, include);
        /* missing include file is not fatal error */
        return NULL;
    }

    if (S_ISDIR(st.st_mode)) {
        if (*dirctx != NULL) {
            CONFerr(CONF_F_PROCESS_INCLUDE,
                    CONF_R_RECURSIVE_DIRECTORY_INCLUDE);
            ERR_add_error_data(1, include);
            return NULL;
        }
        /* a directory, load its contents */
        if ((next = get_next_file(include, dirctx)) != NULL)
            *dirpath = include;
        return next;
    }

    next = BIO_new_file(include, "r");
    return next;
}

/*
 * Get next file from the directory path.
 * Returns BIO of the next file to read and updates dirctx.
 */
static BIO *get_next_file(const char *path, OPENSSL_DIR_CTX **dirctx)
{
    const char *filename;

    while ((filename = OPENSSL_DIR_read(dirctx, path)) != NULL) {
        size_t namelen;

        namelen = strlen(filename);


        if ((namelen > 5 && strcasecmp(filename + namelen - 5, ".conf") == 0)
            || (namelen > 4 && strcasecmp(filename + namelen - 4, ".cnf") == 0)) {
            size_t newlen;
            char *newpath;
            BIO *bio;

            newlen = strlen(path) + namelen + 2;
            newpath = OPENSSL_zalloc(newlen);
            if (newpath == NULL) {
                CONFerr(CONF_F_GET_NEXT_FILE, ERR_R_MALLOC_FAILURE);
                break;
            }
#ifdef OPENSSL_SYS_VMS
            /*
             * If the given path isn't clear VMS syntax,
             * we treat it as on Unix.
             */
            {
                size_t pathlen = strlen(path);

                if (path[pathlen - 1] == ']' || path[pathlen - 1] == '>'
                    || path[pathlen - 1] == ':') {
                    /* Clear VMS directory syntax, just copy as is */
                    OPENSSL_strlcpy(newpath, path, newlen);
                }
            }
#endif
            if (newpath[0] == '\0') {
                OPENSSL_strlcpy(newpath, path, newlen);
                OPENSSL_strlcat(newpath, "/", newlen);
            }
            OPENSSL_strlcat(newpath, filename, newlen);

            bio = BIO_new_file(newpath, "r");
            OPENSSL_free(newpath);
            /* Errors when opening files are non-fatal. */
            if (bio != NULL)
                return bio;
        }
    }
    OPENSSL_DIR_end(dirctx);
    *dirctx = NULL;
    return NULL;
}
#endif

static int is_keytype(const CONF *conf, char c, unsigned short type)
{
    const unsigned short * keytypes = (const unsigned short *) conf->meth_data;
    unsigned char key = (unsigned char)c;

#ifdef CHARSET_EBCDIC
# if CHAR_BIT > 8
    if (key > 255) {
        /* key is out of range for os_toascii table */
        return 0;
    }
# endif
    /* convert key from ebcdic to ascii */
    key = os_toascii[key];
#endif

    if (key > 127) {
        /* key is not a seven bit ascii character */
        return 0;
    }

    return (keytypes[key] & type) ? 1 : 0;
}

static char *eat_ws(CONF *conf, char *p)
{
    while (IS_WS(conf, *p) && (!IS_EOF(conf, *p)))
        p++;
    return p;
}

static void trim_ws(CONF *conf, char *start)
{
    char *p = start;

    while (!IS_EOF(conf, *p))
        p++;
    p--;
    while ((p >= start) && IS_WS(conf, *p))
        p--;
    p++;
    *p = '\0';
}

static char *eat_alpha_numeric(CONF *conf, char *p)
{
    for (;;) {
        if (IS_ESC(conf, *p)) {
            p = scan_esc(conf, p);
            continue;
        }
        if (!IS_ALNUM_PUNCT(conf, *p))
            return p;
        p++;
    }
}

static char *scan_quote(CONF *conf, char *p)
{
    int q = *p;

    p++;
    while (!(IS_EOF(conf, *p)) && (*p != q)) {
        if (IS_ESC(conf, *p)) {
            p++;
            if (IS_EOF(conf, *p))
                return p;
        }
        p++;
    }
    if (*p == q)
        p++;
    return p;
}

static char *scan_dquote(CONF *conf, char *p)
{
    int q = *p;

    p++;
    while (!(IS_EOF(conf, *p))) {
        if (*p == q) {
            if (*(p + 1) == q) {
                p++;
            } else {
                break;
            }
        }
        p++;
    }
    if (*p == q)
        p++;
    return p;
}

static void dump_value_doall_arg(const CONF_VALUE *a, BIO *out)
{
    if (a->name)
        BIO_printf(out, "[%s] %s=%s\n", a->section, a->name, a->value);
    else
        BIO_printf(out, "[[%s]]\n", a->section);
}

IMPLEMENT_LHASH_DOALL_ARG_CONST(CONF_VALUE, BIO);

static int def_dump(const CONF *conf, BIO *out)
{
    lh_CONF_VALUE_doall_BIO(conf->data, dump_value_doall_arg, out);
    return 1;
}

static int def_is_number(const CONF *conf, char c)
{
    return IS_NUMBER(conf, c);
}

static int def_to_int(const CONF *conf, char c)
{
    return c - '0';
}
