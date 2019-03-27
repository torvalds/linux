/*
 * Copyright (c) 2016 Thomas Pornin <pornin@bolet.org>
 *
 * Permission is hereby granted, free of charge, to any person obtaining 
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be 
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, 
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND 
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef BRSSL_H__
#define BRSSL_H__

#ifndef _STANDALONE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#elif !defined(STAND_H) 
#include <stand.h> 
#endif 

#include "bearssl.h"

/*
 * malloc() wrapper:
 * -- If len is 0, then NULL is returned.
 * -- If len is non-zero, and allocation fails, then an error message is
 *    printed and the process exits with an error code.
 */
void *xmalloc(size_t len);

/*
 * free() wrapper, meant to release blocks allocated with xmalloc().
 */
void xfree(void *buf);

/*
 * Duplicate a character string into a newly allocated block.
 */
char *xstrdup(const void *src);

/*
 * Allocate a new block with the provided length, filled with a copy
 * of exactly that many bytes starting at address 'src'.
 */
void *xblobdup(const void *src, size_t len);

/*
 * Duplicate a public key, into newly allocated blocks. The returned
 * key must be later on released with xfreepkey().
 */
br_x509_pkey *xpkeydup(const br_x509_pkey *pk);

/*
 * Release a public key that was allocated with xpkeydup(). If pk is NULL,
 * this function does nothing.
 */
void xfreepkey(br_x509_pkey *pk);

/*
 * Macros for growable arrays.
 */

/*
 * Make a structure type for a vector of 'type'.
 */
#define VECTOR(type)   struct { \
		type *buf; \
		size_t ptr, len; \
	}

/*
 * Constant initialiser for a vector.
 */
#define VEC_INIT   { 0, 0, 0 }

/*
 * Clear a vector.
 */
#define VEC_CLEAR(vec)   do { \
		xfree((vec).buf); \
		(vec).buf = NULL; \
		(vec).ptr = 0; \
		(vec).len = 0; \
	} while (0)

/*
 * Clear a vector, first calling the provided function on each vector
 * element.
 */
#define VEC_CLEAREXT(vec, fun)   do { \
		size_t vec_tmp; \
		for (vec_tmp = 0; vec_tmp < (vec).ptr; vec_tmp ++) { \
			(fun)(&(vec).buf[vec_tmp]); \
		} \
		VEC_CLEAR(vec); \
	} while (0)

/*
 * Add a value at the end of a vector.
 */
#define VEC_ADD(vec, x)   do { \
		(vec).buf = vector_expand((vec).buf, sizeof *((vec).buf), \
			&(vec).ptr, &(vec).len, 1); \
		(vec).buf[(vec).ptr ++] = (x); \
	} while (0)

/*
 * Add several values at the end of a vector.
 */
#define VEC_ADDMANY(vec, xp, num)   do { \
		size_t vec_num = (num); \
		(vec).buf = vector_expand((vec).buf, sizeof *((vec).buf), \
			&(vec).ptr, &(vec).len, vec_num); \
		memcpy((vec).buf + (vec).ptr, \
			(xp), vec_num * sizeof *((vec).buf)); \
		(vec).ptr += vec_num; \
	} while (0)

/*
 * Access a vector element by index. This is a lvalue, and can be modified.
 */
#define VEC_ELT(vec, idx)   ((vec).buf[idx])

/*
 * Get current vector length.
 */
#define VEC_LEN(vec)   ((vec).ptr)

/*
 * Copy all vector elements into a newly allocated block.
 */
#define VEC_TOARRAY(vec)    xblobdup((vec).buf, sizeof *((vec).buf) * (vec).ptr)

/*
 * Internal function used to handle memory allocations for vectors.
 */
void *vector_expand(void *buf,
	size_t esize, size_t *ptr, size_t *len, size_t extra);

/*
 * Type for a vector of bytes.
 */
typedef VECTOR(unsigned char) bvector;

/*
 * Compare two strings for equality; returned value is 1 if the strings
 * are to be considered equal, 0 otherwise. Comparison is case-insensitive
 * (ASCII letters only) and skips some characters (all whitespace, defined
 * as ASCII codes 0 to 32 inclusive, and also '-', '_', '.', '/', '+' and
 * ':').
 */
int eqstr(const char *s1, const char *s2);

