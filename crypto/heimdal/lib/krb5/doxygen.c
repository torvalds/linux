/*
 * Copyright (c) 2007-2008 Kungliga Tekniska Högskolan
 * (Royal Institute of Technology, Stockholm, Sweden).
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "krb5_locl.h"

/**
 *
 */

/*! @mainpage Heimdal Kerberos 5 library
 *
 * @section intro Introduction
 *
 * Heimdal libkrb5 library is a implementation of the Kerberos
 * protocol.
 *
 * Kerberos is a system for authenticating users and services on a
 * network.  It is built upon the assumption that the network is
 * ``unsafe''.  For example, data sent over the network can be
 * eavesdropped and altered, and addresses can also be faked.
 * Therefore they cannot be used for authentication purposes.
 *
 *
 * - @ref krb5_introduction
 * - @ref krb5_principal_intro
 * - @ref krb5_ccache_intro
 * - @ref krb5_keytab_intro
 *
 * If you want to know more about the file formats that is used by
 * Heimdal, please see: @ref krb5_fileformats
 *
 * The project web page: http://www.h5l.org/
 *
 */

/** @defgroup krb5 Heimdal Kerberos 5 library */
/** @defgroup krb5_address Heimdal Kerberos 5 address functions */
/** @defgroup krb5_principal Heimdal Kerberos 5 principal functions */
/** @defgroup krb5_ccache Heimdal Kerberos 5 credential cache functions */
/** @defgroup krb5_crypto Heimdal Kerberos 5 cryptography functions */
/** @defgroup krb5_credential Heimdal Kerberos 5 credential handing functions */
/** @defgroup krb5_deprecated Heimdal Kerberos 5 deprecated functions */
/** @defgroup krb5_digest Heimdal Kerberos 5 digest service */
/** @defgroup krb5_error Heimdal Kerberos 5 error reporting functions */
/** @defgroup krb5_keytab Heimdal Kerberos 5 keytab handling functions */
/** @defgroup krb5_ticket Heimdal Kerberos 5 ticket functions */
/** @defgroup krb5_pac Heimdal Kerberos 5 PAC handling functions */
/** @defgroup krb5_v4compat Heimdal Kerberos 4 compatiblity functions */
/** @defgroup krb5_storage Heimdal Kerberos 5 storage functions */
/** @defgroup krb5_support Heimdal Kerberos 5 support functions */
/** @defgroup krb5_auth Heimdal Kerberos 5 authentication functions */


