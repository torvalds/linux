/* Copyright (c) 2013, Vsevolod Stakhov
 * Copyright (c) 2015 Allan Jude <allanjude@freebsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *       * Redistributions of source code must retain the above copyright
 *         notice, this list of conditions and the following disclaimer.
 *       * Redistributions in binary form must reproduce the above copyright
 *         notice, this list of conditions and the following disclaimer in the
 *         documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED ''AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "ucl.h"
#include "ucl_internal.h"
#include "ucl_chartable.h"
#include "kvec.h"
#include <limits.h>
#include <stdarg.h>
#include <stdio.h> /* for snprintf */

#ifndef _WIN32
#include <glob.h>
#include <sys/param.h>
#else
#ifndef NBBY
#define NBBY 8
#endif
#endif

#ifdef HAVE_LIBGEN_H
#include <libgen.h> /* For dirname */
#endif

typedef kvec_t(ucl_object_t *) ucl_array_t;

#define UCL_ARRAY_GET(ar, obj) ucl_array_t *ar = \
	(ucl_array_t *)((obj) != NULL ? (obj)->value.av : NULL)

#ifdef HAVE_OPENSSL
#include <openssl/err.h>
#include <openssl/sha.h>
#include <openssl/rsa.h>
#include <openssl/ssl.h>
#include <openssl/evp.h>
#endif

#ifdef CURL_FOUND
/* Seems to be broken */
#define CURL_DISABLE_TYPECHECK 1
#include <curl/curl.h>
#endif
#ifdef HAVE_FETCH_H
#include <fetch.h>
#endif

#ifdef _WIN32
#include <windows.h>

#ifndef PROT_READ
#define PROT_READ       1
#endif
#ifndef PROT_WRITE
#define PROT_WRITE      2
#endif
#ifndef PROT_READWRITE
#define PROT_READWRITE  3
#endif
#ifndef MAP_SHARED
#define MAP_SHARED      1
#endif
#ifndef MAP_PRIVATE
#define MAP_PRIVATE     2
#endif
#ifndef MAP_FAILED
#define MAP_FAILED      ((void *) -1)
#endif

static void *ucl_mmap(char *addr, size_t length, int prot, int access, int fd, off_t offset)
{
	void *map = NULL;
	HANDLE handle = INVALID_HANDLE_VALUE;

	switch (prot) {
	default:
	case PROT_READ:
		{
			handle = CreateFileMapping((HANDLE) _get_osfhandle(fd), 0, PAGE_READONLY, 0, length, 0);
			if (!handle) break;
			map = (void *) MapViewOfFile(handle, FILE_MAP_READ, 0, 0, length);
			CloseHandle(handle);
			break;
		}
	case PROT_WRITE:
		{
			handle = CreateFileMapping((HANDLE) _get_osfhandle(fd), 0, PAGE_READWRITE, 0, length, 0);
			if (!handle) break;
			map = (void *) MapViewOfFile(handle, FILE_MAP_WRITE, 0, 0, length);
			CloseHandle(handle);
			break;
		}
	case PROT_READWRITE:
		{
			handle = CreateFileMapping((HANDLE) _get_osfhandle(fd), 0, PAGE_READWRITE, 0, length, 0);
			if (!handle) break;
			map = (void *) MapViewOfFile(handle, FILE_MAP_ALL_ACCESS, 0, 0, length);
			CloseHandle(handle);
			break;
		}
	}
	if (map == (void *) NULL) {
		return (void *) MAP_FAILED;
	}
	return (void *) ((char *) map + offset);
}

static int ucl_munmap(void *map,size_t length)
{
	if (!UnmapViewOfFile(map)) {
		return(-1);
	}
	return(0);
}

static char* ucl_realpath(const char *path, char *resolved_path) {
    char *p;
    char tmp[MAX_PATH + 1];
    strncpy(tmp, path, sizeof(tmp)-1);
    p = tmp;
    while(*p) {
        if (*p == '/') *p = '\\';
        p++;
    }
    return _fullpath(resolved_path, tmp, MAX_PATH);
}
#else
#define ucl_mmap mmap
#define ucl_munmap munmap
#define ucl_realpath realpath
#endif

typedef void (*ucl_object_dtor) (ucl_object_t *obj);
static void ucl_object_free_internal (ucl_object_t *obj, bool allow_rec,
		ucl_object_dtor dtor);
static void ucl_object_dtor_unref (ucl_object_t *obj);

static void
ucl_object_dtor_free (ucl_object_t *obj)
{
	if (obj->trash_stack[UCL_TRASH_KEY] != NULL) {
		UCL_FREE (obj->hh.keylen, obj->trash_stack[UCL_TRASH_KEY]);
	}
	if (obj->trash_stack[UCL_TRASH_VALUE] != NULL) {
		UCL_FREE (obj->len, obj->trash_stack[UCL_TRASH_VALUE]);
	}
	/* Do not free ephemeral objects */
	if ((obj->flags & UCL_OBJECT_EPHEMERAL) == 0) {
		if (obj->type != UCL_USERDATA) {
			UCL_FREE (sizeof (ucl_object_t), obj);
		}
		else {
			struct ucl_object_userdata *ud = (struct ucl_object_userdata *)obj;
			if (ud->dtor) {
				ud->dtor (obj->value.ud);
			}
			UCL_FREE (sizeof (*ud), obj);
		}
	}
}

/*
 * This is a helper function that performs exactly the same as
 * `ucl_object_unref` but it doesn't iterate over elements allowing
 * to use it for individual elements of arrays and multiple values
 */
