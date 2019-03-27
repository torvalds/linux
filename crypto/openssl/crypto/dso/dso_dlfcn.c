/*
 * Copyright 2000-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

/*
 * We need to do this early, because stdio.h includes the header files that
 * handle _GNU_SOURCE and other similar macros.  Defining it later is simply
 * too late, because those headers are protected from re- inclusion.
 */
#ifndef _GNU_SOURCE
# define _GNU_SOURCE            /* make sure dladdr is declared */
#endif

#include "dso_locl.h"
#include "e_os.h"

#ifdef DSO_DLFCN

# ifdef HAVE_DLFCN_H
#  ifdef __osf__
#   define __EXTENSIONS__
#  endif
#  include <dlfcn.h>
#  define HAVE_DLINFO 1
#  if defined(__CYGWIN__) || \
     defined(__SCO_VERSION__) || defined(_SCO_ELF) || \
     (defined(__osf__) && !defined(RTLD_NEXT))     || \
     (defined(__OpenBSD__) && !defined(RTLD_SELF)) || \
        defined(__ANDROID__)
#   undef HAVE_DLINFO
#  endif
# endif

/* Part of the hack in "dlfcn_load" ... */
# define DSO_MAX_TRANSLATED_SIZE 256

static int dlfcn_load(DSO *dso);
static int dlfcn_unload(DSO *dso);
static DSO_FUNC_TYPE dlfcn_bind_func(DSO *dso, const char *symname);
static char *dlfcn_name_converter(DSO *dso, const char *filename);
static char *dlfcn_merger(DSO *dso, const char *filespec1,
                          const char *filespec2);
static int dlfcn_pathbyaddr(void *addr, char *path, int sz);
static void *dlfcn_globallookup(const char *name);

static DSO_METHOD dso_meth_dlfcn = {
    "OpenSSL 'dlfcn' shared library method",
    dlfcn_load,
    dlfcn_unload,
    dlfcn_bind_func,
    NULL,                       /* ctrl */
    dlfcn_name_converter,
    dlfcn_merger,
    NULL,                       /* init */
    NULL,                       /* finish */
    dlfcn_pathbyaddr,
    dlfcn_globallookup
};

DSO_METHOD *DSO_METHOD_openssl(void)
{
    return &dso_meth_dlfcn;
}

/*
 * Prior to using the dlopen() function, we should decide on the flag we
 * send. There's a few different ways of doing this and it's a messy
 * venn-diagram to match up which platforms support what. So as we don't have
 * autoconf yet, I'm implementing a hack that could be hacked further
 * relatively easily to deal with cases as we find them. Initially this is to
 * cope with OpenBSD.
 */
# if defined(__OpenBSD__) || defined(__NetBSD__)
#  ifdef DL_LAZY
#   define DLOPEN_FLAG DL_LAZY
#  else
#   ifdef RTLD_NOW
#    define DLOPEN_FLAG RTLD_NOW
#   else
#    define DLOPEN_FLAG 0
#   endif
#  endif
# else
#  define DLOPEN_FLAG RTLD_NOW  /* Hope this works everywhere else */
# endif

/*
 * For this DSO_METHOD, our meth_data STACK will contain; (i) the handle
 * (void*) returned from dlopen().
 */