/*
 * Convert a string to a positive integer (size_t). Returned value is
 * (size_t)-1 on error. On error, an explicit error message is printed.
 */
size_t parse_size(const char *s);

/*
 * Structure for a known protocol version.
 */
typedef struct {
	const char *name;
	unsigned version;
	const char *comment;
} protocol_version;

/*
 * Known protocol versions. Last element has a NULL name.
 */
extern const protocol_version protocol_versions[];

/*
 * Parse a version name. If the name is not recognized, then an error
 * message is printed, and 0 is returned.
 */
unsigned parse_version(const char *name, size_t len);

/*
 * Type for a known hash function.
 */
typedef struct {
	const char *name;
	const br_hash_class *hclass;
	const char *comment;
} hash_function;

/*
 * Known hash functions. Last element has a NULL name.
 */
extern const hash_function hash_functions[];

/*
 * Parse hash function names. This function expects a comma-separated
 * list of names, and returns a bit mask corresponding to the matched
 * names. If one of the name does not match, or the list is empty, then
 * an error message is printed, and 0 is returned.
 */
unsigned parse_hash_functions(const char *arg);

/*
 * Get a curve name (by ID). If the curve ID is not known, this returns
 * NULL.
 */
const char *get_curve_name(int id);

/*
 * Get a curve name (by ID). The name is written in the provided buffer
 * (zero-terminated). If the curve ID is not known, the name is
 * "unknown (***)" where "***" is the decimal value of the identifier.
 * If the name does not fit in the provided buffer, then dst[0] is set
 * to 0 (unless len is 0, in which case nothing is written), and -1 is
 * returned. Otherwise, the name is written in dst[] (with a terminating
 * 0), and this function returns 0.
 */
int get_curve_name_ext(int id, char *dst, size_t len);

/*
 * Type for a known cipher suite.
 */
typedef struct {
	const char *name;
	uint16_t suite;
	unsigned req;
	const char *comment;
} cipher_suite;

/*
 * Known cipher suites. Last element has a NULL name.
 */
extern const cipher_suite cipher_suites[];

/*
 * Flags for cipher suite requirements.
 */
#define REQ_TLS12          0x0001   /* suite needs TLS 1.2 */
#define REQ_SHA1           0x0002   /* suite needs SHA-1 */
#define REQ_SHA256         0x0004   /* suite needs SHA-256 */
#define REQ_SHA384         0x0008   /* suite needs SHA-384 */
#define REQ_AESCBC         0x0010   /* suite needs AES/CBC encryption */
#define REQ_AESGCM         0x0020   /* suite needs AES/GCM encryption */
#define REQ_AESCCM         0x0040   /* suite needs AES/CCM encryption */
#define REQ_CHAPOL         0x0080   /* suite needs ChaCha20+Poly1305 */
#define REQ_3DESCBC        0x0100   /* suite needs 3DES/CBC encryption */
#define REQ_RSAKEYX        0x0200   /* suite uses RSA key exchange */
#define REQ_ECDHE_RSA      0x0400   /* suite uses ECDHE_RSA key exchange */
#define REQ_ECDHE_ECDSA    0x0800   /* suite uses ECDHE_ECDSA key exchange */
#define REQ_ECDH           0x1000   /* suite uses static ECDH key exchange */

/*
 * Parse a list of cipher suite names. The names are comma-separated. If
 * one of the name is not recognised, or the list is empty, then an
 * appropriate error message is printed, and NULL is returned.
 * The returned array is allocated with xmalloc() and must be released
 * by the caller. That array is terminated with a dummy entry whose 'name'
 * field is NULL. The number of entries (not counting the dummy entry)
 * is also written into '*num'.
 */
cipher_suite *parse_suites(const char *arg, size_t *num);

/*
 * Get the name of a cipher suite. Returned value is NULL if the suite is
 * not recognized.
 */
const char *get_suite_name(unsigned suite);

/*
 * Get the name of a cipher suite. The name is written in the provided
 * buffer; if the suite is not recognised, then the name is
 * "unknown (0x****)" where "****" is the hexadecimal value of the suite.
 * If the name does not fit in the provided buffer, then dst[0] is set
 * to 0 (unless len is 0, in which case nothing is written), and -1 is
 * returned. Otherwise, the name is written in dst[] (with a terminating
 * 0), and this function returns 0.
 */
int get_suite_name_ext(unsigned suite, char *dst, size_t len);

