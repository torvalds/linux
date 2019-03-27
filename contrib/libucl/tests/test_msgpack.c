/*
 * Copyright (c) 2015, Vsevolod Stakhov
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *	 * Redistributions of source code must retain the above copyright
 *	   notice, this list of conditions and the following disclaimer.
 *	 * Redistributions in binary form must reproduce the above copyright
 *	   notice, this list of conditions and the following disclaimer in the
 *	   documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR ''AS IS'' AND ANY
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
#include <ctype.h>

static const int niter = 20;
static const int ntests = 10;
static const int nelt = 20;

static int recursion = 0;

typedef ucl_object_t* (*ucl_msgpack_test)(void);

static ucl_object_t* ucl_test_integer (void);
static ucl_object_t* ucl_test_string (void);
static ucl_object_t* ucl_test_boolean (void);
static ucl_object_t* ucl_test_map (void);
static ucl_object_t* ucl_test_array (void);
static ucl_object_t* ucl_test_large_map (void);
static ucl_object_t* ucl_test_large_array (void);
static ucl_object_t* ucl_test_large_string (void);
static ucl_object_t* ucl_test_null (void);

ucl_msgpack_test tests[] = {
		ucl_test_integer,
		ucl_test_string,
		ucl_test_boolean,
		ucl_test_map,
		ucl_test_array,
		ucl_test_null
};

#define NTESTS (sizeof(tests) / sizeof(tests[0]))

typedef struct
{
	uint64_t state;
	uint64_t inc;
} pcg32_random_t;

pcg32_random_t rng;

/*
 * From http://www.pcg-random.org/
 */
static uint32_t
pcg32_random (void)
{
	uint64_t oldstate = rng.state;

	rng.state = oldstate * 6364136223846793005ULL + (rng.inc | 1);
	uint32_t xorshifted = ((oldstate >> 18u) ^ oldstate) >> 27u;
	uint32_t rot = oldstate >> 59u;
	return (xorshifted >> rot) | (xorshifted << ((-rot) & 31));
}

static const char *
random_key (size_t *lenptr)
{
	static char keybuf[512];
	int keylen, i;
	char c;

	keylen = pcg32_random () % (sizeof (keybuf) - 1) + 1;

	for (i = 0; i < keylen; i ++) {
		do {
			c = pcg32_random () & 0xFF;
		} while (!isgraph (c));

		keybuf[i] = c;
	}

	*lenptr = keylen;
	return keybuf;
}

int
main (int argc, char **argv)
{
	int fd, i, j;
	uint32_t sel;
	ucl_object_t *obj, *elt;
	struct ucl_parser *parser;
	size_t klen, elen, elen2;
	const char *key;
	unsigned char *emitted, *emitted2;
	FILE *out;
	const char *fname_out = NULL;

	switch (argc) {
	case 2:
		fname_out = argv[1];
		break;
	}

	/* Seed prng */
	fd = open ("/dev/urandom", O_RDONLY);
	assert (fd != -1);
	assert (read (fd, &rng, sizeof (rng)) == sizeof (rng));
	close (fd);

	for (i = 0; i < niter; i ++) {
		if (fname_out != NULL) {
			out = fopen (fname_out, "w");
			if (out == NULL) {
				exit (-errno);
			}
		}
		else {
			out = NULL;
		}

		/* Generate phase */
		obj = ucl_object_typed_new (UCL_OBJECT);

		for (j = 0; j < ntests; j ++) {
			sel = pcg32_random () % NTESTS;

			key = random_key (&klen);
			recursion = 0;
			elt = tests[sel]();
			assert (elt != NULL);
			assert (klen != 0);

			ucl_object_insert_key (obj, elt, key, klen, true);
		}

		key = random_key (&klen);
		elt = ucl_test_large_array ();
		ucl_object_insert_key (obj, elt, key, klen, true);

		key = random_key (&klen);
		elt = ucl_test_large_map ();
		ucl_object_insert_key (obj, elt, key, klen, true);

		key = random_key (&klen);
		elt = ucl_test_large_string ();
		ucl_object_insert_key (obj, elt, key, klen, true);

		emitted = ucl_object_emit_len (obj, UCL_EMIT_MSGPACK, &elen);

		assert (emitted != NULL);

		if (out) {
			fprintf (out, "%*.s\n", (int)elen, emitted);

			fclose (out);
		}
		ucl_object_unref (obj);

		parser = ucl_parser_new (0);

		if (!ucl_parser_add_chunk_full (parser, emitted, elen, 0,
				UCL_DUPLICATE_APPEND, UCL_PARSE_MSGPACK)) {
			fprintf (stderr, "error parsing input: %s",
					ucl_parser_get_error (parser));
			assert (0);
		}

		obj = ucl_parser_get_object (parser);
		assert (obj != NULL);

		emitted2 = ucl_object_emit_len (obj, UCL_EMIT_MSGPACK, &elen2);

		assert (emitted2 != NULL);
		assert (elen2 == elen);
		assert (memcmp (emitted, emitted2, elen) == 0);

		ucl_parser_free (parser);
		ucl_object_unref (obj);
		free (emitted);
		free (emitted2);
	}

	return 0;
}