static int dlfcn_load(DSO *dso)
{
    void *ptr = NULL;
    /* See applicable comments in dso_dl.c */
    char *filename = DSO_convert_filename(dso, NULL);
    int flags = DLOPEN_FLAG;
    int saveerrno = get_last_sys_error();

    if (filename == NULL) {
        DSOerr(DSO_F_DLFCN_LOAD, DSO_R_NO_FILENAME);
        goto err;
    }
# ifdef RTLD_GLOBAL
    if (dso->flags & DSO_FLAG_GLOBAL_SYMBOLS)
        flags |= RTLD_GLOBAL;
# endif
# ifdef _AIX
    if (filename[strlen(filename) - 1] == ')')
        flags |= RTLD_MEMBER;
# endif
    ptr = dlopen(filename, flags);
    if (ptr == NULL) {
        DSOerr(DSO_F_DLFCN_LOAD, DSO_R_LOAD_FAILED);
        ERR_add_error_data(4, "filename(", filename, "): ", dlerror());
        goto err;
    }
    /*
     * Some dlopen() implementations (e.g. solaris) do no preserve errno, even
     * on a successful call.
     */
    set_sys_error(saveerrno);
    if (!sk_void_push(dso->meth_data, (char *)ptr)) {
        DSOerr(DSO_F_DLFCN_LOAD, DSO_R_STACK_ERROR);
        goto err;
    }
    /* Success */
    dso->loaded_filename = filename;
    return 1;
 err:
    /* Cleanup! */
    OPENSSL_free(filename);
    if (ptr != NULL)
        dlclose(ptr);
    return 0;
}

static int dlfcn_unload(DSO *dso)
{
    void *ptr;
    if (dso == NULL) {
        DSOerr(DSO_F_DLFCN_UNLOAD, ERR_R_PASSED_NULL_PARAMETER);
        return 0;
    }
    if (sk_void_num(dso->meth_data) < 1)
        return 1;
    ptr = sk_void_pop(dso->meth_data);
    if (ptr == NULL) {
        DSOerr(DSO_F_DLFCN_UNLOAD, DSO_R_NULL_HANDLE);
        /*
         * Should push the value back onto the stack in case of a retry.
         */
        sk_void_push(dso->meth_data, ptr);
        return 0;
    }
    /* For now I'm not aware of any errors associated with dlclose() */
    dlclose(ptr);
    return 1;
}

static DSO_FUNC_TYPE dlfcn_bind_func(DSO *dso, const char *symname)
{
    void *ptr;
    union {
        DSO_FUNC_TYPE sym;
        void *dlret;
    } u;

    if ((dso == NULL) || (symname == NULL)) {
        DSOerr(DSO_F_DLFCN_BIND_FUNC, ERR_R_PASSED_NULL_PARAMETER);
        return NULL;
    }
    if (sk_void_num(dso->meth_data) < 1) {
        DSOerr(DSO_F_DLFCN_BIND_FUNC, DSO_R_STACK_ERROR);
        return NULL;
    }
    ptr = sk_void_value(dso->meth_data, sk_void_num(dso->meth_data) - 1);
    if (ptr == NULL) {
        DSOerr(DSO_F_DLFCN_BIND_FUNC, DSO_R_NULL_HANDLE);
        return NULL;
    }
    u.dlret = dlsym(ptr, symname);
    if (u.dlret == NULL) {
        DSOerr(DSO_F_DLFCN_BIND_FUNC, DSO_R_SYM_FAILURE);
        ERR_add_error_data(4, "symname(", symname, "): ", dlerror());
        return NULL;
    }
    return u.sym;
}

static char *dlfcn_merger(DSO *dso, const char *filespec1,
                          const char *filespec2)
{
    char *merged;

    if (!filespec1 && !filespec2) {
        DSOerr(DSO_F_DLFCN_MERGER, ERR_R_PASSED_NULL_PARAMETER);
        return NULL;
    }
    /*
     * If the first file specification is a rooted path, it rules. same goes
     * if the second file specification is missing.
     */
    if (!filespec2 || (filespec1 != NULL && filespec1[0] == '/')) {
        merged = OPENSSL_strdup(filespec1);
        if (merged == NULL) {
            DSOerr(DSO_F_DLFCN_MERGER, ERR_R_MALLOC_FAILURE);
            return NULL;
        }
    }
    /*
     * If the first file specification is missing, the second one rules.
     */
    else if (!filespec1) {
        merged = OPENSSL_strdup(filespec2);
        if (merged == NULL) {
            DSOerr(DSO_F_DLFCN_MERGER, ERR_R_MALLOC_FAILURE);
            return NULL;
        }
    } else {
        /*
         * This part isn't as trivial as it looks.  It assumes that the
         * second file specification really is a directory, and makes no
         * checks whatsoever.  Therefore, the result becomes the
         * concatenation of filespec2 followed by a slash followed by
         * filespec1.
         */
        int spec2len, len;

        spec2len = strlen(filespec2);
        len = spec2len + strlen(filespec1);

        if (spec2len && filespec2[spec2len - 1] == '/') {
            spec2len--;
            len--;
        }
        merged = OPENSSL_malloc(len + 2);
        if (merged == NULL) {
            DSOerr(DSO_F_DLFCN_MERGER, ERR_R_MALLOC_FAILURE);
            return NULL;
        }
        strcpy(merged, filespec2);
        merged[spec2len] = '/';
        strcpy(&merged[spec2len + 1], filespec1);
    }
    return merged;
}