static void
ucl_object_dtor_unref_single (ucl_object_t *obj)
{
	if (obj != NULL) {
#ifdef HAVE_ATOMIC_BUILTINS
		unsigned int rc = __sync_sub_and_fetch (&obj->ref, 1);
		if (rc == 0) {
#else
		if (--obj->ref == 0) {
#endif
			ucl_object_free_internal (obj, false, ucl_object_dtor_unref);
		}
	}
}

static void
ucl_object_dtor_unref (ucl_object_t *obj)
{
	if (obj->ref == 0) {
		ucl_object_dtor_free (obj);
	}
	else {
		/* This may cause dtor unref being called one more time */
		ucl_object_dtor_unref_single (obj);
	}
}

static void
ucl_object_free_internal (ucl_object_t *obj, bool allow_rec, ucl_object_dtor dtor)
{
	ucl_object_t *tmp, *sub;

	while (obj != NULL) {
		if (obj->type == UCL_ARRAY) {
			UCL_ARRAY_GET (vec, obj);
			unsigned int i;

			if (vec != NULL) {
				for (i = 0; i < vec->n; i ++) {
					sub = kv_A (*vec, i);
					if (sub != NULL) {
						tmp = sub;
						while (sub) {
							tmp = sub->next;
							dtor (sub);
							sub = tmp;
						}
					}
				}
				kv_destroy (*vec);
				UCL_FREE (sizeof (*vec), vec);
			}
			obj->value.av = NULL;
		}
		else if (obj->type == UCL_OBJECT) {
			if (obj->value.ov != NULL) {
				ucl_hash_destroy (obj->value.ov, (ucl_hash_free_func)dtor);
			}
			obj->value.ov = NULL;
		}
		tmp = obj->next;
		dtor (obj);
		obj = tmp;

		if (!allow_rec) {
			break;
		}
	}
}

void
ucl_object_free (ucl_object_t *obj)
{
	ucl_object_free_internal (obj, true, ucl_object_dtor_free);
}

size_t
ucl_unescape_json_string (char *str, size_t len)
{
	char *t = str, *h = str;
	int i, uval;

	if (len <= 1) {
		return len;
	}
	/* t is target (tortoise), h is source (hare) */

	while (len) {
		if (*h == '\\') {
			h ++;

			if (len == 1) {
				/*
				 * If \ is last, then do not try to go further
				 * Issue: #74
				 */
				len --;
				*t++ = '\\';
				continue;
			}

			switch (*h) {
			case 'n':
				*t++ = '\n';
				break;
			case 'r':
				*t++ = '\r';
				break;
			case 'b':
				*t++ = '\b';
				break;
			case 't':
				*t++ = '\t';
				break;
			case 'f':
				*t++ = '\f';
				break;
			case '\\':
				*t++ = '\\';
				break;
			case '"':
				*t++ = '"';
				break;
			case 'u':
				/* Unicode escape */
				uval = 0;
				h ++; /* u character */
				len --;

				if (len > 3) {
					for (i = 0; i < 4; i++) {
						uval <<= 4;
						if (isdigit (h[i])) {
							uval += h[i] - '0';
						}
						else if (h[i] >= 'a' && h[i] <= 'f') {
							uval += h[i] - 'a' + 10;
						}
						else if (h[i] >= 'A' && h[i] <= 'F') {
							uval += h[i] - 'A' + 10;
						}
						else {
							break;
						}
					}

					/* Encode */
					if(uval < 0x80) {
						t[0] = (char)uval;
						t ++;
					}
					else if(uval < 0x800) {
						t[0] = 0xC0 + ((uval & 0x7C0) >> 6);
						t[1] = 0x80 + ((uval & 0x03F));
						t += 2;
					}
					else if(uval < 0x10000) {
						t[0] = 0xE0 + ((uval & 0xF000) >> 12);
						t[1] = 0x80 + ((uval & 0x0FC0) >> 6);
						t[2] = 0x80 + ((uval & 0x003F));
						t += 3;
					}
#if 0
					/* It's not actually supported now */
					else if(uval <= 0x10FFFF) {
						t[0] = 0xF0 + ((uval & 0x1C0000) >> 18);
						t[1] = 0x80 + ((uval & 0x03F000) >> 12);
						t[2] = 0x80 + ((uval & 0x000FC0) >> 6);
						t[3] = 0x80 + ((uval & 0x00003F));
						t += 4;
					}
#endif
					else {
						*t++ = '?';
					}

					/* Consume 4 characters of source */
					h += 4;
					len -= 4;

					if (len > 0) {
						len --; /* for '\' character */
					}
					continue;
				}
				else {
					*t++ = 'u';
				}
				break;
			default:
				*t++ = *h;
				break;
			}
			h ++;
			len --;
		}
		else {
			*t++ = *h++;
		}

		if (len > 0) {
			len --;
		}
	}
	*t = '\0';

	return (t - str);
}

char *
ucl_copy_key_trash (const ucl_object_t *obj)
{
	ucl_object_t *deconst;

	if (obj == NULL) {
		return NULL;
	}
	if (obj->trash_stack[UCL_TRASH_KEY] == NULL && obj->key != NULL) {
		deconst = __DECONST (ucl_object_t *, obj);
		deconst->trash_stack[UCL_TRASH_KEY] = malloc (obj->keylen + 1);
		if (deconst->trash_stack[UCL_TRASH_KEY] != NULL) {
			memcpy (deconst->trash_stack[UCL_TRASH_KEY], obj->key, obj->keylen);
			deconst->trash_stack[UCL_TRASH_KEY][obj->keylen] = '\0';
		}
		deconst->key = obj->trash_stack[UCL_TRASH_KEY];
		deconst->flags |= UCL_OBJECT_ALLOCATED_KEY;
	}

	return obj->trash_stack[UCL_TRASH_KEY];
}

char *
ucl_copy_value_trash (const ucl_object_t *obj)
{
	ucl_object_t *deconst;

	if (obj == NULL) {
		return NULL;
	}
	if (obj->trash_stack[UCL_TRASH_VALUE] == NULL) {
		deconst = __DECONST (ucl_object_t *, obj);
		if (obj->type == UCL_STRING) {

			/* Special case for strings */
			if (obj->flags & UCL_OBJECT_BINARY) {
				deconst->trash_stack[UCL_TRASH_VALUE] = malloc (obj->len);
				if (deconst->trash_stack[UCL_TRASH_VALUE] != NULL) {
					memcpy (deconst->trash_stack[UCL_TRASH_VALUE],
							obj->value.sv,
							obj->len);
					deconst->value.sv = obj->trash_stack[UCL_TRASH_VALUE];
				}
			}
			else {
				deconst->trash_stack[UCL_TRASH_VALUE] = malloc (obj->len + 1);
				if (deconst->trash_stack[UCL_TRASH_VALUE] != NULL) {
					memcpy (deconst->trash_stack[UCL_TRASH_VALUE],
							obj->value.sv,
							obj->len);
					deconst->trash_stack[UCL_TRASH_VALUE][obj->len] = '\0';
					deconst->value.sv = obj->trash_stack[UCL_TRASH_VALUE];
				}
			}
		}
		else {
			/* Just emit value in json notation */
			deconst->trash_stack[UCL_TRASH_VALUE] = ucl_object_emit_single_json (obj);
			deconst->len = strlen (obj->trash_stack[UCL_TRASH_VALUE]);
		}
		deconst->flags |= UCL_OBJECT_ALLOCATED_VALUE;
	}

	return obj->trash_stack[UCL_TRASH_VALUE];
}

ucl_object_t*
ucl_parser_get_object (struct ucl_parser *parser)
{
	if (parser->state != UCL_STATE_ERROR && parser->top_obj != NULL) {
		return ucl_object_ref (parser->top_obj);
	}

	return NULL;
}

void
ucl_parser_free (struct ucl_parser *parser)
{
	struct ucl_stack *stack, *stmp;
	struct ucl_macro *macro, *mtmp;
	struct ucl_chunk *chunk, *ctmp;
	struct ucl_pubkey *key, *ktmp;
	struct ucl_variable *var, *vtmp;
	ucl_object_t *tr, *trtmp;

	if (parser == NULL) {
		return;
	}

	if (parser->top_obj != NULL) {
		ucl_object_unref (parser->top_obj);
	}

	if (parser->includepaths != NULL) {
		ucl_object_unref (parser->includepaths);
	}

	LL_FOREACH_SAFE (parser->stack, stack, stmp) {
		free (stack);
	}
	HASH_ITER (hh, parser->macroes, macro, mtmp) {
		free (macro->name);
		HASH_DEL (parser->macroes, macro);
		UCL_FREE (sizeof (struct ucl_macro), macro);
	}
	LL_FOREACH_SAFE (parser->chunks, chunk, ctmp) {
		UCL_FREE (sizeof (struct ucl_chunk), chunk);
	}
	LL_FOREACH_SAFE (parser->keys, key, ktmp) {
		UCL_FREE (sizeof (struct ucl_pubkey), key);
	}
	LL_FOREACH_SAFE (parser->variables, var, vtmp) {
		free (var->value);
		free (var->var);
		UCL_FREE (sizeof (struct ucl_variable), var);
	}
	LL_FOREACH_SAFE (parser->trash_objs, tr, trtmp) {
		ucl_object_free_internal (tr, false, ucl_object_dtor_free);
	}

	if (parser->err != NULL) {
		utstring_free (parser->err);
	}

	if (parser->cur_file) {
		free (parser->cur_file);
	}

	if (parser->comments) {
		ucl_object_unref (parser->comments);
	}

	UCL_FREE (sizeof (struct ucl_parser), parser);
}

const char *
ucl_parser_get_error(struct ucl_parser *parser)
{
	if (parser == NULL) {
		return NULL;
	}

	if (parser->err == NULL) {
		return NULL;
	}

	return utstring_body (parser->err);
}

int
ucl_parser_get_error_code(struct ucl_parser *parser)
{
	if (parser == NULL) {
		return 0;
	}

	return parser->err_code;
}

unsigned
ucl_parser_get_column(struct ucl_parser *parser)
{
	if (parser == NULL || parser->chunks == NULL) {
		return 0;
	}

	return parser->chunks->column;
}

unsigned
ucl_parser_get_linenum(struct ucl_parser *parser)
{
	if (parser == NULL || parser->chunks == NULL) {
		return 0;
	}

	return parser->chunks->line;
}

void
ucl_parser_clear_error(struct ucl_parser *parser)
{
	if (parser != NULL && parser->err != NULL) {
		utstring_free(parser->err);
		parser->err = NULL;
		parser->err_code = 0;
	}
}

bool
ucl_pubkey_add (struct ucl_parser *parser, const unsigned char *key, size_t len)
{
#ifndef HAVE_OPENSSL
	ucl_create_err (&parser->err, "cannot check signatures without openssl");
	return false;
#else
# if (OPENSSL_VERSION_NUMBER < 0x10000000L)
	ucl_create_err (&parser->err, "cannot check signatures, openssl version is unsupported");
	return EXIT_FAILURE;
# else
	struct ucl_pubkey *nkey;
	BIO *mem;

	mem = BIO_new_mem_buf ((void *)key, len);
	nkey = UCL_ALLOC (sizeof (struct ucl_pubkey));
	if (nkey == NULL) {
		ucl_create_err (&parser->err, "cannot allocate memory for key");
		return false;
	}
	nkey->key = PEM_read_bio_PUBKEY (mem, &nkey->key, NULL, NULL);
	BIO_free (mem);
	if (nkey->key == NULL) {
		UCL_FREE (sizeof (struct ucl_pubkey), nkey);
		ucl_create_err (&parser->err, "%s",
				ERR_error_string (ERR_get_error (), NULL));
		return false;
	}
	LL_PREPEND (parser->keys, nkey);
# endif
#endif
	return true;
}

#ifdef CURL_FOUND
struct ucl_curl_cbdata {
	unsigned char *buf;
	size_t buflen;
};

static size_t
ucl_curl_write_callback (void* contents, size_t size, size_t nmemb, void* ud)
{
	struct ucl_curl_cbdata *cbdata = ud;
	size_t realsize = size * nmemb;

	cbdata->buf = realloc (cbdata->buf, cbdata->buflen + realsize + 1);
	if (cbdata->buf == NULL) {
		return 0;
	}

	memcpy (&(cbdata->buf[cbdata->buflen]), contents, realsize);
	cbdata->buflen += realsize;
	cbdata->buf[cbdata->buflen] = 0;

	return realsize;
}
#endif

/**
 * Fetch a url and save results to the memory buffer
 * @param url url to fetch
 * @param len length of url
 * @param buf target buffer
 * @param buflen target length
 * @return
 */
bool
ucl_fetch_url (const unsigned char *url, unsigned char **buf, size_t *buflen,
		UT_string **err, bool must_exist)
{

#ifdef HAVE_FETCH_H
	struct url *fetch_url;
	struct url_stat us;
	FILE *in;

	fetch_url = fetchParseURL (url);
	if (fetch_url == NULL) {
		ucl_create_err (err, "invalid URL %s: %s",
				url, strerror (errno));
		return false;
	}
	if ((in = fetchXGet (fetch_url, &us, "")) == NULL) {
		if (!must_exist) {
			ucl_create_err (err, "cannot fetch URL %s: %s",
				url, strerror (errno));
		}
		fetchFreeURL (fetch_url);
		return false;
	}

	*buflen = us.size;
	*buf = malloc (*buflen);
	if (*buf == NULL) {
		ucl_create_err (err, "cannot allocate buffer for URL %s: %s",
				url, strerror (errno));
		fclose (in);
		fetchFreeURL (fetch_url);
		return false;
	}

	if (fread (*buf, *buflen, 1, in) != 1) {
		ucl_create_err (err, "cannot read URL %s: %s",
				url, strerror (errno));
		fclose (in);
		fetchFreeURL (fetch_url);
		return false;
	}

	fetchFreeURL (fetch_url);
	return true;
#elif defined(CURL_FOUND)
	CURL *curl;
	int r;
	struct ucl_curl_cbdata cbdata;

	curl = curl_easy_init ();
	if (curl == NULL) {
		ucl_create_err (err, "CURL interface is broken");
		return false;
	}
	if ((r = curl_easy_setopt (curl, CURLOPT_URL, url)) != CURLE_OK) {
		ucl_create_err (err, "invalid URL %s: %s",
				url, curl_easy_strerror (r));
		curl_easy_cleanup (curl);
		return false;
	}
	curl_easy_setopt (curl, CURLOPT_WRITEFUNCTION, ucl_curl_write_callback);
	cbdata.buf = NULL;
	cbdata.buflen = 0;
	curl_easy_setopt (curl, CURLOPT_WRITEDATA, &cbdata);

	if ((r = curl_easy_perform (curl)) != CURLE_OK) {
		if (!must_exist) {
			ucl_create_err (err, "error fetching URL %s: %s",
				url, curl_easy_strerror (r));
		}
		curl_easy_cleanup (curl);
		if (cbdata.buf) {
			free (cbdata.buf);
		}
		return false;
	}
	*buf = cbdata.buf;
	*buflen = cbdata.buflen;

	return true;
#else
	ucl_create_err (err, "URL support is disabled");
	return false;
#endif
}

/**
 * Fetch a file and save results to the memory buffer
 * @param filename filename to fetch
 * @param len length of filename
 * @param buf target buffer
 * @param buflen target length
 * @return
 */
bool
ucl_fetch_file (const unsigned char *filename, unsigned char **buf, size_t *buflen,
		UT_string **err, bool must_exist)
{
	int fd;
	struct stat st;

	if (stat (filename, &st) == -1 || !S_ISREG (st.st_mode)) {
		if (must_exist) {
			ucl_create_err (err, "cannot stat file %s: %s",
					filename, strerror (errno));
		}
		return false;
	}
	if (st.st_size == 0) {
		/* Do not map empty files */
		*buf = NULL;
		*buflen = 0;
	}
	else {
		if ((fd = open (filename, O_RDONLY)) == -1) {
			ucl_create_err (err, "cannot open file %s: %s",
					filename, strerror (errno));
			return false;
		}
		if ((*buf = ucl_mmap (NULL, st.st_size, PROT_READ, MAP_SHARED, fd, 0)) == MAP_FAILED) {
			close (fd);
			ucl_create_err (err, "cannot mmap file %s: %s",
					filename, strerror (errno));
			*buf = NULL;

			return false;
		}
		*buflen = st.st_size;
		close (fd);
	}

	return true;
}


#if (defined(HAVE_OPENSSL) && OPENSSL_VERSION_NUMBER >= 0x10000000L)
static inline bool
ucl_sig_check (const unsigned char *data, size_t datalen,
		const unsigned char *sig, size_t siglen, struct ucl_parser *parser)
{
	struct ucl_pubkey *key;
	char dig[EVP_MAX_MD_SIZE];
	unsigned int diglen;
	EVP_PKEY_CTX *key_ctx;
	EVP_MD_CTX *sign_ctx = NULL;

	sign_ctx = EVP_MD_CTX_create ();

	LL_FOREACH (parser->keys, key) {
		key_ctx = EVP_PKEY_CTX_new (key->key, NULL);
		if (key_ctx != NULL) {
			if (EVP_PKEY_verify_init (key_ctx) <= 0) {
				EVP_PKEY_CTX_free (key_ctx);
				continue;
			}
			if (EVP_PKEY_CTX_set_rsa_padding (key_ctx, RSA_PKCS1_PADDING) <= 0) {
				EVP_PKEY_CTX_free (key_ctx);
				continue;
			}
			if (EVP_PKEY_CTX_set_signature_md (key_ctx, EVP_sha256 ()) <= 0) {
				EVP_PKEY_CTX_free (key_ctx);
				continue;
			}
			EVP_DigestInit (sign_ctx, EVP_sha256 ());
			EVP_DigestUpdate (sign_ctx, data, datalen);
			EVP_DigestFinal (sign_ctx, dig, &diglen);

			if (EVP_PKEY_verify (key_ctx, sig, siglen, dig, diglen) == 1) {
				EVP_MD_CTX_destroy (sign_ctx);
				EVP_PKEY_CTX_free (key_ctx);
				return true;
			}

			EVP_PKEY_CTX_free (key_ctx);
		}
	}

	EVP_MD_CTX_destroy (sign_ctx);

	return false;
}
#endif

struct ucl_include_params {
	bool check_signature;
	bool must_exist;
	bool use_glob;
	bool use_prefix;
	bool soft_fail;
	bool allow_glob;
	unsigned priority;
	enum ucl_duplicate_strategy strat;
	enum ucl_parse_type parse_type;
	const char *prefix;
	const char *target;
};

/**
 * Include an url to configuration
 * @param data
 * @param len
 * @param parser
 * @param err
 * @return
 */
static bool
ucl_include_url (const unsigned char *data, size_t len,
		struct ucl_parser *parser,
		struct ucl_include_params *params)
{

	bool res;
	unsigned char *buf = NULL;
	size_t buflen = 0;
	struct ucl_chunk *chunk;
	char urlbuf[PATH_MAX];
	int prev_state;

	snprintf (urlbuf, sizeof (urlbuf), "%.*s", (int)len, data);

	if (!ucl_fetch_url (urlbuf, &buf, &buflen, &parser->err, params->must_exist)) {
		return !params->must_exist;
	}

	if (params->check_signature) {
#if (defined(HAVE_OPENSSL) && OPENSSL_VERSION_NUMBER >= 0x10000000L)
		unsigned char *sigbuf = NULL;
		size_t siglen = 0;
		/* We need to check signature first */
		snprintf (urlbuf, sizeof (urlbuf), "%.*s.sig", (int)len, data);
		if (!ucl_fetch_url (urlbuf, &sigbuf, &siglen, &parser->err, true)) {
			return false;
		}
		if (!ucl_sig_check (buf, buflen, sigbuf, siglen, parser)) {
			ucl_create_err (&parser->err, "cannot verify url %s: %s",
							urlbuf,
							ERR_error_string (ERR_get_error (), NULL));
			if (siglen > 0) {
				ucl_munmap (sigbuf, siglen);
			}
			return false;
		}
		if (siglen > 0) {
			ucl_munmap (sigbuf, siglen);
		}
#endif
	}

	prev_state = parser->state;
	parser->state = UCL_STATE_INIT;

	res = ucl_parser_add_chunk_full (parser, buf, buflen, params->priority,
			params->strat, params->parse_type);
	if (res == true) {
		/* Remove chunk from the stack */
		chunk = parser->chunks;
		if (chunk != NULL) {
			parser->chunks = chunk->next;
			UCL_FREE (sizeof (struct ucl_chunk), chunk);
		}
	}

	parser->state = prev_state;
	free (buf);

	return res;
}

/**
 * Include a single file to the parser
 * @param data
 * @param len
 * @param parser
 * @param check_signature
 * @param must_exist
 * @param allow_glob
 * @param priority
 * @return
 */
static bool
ucl_include_file_single (const unsigned char *data, size_t len,
		struct ucl_parser *parser, struct ucl_include_params *params)
{
	bool res;
	struct ucl_chunk *chunk;
	unsigned char *buf = NULL;
	char *old_curfile, *ext;
	size_t buflen = 0;
	char filebuf[PATH_MAX], realbuf[PATH_MAX];
	int prev_state;
	struct ucl_variable *cur_var, *tmp_var, *old_curdir = NULL,
			*old_filename = NULL;
	ucl_object_t *nest_obj = NULL, *old_obj = NULL, *new_obj = NULL;
	ucl_hash_t *container = NULL;
	struct ucl_stack *st = NULL;

	snprintf (filebuf, sizeof (filebuf), "%.*s", (int)len, data);
	if (ucl_realpath (filebuf, realbuf) == NULL) {
		if (params->soft_fail) {
			return false;
		}
		if (!params->must_exist) {
			return true;
		}
		ucl_create_err (&parser->err, "cannot open file %s: %s",
									filebuf,
									strerror (errno));
		return false;
	}

	if (parser->cur_file && strcmp (realbuf, parser->cur_file) == 0) {
		/* We are likely including the file itself */
		if (params->soft_fail) {
			return false;
		}

		ucl_create_err (&parser->err, "trying to include the file %s from itself",
				realbuf);
		return false;
	}

	if (!ucl_fetch_file (realbuf, &buf, &buflen, &parser->err, params->must_exist)) {
		if (params->soft_fail) {
			return false;
		}

		return (!params->must_exist || false);
	}

	if (params->check_signature) {
#if (defined(HAVE_OPENSSL) && OPENSSL_VERSION_NUMBER >= 0x10000000L)
		unsigned char *sigbuf = NULL;
		size_t siglen = 0;
		/* We need to check signature first */
		snprintf (filebuf, sizeof (filebuf), "%s.sig", realbuf);
		if (!ucl_fetch_file (filebuf, &sigbuf, &siglen, &parser->err, true)) {
			return false;
		}
		if (!ucl_sig_check (buf, buflen, sigbuf, siglen, parser)) {
			ucl_create_err (&parser->err, "cannot verify file %s: %s",
							filebuf,
							ERR_error_string (ERR_get_error (), NULL));
			if (sigbuf) {
				ucl_munmap (sigbuf, siglen);
			}
			return false;
		}
		if (sigbuf) {
			ucl_munmap (sigbuf, siglen);
		}
#endif
	}

	old_curfile = parser->cur_file;
	parser->cur_file = strdup (realbuf);

	/* Store old file vars */
	DL_FOREACH_SAFE (parser->variables, cur_var, tmp_var) {
		if (strcmp (cur_var->var, "CURDIR") == 0) {
			old_curdir = cur_var;
			DL_DELETE (parser->variables, cur_var);
		}
		else if (strcmp (cur_var->var, "FILENAME") == 0) {
			old_filename = cur_var;
			DL_DELETE (parser->variables, cur_var);
		}
	}

	ucl_parser_set_filevars (parser, realbuf, false);

	prev_state = parser->state;
	parser->state = UCL_STATE_INIT;

	if (params->use_prefix && params->prefix == NULL) {
		/* Auto generate a key name based on the included filename */
		params->prefix = basename (realbuf);
		ext = strrchr (params->prefix, '.');
		if (ext != NULL && (strcmp (ext, ".conf") == 0 || strcmp (ext, ".ucl") == 0)) {
			/* Strip off .conf or .ucl */
			*ext = '\0';
		}
	}
	if (params->prefix != NULL) {
		/* This is a prefixed include */
		container = parser->stack->obj->value.ov;

		old_obj = __DECONST (ucl_object_t *, ucl_hash_search (container,
				params->prefix, strlen (params->prefix)));

		if (strcasecmp (params->target, "array") == 0 && old_obj == NULL) {
			/* Create an array with key: prefix */
			old_obj = ucl_object_new_full (UCL_ARRAY, params->priority);
			old_obj->key = params->prefix;
			old_obj->keylen = strlen (params->prefix);
			ucl_copy_key_trash(old_obj);
			old_obj->prev = old_obj;
			old_obj->next = NULL;

			container = ucl_hash_insert_object (container, old_obj,
					parser->flags & UCL_PARSER_KEY_LOWERCASE);
			parser->stack->obj->len ++;

			nest_obj = ucl_object_new_full (UCL_OBJECT, params->priority);
			nest_obj->prev = nest_obj;
			nest_obj->next = NULL;

			ucl_array_append (old_obj, nest_obj);
		}
		else if (old_obj == NULL) {
			/* Create an object with key: prefix */
			nest_obj = ucl_object_new_full (UCL_OBJECT, params->priority);

			if (nest_obj == NULL) {
				ucl_create_err (&parser->err, "cannot allocate memory for an object");
				if (buf) {
					ucl_munmap (buf, buflen);
				}

				return false;
			}

			nest_obj->key = params->prefix;
			nest_obj->keylen = strlen (params->prefix);
			ucl_copy_key_trash(nest_obj);
			nest_obj->prev = nest_obj;
			nest_obj->next = NULL;

			container = ucl_hash_insert_object (container, nest_obj,
					parser->flags & UCL_PARSER_KEY_LOWERCASE);
			parser->stack->obj->len ++;
		}
		else if (strcasecmp (params->target, "array") == 0 ||
				ucl_object_type(old_obj) == UCL_ARRAY) {
			if (ucl_object_type(old_obj) == UCL_ARRAY) {
				/* Append to the existing array */
				nest_obj = ucl_object_new_full (UCL_OBJECT, params->priority);
				if (nest_obj == NULL) {
					ucl_create_err (&parser->err, "cannot allocate memory for an object");
					if (buf) {
						ucl_munmap (buf, buflen);
					}

					return false;
				}
				nest_obj->prev = nest_obj;
				nest_obj->next = NULL;

				ucl_array_append (old_obj, nest_obj);
			}
			else {
				/* Convert the object to an array */
				new_obj = ucl_object_typed_new (UCL_ARRAY);
				if (new_obj == NULL) {
					ucl_create_err (&parser->err, "cannot allocate memory for an object");
					if (buf) {
						ucl_munmap (buf, buflen);
					}

					return false;
				}
				new_obj->key = old_obj->key;
				new_obj->keylen = old_obj->keylen;
				new_obj->flags |= UCL_OBJECT_MULTIVALUE;
				new_obj->prev = new_obj;
				new_obj->next = NULL;

				nest_obj = ucl_object_new_full (UCL_OBJECT, params->priority);
				if (nest_obj == NULL) {
					ucl_create_err (&parser->err, "cannot allocate memory for an object");
					if (buf) {
						ucl_munmap (buf, buflen);
					}

					return false;
				}
				nest_obj->prev = nest_obj;
				nest_obj->next = NULL;

				ucl_array_append (new_obj, old_obj);
				ucl_array_append (new_obj, nest_obj);
				ucl_hash_replace (container, old_obj, new_obj);
			}
		}
		else {
			if (ucl_object_type (old_obj) == UCL_OBJECT) {
				/* Append to existing Object*/
				nest_obj = old_obj;
			}
			else {
				/* The key is not an object */
				ucl_create_err (&parser->err,
						"Conflicting type for key: %s",
						params->prefix);
				if (buf) {
					ucl_munmap (buf, buflen);
				}

				return false;
			}
		}

		 /* Put all of the content of the include inside that object */
		parser->stack->obj->value.ov = container;

		st = UCL_ALLOC (sizeof (struct ucl_stack));
		if (st == NULL) {
			ucl_create_err (&parser->err, "cannot allocate memory for an object");
			ucl_object_unref (nest_obj);

			if (buf) {
				ucl_munmap (buf, buflen);
			}

			return false;
		}
		st->obj = nest_obj;
		st->level = parser->stack->level;
		LL_PREPEND (parser->stack, st);
		parser->cur_obj = nest_obj;
	}

	res = ucl_parser_add_chunk_full (parser, buf, buflen, params->priority,
			params->strat, params->parse_type);

	if (!res) {
		if (!params->must_exist) {
			/* Free error */
			utstring_free (parser->err);
			parser->err = NULL;
			res = true;
		}
	}

	/* Stop nesting the include, take 1 level off the stack */
	if (params->prefix != NULL && nest_obj != NULL) {
		parser->stack = st->next;
		UCL_FREE (sizeof (struct ucl_stack), st);
	}

	/* Remove chunk from the stack */
	chunk = parser->chunks;
	if (chunk != NULL) {
		parser->chunks = chunk->next;
		UCL_FREE (sizeof (struct ucl_chunk), chunk);
		parser->recursion --;
	}

	/* Restore old file vars */
	if (parser->cur_file) {
		free (parser->cur_file);
	}

	parser->cur_file = old_curfile;
	DL_FOREACH_SAFE (parser->variables, cur_var, tmp_var) {
		if (strcmp (cur_var->var, "CURDIR") == 0 && old_curdir) {
			DL_DELETE (parser->variables, cur_var);
			free (cur_var->var);
			free (cur_var->value);
			UCL_FREE (sizeof (struct ucl_variable), cur_var);
		}
		else if (strcmp (cur_var->var, "FILENAME") == 0 && old_filename) {
			DL_DELETE (parser->variables, cur_var);
			free (cur_var->var);
			free (cur_var->value);
			UCL_FREE (sizeof (struct ucl_variable), cur_var);
		}
	}
	if (old_filename) {
		DL_APPEND (parser->variables, old_filename);
	}
	if (old_curdir) {
		DL_APPEND (parser->variables, old_curdir);
	}

	parser->state = prev_state;

	if (buflen > 0) {
		ucl_munmap (buf, buflen);
	}

	return res;
}

/**
 * Include a file to configuration
 * @param data
 * @param len
 * @param parser
 * @param err
 * @return
 */
static bool
ucl_include_file (const unsigned char *data, size_t len,
		struct ucl_parser *parser, struct ucl_include_params *params)
{
	const unsigned char *p = data, *end = data + len;
	bool need_glob = false;
	int cnt = 0;
	char glob_pattern[PATH_MAX];
	size_t i;

#ifndef _WIN32
	if (!params->allow_glob) {
		return ucl_include_file_single (data, len, parser, params);
	}
	else {
		/* Check for special symbols in a filename */
		while (p != end) {
			if (*p == '*' || *p == '?') {
				need_glob = true;
				break;
			}
			p ++;
		}
		if (need_glob) {
			glob_t globbuf;
			memset (&globbuf, 0, sizeof (globbuf));
			ucl_strlcpy (glob_pattern, (const char *)data,
				(len + 1 < sizeof (glob_pattern) ? len + 1 : sizeof (glob_pattern)));
			if (glob (glob_pattern, 0, NULL, &globbuf) != 0) {
				return (!params->must_exist || false);
			}
			for (i = 0; i < globbuf.gl_pathc; i ++) {
				if (!ucl_include_file_single ((unsigned char *)globbuf.gl_pathv[i],
						strlen (globbuf.gl_pathv[i]), parser, params)) {
					if (params->soft_fail) {
						continue;
					}
					globfree (&globbuf);
					return false;
				}
				cnt ++;
			}
			globfree (&globbuf);

			if (cnt == 0 && params->must_exist) {
				ucl_create_err (&parser->err, "cannot match any files for pattern %s",
					glob_pattern);
				return false;
			}
		}
		else {
			return ucl_include_file_single (data, len, parser, params);
		}
	}
#else
	/* Win32 compilers do not support globbing. Therefore, for Win32,
	   treat allow_glob/need_glob as a NOOP and just return */
	return ucl_include_file_single (data, len, parser, params);
#endif

	return true;
}

/**
 * Common function to handle .*include* macros
 * @param data
 * @param len
 * @param args
 * @param parser
 * @param default_try
 * @param default_sign
 * @return
 */
static bool
ucl_include_common (const unsigned char *data, size_t len,
		const ucl_object_t *args, struct ucl_parser *parser,
		bool default_try,
		bool default_sign)
{
	bool allow_url = false, search = false;
	const char *duplicate;
	const ucl_object_t *param;
	ucl_object_iter_t it = NULL, ip = NULL;
	char ipath[PATH_MAX];
	struct ucl_include_params params;

	/* Default values */
	params.soft_fail = default_try;
	params.allow_glob = false;
	params.check_signature = default_sign;
	params.use_prefix = false;
	params.target = "object";
	params.prefix = NULL;
	params.priority = 0;
	params.parse_type = UCL_PARSE_UCL;
	params.strat = UCL_DUPLICATE_APPEND;
	params.must_exist = !default_try;

	/* Process arguments */
	if (args != NULL && args->type == UCL_OBJECT) {
		while ((param = ucl_object_iterate (args, &it, true)) != NULL) {
			if (param->type == UCL_BOOLEAN) {
				if (strncmp (param->key, "try", param->keylen) == 0) {
					params.must_exist = !ucl_object_toboolean (param);
				}
				else if (strncmp (param->key, "sign", param->keylen) == 0) {
					params.check_signature = ucl_object_toboolean (param);
				}
				else if (strncmp (param->key, "glob", param->keylen) == 0) {
					params.allow_glob = ucl_object_toboolean (param);
				}
				else if (strncmp (param->key, "url", param->keylen) == 0) {
					allow_url = ucl_object_toboolean (param);
				}
				else if (strncmp (param->key, "prefix", param->keylen) == 0) {
					params.use_prefix = ucl_object_toboolean (param);
				}
			}
			else if (param->type == UCL_STRING) {
				if (strncmp (param->key, "key", param->keylen) == 0) {
					params.prefix = ucl_object_tostring (param);
				}
				else if (strncmp (param->key, "target", param->keylen) == 0) {
					params.target = ucl_object_tostring (param);
				}
				else if (strncmp (param->key, "duplicate", param->keylen) == 0) {
					duplicate = ucl_object_tostring (param);

					if (strcmp (duplicate, "append") == 0) {
						params.strat = UCL_DUPLICATE_APPEND;
					}
					else if (strcmp (duplicate, "merge") == 0) {
						params.strat = UCL_DUPLICATE_MERGE;
					}
					else if (strcmp (duplicate, "rewrite") == 0) {
						params.strat = UCL_DUPLICATE_REWRITE;
					}
					else if (strcmp (duplicate, "error") == 0) {
						params.strat = UCL_DUPLICATE_ERROR;
					}
				}
			}
			else if (param->type == UCL_ARRAY) {
				if (strncmp (param->key, "path", param->keylen) == 0) {
					ucl_set_include_path (parser, __DECONST(ucl_object_t *, param));
				}
			}
			else if (param->type == UCL_INT) {
				if (strncmp (param->key, "priority", param->keylen) == 0) {
					params.priority = ucl_object_toint (param);
				}
			}
		}
	}

	if (parser->includepaths == NULL) {
		if (allow_url && ucl_strnstr (data, "://", len) != NULL) {
			/* Globbing is not used for URL's */
			return ucl_include_url (data, len, parser, &params);
		}
		else if (data != NULL) {
			/* Try to load a file */
			return ucl_include_file (data, len, parser, &params);
		}
	}
	else {
		if (allow_url && ucl_strnstr (data, "://", len) != NULL) {
			/* Globbing is not used for URL's */
			return ucl_include_url (data, len, parser, &params);
		}

		ip = ucl_object_iterate_new (parser->includepaths);
		while ((param = ucl_object_iterate_safe (ip, true)) != NULL) {
			if (ucl_object_type(param) == UCL_STRING) {
				snprintf (ipath, sizeof (ipath), "%s/%.*s", ucl_object_tostring(param),
						(int)len, data);
				if ((search = ucl_include_file (ipath, strlen (ipath),
						parser, &params))) {
					if (!params.allow_glob) {
						break;
					}
				}
			}
		}
		ucl_object_iterate_free (ip);
		if (search == true) {
			return true;
		}
		else {
			ucl_create_err (&parser->err,
					"cannot find file: %.*s in search path",
					(int)len, data);
			return false;
		}
	}

	return false;
}

/**
 * Handle include macro
 * @param data include data
 * @param len length of data
 * @param args UCL object representing arguments to the macro
 * @param ud user data
 * @return
 */
bool
ucl_include_handler (const unsigned char *data, size_t len,
		const ucl_object_t *args, void* ud)
{
	struct ucl_parser *parser = ud;

	return ucl_include_common (data, len, args, parser, false, false);
}

/**
 * Handle includes macro
 * @param data include data
 * @param len length of data
 * @param args UCL object representing arguments to the macro
 * @param ud user data
 * @return
 */
bool
ucl_includes_handler (const unsigned char *data, size_t len,
		const ucl_object_t *args, void* ud)
{
	struct ucl_parser *parser = ud;

	return ucl_include_common (data, len, args, parser, false, true);
}

/**
 * Handle tryinclude macro
 * @param data include data
 * @param len length of data
 * @param args UCL object representing arguments to the macro
 * @param ud user data
 * @return
 */
bool
ucl_try_include_handler (const unsigned char *data, size_t len,
		const ucl_object_t *args, void* ud)
{
	struct ucl_parser *parser = ud;

	return ucl_include_common (data, len, args, parser, true, false);
}

/**
 * Handle priority macro
 * @param data include data
 * @param len length of data
 * @param args UCL object representing arguments to the macro
 * @param ud user data
 * @return
 */
bool
ucl_priority_handler (const unsigned char *data, size_t len,
		const ucl_object_t *args, void* ud)
{
	struct ucl_parser *parser = ud;
	unsigned priority = 255;
	const ucl_object_t *param;
	bool found = false;
	char *value = NULL, *leftover = NULL;
	ucl_object_iter_t it = NULL;

	if (parser == NULL) {
		return false;
	}

	/* Process arguments */
	if (args != NULL && args->type == UCL_OBJECT) {
		while ((param = ucl_object_iterate (args, &it, true)) != NULL) {
			if (param->type == UCL_INT) {
				if (strncmp (param->key, "priority", param->keylen) == 0) {
					priority = ucl_object_toint (param);
					found = true;
				}
			}
		}
	}

	if (len > 0) {
		value = malloc(len + 1);
		ucl_strlcpy(value, (const char *)data, len + 1);
		priority = strtol(value, &leftover, 10);
		if (*leftover != '\0') {
			ucl_create_err (&parser->err, "Invalid priority value in macro: %s",
				value);
			free(value);
			return false;
		}
		free(value);
		found = true;
	}

	if (found == true) {
		parser->chunks->priority = priority;
		return true;
	}

	ucl_create_err (&parser->err, "Unable to parse priority macro");
	return false;
}

/**
 * Handle load macro
 * @param data include data
 * @param len length of data
 * @param args UCL object representing arguments to the macro
 * @param ud user data
 * @return
 */
bool
ucl_load_handler (const unsigned char *data, size_t len,
		const ucl_object_t *args, void* ud)
{
	struct ucl_parser *parser = ud;
	const ucl_object_t *param;
	ucl_object_t *obj, *old_obj;
	ucl_object_iter_t it = NULL;
	bool try_load, multiline, test;
	const char *target, *prefix;
	char *load_file, *tmp;
	unsigned char *buf;
	size_t buflen;
	unsigned priority;
	int64_t iv;
	ucl_object_t *container = NULL;
	enum ucl_string_flags flags;

	/* Default values */
	try_load = false;
	multiline = false;
	test = false;
	target = "string";
	prefix = NULL;
	load_file = NULL;
	buf = NULL;
	buflen = 0;
	priority = 0;
	obj = NULL;
	old_obj = NULL;
	flags = 0;

	if (parser == NULL) {
		return false;
	}

	/* Process arguments */
	if (args != NULL && args->type == UCL_OBJECT) {
		while ((param = ucl_object_iterate (args, &it, true)) != NULL) {
			if (param->type == UCL_BOOLEAN) {
				if (strncmp (param->key, "try", param->keylen) == 0) {
					try_load = ucl_object_toboolean (param);
				}
				else if (strncmp (param->key, "multiline", param->keylen) == 0) {
					multiline = ucl_object_toboolean (param);
				}
				else if (strncmp (param->key, "escape", param->keylen) == 0) {
					test = ucl_object_toboolean (param);
					if (test) {
						flags |= UCL_STRING_ESCAPE;
					}
				}
				else if (strncmp (param->key, "trim", param->keylen) == 0) {
					test = ucl_object_toboolean (param);
					if (test) {
						flags |= UCL_STRING_TRIM;
					}
				}
			}
			else if (param->type == UCL_STRING) {
				if (strncmp (param->key, "key", param->keylen) == 0) {
					prefix = ucl_object_tostring (param);
				}
				else if (strncmp (param->key, "target", param->keylen) == 0) {
					target = ucl_object_tostring (param);
				}
			}
			else if (param->type == UCL_INT) {
				if (strncmp (param->key, "priority", param->keylen) == 0) {
					priority = ucl_object_toint (param);
				}
			}
		}
	}

	if (prefix == NULL || strlen (prefix) == 0) {
		ucl_create_err (&parser->err, "No Key specified in load macro");
		return false;
	}

	if (len > 0) {
		load_file = malloc (len + 1);
		if (!load_file) {
			ucl_create_err (&parser->err, "cannot allocate memory for suffix");

			return false;
		}

		snprintf (load_file, len + 1, "%.*s", (int)len, data);

		if (!ucl_fetch_file (load_file, &buf, &buflen, &parser->err,
				!try_load)) {
			free (load_file);

			return (try_load || false);
		}

		free (load_file);
		container = parser->stack->obj;
		old_obj = __DECONST (ucl_object_t *, ucl_object_lookup (container,
				prefix));

		if (old_obj != NULL) {
			ucl_create_err (&parser->err, "Key %s already exists", prefix);
			if (buf) {
				ucl_munmap (buf, buflen);
			}

			return false;
		}

		if (strcasecmp (target, "string") == 0) {
			obj = ucl_object_fromstring_common (buf, buflen, flags);
			ucl_copy_value_trash (obj);
			if (multiline) {
				obj->flags |= UCL_OBJECT_MULTILINE;
			}
		}
		else if (strcasecmp (target, "int") == 0) {
			tmp = malloc (buflen + 1);

			if (tmp == NULL) {
				ucl_create_err (&parser->err, "Memory allocation failed");
				if (buf) {
					ucl_munmap (buf, buflen);
				}

				return false;
			}

			snprintf (tmp, buflen + 1, "%.*s", (int)buflen, buf);
			iv = strtoll (tmp, NULL, 10);
			obj = ucl_object_fromint (iv);
			free (tmp);
		}

		if (buf) {
			ucl_munmap (buf, buflen);
		}

		if (obj != NULL) {
			obj->key = prefix;
			obj->keylen = strlen (prefix);
			ucl_copy_key_trash (obj);
			obj->prev = obj;
			obj->next = NULL;
			ucl_object_set_priority (obj, priority);
			ucl_object_insert_key (container, obj, obj->key, obj->keylen, false);
		}

		return true;
	}

	ucl_create_err (&parser->err, "Unable to parse load macro");
	return false;
}

bool
ucl_inherit_handler (const unsigned char *data, size_t len,
		const ucl_object_t *args, const ucl_object_t *ctx, void* ud)
{
	const ucl_object_t *parent, *cur;
	ucl_object_t *target, *copy;
	ucl_object_iter_t it = NULL;
	bool replace = false;
	struct ucl_parser *parser = ud;

	parent = ucl_object_lookup_len (ctx, data, len);

	/* Some sanity checks */
	if (parent == NULL || ucl_object_type (parent) != UCL_OBJECT) {
		ucl_create_err (&parser->err, "Unable to find inherited object %*.s",
				(int)len, data);
		return false;
	}

	if (parser->stack == NULL || parser->stack->obj == NULL ||
			ucl_object_type (parser->stack->obj) != UCL_OBJECT) {
		ucl_create_err (&parser->err, "Invalid inherit context");
		return false;
	}

	target = parser->stack->obj;

	if (args && (cur = ucl_object_lookup (args, "replace")) != NULL) {
		replace = ucl_object_toboolean (cur);
	}

	while ((cur = ucl_object_iterate (parent, &it, true))) {
		/* We do not replace existing keys */
		if (!replace && ucl_object_lookup_len (target, cur->key, cur->keylen)) {
			continue;
		}

		copy = ucl_object_copy (cur);

		if (!replace) {
			copy->flags |= UCL_OBJECT_INHERITED;
		}

		ucl_object_insert_key (target, copy, copy->key,
				copy->keylen, false);
	}

	return true;
}

bool
ucl_parser_set_filevars (struct ucl_parser *parser, const char *filename, bool need_expand)
{
	char realbuf[PATH_MAX], *curdir;

	if (filename != NULL) {
		if (need_expand) {
			if (ucl_realpath (filename, realbuf) == NULL) {
				return false;
			}
		}
		else {
			ucl_strlcpy (realbuf, filename, sizeof (realbuf));
		}

		/* Define variables */
		ucl_parser_register_variable (parser, "FILENAME", realbuf);
		curdir = dirname (realbuf);
		ucl_parser_register_variable (parser, "CURDIR", curdir);
	}
	else {
		/* Set everything from the current dir */
		curdir = getcwd (realbuf, sizeof (realbuf));
		ucl_parser_register_variable (parser, "FILENAME", "undef");
		ucl_parser_register_variable (parser, "CURDIR", curdir);
	}

	return true;
}

bool
ucl_parser_add_file_full (struct ucl_parser *parser, const char *filename,
		unsigned priority, enum ucl_duplicate_strategy strat,
		enum ucl_parse_type parse_type)
{
	unsigned char *buf;
	size_t len;
	bool ret;
	char realbuf[PATH_MAX];

	if (ucl_realpath (filename, realbuf) == NULL) {
		ucl_create_err (&parser->err, "cannot open file %s: %s",
				filename,
				strerror (errno));
		return false;
	}

	if (!ucl_fetch_file (realbuf, &buf, &len, &parser->err, true)) {
		return false;
	}

	if (parser->cur_file) {
		free (parser->cur_file);
	}
	parser->cur_file = strdup (realbuf);
	ucl_parser_set_filevars (parser, realbuf, false);
	ret = ucl_parser_add_chunk_full (parser, buf, len, priority, strat,
			parse_type);

	if (len > 0) {
		ucl_munmap (buf, len);
	}

	return ret;
}

bool
ucl_parser_add_file_priority (struct ucl_parser *parser, const char *filename,
		unsigned priority)
{
	if (parser == NULL) {
		return false;
	}

	return ucl_parser_add_file_full(parser, filename, priority,
			UCL_DUPLICATE_APPEND, UCL_PARSE_UCL);
}

bool
ucl_parser_add_file (struct ucl_parser *parser, const char *filename)
{
	if (parser == NULL) {
		return false;
	}

	return ucl_parser_add_file_full(parser, filename,
			parser->default_priority, UCL_DUPLICATE_APPEND,
			UCL_PARSE_UCL);
}


bool
ucl_parser_add_fd_full (struct ucl_parser *parser, int fd,
		unsigned priority, enum ucl_duplicate_strategy strat,
		enum ucl_parse_type parse_type)
{
	unsigned char *buf;
	size_t len;
	bool ret;
	struct stat st;

	if (fstat (fd, &st) == -1) {
		ucl_create_err (&parser->err, "cannot stat fd %d: %s",
			fd, strerror (errno));
		return false;
	}
	if (st.st_size == 0) {
		return true;
	}
	if ((buf = ucl_mmap (NULL, st.st_size, PROT_READ, MAP_SHARED, fd, 0)) == MAP_FAILED) {
		ucl_create_err (&parser->err, "cannot mmap fd %d: %s",
			fd, strerror (errno));
		return false;
	}

	if (parser->cur_file) {
		free (parser->cur_file);
	}
	parser->cur_file = NULL;
	len = st.st_size;
	ret = ucl_parser_add_chunk_full (parser, buf, len, priority, strat,
			parse_type);

	if (len > 0) {
		ucl_munmap (buf, len);
	}

	return ret;
}

bool
ucl_parser_add_fd_priority (struct ucl_parser *parser, int fd,
		unsigned priority)
{
	if (parser == NULL) {
		return false;
	}

	return ucl_parser_add_fd_full(parser, fd, parser->default_priority,
			UCL_DUPLICATE_APPEND, UCL_PARSE_UCL);
}

bool
ucl_parser_add_fd (struct ucl_parser *parser, int fd)
{
	if (parser == NULL) {
		return false;
	}

	return ucl_parser_add_fd_priority(parser, fd, parser->default_priority);
}

size_t
ucl_strlcpy (char *dst, const char *src, size_t siz)
{
	char *d = dst;
	const char *s = src;
	size_t n = siz;

	/* Copy as many bytes as will fit */
	if (n != 0) {
		while (--n != 0) {
			if ((*d++ = *s++) == '\0') {
				break;
			}
		}
	}

	if (n == 0 && siz != 0) {
		*d = '\0';
	}

	return (s - src - 1);    /* count does not include NUL */
}

size_t
ucl_strlcpy_unsafe (char *dst, const char *src, size_t siz)
{
	memcpy (dst, src, siz - 1);
	dst[siz - 1] = '\0';

	return siz - 1;
}

size_t
ucl_strlcpy_tolower (char *dst, const char *src, size_t siz)
{
	char *d = dst;
	const char *s = src;
	size_t n = siz;

	/* Copy as many bytes as will fit */
	if (n != 0) {
		while (--n != 0) {
			if ((*d++ = tolower (*s++)) == '\0') {
				break;
			}
		}
	}

	if (n == 0 && siz != 0) {
		*d = '\0';
	}

	return (s - src);    /* count does not include NUL */
}

/*
 * Find the first occurrence of find in s
 */
char *
ucl_strnstr (const char *s, const char *find, int len)
{
	char c, sc;
	int mlen;

	if ((c = *find++) != 0) {
		mlen = strlen (find);
		do {
			do {
				if ((sc = *s++) == 0 || len-- == 0)
					return (NULL);
			} while (sc != c);
		} while (strncmp (s, find, mlen) != 0);
		s--;
	}
	return ((char *)s);
}

/*
 * Find the first occurrence of find in s, ignore case.
 */
char *
ucl_strncasestr (const char *s, const char *find, int len)
{
	char c, sc;
	int mlen;

	if ((c = *find++) != 0) {
		c = tolower (c);
		mlen = strlen (find);
		do {
			do {
				if ((sc = *s++) == 0 || len-- == 0)
					return (NULL);
			} while (tolower (sc) != c);
		} while (strncasecmp (s, find, mlen) != 0);
		s--;
	}
	return ((char *)s);
}

ucl_object_t *
ucl_object_fromstring_common (const char *str, size_t len, enum ucl_string_flags flags)
{
	ucl_object_t *obj;
	const char *start, *end, *p, *pos;
	char *dst, *d;
	size_t escaped_len;

	if (str == NULL) {
		return NULL;
	}

	obj = ucl_object_new ();
	if (obj) {
		if (len == 0) {
			len = strlen (str);
		}
		if (flags & UCL_STRING_TRIM) {
			/* Skip leading spaces */
			for (start = str; (size_t)(start - str) < len; start ++) {
				if (!ucl_test_character (*start, UCL_CHARACTER_WHITESPACE_UNSAFE)) {
					break;
				}
			}
			/* Skip trailing spaces */
			for (end = str + len - 1; end > start; end --) {
				if (!ucl_test_character (*end, UCL_CHARACTER_WHITESPACE_UNSAFE)) {
					break;
				}
			}
			end ++;
		}
		else {
			start = str;
			end = str + len;
		}

		obj->type = UCL_STRING;
		if (flags & UCL_STRING_ESCAPE) {
			for (p = start, escaped_len = 0; p < end; p ++, escaped_len ++) {
				if (ucl_test_character (*p, UCL_CHARACTER_JSON_UNSAFE)) {
					escaped_len ++;
				}
			}
			dst = malloc (escaped_len + 1);
			if (dst != NULL) {
				for (p = start, d = dst; p < end; p ++, d ++) {
					if (ucl_test_character (*p, UCL_CHARACTER_JSON_UNSAFE)) {
						switch (*p) {
						case '\n':
							*d++ = '\\';
							*d = 'n';
							break;
						case '\r':
							*d++ = '\\';
							*d = 'r';
							break;
						case '\b':
							*d++ = '\\';
							*d = 'b';
							break;
						case '\t':
							*d++ = '\\';
							*d = 't';
							break;
						case '\f':
							*d++ = '\\';
							*d = 'f';
							break;
						case '\\':
							*d++ = '\\';
							*d = '\\';
							break;
						case '"':
							*d++ = '\\';
							*d = '"';
							break;
						}
					}
					else {
						*d = *p;
					}
				}
				*d = '\0';
				obj->value.sv = dst;
				obj->trash_stack[UCL_TRASH_VALUE] = dst;
				obj->len = escaped_len;
			}
		}
		else {
			dst = malloc (end - start + 1);
			if (dst != NULL) {
				ucl_strlcpy_unsafe (dst, start, end - start + 1);
				obj->value.sv = dst;
				obj->trash_stack[UCL_TRASH_VALUE] = dst;
				obj->len = end - start;
			}
		}
		if ((flags & UCL_STRING_PARSE) && dst != NULL) {
			/* Parse what we have */
			if (flags & UCL_STRING_PARSE_BOOLEAN) {
				if (!ucl_maybe_parse_boolean (obj, dst, obj->len) && (flags & UCL_STRING_PARSE_NUMBER)) {
					ucl_maybe_parse_number (obj, dst, dst + obj->len, &pos,
							flags & UCL_STRING_PARSE_DOUBLE,
							flags & UCL_STRING_PARSE_BYTES,
							flags & UCL_STRING_PARSE_TIME);
				}
			}
			else {
				ucl_maybe_parse_number (obj, dst, dst + obj->len, &pos,
						flags & UCL_STRING_PARSE_DOUBLE,
						flags & UCL_STRING_PARSE_BYTES,
						flags & UCL_STRING_PARSE_TIME);
			}
		}
	}

	return obj;
}

static bool
ucl_object_insert_key_common (ucl_object_t *top, ucl_object_t *elt,
		const char *key, size_t keylen, bool copy_key, bool merge, bool replace)
{
	ucl_object_t *found, *tmp;
	const ucl_object_t *cur;
	ucl_object_iter_t it = NULL;
	const char *p;
	int ret = true;

	if (elt == NULL || key == NULL) {
		return false;
	}

	if (top == NULL) {
		return false;
	}

	if (top->type != UCL_OBJECT) {
		/* It is possible to convert NULL type to an object */
		if (top->type == UCL_NULL) {
			top->type = UCL_OBJECT;
		}
		else {
			/* Refuse converting of other object types */
			return false;
		}
	}

	if (top->value.ov == NULL) {
		top->value.ov = ucl_hash_create (false);
	}

	if (keylen == 0) {
		keylen = strlen (key);
	}

	for (p = key; p < key + keylen; p ++) {
		if (ucl_test_character (*p, UCL_CHARACTER_UCL_UNSAFE)) {
			elt->flags |= UCL_OBJECT_NEED_KEY_ESCAPE;
			break;
		}
	}

	/* workaround for some use cases */
	if (elt->trash_stack[UCL_TRASH_KEY] != NULL &&
			key != (const char *)elt->trash_stack[UCL_TRASH_KEY]) {
		/* Remove copied key */
		free (elt->trash_stack[UCL_TRASH_KEY]);
		elt->trash_stack[UCL_TRASH_KEY] = NULL;
		elt->flags &= ~UCL_OBJECT_ALLOCATED_KEY;
	}

	elt->key = key;
	elt->keylen = keylen;

	if (copy_key) {
		ucl_copy_key_trash (elt);
	}

	found = __DECONST (ucl_object_t *, ucl_hash_search_obj (top->value.ov, elt));

	if (found == NULL) {
		top->value.ov = ucl_hash_insert_object (top->value.ov, elt, false);
		top->len ++;
		if (replace) {
			ret = false;
		}
	}
	else {
		if (replace) {
			ucl_hash_replace (top->value.ov, found, elt);
			ucl_object_unref (found);
		}
		else if (merge) {
			if (found->type != UCL_OBJECT && elt->type == UCL_OBJECT) {
				/* Insert old elt to new one */
				ucl_object_insert_key_common (elt, found, found->key,
						found->keylen, copy_key, false, false);
				ucl_hash_delete (top->value.ov, found);
				top->value.ov = ucl_hash_insert_object (top->value.ov, elt, false);
			}
			else if (found->type == UCL_OBJECT && elt->type != UCL_OBJECT) {
				/* Insert new to old */
				ucl_object_insert_key_common (found, elt, elt->key,
						elt->keylen, copy_key, false, false);
			}
			else if (found->type == UCL_OBJECT && elt->type == UCL_OBJECT) {
				/* Mix two hashes */
				while ((cur = ucl_object_iterate (elt, &it, true)) != NULL) {
					tmp = ucl_object_ref (cur);
					ucl_object_insert_key_common (found, tmp, cur->key,
							cur->keylen, copy_key, false, false);
				}
				ucl_object_unref (elt);
			}
			else {
				/* Just make a list of scalars */
				DL_APPEND (found, elt);
			}
		}
		else {
			DL_APPEND (found, elt);
		}
	}

	return ret;
}

bool
ucl_object_delete_keyl (ucl_object_t *top, const char *key, size_t keylen)
{
	ucl_object_t *found;

	if (top == NULL || key == NULL) {
		return false;
	}

	found = __DECONST (ucl_object_t *, ucl_object_lookup_len (top, key, keylen));

	if (found == NULL) {
		return false;
	}

	ucl_hash_delete (top->value.ov, found);
	ucl_object_unref (found);
	top->len --;

	return true;
}

bool
ucl_object_delete_key (ucl_object_t *top, const char *key)
{
	return ucl_object_delete_keyl (top, key, strlen (key));
}

ucl_object_t*
ucl_object_pop_keyl (ucl_object_t *top, const char *key, size_t keylen)
{
	const ucl_object_t *found;

	if (top == NULL || key == NULL) {
		return false;
	}
	found = ucl_object_lookup_len (top, key, keylen);

	if (found == NULL) {
		return NULL;
	}
	ucl_hash_delete (top->value.ov, found);
	top->len --;

	return __DECONST (ucl_object_t *, found);
}

ucl_object_t*
ucl_object_pop_key (ucl_object_t *top, const char *key)
{
	return ucl_object_pop_keyl (top, key, strlen (key));
}

bool
ucl_object_insert_key (ucl_object_t *top, ucl_object_t *elt,
		const char *key, size_t keylen, bool copy_key)
{
	return ucl_object_insert_key_common (top, elt, key, keylen, copy_key, false, false);
}

bool
ucl_object_insert_key_merged (ucl_object_t *top, ucl_object_t *elt,
		const char *key, size_t keylen, bool copy_key)
{
	return ucl_object_insert_key_common (top, elt, key, keylen, copy_key, true, false);
}

bool
ucl_object_replace_key (ucl_object_t *top, ucl_object_t *elt,
		const char *key, size_t keylen, bool copy_key)
{
	return ucl_object_insert_key_common (top, elt, key, keylen, copy_key, false, true);
}

bool
ucl_object_merge (ucl_object_t *top, ucl_object_t *elt, bool copy)
{
	ucl_object_t *cur = NULL, *cp = NULL, *found = NULL;
	ucl_object_iter_t iter = NULL;

	if (top == NULL || top->type != UCL_OBJECT || elt == NULL || elt->type != UCL_OBJECT) {
		return false;
	}

	/* Mix two hashes */
	while ((cur = (ucl_object_t*)ucl_hash_iterate (elt->value.ov, &iter))) {
		if (copy) {
			cp = ucl_object_copy (cur);
		}
		else {
			cp = ucl_object_ref (cur);
		}
		found = __DECONST(ucl_object_t *, ucl_hash_search (top->value.ov, cp->key, cp->keylen));
		if (found == NULL) {
			/* The key does not exist */
			top->value.ov = ucl_hash_insert_object (top->value.ov, cp, false);
			top->len ++;
		}
		else {
			/* The key already exists, replace it */
			ucl_hash_replace (top->value.ov, found, cp);
			ucl_object_unref (found);
		}
	}

	return true;
}

const ucl_object_t *
ucl_object_lookup_len (const ucl_object_t *obj, const char *key, size_t klen)
{
	const ucl_object_t *ret;
	ucl_object_t srch;

	if (obj == NULL || obj->type != UCL_OBJECT || key == NULL) {
		return NULL;
	}

	srch.key = key;
	srch.keylen = klen;
	ret = ucl_hash_search_obj (obj->value.ov, &srch);

	return ret;
}

const ucl_object_t *
ucl_object_lookup (const ucl_object_t *obj, const char *key)
{
	if (key == NULL) {
		return NULL;
	}

	return ucl_object_lookup_len (obj, key, strlen (key));
}

const ucl_object_t*
ucl_object_lookup_any (const ucl_object_t *obj,
		const char *key, ...)
{
	va_list ap;
	const ucl_object_t *ret = NULL;
	const char *nk = NULL;

	if (obj == NULL || key == NULL) {
		return NULL;
	}

	ret = ucl_object_lookup_len (obj, key, strlen (key));

	if (ret == NULL) {
		va_start (ap, key);

		while (ret == NULL) {
			nk = va_arg (ap, const char *);

			if (nk == NULL) {
				break;
			}
			else {
				ret = ucl_object_lookup_len (obj, nk, strlen (nk));
			}
		}

		va_end (ap);
	}

	return ret;
}

const ucl_object_t*
ucl_object_iterate (const ucl_object_t *obj, ucl_object_iter_t *iter, bool expand_values)
{
	const ucl_object_t *elt = NULL;

	if (obj == NULL || iter == NULL) {
		return NULL;
	}

	if (expand_values) {
		switch (obj->type) {
		case UCL_OBJECT:
			return (const ucl_object_t*)ucl_hash_iterate (obj->value.ov, iter);
			break;
		case UCL_ARRAY: {
			unsigned int idx;
			UCL_ARRAY_GET (vec, obj);
			idx = (unsigned int)(uintptr_t)(*iter);

			if (vec != NULL) {
				while (idx < kv_size (*vec)) {
					if ((elt = kv_A (*vec, idx)) != NULL) {
						idx ++;
						break;
					}
					idx ++;
				}
				*iter = (void *)(uintptr_t)idx;
			}

			return elt;
			break;
		}
		default:
			/* Go to linear iteration */
			break;
		}
	}
	/* Treat everything as a linear list */
	elt = *iter;
	if (elt == NULL) {
		elt = obj;
	}
	else if (elt == obj) {
		return NULL;
	}
	*iter = __DECONST (void *, elt->next ? elt->next : obj);
	return elt;

	/* Not reached */
	return NULL;
}

const char safe_iter_magic[4] = {'u', 'i', 't', 'e'};
struct ucl_object_safe_iter {
	char magic[4]; /* safety check */
	const ucl_object_t *impl_it; /* implicit object iteration */
	ucl_object_iter_t expl_it; /* explicit iteration */
};

#define UCL_SAFE_ITER(ptr) (struct ucl_object_safe_iter *)(ptr)
#define UCL_SAFE_ITER_CHECK(it) do { \
	assert (it != NULL); \
	assert (memcmp (it->magic, safe_iter_magic, sizeof (it->magic)) == 0); \
 } while (0)