static ucl_object_t*
ucl_test_integer (void)
{
	ucl_object_t *res;
	int count, i;
	uint64_t cur;
	double curf;

	res = ucl_object_typed_new (UCL_ARRAY);
	count = pcg32_random () % nelt;

	for (i = 0; i < count; i ++) {
		cur = ((uint64_t)pcg32_random ()) << 32 | pcg32_random ();
		ucl_array_append (res, ucl_object_fromint (cur % 128));
		ucl_array_append (res, ucl_object_fromint (-(cur % 128)));
		cur = ((uint64_t)pcg32_random ()) << 32 | pcg32_random ();
		ucl_array_append (res, ucl_object_fromint (cur % UINT16_MAX));
		ucl_array_append (res, ucl_object_fromint (-(cur % INT16_MAX)));
		cur = ((uint64_t)pcg32_random ()) << 32 | pcg32_random ();
		ucl_array_append (res, ucl_object_fromint (cur % UINT32_MAX));
		ucl_array_append (res, ucl_object_fromint (-(cur % INT32_MAX)));
		cur = ((uint64_t)pcg32_random ()) << 32 | pcg32_random ();
		ucl_array_append (res, ucl_object_fromint (cur));
		ucl_array_append (res, ucl_object_fromint (-cur));
		/* Double version */
		cur = ((uint64_t)pcg32_random ()) << 32 | pcg32_random ();
		curf = (cur % 128) / 19 * 16;
		ucl_array_append (res, ucl_object_fromdouble (curf));
		cur = ((uint64_t)pcg32_random ()) << 32 | pcg32_random ();
		curf = -(cur % 128) / 19 * 16;
		ucl_array_append (res, ucl_object_fromdouble (curf));
		cur = ((uint64_t)pcg32_random ()) << 32 | pcg32_random ();
		curf = (cur % 65536) / 19 * 16;
		ucl_array_append (res, ucl_object_fromdouble (curf));
		ucl_array_append (res, ucl_object_fromdouble (-curf));
		cur = ((uint64_t)pcg32_random ()) << 32 | pcg32_random ();
		curf = (cur % INT32_MAX) / 19 * 16;
		ucl_array_append (res, ucl_object_fromdouble (curf));
		cur = ((uint64_t)pcg32_random ()) << 32 | pcg32_random ();
		memcpy (&curf, &cur, sizeof (curf));
		ucl_array_append (res, ucl_object_fromint (cur));
	}

	return res;
}

static ucl_object_t*
ucl_test_string (void)
{
	ucl_object_t *res, *elt;
	int count, i;
	uint32_t cur_len;
	char *str;

	res = ucl_object_typed_new (UCL_ARRAY);
	count = pcg32_random () % nelt;

	for (i = 0; i < count; i ++) {
		while ((cur_len = pcg32_random ()) % 128 == 0);

		str = malloc (cur_len % 128);
		ucl_array_append (res, ucl_object_fromstring_common (str, cur_len % 128,
				UCL_STRING_RAW));
		free (str);

		while ((cur_len = pcg32_random ()) % 512 == 0);
		str = malloc (cur_len % 512);
		ucl_array_append (res, ucl_object_fromstring_common (str, cur_len % 512,
				UCL_STRING_RAW));
		free (str);

		while ((cur_len = pcg32_random ()) % 128 == 0);
		str = malloc (cur_len % 128);
		elt = ucl_object_fromstring_common (str, cur_len % 128,
				UCL_STRING_RAW);
		elt->flags |= UCL_OBJECT_BINARY;
		ucl_array_append (res, elt);
		free (str);

		while ((cur_len = pcg32_random ()) % 512 == 0);
		str = malloc (cur_len % 512);
		elt = ucl_object_fromstring_common (str, cur_len % 512,
				UCL_STRING_RAW);
		elt->flags |= UCL_OBJECT_BINARY;
		ucl_array_append (res, elt);
		free (str);
	}

	/* One large string */
	str = malloc (65537);
	elt = ucl_object_fromstring_common (str, 65537,
			UCL_STRING_RAW);
	elt->flags |= UCL_OBJECT_BINARY;
	ucl_array_append (res, elt);
	free (str);

	return res;
}