/*
 * Tell whether a cipher suite uses ECDHE key exchange.
 */
int uses_ecdhe(unsigned suite);

/*
 * Print out all known names (for protocol versions, cipher suites...).
 */
void list_names(void);

/*
 * Print out all known elliptic curve names.
 */
void list_curves(void);

/*
 * Get the symbolic name for an elliptic curve (by ID).
 */
const char *ec_curve_name(int curve);

/*
 * Get a curve by symbolic name. If the name is not recognized, -1 is
 * returned.
 */
int get_curve_by_name(const char *str);

/*
 * Get the symbolic name for a hash function name (by ID).
 */
const char *hash_function_name(int id);

/*
 * Read a file completely. The returned block is allocated with xmalloc()
 * and must be released by the caller.
 * If the file cannot be found or read completely, or is empty, then an
 * appropriate error message is written, and NULL is returned.
 */
unsigned char *read_file(const char *fname, size_t *len);

/*
 * Write a file completely. This returns 0 on success, -1 on error. On
 * error, an appropriate error message is printed.
 */
int write_file(const char *fname, const void *data, size_t len);

/*
 * This function returns non-zero if the provided buffer "looks like"
 * a DER-encoded ASN.1 object (criteria: it has the tag for a SEQUENCE
 * with a definite length that matches the total object length).
 */
int looks_like_DER(const unsigned char *buf, size_t len);

/*
 * Type for a named blob (the 'name' is a normalised PEM header name).
 */
typedef struct {
	char *name;
	unsigned char *data;
	size_t data_len;
} pem_object;

/*
 * Release the contents of a named blob (buffer and name).
 */
void free_pem_object_contents(pem_object *po);

/*
 * Decode a buffer as a PEM file, and return all objects. On error, NULL
 * is returned and an error message is printed. Absence of any object
 * is an error.
 *
 * The returned array is terminated by a dummy object whose 'name' is
 * NULL. The number of objects (not counting the dummy terminator) is
 * written in '*num'.
 */
pem_object *decode_pem(const void *src, size_t len, size_t *num);

/*
 * Get the certificate(s) from a file. This accepts both a single
 * DER-encoded certificate, and a text file that contains
 * PEM-encoded certificates (and possibly other objects, which are
 * then ignored).
 *
 * On decoding error, or if the file turns out to contain no certificate
 * at all, then an error message is printed and NULL is returned.
 *
 * The returned array, and all referenced buffers, are allocated with
 * xmalloc() and must be released by the caller. The returned array
 * ends with a dummy entry whose 'data' field is NULL.
 * The number of decoded certificates (not counting the dummy entry)
 * is written into '*num'.
 */
br_x509_certificate *read_certificates(const char *fname, size_t *num);

/*
 * Release certificates. This releases all certificate data arrays,
 * and the whole array as well.
 */
void free_certificates(br_x509_certificate *certs, size_t num);

/*
 * Interpret a certificate as a trust anchor. The trust anchor is
 * newly allocated with xmalloc() and the caller must release it.
 * On decoding error, an error message is printed, and this function
 * returns NULL.
 */
br_x509_trust_anchor *certificate_to_trust_anchor(br_x509_certificate *xc);

/*
 * Type for a vector of trust anchors.
 */
typedef VECTOR(br_x509_trust_anchor) anchor_list;

/*
 * Release contents for a trust anchor (assuming they were dynamically
 * allocated with xmalloc()). The structure itself is NOT released.
 */
void free_ta_contents(br_x509_trust_anchor *ta);

/*
 * Decode certificates from a file and interpret them as trust anchors.
 * The trust anchors are added to the provided list. The number of found
 * anchors is returned; on error, 0 is returned (finding no anchor at
 * all is considered an error). An appropriate error message is displayed.
 */
size_t read_trust_anchors(anchor_list *dst, const char *fname);

/*
 * Get the "signer key type" for the certificate (key type of the
 * issuing CA). On error, this prints a message on stderr, and returns 0.
 */
int get_cert_signer_algo(br_x509_certificate *xc);

/*
 * Special "no anchor" X.509 validator that wraps around another X.509
 * validator and turns "not trusted" error codes into success. This is
 * by definition insecure, but convenient for debug purposes.
 */
typedef struct {
	const br_x509_class *vtable;
	const br_x509_class **inner;
} x509_noanchor_context;
extern const br_x509_class x509_noanchor_vtable;