/**
 * @page krb5_introduction Introduction to the Kerberos 5 API
 * @section api_overview Kerberos 5 API Overview
 *
 * All functions are documented in manual pages.  This section tries
 * to give an overview of the major components used in Kerberos
 * library, and point to where to look for a specific function.
 *
 * @subsection intro_krb5_context Kerberos context
 *
 * A kerberos context (krb5_context) holds all per thread state. All
 * global variables that are context specific are stored in this
 * structure, including default encryption types, credential cache
 * (for example, a ticket file), and default realms.
 *
 * The internals of the structure should never be accessed directly,
 * functions exist for extracting information.
 *
 * See the manual page for krb5_init_context() how to create a context
 * and module @ref krb5 for more information about the functions.
 *
 * @subsection intro_krb5_auth_context Kerberos authentication context
 *
 * Kerberos authentication context (krb5_auth_context) holds all
 * context related to an authenticated connection, in a similar way to
 * the kerberos context that holds the context for the thread or
 * process.
 *
 * The krb5_auth_context is used by various functions that are
 * directly related to authentication between the
 * server/client. Example of data that this structure contains are
 * various flags, addresses of client and server, port numbers,
 * keyblocks (and subkeys), sequence numbers, replay cache, and
 * checksum types.
 *
 * @subsection intro_krb5_principal Kerberos principal
 *
 * The Kerberos principal is the structure that identifies a user or
 * service in Kerberos. The structure that holds the principal is the
 * krb5_principal. There are function to extract the realm and
 * elements of the principal, but most applications have no reason to
 * inspect the content of the structure.
 *
 * The are several ways to create a principal (with different degree of
 * portability), and one way to free it.
 *
 * See also the page @ref krb5_principal_intro for more information and also
 * module @ref krb5_principal.
 *
 * @subsection intro_krb5_ccache Credential cache
 *
 * A credential cache holds the tickets for a user. A given user can
 * have several credential caches, one for each realm where the user
 * have the initial tickets (the first krbtgt).
 *
 * The credential cache data can be stored internally in different
 * way, each of them for different proposes.  File credential (FILE)
 * caches and processes based (KCM) caches are for permanent
 * storage. While memory caches (MEMORY) are local caches to the local
 * process.
 *
 * Caches are opened with krb5_cc_resolve() or created with
 * krb5_cc_new_unique().
 *
 * If the cache needs to be opened again (using krb5_cc_resolve())
 * krb5_cc_close() will close the handle, but not the remove the
 * cache. krb5_cc_destroy() will zero out the cache, remove the cache
 * so it can no longer be referenced.
 *
 * See also @ref krb5_ccache_intro and @ref krb5_ccache .
 *
 * @subsection intro_krb5_error_code Kerberos errors
 *
 * Kerberos errors are based on the com_err library.  All error codes are
 * 32-bit signed numbers, the first 24 bits define what subsystem the
 * error originates from, and last 8 bits are 255 error codes within the
 * library.  Each error code have fixed string associated with it.  For
 * example, the error-code -1765328383 have the symbolic name
 * KRB5KDC_ERR_NAME_EXP, and associated error string ``Client's entry in
 * database has expired''.
 *
 * This is a great improvement compared to just getting one of the unix
 * error-codes back.  However, Heimdal have an extention to pass back
 * customised errors messages.  Instead of getting ``Key table entry not
 * found'', the user might back ``failed to find
 * host/host.example.com\@EXAMLE.COM(kvno 3) in keytab /etc/krb5.keytab
 * (des-cbc-crc)''.  This improves the chance that the user find the
 * cause of the error so you should use the customised error message
 * whenever it's available.
 *
 * See also module @ref krb5_error .
 *
 *
 * @subsection intro_krb5_keytab Keytab management
 *
 * A keytab is a storage for locally stored keys. Heimdal includes keytab
 * support for Kerberos 5 keytabs, Kerberos 4 srvtab, AFS-KeyFile's,
 * and for storing keys in memory.
 *
 * Keytabs are used for servers and long-running services.
 *
 * See also @ref krb5_keytab_intro and @ref krb5_keytab .
 *
 * @subsection intro_krb5_crypto Kerberos crypto
 *
 * Heimdal includes a implementation of the Kerberos crypto framework,
 * all crypto operations. To create a crypto context call krb5_crypto_init().
 *
 * See also module @ref krb5_crypto .
 *
 * @section kerberos5_client Walkthrough of a sample Kerberos 5 client
 *
 * This example contains parts of a sample TCP Kerberos 5 clients, if you
 * want a real working client, please look in appl/test directory in
 * the Heimdal distribution.
 *
 * All Kerberos error-codes that are returned from kerberos functions in
 * this program are passed to krb5_err, that will print a
 * descriptive text of the error code and exit. Graphical programs can
 * convert error-code to a human readable error-string with the
 * krb5_get_error_message() function.
 *
 * Note that you should not use any Kerberos function before
 * krb5_init_context() have completed successfully. That is the
 * reason err() is used when krb5_init_context() fails.
 *
 * First the client needs to call krb5_init_context to initialise
 * the Kerberos 5 library. This is only needed once per thread
 * in the program. If the function returns a non-zero value it indicates
 * that either the Kerberos implementation is failing or it's disabled on
 * this host.
 *
 * @code
 * #include <krb5.h>
 *
 * int
 * main(int argc, char **argv)
 * {
 *         krb5_context context;
 *
 *         if (krb5_init_context(&context))
 *                 errx (1, "krb5_context");
 * @endcode
 *
 * Now the client wants to connect to the host at the other end. The
 * preferred way of doing this is using getaddrinfo (for
 * operating system that have this function implemented), since getaddrinfo
 * is neutral to the address type and can use any protocol that is available.
 *
 * @code
 *         struct addrinfo *ai, *a;
 *         struct addrinfo hints;
 *         int error;
 *
 *         memset (&hints, 0, sizeof(hints));
 *         hints.ai_socktype = SOCK_STREAM;
 *         hints.ai_protocol = IPPROTO_TCP;
 *
 *         error = getaddrinfo (hostname, "pop3", &hints, &ai);
 *         if (error)
 *                 errx (1, "%s: %s", hostname, gai_strerror(error));
 *
 *         for (a = ai; a != NULL; a = a->ai_next) {
 *                 int s;
 *
 *                 s = socket (a->ai_family, a->ai_socktype, a->ai_protocol);
 *                 if (s < 0)
 *                         continue;
 *                 if (connect (s, a->ai_addr, a->ai_addrlen) < 0) {
 *                         warn ("connect(%s)", hostname);
 *                             close (s);
 *                             continue;
 *                 }
 *                 freeaddrinfo (ai);
 *                 ai = NULL;
 *         }
 *         if (ai) {
 *                     freeaddrinfo (ai);
 *                     errx ("failed to contact %s", hostname);
 *         }
 * @endcode
 *
 * Before authenticating, an authentication context needs to be
 * created. This context keeps all information for one (to be) authenticated
 * connection (see krb5_auth_context).
 *
 * @code
 *         status = krb5_auth_con_init (context, &auth_context);
 *         if (status)
 *                 krb5_err (context, 1, status, "krb5_auth_con_init");
 * @endcode
 *
 * For setting the address in the authentication there is a help function
 * krb5_auth_con_setaddrs_from_fd() that does everything that is needed
 * when given a connected file descriptor to the socket.
 *
 * @code
 *         status = krb5_auth_con_setaddrs_from_fd (context,
 *                                                  auth_context,
 *                                                  &sock);
 *         if (status)
 *                 krb5_err (context, 1, status,
 *                           "krb5_auth_con_setaddrs_from_fd");
 * @endcode
 *
 * The next step is to build a server principal for the service we want
 * to connect to. (See also krb5_sname_to_principal().)
 *
 * @code
 *         status = krb5_sname_to_principal (context,
 *                                           hostname,
 *                                           service,
 *                                           KRB5_NT_SRV_HST,
 *                                           &server);
 *         if (status)
 *                 krb5_err (context, 1, status, "krb5_sname_to_principal");
 * @endcode
 *
 * The client principal is not passed to krb5_sendauth()
 * function, this causes the krb5_sendauth() function to try to figure it
 * out itself.
 *
 * The server program is using the function krb5_recvauth() to
 * receive the Kerberos 5 authenticator.
 *
 * In this case, mutual authentication will be tried. That means that the server
 * will authenticate to the client. Using mutual authentication
 * is good since it enables the user to verify that they are talking to the
 * right server (a server that knows the key).
 *
 * If you are using a non-blocking socket you will need to do all work of
 * krb5_sendauth() yourself. Basically you need to send over the
 * authenticator from krb5_mk_req() and, in case of mutual
 * authentication, verifying the result from the server with
 * krb5_rd_rep().
 *
 * @code
 *         status = krb5_sendauth (context,
 *                                 &auth_context,
 *                                 &sock,
 *                                 VERSION,
 *                                 NULL,
 *                                 server,
 *                                 AP_OPTS_MUTUAL_REQUIRED,
 *                                 NULL,
 *                                 NULL,
 *                                 NULL,
 *                                 NULL,
 *                                 NULL,
 *                                 NULL);
 *         if (status)
 *                 krb5_err (context, 1, status, "krb5_sendauth");
 * @endcode
 *
 * Once authentication has been performed, it is time to send some
 * data. First we create a krb5_data structure, then we sign it with
 * krb5_mk_safe() using the auth_context that contains the
 * session-key that was exchanged in the
 * krb5_sendauth()/krb5_recvauth() authentication
 * sequence.
 *
 * @code
 *         data.data   = "hej";
 *         data.length = 3;
 *
 *         krb5_data_zero (&packet);
 *
 *         status = krb5_mk_safe (context,
 *                                auth_context,
 *                                &data,
 *                                &packet,
 *                                NULL);
 *         if (status)
 *                 krb5_err (context, 1, status, "krb5_mk_safe");
 * @endcode
 *
 * And send it over the network.
 *
 * @code
 *         len = packet.length;
 *         net_len = htonl(len);
 *
 *         if (krb5_net_write (context, &sock, &net_len, 4) != 4)
 *                 err (1, "krb5_net_write");
 *         if (krb5_net_write (context, &sock, packet.data, len) != len)
 *                 err (1, "krb5_net_write");
 * @endcode
 *
 * To send encrypted (and signed) data krb5_mk_priv() should be
 * used instead. krb5_mk_priv() works the same way as
 * krb5_mk_safe(), with the exception that it encrypts the data
 * in addition to signing it.
 *
 * @code
 *         data.data   = "hemligt";
 *         data.length = 7;
 *
 *         krb5_data_free (&packet);
 *
 *         status = krb5_mk_priv (context,
 *                                auth_context,
 *                                &data,
 *                                &packet,
 *                                NULL);
 *         if (status)
 *                 krb5_err (context, 1, status, "krb5_mk_priv");
 * @endcode
 *
 * And send it over the network.
 *
 * @code
 *         len = packet.length;
 *         net_len = htonl(len);
 *
 *         if (krb5_net_write (context, &sock, &net_len, 4) != 4)
 *                 err (1, "krb5_net_write");
 *         if (krb5_net_write (context, &sock, packet.data, len) != len)
 *                 err (1, "krb5_net_write");
 *
 * @endcode
 *
 * The server is using krb5_rd_safe() and
 * krb5_rd_priv() to verify the signature and decrypt the packet.
 *
 * @section intro_krb5_verify_user Validating a password in an application
 *
 * See the manual page for krb5_verify_user().
 *
 * @section mit_differences API differences to MIT Kerberos
 *
 * This section is somewhat disorganised, but so far there is no overall
 * structure to the differences, though some of the have their root in
 * that Heimdal uses an ASN.1 compiler and MIT doesn't.
 *
 * @subsection mit_krb5_principal Principal and realms
 *
 * Heimdal stores the realm as a krb5_realm, that is a char *.
 * MIT Kerberos uses a krb5_data to store a realm.
 *
 * In Heimdal krb5_principal doesn't contain the component
 * name_type; it's instead stored in component
 * name.name_type. To get and set the nametype in Heimdal, use
 * krb5_principal_get_type() and
 * krb5_principal_set_type().
 *
 * For more information about principal and realms, see
 * krb5_principal.
 *
 * @subsection mit_krb5_error_code Error messages
 *
 * To get the error string, Heimdal uses
 * krb5_get_error_message(). This is to return custom error messages
 * (like ``Can't find host/datan.example.com\@CODE.COM in
 * /etc/krb5.conf.'' instead of a ``Key table entry not found'' that
 * error_message returns.
 *
 * Heimdal uses a threadsafe(r) version of the com_err interface; the
 * global com_err table isn't initialised.  Then
 * error_message returns quite a boring error string (just
 * the error code itself).
 *
 *
 */