static ucl_object_t*
ucl_test_boolean (void)
{
	ucl_object_t *res;
	int count, i;

	res = ucl_object_typed_new (UCL_ARRAY);
	count = pcg32_random () % nelt;

	for (i = 0; i < count; i ++) {
		ucl_array_append (res, ucl_object_frombool (pcg32_random () % 2));
	}

	return res;
}

static ucl_object_t*
ucl_test_map (void)
{
	ucl_object_t *res, *cur;
	int count, i;
	uint32_t cur_len, sel;
	size_t klen;
	const char *key;

	res = ucl_object_typed_new (UCL_OBJECT);
	count = pcg32_random () % nelt;

	recursion ++;

	for (i = 0; i < count; i ++) {

		if (recursion > 10) {
			for (;;) {
				sel = pcg32_random () % NTESTS;
				if (tests[sel] != ucl_test_map &&
						tests[sel] != ucl_test_array) {
					break;
				}
			}
		}
		else {
			sel = pcg32_random () % NTESTS;
		}

		key = random_key (&klen);
		cur = tests[sel]();
		assert (cur != NULL);
		assert (klen != 0);

		ucl_object_insert_key (res, cur, key, klen, true);

		/* Multi value key */
		cur = tests[sel]();
		assert (cur != NULL);

		ucl_object_insert_key (res, cur, key, klen, true);
	}

	return res;
}

static ucl_object_t*
ucl_test_large_map (void)
{
	ucl_object_t *res, *cur;
	int count, i;
	uint32_t cur_len;
	size_t klen;
	const char *key;

	res = ucl_object_typed_new (UCL_OBJECT);
	count = 65537;

	recursion ++;

	for (i = 0; i < count; i ++) {
		key = random_key (&klen);
		cur = ucl_test_boolean ();
		assert (cur != NULL);
		assert (klen != 0);

		ucl_object_insert_key (res, cur, key, klen, true);
	}

	return res;
}

static ucl_object_t*
ucl_test_array (void)
{
	ucl_object_t *res, *cur;
	int count, i;
	uint32_t cur_len, sel;

	res = ucl_object_typed_new (UCL_ARRAY);
	count = pcg32_random () % nelt;

	recursion ++;

	for (i = 0; i < count; i ++) {
		if (recursion > 10) {
			for (;;) {
				sel = pcg32_random () % NTESTS;
				if (tests[sel] != ucl_test_map &&
						tests[sel] != ucl_test_array) {
					break;
				}
			}
		}
		else {
			sel = pcg32_random () % NTESTS;
		}

		cur = tests[sel]();
		assert (cur != NULL);

		ucl_array_append (res, cur);
	}

	return res;
}

static ucl_object_t*
ucl_test_large_array (void)
{
	ucl_object_t *res, *cur;
	int count, i;
	uint32_t cur_len;

	res = ucl_object_typed_new (UCL_ARRAY);
	count = 65537;

	recursion ++;

	for (i = 0; i < count; i ++) {
		cur = ucl_test_boolean ();
		assert (cur != NULL);

		ucl_array_append (res, cur);
	}

	return res;
}

static ucl_object_t*
ucl_test_large_string (void)
{
	ucl_object_t *res;
	char *str;
	uint32_t cur_len;

	while ((cur_len = pcg32_random ()) % 100000 == 0);
	str = malloc (cur_len % 100000);
	res = ucl_object_fromstring_common (str, cur_len % 100000,
				UCL_STRING_RAW);
	res->flags |= UCL_OBJECT_BINARY;

	return res;
}

static ucl_object_t*
ucl_test_null (void)
{
	return ucl_object_typed_new (UCL_NULL);
}