/*
 * Initialise a "no anchor" X.509 validator.
 */
void x509_noanchor_init(x509_noanchor_context *xwc,
	const br_x509_class **inner);

/*
 * Aggregate type for a private key.
 */
typedef struct {
	int key_type;  /* BR_KEYTYPE_RSA or BR_KEYTYPE_EC */
	union {
		br_rsa_private_key rsa;
		br_ec_private_key ec;
	} key;
} private_key;

/*
 * Decode a private key from a file. On error, this prints an error
 * message and returns NULL.
 */
private_key *read_private_key(const char *fname);

/*
 * Free a private key.
 */
void free_private_key(private_key *sk);

/*
 * Get the encoded OID for a given hash function (to use with PKCS#1
 * signatures). If the hash function ID is 0 (for MD5+SHA-1), or if
 * the ID is not one of the SHA-* functions (SHA-1, SHA-224, SHA-256,
 * SHA-384, SHA-512), then this function returns NULL.
 */
const unsigned char *get_hash_oid(int id);

/*
 * Get a hash implementation by ID. This returns NULL if the hash
 * implementation is not available.
 */
const br_hash_class *get_hash_impl(int id);

/*
 * Find the symbolic name and the description for an error. If 'err' is
 * recognised then the error symbolic name is returned; if 'comment' is
 * not NULL then '*comment' is then set to a descriptive human-readable
 * message. If the error code 'err' is not recognised, then '*comment' is
 * untouched and this function returns NULL.
 */
const char *find_error_name(int err, const char **comment);

/*
 * Find the symbolic name for an algorithm implementation. Provided
 * pointer should be a pointer to a vtable or to a function, where
 * appropriate. If not recognised, then the string "UNKNOWN" is returned.
 *
 * If 'long_name' is non-zero, then the returned name recalls the
 * algorithm type as well; otherwise, only the core implementation name
 * is returned (e.g. the long name could be 'aes_big_cbcenc' while the
 * short name is 'big').
 */
const char *get_algo_name(const void *algo, int long_name);

/*
 * Run a SSL engine, with a socket connected to the peer, and using
 * stdin/stdout to exchange application data. The socket must be a
 * non-blocking descriptor.
 *
 * To help with Win32 compatibility, the socket descriptor is provided
 * as an "unsigned long" value.
 *
 * Returned value:
 *    0        SSL connection closed successfully
 *    x > 0    SSL error "x"
 *   -1        early socket close
 *   -2        stdout was closed, or something failed badly
 */
int run_ssl_engine(br_ssl_engine_context *eng,
	unsigned long fd, unsigned flags);

#define RUN_ENGINE_VERBOSE     0x0001  /* enable verbose messages */
#define RUN_ENGINE_TRACE       0x0002  /* hex dump of records */

/*
 * Do the "client" command. Returned value is 0 on success, -1 on failure.
 * Command-line arguments start _after_ the command name.
 */
int do_client(int argc, char *argv[]);

/*
 * Do the "server" command. Returned value is 0 on success, -1 on failure.
 * Command-line arguments start _after_ the command name.
 */
int do_server(int argc, char *argv[]);

/*
 * Do the "verify" command. Returned value is 0 on success, -1 on failure.
 * Command-line arguments start _after_ the command name.
 */
int do_verify(int argc, char *argv[]);

/*
 * Do the "skey" command. Returned value is 0 on success, -1 on failure.
 * Command-line arguments start _after_ the command name.
 */
int do_skey(int argc, char *argv[]);

/*
 * Do the "ta" command. Returned value is 0 on success, -1 on failure.
 * Command-line arguments start _after_ the command name.
 */
int do_ta(int argc, char *argv[]);

/*
 * Do the "chain" command. Returned value is 0 on success, -1 on failure.
 * Command-line arguments start _after_ the command name.
 */
int do_chain(int argc, char *argv[]);

/*
 * Do the "twrch" command. Returned value is 0 on success, -1 on failure
 * (processing or arguments), or a non-zero exit code. Command-line
 * arguments start _after_ the command name.
 */
int do_twrch(int argc, char *argv[]);

/*
 * Do the "impl" command. Returned value is 0 on success, -1 on failure.
 * Command-line arguments start _after_ the command name.
 */
int do_impl(int argc, char *argv[]);

#endif