ucl_object_iter_t
ucl_object_iterate_new (const ucl_object_t *obj)
{
	struct ucl_object_safe_iter *it;

	it = UCL_ALLOC (sizeof (*it));
	if (it != NULL) {
		memcpy (it->magic, safe_iter_magic, sizeof (it->magic));
		it->expl_it = NULL;
		it->impl_it = obj;
	}

	return (ucl_object_iter_t)it;
}


ucl_object_iter_t
ucl_object_iterate_reset (ucl_object_iter_t it, const ucl_object_t *obj)
{
	struct ucl_object_safe_iter *rit = UCL_SAFE_ITER (it);

	UCL_SAFE_ITER_CHECK (rit);

	if (rit->expl_it != NULL) {
		UCL_FREE (sizeof (*rit->expl_it), rit->expl_it);
	}

	rit->impl_it = obj;
	rit->expl_it = NULL;

	return it;
}

const ucl_object_t*
ucl_object_iterate_safe (ucl_object_iter_t it, bool expand_values)
{
	return ucl_object_iterate_full (it, expand_values ? UCL_ITERATE_BOTH :
			UCL_ITERATE_IMPLICIT);
}

const ucl_object_t*
ucl_object_iterate_full (ucl_object_iter_t it, enum ucl_iterate_type type)
{
	struct ucl_object_safe_iter *rit = UCL_SAFE_ITER (it);
	const ucl_object_t *ret = NULL;

	UCL_SAFE_ITER_CHECK (rit);

	if (rit->impl_it == NULL) {
		return NULL;
	}

	if (rit->impl_it->type == UCL_OBJECT || rit->impl_it->type == UCL_ARRAY) {
		ret = ucl_object_iterate (rit->impl_it, &rit->expl_it, true);

		if (ret == NULL && (type & UCL_ITERATE_IMPLICIT)) {
			/* Need to switch to another implicit object in chain */
			rit->impl_it = rit->impl_it->next;
			rit->expl_it = NULL;

			return ucl_object_iterate_safe (it, type);
		}
	}
	else {
		/* Just iterate over the implicit array */
		ret = rit->impl_it;
		rit->impl_it = rit->impl_it->next;

		if (type & UCL_ITERATE_EXPLICIT) {
			/* We flatten objects if need to expand values */
			if (ret->type == UCL_OBJECT || ret->type == UCL_ARRAY) {
				return ucl_object_iterate_safe (it, type);
			}
		}
	}

	return ret;
}

