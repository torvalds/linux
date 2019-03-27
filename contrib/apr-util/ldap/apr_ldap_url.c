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

/* Portions Copyright 1998-2002 The OpenLDAP Foundation
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted only as authorized by the OpenLDAP
 * Public License.  A copy of this license is available at
 * http://www.OpenLDAP.org/license.html or in file LICENSE in the
 * top-level directory of the distribution.
 * 
 * OpenLDAP is a registered trademark of the OpenLDAP Foundation.
 * 
 * Individual files and/or contributed packages may be copyright by
 * other parties and subject to additional restrictions.
 * 
 * This work is derived from the University of Michigan LDAP v3.3
 * distribution.  Information concerning this software is available
 * at: http://www.umich.edu/~dirsvcs/ldap/
 * 
 * This work also contains materials derived from public sources.
 * 
 * Additional information about OpenLDAP can be obtained at:
 *     http://www.openldap.org/
 */

/* 
 * Portions Copyright (c) 1992-1996 Regents of the University of Michigan.
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms are permitted
 * provided that this notice is preserved and that due credit is given
 * to the University of Michigan at Ann Arbor. The name of the University
 * may not be used to endorse or promote products derived from this
 * software without specific prior written permission. This software
 * is provided ``as is'' without express or implied warranty.
 */

/*  apr_ldap_url.c -- LDAP URL (RFC 2255) related routines
 *
 *  Win32 and perhaps other non-OpenLDAP based ldap libraries may be
 *  missing ldap_url_* APIs.  We focus here on the one significant
 *  aspect, which is parsing.  We have [for the time being] omitted
 *  the ldap_url_search APIs.
 *
 *  LDAP URLs look like this:
 *    ldap[is]://host:port[/[dn[?[attributes][?[scope][?[filter][?exts]]]]]]
 *
 *  where:
 *   attributes is a comma separated list
 *   scope is one of these three strings:  base one sub (default=base)
 *   filter is an string-represented filter as in RFC 2254
 *
 *  e.g.,  ldap://host:port/dc=com?o,cn?base?o=openldap?extension
 *
 *  Tolerates URLs that look like: <ldapurl> and <URL:ldapurl>
 */

#include "apu.h"
#include "apr_pools.h"
#include "apr_general.h"
#include "apr_strings.h"
#include "apr_ldap.h"

#if APR_HAS_LDAP

#if APR_HAVE_STDLIB_H
#include <stdlib.h>
#endif

#ifndef LDAPS_PORT
#define LDAPS_PORT              636  /* ldaps:/// default LDAP over TLS port */
#endif

#define APR_LDAP_URL_PREFIX         "ldap://"
#define APR_LDAP_URL_PREFIX_LEN     (sizeof(APR_LDAP_URL_PREFIX)-1)
#define APR_LDAPS_URL_PREFIX        "ldaps://"
#define APR_LDAPS_URL_PREFIX_LEN    (sizeof(APR_LDAPS_URL_PREFIX)-1)
#define APR_LDAPI_URL_PREFIX        "ldapi://"
#define APR_LDAPI_URL_PREFIX_LEN    (sizeof(APR_LDAPI_URL_PREFIX)-1)
#define APR_LDAP_URL_URLCOLON       "URL:"
#define APR_LDAP_URL_URLCOLON_LEN   (sizeof(APR_LDAP_URL_URLCOLON)-1)


/* local functions */
static const char* skip_url_prefix(const char *url,
                                   int *enclosedp,
                                   const char **scheme);

static void apr_ldap_pvt_hex_unescape(char *s);

static int apr_ldap_pvt_unhex(int c);

static char **apr_ldap_str2charray(apr_pool_t *pool,
                                   const char *str,
                                   const char *brkstr);


/**
 * Is this URL an ldap url?
 *
 */
APU_DECLARE(int) apr_ldap_is_ldap_url(const char *url)
{
    int enclosed;
    const char * scheme;

    if( url == NULL ) {
        return 0;
    }

    if( skip_url_prefix( url, &enclosed, &scheme ) == NULL ) {
        return 0;
    }

    return 1;
}

/**
 * Is this URL a secure ldap url?
 *
 */
