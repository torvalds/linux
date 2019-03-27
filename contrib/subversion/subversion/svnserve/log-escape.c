/*
 * log-escape.c :  Functions for escaping log items
 *                 copied from Apache httpd
 *
 * ====================================================================
 * Licensed to the Apache Software Foundation (ASF) under one or more
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
 *
 * ====================================================================
 *    Licensed to the Apache Software Foundation (ASF) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The ASF licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
 * ====================================================================
 */


#include <apr.h>
#define APR_WANT_STRFUNC
#include <apr_want.h>
#include "server.h"
#include "svn_ctype.h"

/* copied from httpd-2.2.4/server/util.c */
/* c2x takes an unsigned, and expects the caller has guaranteed that
 * 0 <= what < 256... which usually means that you have to cast to
 * unsigned char first, because (unsigned)(char)(x) first goes through
 * signed extension to an int before the unsigned cast.
 *
 * The reason for this assumption is to assist gcc code generation --
 * the unsigned char -> unsigned extension is already done earlier in
 * both uses of this code, so there's no need to waste time doing it
 * again.
 */
static const char c2x_table[] = "0123456789abcdef";

/* copied from httpd-2.2.4/server/util.c */
static APR_INLINE unsigned char *c2x(unsigned what, unsigned char prefix,
                                     unsigned char *where)
{
#if APR_CHARSET_EBCDIC
    what = apr_xlate_conv_byte(ap_hdrs_to_ascii, (unsigned char)what);
#endif /*APR_CHARSET_EBCDIC*/
    *where++ = prefix;
    *where++ = c2x_table[what >> 4];
    *where++ = c2x_table[what & 0xf];
    return where;
}

/* copied from httpd-2.2.4/server/util.c */
apr_size_t escape_errorlog_item(char *dest, const char *source,
                                apr_size_t buflen)
{
    unsigned char *d, *ep;
    const unsigned char *s;

    if (!source || !buflen) { /* be safe */
        return 0;
    }

    d = (unsigned char *)dest;
    s = (const unsigned char *)source;
    ep = d + buflen - 1;

    for (; d < ep && *s; ++s) {

        /* httpd-2.2.4/server/util.c has this:
             if (TEST_CHAR(*s, T_ESCAPE_LOGITEM)) {
           which does this same check with a fast lookup table.  Well,
           mostly the same; we don't escape quotes, as that does.
        */
        if (*s && (   !svn_ctype_isprint(*s)
                   || *s == '\\'
                   || svn_ctype_iscntrl(*s))) {
            *d++ = '\\';
            if (d >= ep) {
                --d;
                break;
            }

            switch(*s) {
            case '\b':
                *d++ = 'b';
                break;
            case '\n':
                *d++ = 'n';
                break;
            case '\r':
                *d++ = 'r';
                break;
            case '\t':
                *d++ = 't';
                break;
            case '\v':
                *d++ = 'v';
                break;
            case '\\':
                *d++ = *s;
                break;
            case '"': /* no need for this in error log */
                d[-1] = *s;
                break;
            default:
                if (d >= ep - 2) {
                    ep = --d; /* break the for loop as well */
                    break;
                }
                c2x(*s, 'x', d);
                d += 3;
            }
        }
        else {
            *d++ = *s;
        }
    }
    *d = '\0';

    return (d - (unsigned char *)dest);
}
