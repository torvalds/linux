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

#include "apr.h"
#include "apr_private.h"
#include "apr_strings.h"
#include "apr_portable.h"

#ifdef HAVE_LANGINFO_H
#include <langinfo.h>
#endif

/*
 * simple heuristic to determine codepage of source code so that
 * literal strings (e.g., "GET /\r\n") in source code can be translated
 * properly
 *
 * If appropriate, a symbol can be set at configure time to determine
 * this.  On EBCDIC platforms, it will be important how the code was
 * unpacked.
 */

APR_DECLARE(const char*) apr_os_default_encoding (apr_pool_t *pool)
{
#ifdef __MVS__
#    ifdef __CODESET__
        return __CODESET__;
#    else
        return "IBM-1047";
#    endif
#endif

    if ('}' == 0xD0) {
        return "IBM-1047";
    }

    if ('{' == 0xFB) {
        return "EDF04";
    }

    if ('A' == 0xC1) {
        return "EBCDIC"; /* not useful */
    }

    if ('A' == 0x41) {
        return "ISO-8859-1"; /* not necessarily true */
    }

    return "unknown";
}


APR_DECLARE(const char*) apr_os_locale_encoding (apr_pool_t *pool)
{
#if defined(HAVE_NL_LANGINFO) && defined(CODESET)
    const char *charset;

    charset = nl_langinfo(CODESET);
    if (charset && *charset) {
#ifdef _OSD_POSIX /* Bug workaround - delete as soon as fixed in OSD_POSIX */
        /* Some versions of OSD_POSIX return nl_langinfo(CODESET)="^[nN]" */
        /* Ignore the bogus information and use apr_os_default_encoding() */
        if (charset[0] != '^')
#endif
        return apr_pstrdup(pool, charset);
    }
#endif

    return apr_os_default_encoding(pool);
}
