/* Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "apr_arch_dso.h"
#include "apr_strings.h"
#include "apr_portable.h"

#if APR_HAS_DSO

#if !defined(DSO_USE_DLFCN) && !defined(DSO_USE_SHL) && !defined(DSO_USE_DYLD)
#error No DSO implementation specified.
#endif

#ifdef HAVE_STDDEF_H
#include <stddef.h>
#endif
#if APR_HAVE_STDLIB_H
#include <stdlib.h> /* malloc(), free() */
#endif
#if APR_HAVE_STRING_H
#include <string.h> /* for strerror() on HP-UX */
#endif

#if defined(DSO_USE_DYLD)
#define DYLD_LIBRARY_HANDLE (void *)-1
#endif

APR_DECLARE(apr_status_t) apr_os_dso_handle_put(apr_dso_handle_t **aprdso,
                                                apr_os_dso_handle_t osdso,
                                                apr_pool_t *pool)
{
    *aprdso = apr_pcalloc(pool, sizeof **aprdso);
    (*aprdso)->handle = osdso;
    (*aprdso)->pool = pool;
    return APR_SUCCESS;
}

APR_DECLARE(apr_status_t) apr_os_dso_handle_get(apr_os_dso_handle_t *osdso,
                                                apr_dso_handle_t *aprdso)
{
    *osdso = aprdso->handle;
    return APR_SUCCESS;
}

static apr_status_t dso_cleanup(void *thedso)
{
    apr_dso_handle_t *dso = thedso;

    if (dso->handle == NULL)
        return APR_SUCCESS;

#if defined(DSO_USE_SHL)
    shl_unload((shl_t)dso->handle);
#elif defined(DSO_USE_DYLD)
    if (dso->handle != DYLD_LIBRARY_HANDLE) {
        NSUnLinkModule(dso->handle, FALSE);
    }
#elif defined(DSO_USE_DLFCN)
    if (dlclose(dso->handle) != 0)
        return APR_EINIT;
#endif
    dso->handle = NULL;

    return APR_SUCCESS;
}

APR_DECLARE(apr_status_t) apr_dso_load(apr_dso_handle_t **res_handle, 
                                       const char *path, apr_pool_t *pool)
{
#if defined(DSO_USE_SHL)
    shl_t os_handle = shl_load(path, BIND_IMMEDIATE, 0L);

#elif defined(DSO_USE_DYLD)
    NSObjectFileImage image;
    NSModule os_handle = NULL;
    NSObjectFileImageReturnCode dsoerr;
    const char* err_msg = NULL;
    dsoerr = NSCreateObjectFileImageFromFile(path, &image);

    if (dsoerr == NSObjectFileImageSuccess) {
#if defined(NSLINKMODULE_OPTION_RETURN_ON_ERROR) && defined(NSLINKMODULE_OPTION_NONE)
        os_handle = NSLinkModule(image, path,
                                 NSLINKMODULE_OPTION_RETURN_ON_ERROR |
                                 NSLINKMODULE_OPTION_NONE);
        /* If something went wrong, get the errors... */
        if (!os_handle) {
            NSLinkEditErrors errors;
            int errorNumber;
            const char *fileName;
            NSLinkEditError(&errors, &errorNumber, &fileName, &err_msg);
        }
#else
        os_handle = NSLinkModule(image, path, FALSE);
#endif
        NSDestroyObjectFileImage(image);
    }
    else if ((dsoerr == NSObjectFileImageFormat ||
             dsoerr == NSObjectFileImageInappropriateFile) &&
             NSAddLibrary(path) == TRUE) {
        os_handle = (NSModule)DYLD_LIBRARY_HANDLE;
    }
    else {
        err_msg = "cannot create object file image or add library";
    }

#elif defined(DSO_USE_DLFCN)
#if defined(OSF1) || defined(SEQUENT) || defined(SNI) ||\
    (defined(__FreeBSD_version) && (__FreeBSD_version >= 220000)) ||\
    defined(__DragonFly__)
    void *os_handle = dlopen((char *)path, RTLD_NOW | RTLD_GLOBAL);

#else
    int flags = RTLD_NOW | RTLD_GLOBAL;
    void *os_handle;
#ifdef _AIX
    if (strchr(path + 1, '(') && path[strlen(path) - 1] == ')')
    {
        /* This special archive.a(dso.so) syntax is required for
         * the way libtool likes to build shared libraries on AIX.
         * dlopen() support for such a library requires that the
         * RTLD_MEMBER flag be enabled.
         */
        flags |= RTLD_MEMBER;
    }
#endif
    os_handle = dlopen(path, flags);
#endif    
#endif /* DSO_USE_x */

    *res_handle = apr_pcalloc(pool, sizeof(**res_handle));

    if(os_handle == NULL) {
#if defined(DSO_USE_SHL)
        (*res_handle)->errormsg = strerror(errno);
        return APR_EDSOOPEN;
#elif defined(DSO_USE_DYLD)
        (*res_handle)->errormsg = (err_msg) ? err_msg : "link failed";
        return APR_EDSOOPEN;
#elif defined(DSO_USE_DLFCN)
        (*res_handle)->errormsg = dlerror();
        return APR_EDSOOPEN;
#endif
    }

    (*res_handle)->handle = (void*)os_handle;
    (*res_handle)->pool = pool;
    (*res_handle)->errormsg = NULL;

    apr_pool_cleanup_register(pool, *res_handle, dso_cleanup, apr_pool_cleanup_null);

    return APR_SUCCESS;
}
    