void
ucl_object_iterate_free (ucl_object_iter_t it)
{
	struct ucl_object_safe_iter *rit = UCL_SAFE_ITER (it);

	UCL_SAFE_ITER_CHECK (rit);

	if (rit->expl_it != NULL) {
		UCL_FREE (sizeof (*rit->expl_it), rit->expl_it);
	}

	UCL_FREE (sizeof (*rit), it);
}

const ucl_object_t *
ucl_object_lookup_path (const ucl_object_t *top, const char *path_in) {
	return ucl_object_lookup_path_char (top, path_in, '.');
}


const ucl_object_t *
ucl_object_lookup_path_char (const ucl_object_t *top, const char *path_in, const char sep) {
	const ucl_object_t *o = NULL, *found;
	const char *p, *c;
	char *err_str;
	unsigned index;

	if (path_in == NULL || top == NULL) {
		return NULL;
	}

	found = NULL;
	p = path_in;

	/* Skip leading dots */
	while (*p == sep) {
		p ++;
	}

	c = p;
	while (*p != '\0') {
		p ++;
		if (*p == sep || *p == '\0') {
			if (p > c) {
				switch (top->type) {
				case UCL_ARRAY:
					/* Key should be an int */
					index = strtoul (c, &err_str, 10);
					if (err_str != NULL && (*err_str != sep && *err_str != '\0')) {
						return NULL;
					}
					o = ucl_array_find_index (top, index);
					break;
				default:
					o = ucl_object_lookup_len (top, c, p - c);
					break;
				}
				if (o == NULL) {
					return NULL;
				}
				top = o;
			}
			if (*p != '\0') {
				c = p + 1;
			}
		}
	}
	found = o;

	return found;
}


