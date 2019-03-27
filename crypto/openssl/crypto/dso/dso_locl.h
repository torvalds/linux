/*
 * Copyright 2016 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the OpenSSL license (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include <stdio.h>
#include "internal/cryptlib.h"
#include "internal/dso.h"
#include "internal/dso_conf.h"
#include "internal/refcount.h"

/**********************************************************************/
/* The low-level handle type used to refer to a loaded shared library */

struct dso_st {
    DSO_METHOD *meth;
    /*
     * Standard dlopen uses a (void *). Win32 uses a HANDLE. VMS doesn't use
     * anything but will need to cache the filename for use in the dso_bind
     * handler. All in all, let each method control its own destiny.
     * "Handles" and such go in a STACK.
     */
    STACK_OF(void) *meth_data;
    CRYPTO_REF_COUNT references;
    int flags;
    /*
     * For use by applications etc ... use this for your bits'n'pieces, don't
     * touch meth_data!
     */
    CRYPTO_EX_DATA ex_data;
    /*
     * If this callback function pointer is set to non-NULL, then it will be
     * used in DSO_load() in place of meth->dso_name_converter. NB: This
     * should normally set using DSO_set_name_converter().
     */
    DSO_NAME_CONVERTER_FUNC name_converter;
    /*
     * If this callback function pointer is set to non-NULL, then it will be
     * used in DSO_load() in place of meth->dso_merger. NB: This should
     * normally set using DSO_set_merger().
     */
    DSO_MERGER_FUNC merger;
    /*
     * This is populated with (a copy of) the platform-independent filename
     * used for this DSO.
     */
    char *filename;
    /*
     * This is populated with (a copy of) the translated filename by which
     * the DSO was actually loaded. It is NULL iff the DSO is not currently
     * loaded. NB: This is here because the filename translation process may
     * involve a callback being invoked more than once not only to convert to
     * a platform-specific form, but also to try different filenames in the
     * process of trying to perform a load. As such, this variable can be
     * used to indicate (a) whether this DSO structure corresponds to a
     * loaded library or not, and (b) the filename with which it was actually
     * loaded.
     */
    char *loaded_filename;
    CRYPTO_RWLOCK *lock;
};

struct dso_meth_st {
    const char *name;
    /*
     * Loads a shared library, NB: new DSO_METHODs must ensure that a
     * successful load populates the loaded_filename field, and likewise a
     * successful unload OPENSSL_frees and NULLs it out.
     */
    int (*dso_load) (DSO *dso);
    /* Unloads a shared library */
    int (*dso_unload) (DSO *dso);
    /*
     * Binds a function - assumes a return type of DSO_FUNC_TYPE. This should
     * be cast to the real function prototype by the caller. Platforms that
     * don't have compatible representations for different prototypes (this
     * is possible within ANSI C) are highly unlikely to have shared
     * libraries at all, let alone a DSO_METHOD implemented for them.
     */
    DSO_FUNC_TYPE (*dso_bind_func) (DSO *dso, const char *symname);
    /*
     * The generic (yuck) "ctrl()" function. NB: Negative return values
     * (rather than zero) indicate errors.
     */
    long (*dso_ctrl) (DSO *dso, int cmd, long larg, void *parg);
    /*
     * The default DSO_METHOD-specific function for converting filenames to a
     * canonical native form.
     */
    DSO_NAME_CONVERTER_FUNC dso_name_converter;
    /*
     * The default DSO_METHOD-specific function for converting filenames to a
     * canonical native form.
     */
    DSO_MERGER_FUNC dso_merger;
    /* [De]Initialisation handlers. */
    int (*init) (DSO *dso);
    int (*finish) (DSO *dso);
    /* Return pathname of the module containing location */
    int (*pathbyaddr) (void *addr, char *path, int sz);
    /* Perform global symbol lookup, i.e. among *all* modules */
    void *(*globallookup) (const char *symname);
};