APU_DECLARE(int) apr_ldap_is_ldaps_url(const char *url)
{
    int enclosed;
    const char * scheme;

    if( url == NULL ) {
        return 0;
    }

    if( skip_url_prefix( url, &enclosed, &scheme ) == NULL ) {
        return 0;
    }

    return strcmp(scheme, "ldaps") == 0;
}

/**
 * Is this URL an ldap socket url?
 *
 */
APU_DECLARE(int) apr_ldap_is_ldapi_url(const char *url)
{
    int enclosed;
    const char * scheme;

    if( url == NULL ) {
        return 0;
    }

    if( skip_url_prefix( url, &enclosed, &scheme ) == NULL ) {
        return 0;
    }

    return strcmp(scheme, "ldapi") == 0;
}


static const char *skip_url_prefix(const char *url, int *enclosedp,
                                   const char **scheme)
{
    /*
     * return non-zero if this looks like a LDAP URL; zero if not
     * if non-zero returned, *urlp will be moved past "ldap://" part of URL
     */
    const char *p;

    if ( url == NULL ) {
        return( NULL );
    }

    p = url;

    /* skip leading '<' (if any) */
    if ( *p == '<' ) {
        *enclosedp = 1;
        ++p;
    } else {
        *enclosedp = 0;
    }

    /* skip leading "URL:" (if any) */
    if ( strncasecmp( p, APR_LDAP_URL_URLCOLON, APR_LDAP_URL_URLCOLON_LEN ) == 0 ) {
        p += APR_LDAP_URL_URLCOLON_LEN;
    }

    /* check for "ldap://" prefix */
    if ( strncasecmp( p, APR_LDAP_URL_PREFIX, APR_LDAP_URL_PREFIX_LEN ) == 0 ) {
        /* skip over "ldap://" prefix and return success */
        p += APR_LDAP_URL_PREFIX_LEN;
        *scheme = "ldap";
        return( p );
    }

    /* check for "ldaps://" prefix */
    if ( strncasecmp( p, APR_LDAPS_URL_PREFIX, APR_LDAPS_URL_PREFIX_LEN ) == 0 ) {
        /* skip over "ldaps://" prefix and return success */
        p += APR_LDAPS_URL_PREFIX_LEN;
        *scheme = "ldaps";
        return( p );
    }

    /* check for "ldapi://" prefix */
    if ( strncasecmp( p, APR_LDAPI_URL_PREFIX, APR_LDAPI_URL_PREFIX_LEN ) == 0 ) {
        /* skip over "ldapi://" prefix and return success */
        p += APR_LDAPI_URL_PREFIX_LEN;
        *scheme = "ldapi";
        return( p );
    }

    return( NULL );
}


static int str2scope(const char *p)
{
    if ( strcasecmp( p, "one" ) == 0 ) {
        return LDAP_SCOPE_ONELEVEL;

    } else if ( strcasecmp( p, "onetree" ) == 0 ) {
        return LDAP_SCOPE_ONELEVEL;

    } else if ( strcasecmp( p, "base" ) == 0 ) {
        return LDAP_SCOPE_BASE;

    } else if ( strcasecmp( p, "sub" ) == 0 ) {
        return LDAP_SCOPE_SUBTREE;

    } else if ( strcasecmp( p, "subtree" ) == 0 ) {
        return LDAP_SCOPE_SUBTREE;
    }

    return( -1 );
}


/**
 * Parse the URL provided into an apr_ldap_url_desc_t object.
 *
 * APR_SUCCESS is returned on success, APR_EGENERAL on failure.
 * The LDAP result code and reason string is returned in the
 * apr_ldap_err_t structure.
 */