static char *dlfcn_name_converter(DSO *dso, const char *filename)
{
    char *translated;
    int len, rsize, transform;

    len = strlen(filename);
    rsize = len + 1;
    transform = (strstr(filename, "/") == NULL);
    if (transform) {
        /* We will convert this to "%s.so" or "lib%s.so" etc */
        rsize += strlen(DSO_EXTENSION);    /* The length of ".so" */
        if ((DSO_flags(dso) & DSO_FLAG_NAME_TRANSLATION_EXT_ONLY) == 0)
            rsize += 3;         /* The length of "lib" */
    }
    translated = OPENSSL_malloc(rsize);
    if (translated == NULL) {
        DSOerr(DSO_F_DLFCN_NAME_CONVERTER, DSO_R_NAME_TRANSLATION_FAILED);
        return NULL;
    }
    if (transform) {
        if ((DSO_flags(dso) & DSO_FLAG_NAME_TRANSLATION_EXT_ONLY) == 0)
            sprintf(translated, "lib%s" DSO_EXTENSION, filename);
        else
            sprintf(translated, "%s" DSO_EXTENSION, filename);
    } else
        sprintf(translated, "%s", filename);
    return translated;
}

# ifdef __sgi
/*-
This is a quote from IRIX manual for dladdr(3c):

     <dlfcn.h> does not contain a prototype for dladdr or definition of
     Dl_info.  The #include <dlfcn.h>  in the SYNOPSIS line is traditional,
     but contains no dladdr prototype and no IRIX library contains an
     implementation.  Write your own declaration based on the code below.

     The following code is dependent on internal interfaces that are not
     part of the IRIX compatibility guarantee; however, there is no future
     intention to change this interface, so on a practical level, the code
     below is safe to use on IRIX.
*/
#  include <rld_interface.h>
#  ifndef _RLD_INTERFACE_DLFCN_H_DLADDR
#   define _RLD_INTERFACE_DLFCN_H_DLADDR
typedef struct Dl_info {
    const char *dli_fname;
    void *dli_fbase;
    const char *dli_sname;
    void *dli_saddr;
    int dli_version;
    int dli_reserved1;
    long dli_reserved[4];
} Dl_info;
#  else
typedef struct Dl_info Dl_info;
#  endif
#  define _RLD_DLADDR             14

static int dladdr(void *address, Dl_info *dl)
{
    void *v;
    v = _rld_new_interface(_RLD_DLADDR, address, dl);
    return (int)v;
}
# endif                         /* __sgi */

# ifdef _AIX
/*-
 * See IBM's AIX Version 7.2, Technical Reference:
 *  Base Operating System and Extensions, Volume 1 and 2
 *  https://www.ibm.com/support/knowledgecenter/ssw_aix_72/com.ibm.aix.base/technicalreferences.htm
 */
#  include <sys/ldr.h>
#  include <errno.h>
/* ~ 64 * (sizeof(struct ld_info) + _XOPEN_PATH_MAX + _XOPEN_NAME_MAX) */
#  define DLFCN_LDINFO_SIZE 86976
typedef struct Dl_info {
    const char *dli_fname;
} Dl_info;
/*
 * This dladdr()-implementation will also find the ptrgl (Pointer Glue) virtual
 * address of a function, which is just located in the DATA segment instead of
 * the TEXT segment.
 */
