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

#include "apu.h"
#include "apu_config.h"
#include "apr_lib.h"
#include "apr_strings.h"
#include "apr_portable.h"
#include "apr_xlate.h"

/* If no implementation is available, don't generate code here since
 * apr_xlate.h emitted macros which return APR_ENOTIMPL.
 */

#if APR_HAS_XLATE

#ifdef HAVE_STDDEF_H
#include <stddef.h> /* for NULL */
#endif
#if APR_HAVE_STRING_H
#include <string.h>
#endif
#if APR_HAVE_STRINGS_H
#include <strings.h>
#endif
#ifdef HAVE_ICONV_H
#include <iconv.h>
#endif
#if APU_HAVE_APR_ICONV
#include <apr_iconv.h>
#endif

#if defined(APU_ICONV_INBUF_CONST) || APU_HAVE_APR_ICONV
#define ICONV_INBUF_TYPE const char **
#else
#define ICONV_INBUF_TYPE char **
#endif

#ifndef min
#define min(x,y) ((x) <= (y) ? (x) : (y))
#endif

struct apr_xlate_t {
    apr_pool_t *pool;
    char *frompage;
    char *topage;
    char *sbcs_table;
#if APU_HAVE_ICONV
    iconv_t ich;
#elif APU_HAVE_APR_ICONV
    apr_iconv_t ich;
#endif
};


static const char *handle_special_names(const char *page, apr_pool_t *pool)
{
    if (page == APR_DEFAULT_CHARSET) {
        return apr_os_default_encoding(pool);
    }
    else if (page == APR_LOCALE_CHARSET) {
        return apr_os_locale_encoding(pool);
    }
    else {
        return page;
    }
}

static apr_status_t apr_xlate_cleanup(void *convset)
{
    apr_xlate_t *old = convset;

#if APU_HAVE_APR_ICONV
    if (old->ich != (apr_iconv_t)-1) {
        return apr_iconv_close(old->ich, old->pool);
    }

#elif APU_HAVE_ICONV
    if (old->ich != (iconv_t)-1) {
        if (iconv_close(old->ich)) {
            int rv = errno;

            /* Sometimes, iconv is not good about setting errno. */
            return rv ? rv : APR_EINVAL;
        }
    }
#endif

    return APR_SUCCESS;
}

#if APU_HAVE_ICONV
static void check_sbcs(apr_xlate_t *convset)
{
    char inbuf[256], outbuf[256];
    char *inbufptr = inbuf;
    char *outbufptr = outbuf;
    apr_size_t inbytes_left, outbytes_left;
    int i;
    apr_size_t translated;

    for (i = 0; i < sizeof(inbuf); i++) {
        inbuf[i] = i;
    }

    inbytes_left = outbytes_left = sizeof(inbuf);
    translated = iconv(convset->ich, (ICONV_INBUF_TYPE)&inbufptr,
                       &inbytes_left, &outbufptr, &outbytes_left);

    if (translated != (apr_size_t)-1
        && inbytes_left == 0
        && outbytes_left == 0) {
        /* hurray... this is simple translation; save the table,
         * close the iconv descriptor
         */

        convset->sbcs_table = apr_palloc(convset->pool, sizeof(outbuf));
        memcpy(convset->sbcs_table, outbuf, sizeof(outbuf));
        iconv_close(convset->ich);
        convset->ich = (iconv_t)-1;

        /* TODO: add the table to the cache */
    }
    else {
        /* reset the iconv descriptor, since it's now in an undefined
         * state. */
        iconv_close(convset->ich);
        convset->ich = iconv_open(convset->topage, convset->frompage);
    }
}
#elif APU_HAVE_APR_ICONV
static void check_sbcs(apr_xlate_t *convset)
{
    char inbuf[256], outbuf[256];
    char *inbufptr = inbuf;
    char *outbufptr = outbuf;
    apr_size_t inbytes_left, outbytes_left;
    int i;
    apr_size_t translated;
    apr_status_t rv;

    for (i = 0; i < sizeof(inbuf); i++) {
        inbuf[i] = i;
    }

    inbytes_left = outbytes_left = sizeof(inbuf);
    rv = apr_iconv(convset->ich, (ICONV_INBUF_TYPE)&inbufptr,
                   &inbytes_left, &outbufptr, &outbytes_left,
                   &translated);

    if ((rv == APR_SUCCESS)
        && (translated != (apr_size_t)-1)
        && inbytes_left == 0
        && outbytes_left == 0) {
        /* hurray... this is simple translation; save the table,
         * close the iconv descriptor
         */

        convset->sbcs_table = apr_palloc(convset->pool, sizeof(outbuf));
        memcpy(convset->sbcs_table, outbuf, sizeof(outbuf));
        apr_iconv_close(convset->ich, convset->pool);
        convset->ich = (apr_iconv_t)-1;

        /* TODO: add the table to the cache */
    }
    else {
        /* reset the iconv descriptor, since it's now in an undefined
         * state. */
        apr_iconv_close(convset->ich, convset->pool);
        rv = apr_iconv_open(convset->topage, convset->frompage, 
                            convset->pool, &convset->ich);
    }
}
#endif /* APU_HAVE_APR_ICONV */