APU_DECLARE(int) apr_ldap_url_parse_ext(apr_pool_t *pool,
                                        const char *url_in,
                                        apr_ldap_url_desc_t **ludpp,
                                        apr_ldap_err_t **result_err)
{
    apr_ldap_url_desc_t *ludp;
    char        *p, *q, *r;
    int         i, enclosed;
    const char  *scheme = NULL;
    const char  *url_tmp;
    char        *url;

    apr_ldap_err_t *result = (apr_ldap_err_t *)apr_pcalloc(pool, sizeof(apr_ldap_err_t));
    *result_err = result;

    /* sanity check our parameters */
    if( url_in == NULL || ludpp == NULL ) {
        result->reason = "Either the LDAP URL, or the URL structure was NULL. Oops.";
        result->rc = APR_LDAP_URL_ERR_PARAM;
        return APR_EGENERAL;
    }

    *ludpp = NULL;  /* pessimistic */

    url_tmp = skip_url_prefix( url_in, &enclosed, &scheme );
    if ( url_tmp == NULL ) {
        result->reason = "The scheme was not recognised as a valid LDAP URL scheme.";
        result->rc = APR_LDAP_URL_ERR_BADSCHEME;
        return APR_EGENERAL;
    }

    /* make working copy of the remainder of the URL */
    url = (char *)apr_pstrdup(pool, url_tmp);
    if ( url == NULL ) {
        result->reason = "Out of memory parsing LDAP URL.";
        result->rc = APR_LDAP_URL_ERR_MEM;
        return APR_EGENERAL;
    }

    if ( enclosed ) {
        p = &url[strlen(url)-1];

        if( *p != '>' ) {
            result->reason = "Bad enclosure error while parsing LDAP URL.";
            result->rc = APR_LDAP_URL_ERR_BADENCLOSURE;
            return APR_EGENERAL;
        }

        *p = '\0';
    }

    /* allocate return struct */
    ludp = (apr_ldap_url_desc_t *)apr_pcalloc(pool, sizeof(apr_ldap_url_desc_t));
    if ( ludp == NULL ) {
        result->reason = "Out of memory parsing LDAP URL.";
        result->rc = APR_LDAP_URL_ERR_MEM;
        return APR_EGENERAL;
    }

    ludp->lud_next = NULL;
    ludp->lud_host = NULL;
    ludp->lud_port = LDAP_PORT;
    ludp->lud_dn = NULL;
    ludp->lud_attrs = NULL;
    ludp->lud_filter = NULL;
    ludp->lud_scope = -1;
    ludp->lud_filter = NULL;
    ludp->lud_exts = NULL;

    ludp->lud_scheme = (char *)apr_pstrdup(pool, scheme);
    if ( ludp->lud_scheme == NULL ) {
        result->reason = "Out of memory parsing LDAP URL.";
        result->rc = APR_LDAP_URL_ERR_MEM;
        return APR_EGENERAL;
    }

    if( strcasecmp( ludp->lud_scheme, "ldaps" ) == 0 ) {
        ludp->lud_port = LDAPS_PORT;
    }

    /* scan forward for '/' that marks end of hostport and begin. of dn */
    p = strchr( url, '/' );

    if( p != NULL ) {
        /* terminate hostport; point to start of dn */
        *p++ = '\0';
    }

    /* IPv6 syntax with [ip address]:port */
    if ( *url == '[' ) {
        r = strchr( url, ']' );
        if ( r == NULL ) {
            result->reason = "Bad LDAP URL while parsing IPV6 syntax.";
            result->rc = APR_LDAP_URL_ERR_BADURL;
            return APR_EGENERAL;
        }
        *r++ = '\0';
        q = strrchr( r, ':' );
    } else {
        q = strrchr( url, ':' );
    }

    if ( q != NULL ) {
        apr_ldap_pvt_hex_unescape( ++q );

        if( *q == '\0' ) {
            result->reason = "Bad LDAP URL while parsing.";
            result->rc = APR_LDAP_URL_ERR_BADURL;
            return APR_EGENERAL;
        }

        ludp->lud_port = atoi( q );
    }

    apr_ldap_pvt_hex_unescape( url );

    /* If [ip address]:port syntax, url is [ip and we skip the [ */
    ludp->lud_host = (char *)apr_pstrdup(pool, url + ( *url == '[' ));
    if( ludp->lud_host == NULL ) {
        result->reason = "Out of memory parsing LDAP URL.";
        result->rc = APR_LDAP_URL_ERR_MEM;
        return APR_EGENERAL;
    }

    /*
     * Kludge.  ldap://111.222.333.444:389??cn=abc,o=company
     *
     * On early Novell releases, search references/referrals were returned
     * in this format, i.e., the dn was kind of in the scope position,
     * but the required slash is missing. The whole thing is illegal syntax,
     * but we need to account for it. Fortunately it can't be confused with
     * anything real.
     */
    if( (p == NULL) && (q != NULL) && ((q = strchr( q, '?')) != NULL)) {
        q++;
        /* ? immediately followed by question */
        if( *q == '?') {
            q++;
            if( *q != '\0' ) {
                /* parse dn part */
                apr_ldap_pvt_hex_unescape( q );
                ludp->lud_dn = (char *)apr_pstrdup(pool, q);
            } else {
                ludp->lud_dn = (char *)apr_pstrdup(pool, "");
            }

            if( ludp->lud_dn == NULL ) {
                result->reason = "Out of memory parsing LDAP URL.";
                result->rc = APR_LDAP_URL_ERR_MEM;
                return APR_EGENERAL;
            }
        }
    }

    if( p == NULL ) {
        *ludpp = ludp;
        return APR_SUCCESS;
    }

    /* scan forward for '?' that may marks end of dn */
    q = strchr( p, '?' );

    if( q != NULL ) {
        /* terminate dn part */
        *q++ = '\0';
    }

    if( *p != '\0' ) {
        /* parse dn part */
        apr_ldap_pvt_hex_unescape( p );
        ludp->lud_dn = (char *)apr_pstrdup(pool, p);
    } else {
        ludp->lud_dn = (char *)apr_pstrdup(pool, "");
    }

    if( ludp->lud_dn == NULL ) {
        result->reason = "Out of memory parsing LDAP URL.";
        result->rc = APR_LDAP_URL_ERR_MEM;
        return APR_EGENERAL;
    }

    if( q == NULL ) {
        /* no more */
        *ludpp = ludp;
        return APR_SUCCESS;
    }

    /* scan forward for '?' that may marks end of attributes */
    p = q;
    q = strchr( p, '?' );

    if( q != NULL ) {
        /* terminate attributes part */
        *q++ = '\0';
    }

    if( *p != '\0' ) {
        /* parse attributes */
        apr_ldap_pvt_hex_unescape( p );
        ludp->lud_attrs = apr_ldap_str2charray(pool, p, ",");

        if( ludp->lud_attrs == NULL ) {
            result->reason = "Bad attributes encountered while parsing LDAP URL.";
            result->rc = APR_LDAP_URL_ERR_BADATTRS;
            return APR_EGENERAL;
        }
    }

    if ( q == NULL ) {
        /* no more */
        *ludpp = ludp;
        return APR_SUCCESS;
    }

    /* scan forward for '?' that may marks end of scope */
    p = q;
    q = strchr( p, '?' );

    if( q != NULL ) {
        /* terminate the scope part */
        *q++ = '\0';
    }

    if( *p != '\0' ) {
        /* parse the scope */
        apr_ldap_pvt_hex_unescape( p );
        ludp->lud_scope = str2scope( p );

        if( ludp->lud_scope == -1 ) {
            result->reason = "Bad scope encountered while parsing LDAP URL.";
            result->rc = APR_LDAP_URL_ERR_BADSCOPE;
            return APR_EGENERAL;
        }
    }

    if ( q == NULL ) {
        /* no more */
        *ludpp = ludp;
        return APR_SUCCESS;
    }

    /* scan forward for '?' that may marks end of filter */
    p = q;
    q = strchr( p, '?' );

    if( q != NULL ) {
        /* terminate the filter part */
        *q++ = '\0';
    }

    if( *p != '\0' ) {
        /* parse the filter */
        apr_ldap_pvt_hex_unescape( p );

        if( ! *p ) {
            /* missing filter */
            result->reason = "Bad filter encountered while parsing LDAP URL.";
            result->rc = APR_LDAP_URL_ERR_BADFILTER;
            return APR_EGENERAL;
        }

        ludp->lud_filter = (char *)apr_pstrdup(pool, p);
        if( ludp->lud_filter == NULL ) {
            result->reason = "Out of memory parsing LDAP URL.";
            result->rc = APR_LDAP_URL_ERR_MEM;
            return APR_EGENERAL;
        }
    }

    if ( q == NULL ) {
        /* no more */
        *ludpp = ludp;
        return APR_SUCCESS;
    }

    /* scan forward for '?' that may marks end of extensions */
    p = q;
    q = strchr( p, '?' );

    if( q != NULL ) {
        /* extra '?' */
        result->reason = "Bad URL encountered while parsing LDAP URL.";
        result->rc = APR_LDAP_URL_ERR_BADURL;
        return APR_EGENERAL;
    }

    /* parse the extensions */
    ludp->lud_exts = apr_ldap_str2charray(pool, p, ",");
    if( ludp->lud_exts == NULL ) {
        result->reason = "Bad extensions encountered while parsing LDAP URL.";
        result->rc = APR_LDAP_URL_ERR_BADEXTS;
        return APR_EGENERAL;
    }

    for( i=0; ludp->lud_exts[i] != NULL; i++ ) {
        apr_ldap_pvt_hex_unescape( ludp->lud_exts[i] );

        if( *ludp->lud_exts[i] == '!' ) {
            /* count the number of critical extensions */
            ludp->lud_crit_exts++;
        }
    }

    if( i == 0 ) {
        /* must have 1 or more */
        result->reason = "Bad extensions encountered while parsing LDAP URL.";
        result->rc = APR_LDAP_URL_ERR_BADEXTS;
        return APR_EGENERAL;
    }

    /* no more */
    *ludpp = ludp;
    return APR_SUCCESS;
}


