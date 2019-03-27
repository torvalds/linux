/*
 * Copyright 2000-2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include "dso_locl.h"

#ifdef DSO_DL

# include <dl.h>

/* Part of the hack in "dl_load" ... */
# define DSO_MAX_TRANSLATED_SIZE 256

static int dl_load(DSO *dso);
static int dl_unload(DSO *dso);
static DSO_FUNC_TYPE dl_bind_func(DSO *dso, const char *symname);
static char *dl_name_converter(DSO *dso, const char *filename);
static char *dl_merger(DSO *dso, const char *filespec1,
                       const char *filespec2);
static int dl_pathbyaddr(void *addr, char *path, int sz);
static void *dl_globallookup(const char *name);

static DSO_METHOD dso_meth_dl = {
    "OpenSSL 'dl' shared library method",
    dl_load,
    dl_unload,
    dl_bind_func,
    NULL,                       /* ctrl */
    dl_name_converter,
    dl_merger,
    NULL,                       /* init */
    NULL,                       /* finish */
    dl_pathbyaddr,
    dl_globallookup
};

DSO_METHOD *DSO_METHOD_openssl(void)
{
    return &dso_meth_dl;
}

/*
 * For this DSO_METHOD, our meth_data STACK will contain; (i) the handle
 * (shl_t) returned from shl_load(). NB: I checked on HPUX11 and shl_t is
 * itself a pointer type so the cast is safe.
 */

static int dl_load(DSO *dso)
{
    shl_t ptr = NULL;
    /*
     * We don't do any fancy retries or anything, just take the method's (or
     * DSO's if it has the callback set) best translation of the
     * platform-independent filename and try once with that.
     */
    char *filename = DSO_convert_filename(dso, NULL);

    if (filename == NULL) {
        DSOerr(DSO_F_DL_LOAD, DSO_R_NO_FILENAME);
        goto err;
    }
    ptr = shl_load(filename, BIND_IMMEDIATE |
                   (dso->flags & DSO_FLAG_NO_NAME_TRANSLATION ? 0 :
                    DYNAMIC_PATH), 0L);
    if (ptr == NULL) {
        char errbuf[160];
        DSOerr(DSO_F_DL_LOAD, DSO_R_LOAD_FAILED);
        if (openssl_strerror_r(errno, errbuf, sizeof(errbuf)))
            ERR_add_error_data(4, "filename(", filename, "): ", errbuf);
        goto err;
    }
    if (!sk_push(dso->meth_data, (char *)ptr)) {
        DSOerr(DSO_F_DL_LOAD, DSO_R_STACK_ERROR);
        goto err;
    }
    /*
     * Success, stick the converted filename we've loaded under into the DSO
     * (it also serves as the indicator that we are currently loaded).
     */
    dso->loaded_filename = filename;
    return 1;
 err:
    /* Cleanup! */
    OPENSSL_free(filename);
    if (ptr != NULL)
        shl_unload(ptr);
    return 0;
}

static int dl_unload(DSO *dso)
{
    shl_t ptr;
    if (dso == NULL) {
        DSOerr(DSO_F_DL_UNLOAD, ERR_R_PASSED_NULL_PARAMETER);
        return 0;
    }
    if (sk_num(dso->meth_data) < 1)
        return 1;
    /* Is this statement legal? */
    ptr = (shl_t) sk_pop(dso->meth_data);
    if (ptr == NULL) {
        DSOerr(DSO_F_DL_UNLOAD, DSO_R_NULL_HANDLE);
        /*
         * Should push the value back onto the stack in case of a retry.
         */
        sk_push(dso->meth_data, (char *)ptr);
        return 0;
    }
    shl_unload(ptr);
    return 1;
}

static DSO_FUNC_TYPE dl_bind_func(DSO *dso, const char *symname)
{
    shl_t ptr;
    void *sym;

    if ((dso == NULL) || (symname == NULL)) {
        DSOerr(DSO_F_DL_BIND_FUNC, ERR_R_PASSED_NULL_PARAMETER);
        return NULL;
    }
    if (sk_num(dso->meth_data) < 1) {
        DSOerr(DSO_F_DL_BIND_FUNC, DSO_R_STACK_ERROR);
        return NULL;
    }
    ptr = (shl_t) sk_value(dso->meth_data, sk_num(dso->meth_data) - 1);
    if (ptr == NULL) {
        DSOerr(DSO_F_DL_BIND_FUNC, DSO_R_NULL_HANDLE);
        return NULL;
    }
    if (shl_findsym(&ptr, symname, TYPE_UNDEFINED, &sym) < 0) {
        char errbuf[160];
        DSOerr(DSO_F_DL_BIND_FUNC, DSO_R_SYM_FAILURE);
        if (openssl_strerror_r(errno, errbuf, sizeof(errbuf)))
            ERR_add_error_data(4, "symname(", symname, "): ", errbuf);
        return NULL;
    }
    return (DSO_FUNC_TYPE)sym;
}