/**
 *
 *
 * @page krb5_fileformats File formats
 *
 * @section fileformats File formats
 *
 * This section documents the diffrent file formats that are used in
 * Heimdal and other Kerberos implementations.
 *
 * @subsection file_keytab keytab
 *
 * The keytab binary format is not a standard format. The format has
 * evolved and may continue to. It is however understood by several
 * Kerberos implementations including Heimdal, MIT, Sun's Java ktab and
 * are created by the ktpass.exe utility from Windows. So it has
 * established itself as the defacto format for storing Kerberos keys.
 *
 * The following C-like structure definitions illustrate the MIT keytab
 * file format. All values are in network byte order. All text is ASCII.
 *
 * @code
 *   keytab {
 *       uint16_t file_format_version;                    # 0x502
 *       keytab_entry entries[*];
 *   };
 *
 *   keytab_entry {
 *       int32_t size;
 *       uint16_t num_components;   # subtract 1 if version 0x501
 *       counted_octet_string realm;
 *       counted_octet_string components[num_components];
 *       uint32_t name_type;       # not present if version 0x501
 *       uint32_t timestamp;
 *       uint8_t vno8;
 *       keyblock key;
 *       uint32_t vno; #only present if >= 4 bytes left in entry
 *       uint32_t flags; #only present if >= 4 bytes left in entry
 *   };
 *
 *   counted_octet_string {
 *       uint16_t length;
 *       uint8_t data[length];
 *   };
 *
 *   keyblock {
 *       uint16_t type;
 *       counted_octet_string;
 *   };
 * @endcode
 *
 * All numbers are stored in network byteorder (big endian) format.
 *
 * The keytab file format begins with the 16 bit file_format_version which
 * at the time this document was authored is 0x502. The format of older
 * keytabs is described at the end of this document.
 *
 * The file_format_version is immediately followed by an array of
 * keytab_entry structures which are prefixed with a 32 bit size indicating
 * the number of bytes that follow in the entry. Note that the size should be
 * evaluated as signed. This is because a negative value indicates that the
 * entry is in fact empty (e.g. it has been deleted) and that the negative
 * value of that negative value (which is of course a positive value) is
 * the offset to the next keytab_entry. Based on these size values alone
 * the entire keytab file can be traversed.
 *
 * The size is followed by a 16 bit num_components field indicating the
 * number of counted_octet_string components in the components array.
 *
 * The num_components field is followed by a counted_octet_string
 * representing the realm of the principal.
 *
 * A counted_octet_string is simply an array of bytes prefixed with a 16
 * bit length. For the realm and name components, the counted_octet_string
 * bytes are ASCII encoded text with no zero terminator.
 *
 * Following the realm is the components array that represents the name of
 * the principal. The text of these components may be joined with slashs
 * to construct the typical SPN representation. For example, the service
 * principal HTTP/www.foo.net\@FOO.NET would consist of name components
 * "HTTP" followed by "www.foo.net".
 *
 * Following the components array is the 32 bit name_type (e.g. 1 is
 * KRB5_NT_PRINCIPAL, 2 is KRB5_NT_SRV_INST, 5 is KRB5_NT_UID, etc). In
 * practice the name_type is almost certainly 1 meaning KRB5_NT_PRINCIPAL.
 *
 * The 32 bit timestamp indicates the time the key was established for that
 * principal. The value represents the number of seconds since Jan 1, 1970.
 *
 * The 8 bit vno8 field is the version number of the key. This value is
 * overridden by the 32 bit vno field if it is present. The vno8 field is
 * filled with the lower 8 bits of the 32 bit protocol kvno field.
 *
 * The keyblock structure consists of a 16 bit value indicating the
 * encryption type and is a counted_octet_string containing the key.  The
 * encryption type is the same as the Kerberos standard (e.g. 3 is
 * des-cbc-md5, 23 is arcfour-hmac-md5, etc).
 *
 * The last field of the keytab_entry structure is optional. If the size of
 * the keytab_entry indicates that there are at least 4 bytes remaining,
 * a 32 bit value representing the key version number is present. This
 * value supersedes the 8 bit vno8 value preceeding the keyblock.
 *
 * Older keytabs with a file_format_version of 0x501 are different in
 * three ways:
 *
 * - All integers are in host byte order [1].
 * - The num_components field is 1 too large (i.e. after decoding, decrement by 1).
 * - The 32 bit name_type field is not present.
 *
 * [1] The file_format_version field should really be treated as two
 * separate 8 bit quantities representing the major and minor version
 * number respectively.
 *
 * @subsection file_hdb_dump Heimdal database dump file
 *
 * Format of the Heimdal text dump file as of Heimdal 0.6.3:
 *
 * Each line in the dump file is one entry in the database.
 *
 * Each field of a line is separated by one or more spaces, with the
 * exception of fields consisting of principals containing spaces, where
 * space can be quoted with \ and \ is quoted by \.
 *
 * Fields and their types are:
 *
 * @code
 * 	Quoted princial (quote character is \) [string]
 * 	Keys [keys]
 * 	Created by [event]
 * 	Modified by [event optional]
 * 	Valid start time [time optional]
 * 	Valid end time [time optional]
 * 	Password end valid time [time optional]
 * 	Max lifetime of ticket [time optional]
 * 	Max renew time of ticket [integer optional]
 * 	Flags [hdb flags]
 * 	Generation number [generation optional]
 * 	Extensions [extentions optional]
 * @endcode
 *
 * Fields following these silently are ignored.
 *
 * All optional fields will be skipped if they fail to parse (or comprise
 * the optional field marker of "-", w/o quotes).
 *
 * Example:
 *
 * @code
 * fred\@CODE.COM 27:1:16:e8b4c8fc7e60b9e641dcf4cff3f08a701d982a2f89ba373733d26ca59ba6c789666f6b8bfcf169412bb1e5dceb9b33cda29f3412:-:1:3:4498a933881178c744f4232172dcd774c64e81fa6d05ecdf643a7e390624a0ebf3c7407a:-:1:2:b01934b13eb795d76f3a80717d469639b4da0cfb644161340ef44fdeb375e54d684dbb85:-:1:1:ea8e16d8078bf60c781da90f508d4deccba70595258b9d31888d33987cd31af0c9cced2e:- 20020415130120:admin\@CODE.COM 20041221112428:fred\@CODE.COM - - - 86400 604800 126 20020415130120:793707:28 -
 * @endcode
 *
 * Encoding of types are as follows:
 *
 * - keys
 *
 * @code
 * kvno:[masterkvno:keytype:keydata:salt]{zero or more separated by :}
 * @endcode
 *
 * kvno is the key version number.
 *
 * keydata is hex-encoded
 *
 * masterkvno is the kvno of the database master key.  If this field is
 * empty, the kadmin load and merge operations will encrypt the key data
 * with the master key if there is one.  Otherwise the key data will be
 * imported asis.
 *
 * salt is encoded as "-" (no/default salt) or
 *
 * @code
 * salt-type /
 * salt-type / "string"
 * salt-type / hex-encoded-data
 * @endcode
 *
 * keytype is the protocol enctype number; see enum ENCTYPE in
 * include/krb5_asn1.h for values.
 *
 * Example:
 * @code
 * 27:1:16:e8b4c8fc7e60b9e641dcf4cff3f08a701d982a2f89ba373733d26ca59ba6c789666f6b8bfcf169412bb1e5dceb9b33cda29f3412:-:1:3:4498a933881178c744f4232172dcd774c64e81fa6d05ecdf643a7e390624a0ebf3c7407a:-:1:2:b01934b13eb795d76f3a80717d469639b4da0cfb644161340ef44fdeb375e54d684dbb85:-:1:1:ea8e16d8078bf60c781da90f508d4deccba70595258b9d31888d33987cd31af0c9cced2e:-
 * @endcode
 *
 *
 * @code
 * kvno=27,{key: masterkvno=1,keytype=des3-cbc-sha1,keydata=..., default salt}...
 * @endcode
 *
 * - time
 *
 * Format of the time is: YYYYmmddHHMMSS, corresponding to strftime
 * format "%Y%m%d%k%M%S".
 *
 * Time is expressed in UTC.
 *
 * Time can be optional (using -), when the time 0 is used.
 *
 * Example:
 *
 * @code
 * 20041221112428
 * @endcode
 *
 * - event
 *
 * @code
 * 	time:principal
 * @endcode
 *
 * time is as given in format time
 *
 * principal is a string.  Not quoting it may not work in earlier
 * versions of Heimdal.
 *
 * Example:
 * @code
 * 20041221112428:bloggs\@CODE.COM
 * @endcode
 *
 * - hdb flags
 *
 * Integer encoding of HDB flags, see HDBFlags in lib/hdb/hdb.asn1. Each
 * bit in the integer is the same as the bit in the specification.
 *
 * - generation:
 *
 * @code
 * time:usec:gen
 * @endcode
 *
 *
 * usec is a the microsecond, integer.
 * gen is generation number, integer.
 *
 * The generation can be defaulted (using '-') or the empty string
 *
 * - extensions:
 *
 * @code
 * first-hex-encoded-HDB-Extension[:second-...]
 * @endcode
 *
 * HDB-extension is encoded the DER encoded HDB-Extension from
 * lib/hdb/hdb.asn1. Consumers HDB extensions should be aware that
 * unknown entires needs to be preserved even thought the ASN.1 data
 * content might be unknown. There is a critical flag in the data to show
 * to the KDC that the entry MUST be understod if the entry is to be
 * used.
 *
 *
 */