static void make_identity_table(apr_xlate_t *convset)
{
  int i;

  convset->sbcs_table = apr_palloc(convset->pool, 256);
  for (i = 0; i < 256; i++)
      convset->sbcs_table[i] = i;
}

APU_DECLARE(apr_status_t) apr_xlate_open(apr_xlate_t **convset,
                                         const char *topage,
                                         const char *frompage,
                                         apr_pool_t *pool)
{
    apr_status_t rv;
    apr_xlate_t *new;
    int found = 0;

    *convset = NULL;

    topage = handle_special_names(topage, pool);
    frompage = handle_special_names(frompage, pool);

    new = (apr_xlate_t *)apr_pcalloc(pool, sizeof(apr_xlate_t));
    if (!new) {
        return APR_ENOMEM;
    }

    new->pool = pool;
    new->topage = apr_pstrdup(pool, topage);
    new->frompage = apr_pstrdup(pool, frompage);
    if (!new->topage || !new->frompage) {
        return APR_ENOMEM;
    }

#ifdef TODO
    /* search cache of codepage pairs; we may be able to avoid the
     * expensive iconv_open()
     */

    set found to non-zero if found in the cache
#endif

    if ((! found) && (strcmp(topage, frompage) == 0)) {
        /* to and from are the same */
        found = 1;
        make_identity_table(new);
    }

#if APU_HAVE_APR_ICONV
    if (!found) {
        rv = apr_iconv_open(topage, frompage, pool, &new->ich);
        if (rv != APR_SUCCESS) {
            return rv;
        }
        found = 1;
        check_sbcs(new);
    } else
        new->ich = (apr_iconv_t)-1;

#elif APU_HAVE_ICONV
    if (!found) {
        new->ich = iconv_open(topage, frompage);
        if (new->ich == (iconv_t)-1) {
            int rv = errno;
            /* Sometimes, iconv is not good about setting errno. */
            return rv ? rv : APR_EINVAL;
        }
        found = 1;
        check_sbcs(new);
    } else
        new->ich = (iconv_t)-1;
#endif /* APU_HAVE_ICONV */

    if (found) {
        *convset = new;
        apr_pool_cleanup_register(pool, (void *)new, apr_xlate_cleanup,
                            apr_pool_cleanup_null);
        rv = APR_SUCCESS;
    }
    else {
        rv = APR_EINVAL; /* iconv() would return EINVAL if it
                                couldn't handle the pair */
    }

    return rv;
}

APU_DECLARE(apr_status_t) apr_xlate_sb_get(apr_xlate_t *convset, int *onoff)
{
    *onoff = convset->sbcs_table != NULL;
    return APR_SUCCESS;
}