ucl_object_t *
ucl_object_new (void)
{
	return ucl_object_typed_new (UCL_NULL);
}

ucl_object_t *
ucl_object_typed_new (ucl_type_t type)
{
	return ucl_object_new_full (type, 0);
}

ucl_object_t *
ucl_object_new_full (ucl_type_t type, unsigned priority)
{
	ucl_object_t *new;

	if (type != UCL_USERDATA) {
		new = UCL_ALLOC (sizeof (ucl_object_t));
		if (new != NULL) {
			memset (new, 0, sizeof (ucl_object_t));
			new->ref = 1;
			new->type = (type <= UCL_NULL ? type : UCL_NULL);
			new->next = NULL;
			new->prev = new;
			ucl_object_set_priority (new, priority);

			if (type == UCL_ARRAY) {
				new->value.av = UCL_ALLOC (sizeof (ucl_array_t));
				if (new->value.av) {
					memset (new->value.av, 0, sizeof (ucl_array_t));
					UCL_ARRAY_GET (vec, new);

					/* Preallocate some space for arrays */
					kv_resize (ucl_object_t *, *vec, 8);
				}
			}
		}
	}
	else {
		new = ucl_object_new_userdata (NULL, NULL, NULL);
		ucl_object_set_priority (new, priority);
	}

	return new;
}

