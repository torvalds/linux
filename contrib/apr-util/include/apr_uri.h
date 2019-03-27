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

/*
 * apr_uri.h: External Interface of apr_uri.c
 */

/**
 * @file apr_uri.h
 * @brief APR-UTIL URI Routines
 */

#ifndef APR_URI_H
#define APR_URI_H

#include "apu.h"

#include "apr_network_io.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup APR_Util_URI URI
 * @ingroup APR_Util
 * @{
 */

#define APR_URI_FTP_DEFAULT_PORT         21 /**< default FTP port */
#define APR_URI_SSH_DEFAULT_PORT         22 /**< default SSH port */
#define APR_URI_TELNET_DEFAULT_PORT      23 /**< default telnet port */
#define APR_URI_GOPHER_DEFAULT_PORT      70 /**< default Gopher port */
#define APR_URI_HTTP_DEFAULT_PORT        80 /**< default HTTP port */
#define APR_URI_POP_DEFAULT_PORT        110 /**< default POP port */
#define APR_URI_NNTP_DEFAULT_PORT       119 /**< default NNTP port */
#define APR_URI_IMAP_DEFAULT_PORT       143 /**< default IMAP port */
#define APR_URI_PROSPERO_DEFAULT_PORT   191 /**< default Prospero port */
#define APR_URI_WAIS_DEFAULT_PORT       210 /**< default WAIS port */
#define APR_URI_LDAP_DEFAULT_PORT       389 /**< default LDAP port */
#define APR_URI_HTTPS_DEFAULT_PORT      443 /**< default HTTPS port */
#define APR_URI_RTSP_DEFAULT_PORT       554 /**< default RTSP port */
#define APR_URI_SNEWS_DEFAULT_PORT      563 /**< default SNEWS port */
#define APR_URI_ACAP_DEFAULT_PORT       674 /**< default ACAP port */
#define APR_URI_NFS_DEFAULT_PORT       2049 /**< default NFS port */
#define APR_URI_TIP_DEFAULT_PORT       3372 /**< default TIP port */
#define APR_URI_SIP_DEFAULT_PORT       5060 /**< default SIP port */

/** Flags passed to unparse_uri_components(): */
/** suppress "scheme://user\@site:port" */
#define APR_URI_UNP_OMITSITEPART    (1U<<0)
/** Just omit user */
#define APR_URI_UNP_OMITUSER        (1U<<1)
/** Just omit password */
#define APR_URI_UNP_OMITPASSWORD    (1U<<2)
/** omit "user:password\@" part */
#define APR_URI_UNP_OMITUSERINFO    (APR_URI_UNP_OMITUSER | \
                                     APR_URI_UNP_OMITPASSWORD)
/** Show plain text password (default: show XXXXXXXX) */
#define APR_URI_UNP_REVEALPASSWORD  (1U<<3)
/** Show "scheme://user\@site:port" only */
#define APR_URI_UNP_OMITPATHINFO    (1U<<4)
/** Omit the "?queryarg" from the path */
#define APR_URI_UNP_OMITQUERY       (1U<<5)

/** @see apr_uri_t */
typedef struct apr_uri_t apr_uri_t;

/**
 * A structure to encompass all of the fields in a uri
 */
struct apr_uri_t {
    /** scheme ("http"/"ftp"/...) */
    char *scheme;
    /** combined [user[:password]\@]host[:port] */
    char *hostinfo;
    /** user name, as in http://user:passwd\@host:port/ */
    char *user;
    /** password, as in http://user:passwd\@host:port/ */
    char *password;
    /** hostname from URI (or from Host: header) */
    char *hostname;
    /** port string (integer representation is in "port") */
    char *port_str;
    /** the request path (or NULL if only scheme://host was given) */
    char *path;
    /** Everything after a '?' in the path, if present */
    char *query;
    /** Trailing "#fragment" string, if present */
    char *fragment;

    /** structure returned from gethostbyname() */
    struct hostent *hostent;

    /** The port number, numeric, valid only if port_str != NULL */
    apr_port_t port;
    
    /** has the structure been initialized */
    unsigned is_initialized:1;

    /** has the DNS been looked up yet */
    unsigned dns_looked_up:1;
    /** has the dns been resolved yet */
    unsigned dns_resolved:1;
};

/* apr_uri.c */
/**
 * Return the default port for a given scheme.  The schemes recognized are
 * http, ftp, https, gopher, wais, nntp, snews, and prospero
 * @param scheme_str The string that contains the current scheme
 * @return The default port for this scheme
 */ 
APU_DECLARE(apr_port_t) apr_uri_port_of_scheme(const char *scheme_str);

/**
 * Unparse a apr_uri_t structure to an URI string.  Optionally 
 * suppress the password for security reasons.
 * @param p The pool to allocate out of
 * @param uptr All of the parts of the uri
 * @param flags How to unparse the uri.  One of:
 * <PRE>
 *    APR_URI_UNP_OMITSITEPART        Suppress "scheme://user\@site:port" 
 *    APR_URI_UNP_OMITUSER            Just omit user 
 *    APR_URI_UNP_OMITPASSWORD        Just omit password 
 *    APR_URI_UNP_OMITUSERINFO        Omit "user:password\@" part
 *    APR_URI_UNP_REVEALPASSWORD      Show plain text password (default: show XXXXXXXX)
 *    APR_URI_UNP_OMITPATHINFO        Show "scheme://user\@site:port" only 
 *    APR_URI_UNP_OMITQUERY           Omit "?queryarg" or "#fragment" 
 * </PRE>
 * @return The uri as a string
 */
APU_DECLARE(char *) apr_uri_unparse(apr_pool_t *p, 
                                    const apr_uri_t *uptr,
                                    unsigned flags);

/**
 * Parse a given URI, fill in all supplied fields of a apr_uri_t
 * structure. This eliminates the necessity of extracting host, port,
 * path, query info repeatedly in the modules.
 * @param p The pool to allocate out of
 * @param uri The uri to parse
 * @param uptr The apr_uri_t to fill out
 * @return APR_SUCCESS for success or error code
 */
APU_DECLARE(apr_status_t) apr_uri_parse(apr_pool_t *p, const char *uri, 
                                        apr_uri_t *uptr);

/**
 * Special case for CONNECT parsing: it comes with the hostinfo part only
 * @param p The pool to allocate out of
 * @param hostinfo The hostinfo string to parse
 * @param uptr The apr_uri_t to fill out
 * @return APR_SUCCESS for success or error code
 */
APU_DECLARE(apr_status_t) apr_uri_parse_hostinfo(apr_pool_t *p, 
                                                 const char *hostinfo, 
                                                 apr_uri_t *uptr);

/** @} */
#ifdef __cplusplus
}
#endif

#endif /* APR_URI_H */