APU_DECLARE(apr_status_t) apr_xlate_conv_buffer(apr_xlate_t *convset,
                                                const char *inbuf,
                                                apr_size_t *inbytes_left,
                                                char *outbuf,
                                                apr_size_t *outbytes_left)
{
    apr_status_t status = APR_SUCCESS;

#if APU_HAVE_APR_ICONV
    if (convset->ich != (apr_iconv_t)-1) {
        const char *inbufptr = inbuf;
        apr_size_t translated;
        char *outbufptr = outbuf;
        status = apr_iconv(convset->ich, &inbufptr, inbytes_left,
                           &outbufptr, outbytes_left, &translated);

        /* If everything went fine but we ran out of buffer, don't
         * report it as an error.  Caller needs to look at the two
         * bytes-left values anyway.
         *
         * There are three expected cases where rc is -1.  In each of
         * these cases, *inbytes_left != 0.
         * a) the non-error condition where we ran out of output
         *    buffer
         * b) the non-error condition where we ran out of input (i.e.,
         *    the last input character is incomplete)
         * c) the error condition where the input is invalid
         */
        switch (status) {

            case APR_BADARG:  /* out of space on output */
                status = 0; /* change table lookup code below if you
                               make this an error */
                break;

            case APR_EINVAL: /* input character not complete (yet) */
                status = APR_INCOMPLETE;
                break;

            case APR_BADCH: /* bad input byte */
                status = APR_EINVAL;
                break;

             /* Sometimes, iconv is not good about setting errno. */
            case 0:
                if (inbytes_left && *inbytes_left)
                    status = APR_INCOMPLETE;
                break;

            default:
                break;
        }
    }
    else

#elif APU_HAVE_ICONV
    if (convset->ich != (iconv_t)-1) {
        const char *inbufptr = inbuf;
        char *outbufptr = outbuf;
        apr_size_t translated;
        translated = iconv(convset->ich, (ICONV_INBUF_TYPE)&inbufptr,
                           inbytes_left, &outbufptr, outbytes_left);

        /* If everything went fine but we ran out of buffer, don't
         * report it as an error.  Caller needs to look at the two
         * bytes-left values anyway.
         *
         * There are three expected cases where rc is -1.  In each of
         * these cases, *inbytes_left != 0.
         * a) the non-error condition where we ran out of output
         *    buffer
         * b) the non-error condition where we ran out of input (i.e.,
         *    the last input character is incomplete)
         * c) the error condition where the input is invalid
         */
        if (translated == (apr_size_t)-1) {
            int rv = errno;
            switch (rv) {

            case E2BIG:  /* out of space on output */
                status = 0; /* change table lookup code below if you
                               make this an error */
                break;

            case EINVAL: /* input character not complete (yet) */
                status = APR_INCOMPLETE;
                break;

            case EILSEQ: /* bad input byte */
                status = APR_EINVAL;
                break;

             /* Sometimes, iconv is not good about setting errno. */
            case 0:
                status = APR_INCOMPLETE;
                break;

            default:
                status = rv;
                break;
            }
        }
    }
    else
#endif

    if (inbuf) {
        apr_size_t to_convert = min(*inbytes_left, *outbytes_left);
        apr_size_t converted = to_convert;
        char *table = convset->sbcs_table;

        while (to_convert) {
            *outbuf = table[(unsigned char)*inbuf];
            ++outbuf;
            ++inbuf;
            --to_convert;
        }
        *inbytes_left -= converted;
        *outbytes_left -= converted;
    }

    return status;
}

APU_DECLARE(apr_int32_t) apr_xlate_conv_byte(apr_xlate_t *convset,
                                             unsigned char inchar)
{
    if (convset->sbcs_table) {
        return convset->sbcs_table[inchar];
    }
    else {
        return -1;
    }
}

APU_DECLARE(apr_status_t) apr_xlate_close(apr_xlate_t *convset)
{
    return apr_pool_cleanup_run(convset->pool, convset, apr_xlate_cleanup);
}

#else /* !APR_HAS_XLATE */

APU_DECLARE(apr_status_t) apr_xlate_open(apr_xlate_t **convset,
                                         const char *topage,
                                         const char *frompage,
                                         apr_pool_t *pool)
{
    return APR_ENOTIMPL;
}

APU_DECLARE(apr_status_t) apr_xlate_sb_get(apr_xlate_t *convset, int *onoff)
{
    return APR_ENOTIMPL;
}

APU_DECLARE(apr_int32_t) apr_xlate_conv_byte(apr_xlate_t *convset,
                                             unsigned char inchar)
{
    return (-1);
}

APU_DECLARE(apr_status_t) apr_xlate_conv_buffer(apr_xlate_t *convset,
                                                const char *inbuf,
                                                apr_size_t *inbytes_left,
                                                char *outbuf,
                                                apr_size_t *outbytes_left)
{
    return APR_ENOTIMPL;
}

APU_DECLARE(apr_status_t) apr_xlate_close(apr_xlate_t *convset)
{
    return APR_ENOTIMPL;
}

#endif /* APR_HAS_XLATE */