ucl_object_t*
ucl_object_new_userdata (ucl_userdata_dtor dtor,
		ucl_userdata_emitter emitter,
		void *ptr)
{
	struct ucl_object_userdata *new;
	size_t nsize = sizeof (*new);

	new = UCL_ALLOC (nsize);
	if (new != NULL) {
		memset (new, 0, nsize);
		new->obj.ref = 1;
		new->obj.type = UCL_USERDATA;
		new->obj.next = NULL;
		new->obj.prev = (ucl_object_t *)new;
		new->dtor = dtor;
		new->emitter = emitter;
		new->obj.value.ud = ptr;
	}

	return (ucl_object_t *)new;
}

ucl_type_t
ucl_object_type (const ucl_object_t *obj)
{
	if (obj == NULL) {
		return UCL_NULL;
	}

	return obj->type;
}

ucl_object_t*
ucl_object_fromstring (const char *str)
{
	return ucl_object_fromstring_common (str, 0, UCL_STRING_ESCAPE);
}

ucl_object_t *
ucl_object_fromlstring (const char *str, size_t len)
{
	return ucl_object_fromstring_common (str, len, UCL_STRING_ESCAPE);
}

ucl_object_t *
ucl_object_fromint (int64_t iv)
{
	ucl_object_t *obj;

	obj = ucl_object_new ();
	if (obj != NULL) {
		obj->type = UCL_INT;
		obj->value.iv = iv;
	}

	return obj;
}