APR_DECLARE(apr_status_t) apr_dso_unload(apr_dso_handle_t *handle)
{
    return apr_pool_cleanup_run(handle->pool, handle, dso_cleanup);
}

APR_DECLARE(apr_status_t) apr_dso_sym(apr_dso_handle_sym_t *ressym, 
                                      apr_dso_handle_t *handle, 
                                      const char *symname)
{
#if defined(DSO_USE_SHL)
    void *symaddr = NULL;
    int status;

    errno = 0;
    status = shl_findsym((void *)&handle->handle, symname, TYPE_PROCEDURE, &symaddr);
    if (status == -1 && errno == 0) /* try TYPE_DATA instead */
        status = shl_findsym((void *)&handle->handle, symname, TYPE_DATA, &symaddr);
    if (status == -1)
        return APR_ESYMNOTFOUND;
    *ressym = symaddr;
    return APR_SUCCESS;

#elif defined(DSO_USE_DYLD)
    void *retval = NULL;
    NSSymbol symbol;
    char *symname2 = (char*)malloc(sizeof(char)*(strlen(symname)+2));
    sprintf(symname2, "_%s", symname);
#ifdef NSLINKMODULE_OPTION_PRIVATE
    if (handle->handle == DYLD_LIBRARY_HANDLE) {
        symbol = NSLookupAndBindSymbol(symname2);
    }
    else {
        symbol = NSLookupSymbolInModule((NSModule)handle->handle, symname2);
    }
#else
    symbol = NSLookupAndBindSymbol(symname2);
#endif
    free(symname2);
    if (symbol == NULL) {
        handle->errormsg = "undefined symbol";
	return APR_ESYMNOTFOUND;
    }
    retval = NSAddressOfSymbol(symbol);
    if (retval == NULL) {
        handle->errormsg = "cannot resolve symbol";
	return APR_ESYMNOTFOUND;
    }
    *ressym = retval;
    return APR_SUCCESS;
#elif defined(DSO_USE_DLFCN)

#if defined(DLSYM_NEEDS_UNDERSCORE)
    void *retval;
    char *symbol = (char*)malloc(sizeof(char)*(strlen(symname)+2));
    sprintf(symbol, "_%s", symname);
    retval = dlsym(handle->handle, symbol);
    free(symbol);
#elif defined(SEQUENT) || defined(SNI)
    void *retval = dlsym(handle->handle, (char *)symname);
#else
    void *retval = dlsym(handle->handle, symname);
#endif /* DLSYM_NEEDS_UNDERSCORE */

    if (retval == NULL) {
        handle->errormsg = dlerror();
        return APR_ESYMNOTFOUND;
    }

    *ressym = retval;
    
    return APR_SUCCESS;
#endif /* DSO_USE_x */
}

APR_DECLARE(const char *) apr_dso_error(apr_dso_handle_t *dso, char *buffer, 
                                        apr_size_t buflen)
{
    if (dso->errormsg) {
        apr_cpystrn(buffer, dso->errormsg, buflen);
        return dso->errormsg;
    }
    return "No Error";
}

#endif