static char *dl_merger(DSO *dso, const char *filespec1, const char *filespec2)
{
    char *merged;

    if (!filespec1 && !filespec2) {
        DSOerr(DSO_F_DL_MERGER, ERR_R_PASSED_NULL_PARAMETER);
        return NULL;
    }
    /*
     * If the first file specification is a rooted path, it rules. same goes
     * if the second file specification is missing.
     */
    if (!filespec2 || filespec1[0] == '/') {
        merged = OPENSSL_strdup(filespec1);
        if (merged == NULL) {
            DSOerr(DSO_F_DL_MERGER, ERR_R_MALLOC_FAILURE);
            return NULL;
        }
    }
    /*
     * If the first file specification is missing, the second one rules.
     */
    else if (!filespec1) {
        merged = OPENSSL_strdup(filespec2);
        if (merged == NULL) {
            DSOerr(DSO_F_DL_MERGER, ERR_R_MALLOC_FAILURE);
            return NULL;
        }
    } else
        /*
         * This part isn't as trivial as it looks.  It assumes that the
         * second file specification really is a directory, and makes no
         * checks whatsoever.  Therefore, the result becomes the
         * concatenation of filespec2 followed by a slash followed by
         * filespec1.
         */
    {
        int spec2len, len;

        spec2len = (filespec2 ? strlen(filespec2) : 0);
        len = spec2len + (filespec1 ? strlen(filespec1) : 0);

        if (spec2len && filespec2[spec2len - 1] == '/') {
            spec2len--;
            len--;
        }
        merged = OPENSSL_malloc(len + 2);
        if (merged == NULL) {
            DSOerr(DSO_F_DL_MERGER, ERR_R_MALLOC_FAILURE);
            return NULL;
        }
        strcpy(merged, filespec2);
        merged[spec2len] = '/';
        strcpy(&merged[spec2len + 1], filespec1);
    }
    return merged;
}

/*
 * This function is identical to the one in dso_dlfcn.c, but as it is highly
 * unlikely that both the "dl" *and* "dlfcn" variants are being compiled at
 * the same time, there's no great duplicating the code. Figuring out an
 * elegant way to share one copy of the code would be more difficult and
 * would not leave the implementations independent.
 */
static char *dl_name_converter(DSO *dso, const char *filename)
{
    char *translated;
    int len, rsize, transform;

    len = strlen(filename);
    rsize = len + 1;
    transform = (strstr(filename, "/") == NULL);
    {
        /* We will convert this to "%s.s?" or "lib%s.s?" */
        rsize += strlen(DSO_EXTENSION); /* The length of ".s?" */
        if ((DSO_flags(dso) & DSO_FLAG_NAME_TRANSLATION_EXT_ONLY) == 0)
            rsize += 3;         /* The length of "lib" */
    }
    translated = OPENSSL_malloc(rsize);
    if (translated == NULL) {
        DSOerr(DSO_F_DL_NAME_CONVERTER, DSO_R_NAME_TRANSLATION_FAILED);
        return NULL;
    }
    if (transform) {
        if ((DSO_flags(dso) & DSO_FLAG_NAME_TRANSLATION_EXT_ONLY) == 0)
            sprintf(translated, "lib%s%s", filename, DSO_EXTENSION);
        else
            sprintf(translated, "%s%s", filename, DSO_EXTENSION);
    } else
        sprintf(translated, "%s", filename);
    return translated;
}

static int dl_pathbyaddr(void *addr, char *path, int sz)
{
    struct shl_descriptor inf;
    int i, len;

    if (addr == NULL) {
        union {
            int (*f) (void *, char *, int);
            void *p;
        } t = {
            dl_pathbyaddr
        };
        addr = t.p;
    }

    for (i = -1; shl_get_r(i, &inf) == 0; i++) {
        if (((size_t)addr >= inf.tstart && (size_t)addr < inf.tend) ||
            ((size_t)addr >= inf.dstart && (size_t)addr < inf.dend)) {
            len = (int)strlen(inf.filename);
            if (sz <= 0)
                return len + 1;
            if (len >= sz)
                len = sz - 1;
            memcpy(path, inf.filename, len);
            path[len++] = 0;
            return len;
        }
    }

    return -1;
}

static void *dl_globallookup(const char *name)
{
    void *ret;
    shl_t h = NULL;

    return shl_findsym(&h, name, TYPE_UNDEFINED, &ret) ? NULL : ret;
}
#endif                          /* DSO_DL */