ucl_object_t *
ucl_object_fromdouble (double dv)
{
	ucl_object_t *obj;

	obj = ucl_object_new ();
	if (obj != NULL) {
		obj->type = UCL_FLOAT;
		obj->value.dv = dv;
	}

	return obj;
}

ucl_object_t*
ucl_object_frombool (bool bv)
{
	ucl_object_t *obj;

	obj = ucl_object_new ();
	if (obj != NULL) {
		obj->type = UCL_BOOLEAN;
		obj->value.iv = bv;
	}

	return obj;
}

bool
ucl_array_append (ucl_object_t *top, ucl_object_t *elt)
{
	UCL_ARRAY_GET (vec, top);

	if (elt == NULL || top == NULL) {
		return false;
	}

	if (vec == NULL) {
		vec = UCL_ALLOC (sizeof (*vec));

		if (vec == NULL) {
			return false;
		}

		kv_init (*vec);
		top->value.av = (void *)vec;
	}

	kv_push (ucl_object_t *, *vec, elt);

	top->len ++;

	return true;
}

bool
ucl_array_prepend (ucl_object_t *top, ucl_object_t *elt)
{
	UCL_ARRAY_GET (vec, top);

	if (elt == NULL || top == NULL) {
		return false;
	}

	if (vec == NULL) {
		vec = UCL_ALLOC (sizeof (*vec));
		kv_init (*vec);
		top->value.av = (void *)vec;
		kv_push (ucl_object_t *, *vec, elt);
	}
	else {
		/* Slow O(n) algorithm */
		kv_prepend (ucl_object_t *, *vec, elt);
	}

	top->len ++;

	return true;
}

bool
ucl_array_merge (ucl_object_t *top, ucl_object_t *elt, bool copy)
{
	unsigned i;
	ucl_object_t *cp = NULL;
	ucl_object_t **obj;

	if (elt == NULL || top == NULL || top->type != UCL_ARRAY || elt->type != UCL_ARRAY) {
		return false;
	}

	if (copy) {
		cp = ucl_object_copy (elt);
	}
	else {
		cp = ucl_object_ref (elt);
	}

	UCL_ARRAY_GET (v1, top);
	UCL_ARRAY_GET (v2, cp);

	if (v1 && v2) {
		kv_concat (ucl_object_t *, *v1, *v2);

		for (i = v2->n; i < v1->n; i ++) {
			obj = &kv_A (*v1, i);
			if (*obj == NULL) {
				continue;
			}
			top->len ++;
		}
	}

	return true;
}

ucl_object_t *
ucl_array_delete (ucl_object_t *top, ucl_object_t *elt)
{
	UCL_ARRAY_GET (vec, top);
	ucl_object_t *ret = NULL;
	unsigned i;

	if (vec == NULL) {
		return NULL;
	}

	for (i = 0; i < vec->n; i ++) {
		if (kv_A (*vec, i) == elt) {
			kv_del (ucl_object_t *, *vec, i);
			ret = elt;
			top->len --;
			break;
		}
	}

	return ret;
}

const ucl_object_t *
ucl_array_head (const ucl_object_t *top)
{
	UCL_ARRAY_GET (vec, top);

	if (vec == NULL || top == NULL || top->type != UCL_ARRAY ||
			top->value.av == NULL) {
		return NULL;
	}

	return (vec->n > 0 ? vec->a[0] : NULL);
}

const ucl_object_t *
ucl_array_tail (const ucl_object_t *top)
{
	UCL_ARRAY_GET (vec, top);

	if (top == NULL || top->type != UCL_ARRAY || top->value.av == NULL) {
		return NULL;
	}

	return (vec->n > 0 ? vec->a[vec->n - 1] : NULL);
}

ucl_object_t *
ucl_array_pop_last (ucl_object_t *top)
{
	UCL_ARRAY_GET (vec, top);
	ucl_object_t **obj, *ret = NULL;

	if (vec != NULL && vec->n > 0) {
		obj = &kv_A (*vec, vec->n - 1);
		ret = *obj;
		kv_del (ucl_object_t *, *vec, vec->n - 1);
		top->len --;
	}

	return ret;
}

ucl_object_t *
ucl_array_pop_first (ucl_object_t *top)
{
	UCL_ARRAY_GET (vec, top);
	ucl_object_t **obj, *ret = NULL;

	if (vec != NULL && vec->n > 0) {
		obj = &kv_A (*vec, 0);
		ret = *obj;
		kv_del (ucl_object_t *, *vec, 0);
		top->len --;
	}

	return ret;
}

const ucl_object_t *
ucl_array_find_index (const ucl_object_t *top, unsigned int index)
{
	UCL_ARRAY_GET (vec, top);

	if (vec != NULL && vec->n > 0 && index < vec->n) {
		return kv_A (*vec, index);
	}

	return NULL;
}

unsigned int
ucl_array_index_of (ucl_object_t *top, ucl_object_t *elt)
{
	UCL_ARRAY_GET (vec, top);
	unsigned i;

	if (vec == NULL) {
		return (unsigned int)(-1);
	}

	for (i = 0; i < vec->n; i ++) {
		if (kv_A (*vec, i) == elt) {
			return i;
		}
	}

	return (unsigned int)(-1);
}

ucl_object_t *
ucl_array_replace_index (ucl_object_t *top, ucl_object_t *elt,
	unsigned int index)
{
	UCL_ARRAY_GET (vec, top);
	ucl_object_t *ret = NULL;

	if (vec != NULL && vec->n > 0 && index < vec->n) {
		ret = kv_A (*vec, index);
		kv_A (*vec, index) = elt;
	}

	return ret;
}

ucl_object_t *
ucl_elt_append (ucl_object_t *head, ucl_object_t *elt)
{

	if (head == NULL) {
		elt->next = NULL;
		elt->prev = elt;
		head = elt;
	}
	else {
		elt->prev = head->prev;
		head->prev->next = elt;
		head->prev = elt;
		elt->next = NULL;
	}

	return head;
}

bool
ucl_object_todouble_safe (const ucl_object_t *obj, double *target)
{
	if (obj == NULL || target == NULL) {
		return false;
	}
	switch (obj->type) {
	case UCL_INT:
		*target = obj->value.iv; /* Probaly could cause overflow */
		break;
	case UCL_FLOAT:
	case UCL_TIME:
		*target = obj->value.dv;
		break;
	default:
		return false;
	}

	return true;
}

double
ucl_object_todouble (const ucl_object_t *obj)
{
	double result = 0.;

	ucl_object_todouble_safe (obj, &result);
	return result;
}

bool
ucl_object_toint_safe (const ucl_object_t *obj, int64_t *target)
{
	if (obj == NULL || target == NULL) {
		return false;
	}
	switch (obj->type) {
	case UCL_INT:
		*target = obj->value.iv;
		break;
	case UCL_FLOAT:
	case UCL_TIME:
		*target = obj->value.dv; /* Loosing of decimal points */
		break;
	default:
		return false;
	}

	return true;
}

int64_t
ucl_object_toint (const ucl_object_t *obj)
{
	int64_t result = 0;

	ucl_object_toint_safe (obj, &result);
	return result;
}