/**
 * Parse the URL provided into an apr_ldap_url_desc_t object.
 *
 * APR_SUCCESS is returned on success, APR_EGENERAL on failure.
 * The LDAP result code and reason string is returned in the
 * apr_ldap_err_t structure.
 */
APU_DECLARE(int) apr_ldap_url_parse(apr_pool_t *pool,
                                    const char *url_in,
                                    apr_ldap_url_desc_t **ludpp,
                                    apr_ldap_err_t **result_err)
{

    int rc = apr_ldap_url_parse_ext(pool, url_in, ludpp, result_err);
    if( rc != APR_SUCCESS ) {
        return rc;
    }

    if ((*ludpp)->lud_scope == -1) {
        (*ludpp)->lud_scope = LDAP_SCOPE_BASE;
    }

    if ((*ludpp)->lud_host != NULL && *(*ludpp)->lud_host == '\0') {
        (*ludpp)->lud_host = NULL;
    }

    return rc;

}


static void apr_ldap_pvt_hex_unescape(char *s)
{
    /*
     * Remove URL hex escapes from s... done in place.  The basic concept for
     * this routine is borrowed from the WWW library HTUnEscape() routine.
     */
    char    *p;

    for ( p = s; *s != '\0'; ++s ) {
        if ( *s == '%' ) {
            if ( *++s == '\0' ) {
                break;
            }
            *p = apr_ldap_pvt_unhex( *s ) << 4;
            if ( *++s == '\0' ) {
                break;
            }
            *p++ += apr_ldap_pvt_unhex( *s );
        } else {
            *p++ = *s;
        }
    }

    *p = '\0';
}