static int dladdr(void *ptr, Dl_info *dl)
{
    uintptr_t addr = (uintptr_t)ptr;
    unsigned int found = 0;
    struct ld_info *ldinfos, *next_ldi, *this_ldi;

    if ((ldinfos = OPENSSL_malloc(DLFCN_LDINFO_SIZE)) == NULL) {
        errno = ENOMEM;
        dl->dli_fname = NULL;
        return 0;
    }

    if ((loadquery(L_GETINFO, (void *)ldinfos, DLFCN_LDINFO_SIZE)) < 0) {
        /*-
         * Error handling is done through errno and dlerror() reading errno:
         *  ENOMEM (ldinfos buffer is too small),
         *  EINVAL (invalid flags),
         *  EFAULT (invalid ldinfos ptr)
         */
        OPENSSL_free((void *)ldinfos);
        dl->dli_fname = NULL;
        return 0;
    }
    next_ldi = ldinfos;

    do {
        this_ldi = next_ldi;
        if (((addr >= (uintptr_t)this_ldi->ldinfo_textorg)
             && (addr < ((uintptr_t)this_ldi->ldinfo_textorg +
                         this_ldi->ldinfo_textsize)))
            || ((addr >= (uintptr_t)this_ldi->ldinfo_dataorg)
                && (addr < ((uintptr_t)this_ldi->ldinfo_dataorg +
                            this_ldi->ldinfo_datasize)))) {
            char *buffer, *member;
            size_t buffer_sz, member_len;

            buffer_sz = strlen(this_ldi->ldinfo_filename) + 1;
            member = this_ldi->ldinfo_filename + buffer_sz;
            if ((member_len = strlen(member)) > 0)
                buffer_sz += 1 + member_len + 1;
            found = 1;
            if ((buffer = OPENSSL_malloc(buffer_sz)) != NULL) {
                OPENSSL_strlcpy(buffer, this_ldi->ldinfo_filename, buffer_sz);
                if (member_len > 0) {
                    /*
                     * Need to respect a possible member name and not just
                     * returning the path name in this case. See docs:
                     * sys/ldr.h, loadquery() and dlopen()/RTLD_MEMBER.
                     */
                    OPENSSL_strlcat(buffer, "(", buffer_sz);
                    OPENSSL_strlcat(buffer, member, buffer_sz);
                    OPENSSL_strlcat(buffer, ")", buffer_sz);
                }
                dl->dli_fname = buffer;
            } else {
                errno = ENOMEM;
            }
        } else {
            next_ldi = (struct ld_info *)((uintptr_t)this_ldi +
                                          this_ldi->ldinfo_next);
        }
    } while (this_ldi->ldinfo_next && !found);
    OPENSSL_free((void *)ldinfos);
    return (found && dl->dli_fname != NULL);
}
# endif                         /* _AIX */

static int dlfcn_pathbyaddr(void *addr, char *path, int sz)
{
# ifdef HAVE_DLINFO
    Dl_info dli;
    int len;

    if (addr == NULL) {
        union {
            int (*f) (void *, char *, int);
            void *p;
        } t = {
            dlfcn_pathbyaddr
        };
        addr = t.p;
    }

    if (dladdr(addr, &dli)) {
        len = (int)strlen(dli.dli_fname);
        if (sz <= 0) {
#  ifdef _AIX
            OPENSSL_free((void *)dli.dli_fname);
#  endif
            return len + 1;
        }
        if (len >= sz)
            len = sz - 1;
        memcpy(path, dli.dli_fname, len);
        path[len++] = 0;
#  ifdef _AIX
        OPENSSL_free((void *)dli.dli_fname);
#  endif
        return len;
    }

    ERR_add_error_data(2, "dlfcn_pathbyaddr(): ", dlerror());
# endif
    return -1;
}

static void *dlfcn_globallookup(const char *name)
{
    void *ret = NULL, *handle = dlopen(NULL, RTLD_LAZY);

    if (handle) {
        ret = dlsym(handle, name);
        dlclose(handle);
    }

    return ret;
}
#endif                          /* DSO_DLFCN */