bool
ucl_object_toboolean_safe (const ucl_object_t *obj, bool *target)
{
	if (obj == NULL || target == NULL) {
		return false;
	}
	switch (obj->type) {
	case UCL_BOOLEAN:
		*target = (obj->value.iv == true);
		break;
	default:
		return false;
	}

	return true;
}

bool
ucl_object_toboolean (const ucl_object_t *obj)
{
	bool result = false;

	ucl_object_toboolean_safe (obj, &result);
	return result;
}

bool
ucl_object_tostring_safe (const ucl_object_t *obj, const char **target)
{
	if (obj == NULL || target == NULL) {
		return false;
	}

	switch (obj->type) {
	case UCL_STRING:
		if (!(obj->flags & UCL_OBJECT_BINARY)) {
			*target = ucl_copy_value_trash (obj);
		}
		break;
	default:
		return false;
	}

	return true;
}

const char *
ucl_object_tostring (const ucl_object_t *obj)
{
	const char *result = NULL;

	ucl_object_tostring_safe (obj, &result);
	return result;
}

const char *
ucl_object_tostring_forced (const ucl_object_t *obj)
{
	/* TODO: For binary strings we might encode string here */
	if (!(obj->flags & UCL_OBJECT_BINARY)) {
		return ucl_copy_value_trash (obj);
	}

	return NULL;
}

bool
ucl_object_tolstring_safe (const ucl_object_t *obj, const char **target, size_t *tlen)
{
	if (obj == NULL || target == NULL) {
		return false;
	}
	switch (obj->type) {
	case UCL_STRING:
		*target = obj->value.sv;
		if (tlen != NULL) {
			*tlen = obj->len;
		}
		break;
	default:
		return false;
	}

	return true;
}

const char *
ucl_object_tolstring (const ucl_object_t *obj, size_t *tlen)
{
	const char *result = NULL;

	ucl_object_tolstring_safe (obj, &result, tlen);
	return result;
}

const char *
ucl_object_key (const ucl_object_t *obj)
{
	return ucl_copy_key_trash (obj);
}

const char *
ucl_object_keyl (const ucl_object_t *obj, size_t *len)
{
	if (len == NULL || obj == NULL) {
		return NULL;
	}
	*len = obj->keylen;
	return obj->key;
}

ucl_object_t *
ucl_object_ref (const ucl_object_t *obj)
{
	ucl_object_t *res = NULL;

	if (obj != NULL) {
		if (obj->flags & UCL_OBJECT_EPHEMERAL) {
			/*
			 * Use deep copy for ephemeral objects, note that its refcount
			 * is NOT increased, since ephemeral objects does not need refcount
			 * at all
			 */
			res = ucl_object_copy (obj);
		}
		else {
			res = __DECONST (ucl_object_t *, obj);
#ifdef HAVE_ATOMIC_BUILTINS
			(void)__sync_add_and_fetch (&res->ref, 1);
#else
			res->ref ++;
#endif
		}
	}
	return res;
}

static ucl_object_t *
ucl_object_copy_internal (const ucl_object_t *other, bool allow_array)
{

	ucl_object_t *new;
	ucl_object_iter_t it = NULL;
	const ucl_object_t *cur;

	new = malloc (sizeof (*new));

	if (new != NULL) {
		memcpy (new, other, sizeof (*new));
		if (other->flags & UCL_OBJECT_EPHEMERAL) {
			/* Copied object is always non ephemeral */
			new->flags &= ~UCL_OBJECT_EPHEMERAL;
		}
		new->ref = 1;
		/* Unlink from others */
		new->next = NULL;
		new->prev = new;

		/* deep copy of values stored */
		if (other->trash_stack[UCL_TRASH_KEY] != NULL) {
			new->trash_stack[UCL_TRASH_KEY] =
					strdup (other->trash_stack[UCL_TRASH_KEY]);
			if (other->key == (const char *)other->trash_stack[UCL_TRASH_KEY]) {
				new->key = new->trash_stack[UCL_TRASH_KEY];
			}
		}
		if (other->trash_stack[UCL_TRASH_VALUE] != NULL) {
			new->trash_stack[UCL_TRASH_VALUE] =
					strdup (other->trash_stack[UCL_TRASH_VALUE]);
			if (new->type == UCL_STRING) {
				new->value.sv = new->trash_stack[UCL_TRASH_VALUE];
			}
		}

		if (other->type == UCL_ARRAY || other->type == UCL_OBJECT) {
			/* reset old value */
			memset (&new->value, 0, sizeof (new->value));

			while ((cur = ucl_object_iterate (other, &it, true)) != NULL) {
				if (other->type == UCL_ARRAY) {
					ucl_array_append (new, ucl_object_copy_internal (cur, false));
				}
				else {
					ucl_object_t *cp = ucl_object_copy_internal (cur, true);
					if (cp != NULL) {
						ucl_object_insert_key (new, cp, cp->key, cp->keylen,
								false);
					}
				}
			}
		}
		else if (allow_array && other->next != NULL) {
			LL_FOREACH (other->next, cur) {
				ucl_object_t *cp = ucl_object_copy_internal (cur, false);
				if (cp != NULL) {
					DL_APPEND (new, cp);
				}
			}
		}
	}

	return new;
}

ucl_object_t *
ucl_object_copy (const ucl_object_t *other)
{
	return ucl_object_copy_internal (other, true);
}

void
ucl_object_unref (ucl_object_t *obj)
{
	if (obj != NULL) {
#ifdef HAVE_ATOMIC_BUILTINS
		unsigned int rc = __sync_sub_and_fetch (&obj->ref, 1);
		if (rc == 0) {
#else
		if (--obj->ref == 0) {
#endif
			ucl_object_free_internal (obj, true, ucl_object_dtor_unref);
		}
	}
}

int
ucl_object_compare (const ucl_object_t *o1, const ucl_object_t *o2)
{
	const ucl_object_t *it1, *it2;
	ucl_object_iter_t iter = NULL;
	int ret = 0;

	if (o1->type != o2->type) {
		return (o1->type) - (o2->type);
	}

	switch (o1->type) {
	case UCL_STRING:
		if (o1->len == o2->len && o1->len > 0) {
			ret = strcmp (ucl_object_tostring(o1), ucl_object_tostring(o2));
		}
		else {
			ret = o1->len - o2->len;
		}
		break;
	case UCL_FLOAT:
	case UCL_INT:
	case UCL_TIME:
		ret = ucl_object_todouble (o1) - ucl_object_todouble (o2);
		break;
	case UCL_BOOLEAN:
		ret = ucl_object_toboolean (o1) - ucl_object_toboolean (o2);
		break;
	case UCL_ARRAY:
		if (o1->len == o2->len && o1->len > 0) {
			UCL_ARRAY_GET (vec1, o1);
			UCL_ARRAY_GET (vec2, o2);
			unsigned i;

			/* Compare all elements in both arrays */
			for (i = 0; i < vec1->n; i ++) {
				it1 = kv_A (*vec1, i);
				it2 = kv_A (*vec2, i);

				if (it1 == NULL && it2 != NULL) {
					return -1;
				}
				else if (it2 == NULL && it1 != NULL) {
					return 1;
				}
				else if (it1 != NULL && it2 != NULL) {
					ret = ucl_object_compare (it1, it2);
					if (ret != 0) {
						break;
					}
				}
			}
		}
		else {
			ret = o1->len - o2->len;
		}
		break;
	case UCL_OBJECT:
		if (o1->len == o2->len && o1->len > 0) {
			while ((it1 = ucl_object_iterate (o1, &iter, true)) != NULL) {
				it2 = ucl_object_lookup (o2, ucl_object_key (it1));
				if (it2 == NULL) {
					ret = 1;
					break;
				}
				ret = ucl_object_compare (it1, it2);
				if (ret != 0) {
					break;
				}
			}
		}
		else {
			ret = o1->len - o2->len;
		}
		break;
	default:
		ret = 0;
		break;
	}

	return ret;
}

int
ucl_object_compare_qsort (const ucl_object_t **o1,
		const ucl_object_t **o2)
{
	return ucl_object_compare (*o1, *o2);
}

void
ucl_object_array_sort (ucl_object_t *ar,
		int (*cmp)(const ucl_object_t **o1, const ucl_object_t **o2))
{
	UCL_ARRAY_GET (vec, ar);

	if (cmp == NULL || ar == NULL || ar->type != UCL_ARRAY) {
		return;
	}

	qsort (vec->a, vec->n, sizeof (ucl_object_t *),
			(int (*)(const void *, const void *))cmp);
}

#define PRIOBITS 4

unsigned int
ucl_object_get_priority (const ucl_object_t *obj)
{
	if (obj == NULL) {
		return 0;
	}

	return (obj->flags >> ((sizeof (obj->flags) * NBBY) - PRIOBITS));
}

void
ucl_object_set_priority (ucl_object_t *obj,
		unsigned int priority)
{
	if (obj != NULL) {
		priority &= (0x1 << PRIOBITS) - 1;
		priority <<= ((sizeof (obj->flags) * NBBY) - PRIOBITS);
		priority |= obj->flags & ((1 << ((sizeof (obj->flags) * NBBY) -
				PRIOBITS)) - 1);
		obj->flags = priority;
	}
}

bool
ucl_object_string_to_type (const char *input, ucl_type_t *res)
{
	if (strcasecmp (input, "object") == 0) {
		*res = UCL_OBJECT;
	}
	else if (strcasecmp (input, "array") == 0) {
		*res = UCL_ARRAY;
	}
	else if (strcasecmp (input, "integer") == 0) {
		*res = UCL_INT;
	}
	else if (strcasecmp (input, "number") == 0) {
		*res = UCL_FLOAT;
	}
	else if (strcasecmp (input, "string") == 0) {
		*res = UCL_STRING;
	}
	else if (strcasecmp (input, "boolean") == 0) {
		*res = UCL_BOOLEAN;
	}
	else if (strcasecmp (input, "null") == 0) {
		*res = UCL_NULL;
	}
	else if (strcasecmp (input, "userdata") == 0) {
		*res = UCL_USERDATA;
	}
	else {
		return false;
	}

	return true;
}

const char *
ucl_object_type_to_string (ucl_type_t type)
{
	const char *res = "unknown";

	switch (type) {
	case UCL_OBJECT:
		res = "object";
		break;
	case UCL_ARRAY:
		res = "array";
		break;
	case UCL_INT:
		res = "integer";
		break;
	case UCL_FLOAT:
	case UCL_TIME:
		res = "number";
		break;
	case UCL_STRING:
		res = "string";
		break;
	case UCL_BOOLEAN:
		res = "boolean";
		break;
	case UCL_USERDATA:
		res = "userdata";
		break;
	case UCL_NULL:
		res = "null";
		break;
	}

	return res;
}

const ucl_object_t *
ucl_parser_get_comments (struct ucl_parser *parser)
{
	if (parser && parser->comments) {
		return parser->comments;
	}

	return NULL;
}

const ucl_object_t *
ucl_comments_find (const ucl_object_t *comments,
		const ucl_object_t *srch)
{
	if (comments && srch) {
		return ucl_object_lookup_len (comments, (const char *)&srch,
				sizeof (void *));
	}

	return NULL;
}

bool
ucl_comments_move (ucl_object_t *comments,
		const ucl_object_t *from, const ucl_object_t *to)
{
	const ucl_object_t *found;
	ucl_object_t *obj;

	if (comments && from && to) {
		found = ucl_object_lookup_len (comments,
				(const char *)&from, sizeof (void *));

		if (found) {
			/* Replace key */
			obj = ucl_object_ref (found);
			ucl_object_delete_keyl (comments, (const char *)&from,
					sizeof (void *));
			ucl_object_insert_key (comments, obj, (const char *)&to,
					sizeof (void *), true);

			return true;
		}
	}

	return false;
}

void
ucl_comments_add (ucl_object_t *comments, const ucl_object_t *obj,
		const char *comment)
{
	if (comments && obj && comment) {
		ucl_object_insert_key (comments, ucl_object_fromstring (comment),
				(const char *)&obj, sizeof (void *), true);
	}
}