static int apr_ldap_pvt_unhex(int c)
{
    return( c >= '0' && c <= '9' ? c - '0'
        : c >= 'A' && c <= 'F' ? c - 'A' + 10
        : c - 'a' + 10 );
}


/**
 * Convert a string to a character array
 */
static char **apr_ldap_str2charray(apr_pool_t *pool,
                                   const char *str_in,
                                   const char *brkstr)
{
    char    **res;
    char    *str, *s;
    char    *lasts;
    int i;

    /* protect the input string from strtok */
    str = (char *)apr_pstrdup(pool, str_in);
    if( str == NULL ) {
        return NULL;
    }

    i = 1;
    for ( s = str; *s; s++ ) {
        /* Warning: this strchr was previously ldap_utf8_strchr(), check
         * whether this particular code has any charset issues.
         */
        if ( strchr( brkstr, *s ) != NULL ) {
            i++;
        }
    }

    res = (char **) apr_pcalloc(pool, (i + 1) * sizeof(char *));
    if( res == NULL ) {
        return NULL;
    }

    i = 0;

    for ( s = (char *)apr_strtok( str, brkstr, &lasts );
          s != NULL;
          s = (char *)apr_strtok( NULL, brkstr, &lasts ) ) {

        res[i] = (char *)apr_pstrdup(pool, s);
        if(res[i] == NULL) {
            return NULL;
        }

        i++;
    }

    res[i] = NULL;

    return( res );

}

#endif /* APR_HAS_LDAP */
