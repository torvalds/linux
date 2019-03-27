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


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "ucl.h"
#include "ucl_internal.h"

#ifdef HAVE_ENDIAN_H
#include <endian.h>
#elif defined(HAVE_SYS_ENDIAN_H)
#include <sys/endian.h>
#elif defined(HAVE_MACHINE_ENDIAN_H)
#include <machine/endian.h>
#endif

#if !defined(__LITTLE_ENDIAN__) && !defined(__BIG_ENDIAN__)
	#if __BYTE_ORDER == __LITTLE_ENDIAN
		#define __LITTLE_ENDIAN__
	#elif __BYTE_ORDER == __BIG_ENDIAN
		#define __BIG_ENDIAN__
	#elif _WIN32
		#define __LITTLE_ENDIAN__
	#endif
#endif

#define SWAP_LE_BE16(val)	((uint16_t) ( 		\
		(uint16_t) ((uint16_t) (val) >> 8) |	\
		(uint16_t) ((uint16_t) (val) << 8)))

#if defined(__clang__) || (defined(__GNUC__) && __GNUC__ >= 4 && defined (__GNUC_MINOR__) && __GNUC_MINOR__ >= 3)
#	define SWAP_LE_BE32(val) ((uint32_t)__builtin_bswap32 ((uint32_t)(val)))
#	define SWAP_LE_BE64(val) ((uint64_t)__builtin_bswap64 ((uint64_t)(val)))
#else
	#define SWAP_LE_BE32(val)	((uint32_t)( \
		(((uint32_t)(val) & (uint32_t)0x000000ffU) << 24) | \
		(((uint32_t)(val) & (uint32_t)0x0000ff00U) <<  8) | \
		(((uint32_t)(val) & (uint32_t)0x00ff0000U) >>  8) | \
		(((uint32_t)(val) & (uint32_t)0xff000000U) >> 24)))

	#define SWAP_LE_BE64(val)	((uint64_t)( 			\
		  (((uint64_t)(val) &							\
		(uint64_t)(0x00000000000000ffULL)) << 56) |		\
		  (((uint64_t)(val) &							\
		(uint64_t)(0x000000000000ff00ULL)) << 40) |		\
		  (((uint64_t)(val) &							\
		(uint64_t)(0x0000000000ff0000ULL)) << 24) |		\
		  (((uint64_t)(val) &							\
		(uint64_t) (0x00000000ff000000ULL)) <<  8) |	\
		  (((uint64_t)(val) &							\
		(uint64_t)(0x000000ff00000000ULL)) >>  8) |		\
		  (((uint64_t)(val) &							\
		(uint64_t)(0x0000ff0000000000ULL)) >> 24) |		\
		  (((uint64_t)(val) &							\
		(uint64_t)(0x00ff000000000000ULL)) >> 40) |		\
		  (((uint64_t)(val) &							\
		(uint64_t)(0xff00000000000000ULL)) >> 56)))
#endif

#ifdef __LITTLE_ENDIAN__
#define TO_BE16 SWAP_LE_BE16
#define TO_BE32 SWAP_LE_BE32
#define TO_BE64 SWAP_LE_BE64
#define FROM_BE16 SWAP_LE_BE16
#define FROM_BE32 SWAP_LE_BE32
#define FROM_BE64 SWAP_LE_BE64
#else
#define TO_BE16(val) (uint16_t)(val)
#define TO_BE32(val) (uint32_t)(val)
#define TO_BE64(val) (uint64_t)(val)
#define FROM_BE16(val) (uint16_t)(val)
#define FROM_BE32(val) (uint32_t)(val)
#define FROM_BE64(val) (uint64_t)(val)
#endif

void
ucl_emitter_print_int_msgpack (struct ucl_emitter_context *ctx, int64_t val)
{
	const struct ucl_emitter_functions *func = ctx->func;
	unsigned char buf[sizeof(uint64_t) + 1];
	const unsigned char mask_positive = 0x7f, mask_negative = 0xe0,
		uint8_ch = 0xcc, uint16_ch = 0xcd, uint32_ch = 0xce, uint64_ch = 0xcf,
		int8_ch = 0xd0, int16_ch = 0xd1, int32_ch = 0xd2, int64_ch = 0xd3;
	unsigned len;

	if (val >= 0) {
		if (val <= 0x7f) {
			/* Fixed num 7 bits */
			len = 1;
			buf[0] = mask_positive & val;
		}
		else if (val <= UINT8_MAX) {
			len = 2;
			buf[0] = uint8_ch;
			buf[1] = val & 0xff;
		}
		else if (val <= UINT16_MAX) {
			uint16_t v = TO_BE16 (val);

			len = 3;
			buf[0] = uint16_ch;
			memcpy (&buf[1], &v, sizeof (v));
		}
		else if (val <= UINT32_MAX) {
			uint32_t v = TO_BE32 (val);

			len = 5;
			buf[0] = uint32_ch;
			memcpy (&buf[1], &v, sizeof (v));
		}
		else {
			uint64_t v = TO_BE64 (val);

			len = 9;
			buf[0] = uint64_ch;
			memcpy (&buf[1], &v, sizeof (v));
		}
	}
	else {
		uint64_t uval;
		/* Bithack abs */
		uval = ((val ^ (val >> 63)) - (val >> 63));

		if (val > -(1 << 5)) {
			len = 1;
			buf[0] = (mask_negative | uval) & 0xff;
		}
		else if (uval <= INT8_MAX) {
			uint8_t v = (uint8_t)val;
			len = 2;
			buf[0] = int8_ch;
			buf[1] = v;
		}
		else if (uval <= INT16_MAX) {
			uint16_t v = TO_BE16 (val);

			len = 3;
			buf[0] = int16_ch;
			memcpy (&buf[1], &v, sizeof (v));
		}
		else if (uval <= INT32_MAX) {
			uint32_t v = TO_BE32 (val);

			len = 5;
			buf[0] = int32_ch;
			memcpy (&buf[1], &v, sizeof (v));
		}
		else {
			uint64_t v = TO_BE64 (val);

			len = 9;
			buf[0] = int64_ch;
			memcpy (&buf[1], &v, sizeof (v));
		}
	}

	func->ucl_emitter_append_len (buf, len, func->ud);
}

void
ucl_emitter_print_double_msgpack (struct ucl_emitter_context *ctx, double val)
{
	const struct ucl_emitter_functions *func = ctx->func;
	union {
		double d;
		uint64_t i;
	} u;
	const unsigned char dbl_ch = 0xcb;
	unsigned char buf[sizeof(double) + 1];

	/* Convert to big endian */
	u.d = val;
	u.i = TO_BE64 (u.i);

	buf[0] = dbl_ch;
	memcpy (&buf[1], &u.d, sizeof (double));
	func->ucl_emitter_append_len (buf, sizeof (buf), func->ud);
}

void
ucl_emitter_print_bool_msgpack (struct ucl_emitter_context *ctx, bool val)
{
	const struct ucl_emitter_functions *func = ctx->func;
	const unsigned char true_ch = 0xc3, false_ch = 0xc2;

	func->ucl_emitter_append_character (val ? true_ch : false_ch, 1, func->ud);
}

void
ucl_emitter_print_string_msgpack (struct ucl_emitter_context *ctx,
		const char *s, size_t len)
{
	const struct ucl_emitter_functions *func = ctx->func;
	const unsigned char fix_mask = 0xA0, l8_ch = 0xd9, l16_ch = 0xda, l32_ch = 0xdb;
	unsigned char buf[5];
	unsigned blen;

	if (len <= 0x1F) {
		blen = 1;
		buf[0] = (len | fix_mask) & 0xff;
	}
	else if (len <= 0xff) {
		blen = 2;
		buf[0] = l8_ch;
		buf[1] = len & 0xff;
	}
	else if (len <= 0xffff) {
		uint16_t bl = TO_BE16 (len);

		blen = 3;
		buf[0] = l16_ch;
		memcpy (&buf[1], &bl, sizeof (bl));
	}
	else {
		uint32_t bl = TO_BE32 (len);

		blen = 5;
		buf[0] = l32_ch;
		memcpy (&buf[1], &bl, sizeof (bl));
	}

	func->ucl_emitter_append_len (buf, blen, func->ud);
	func->ucl_emitter_append_len (s, len, func->ud);
}

void
ucl_emitter_print_binary_string_msgpack (struct ucl_emitter_context *ctx,
		const char *s, size_t len)
{
	const struct ucl_emitter_functions *func = ctx->func;
	const unsigned char l8_ch = 0xc4, l16_ch = 0xc5, l32_ch = 0xc6;
	unsigned char buf[5];
	unsigned blen;

	if (len <= 0xff) {
		blen = 2;
		buf[0] = l8_ch;
		buf[1] = len & 0xff;
	}
	else if (len <= 0xffff) {
		uint16_t bl = TO_BE16 (len);

		blen = 3;
		buf[0] = l16_ch;
		memcpy (&buf[1], &bl, sizeof (bl));
	}
	else {
		uint32_t bl = TO_BE32 (len);

		blen = 5;
		buf[0] = l32_ch;
		memcpy (&buf[1], &bl, sizeof (bl));
	}

	func->ucl_emitter_append_len (buf, blen, func->ud);
	func->ucl_emitter_append_len (s, len, func->ud);
}

void
ucl_emitter_print_null_msgpack (struct ucl_emitter_context *ctx)
{
	const struct ucl_emitter_functions *func = ctx->func;
	const unsigned char nil = 0xc0;

	func->ucl_emitter_append_character (nil, 1, func->ud);
}

void
ucl_emitter_print_key_msgpack (bool print_key, struct ucl_emitter_context *ctx,
		const ucl_object_t *obj)
{
	if (print_key) {
		ucl_emitter_print_string_msgpack (ctx, obj->key, obj->keylen);
	}
}

void
ucl_emitter_print_array_msgpack (struct ucl_emitter_context *ctx, size_t len)
{
	const struct ucl_emitter_functions *func = ctx->func;
	const unsigned char fix_mask = 0x90, l16_ch = 0xdc, l32_ch = 0xdd;
	unsigned char buf[5];
	unsigned blen;

	if (len <= 0xF) {
		blen = 1;
		buf[0] = (len | fix_mask) & 0xff;
	}
	else if (len <= 0xffff) {
		uint16_t bl = TO_BE16 (len);

		blen = 3;
		buf[0] = l16_ch;
		memcpy (&buf[1], &bl, sizeof (bl));
	}
	else {
		uint32_t bl = TO_BE32 (len);

		blen = 5;
		buf[0] = l32_ch;
		memcpy (&buf[1], &bl, sizeof (bl));
	}

	func->ucl_emitter_append_len (buf, blen, func->ud);
}

void
ucl_emitter_print_object_msgpack (struct ucl_emitter_context *ctx, size_t len)
{
	const struct ucl_emitter_functions *func = ctx->func;
	const unsigned char fix_mask = 0x80, l16_ch = 0xde, l32_ch = 0xdf;
	unsigned char buf[5];
	unsigned blen;

	if (len <= 0xF) {
		blen = 1;
		buf[0] = (len | fix_mask) & 0xff;
	}
	else if (len <= 0xffff) {
		uint16_t bl = TO_BE16 (len);

		blen = 3;
		buf[0] = l16_ch;
		memcpy (&buf[1], &bl, sizeof (bl));
	}
	else {
		uint32_t bl = TO_BE32 (len);

		blen = 5;
		buf[0] = l32_ch;
		memcpy (&buf[1], &bl, sizeof (bl));
	}

	func->ucl_emitter_append_len (buf, blen, func->ud);
}


enum ucl_msgpack_format {
	msgpack_positive_fixint = 0,
	msgpack_fixmap,
	msgpack_fixarray,
	msgpack_fixstr,
	msgpack_nil,
	msgpack_false,
	msgpack_true,
	msgpack_bin8,
	msgpack_bin16,
	msgpack_bin32,
	msgpack_ext8,
	msgpack_ext16,
	msgpack_ext32,
	msgpack_float32,
	msgpack_float64,
	msgpack_uint8,
	msgpack_uint16,
	msgpack_uint32,
	msgpack_uint64,
	msgpack_int8,
	msgpack_int16,
	msgpack_int32,
	msgpack_int64,
	msgpack_fixext1,
	msgpack_fixext2,
	msgpack_fixext4,
	msgpack_fixext8,
	msgpack_fixext16,
	msgpack_str8,
	msgpack_str16,
	msgpack_str32,
	msgpack_array16,
	msgpack_array32,
	msgpack_map16,
	msgpack_map32,
	msgpack_negative_fixint,
	msgpack_invalid
};

typedef ssize_t (*ucl_msgpack_parse_function)(struct ucl_parser *parser,
		struct ucl_stack *container, size_t len, enum ucl_msgpack_format fmt,
		const unsigned char *pos, size_t remain);

static ssize_t ucl_msgpack_parse_map (struct ucl_parser *parser,
		struct ucl_stack *container, size_t len, enum ucl_msgpack_format fmt,
		const unsigned char *pos, size_t remain);
static ssize_t ucl_msgpack_parse_array (struct ucl_parser *parser,
		struct ucl_stack *container, size_t len, enum ucl_msgpack_format fmt,
		const unsigned char *pos, size_t remain);
static ssize_t ucl_msgpack_parse_string (struct ucl_parser *parser,
		struct ucl_stack *container, size_t len, enum ucl_msgpack_format fmt,
		const unsigned char *pos, size_t remain);
static ssize_t ucl_msgpack_parse_int (struct ucl_parser *parser,
		struct ucl_stack *container, size_t len, enum ucl_msgpack_format fmt,
		const unsigned char *pos, size_t remain);
static ssize_t ucl_msgpack_parse_float (struct ucl_parser *parser,
		struct ucl_stack *container, size_t len, enum ucl_msgpack_format fmt,
		const unsigned char *pos, size_t remain);
static ssize_t ucl_msgpack_parse_bool (struct ucl_parser *parser,
		struct ucl_stack *container, size_t len, enum ucl_msgpack_format fmt,
		const unsigned char *pos, size_t remain);
static ssize_t ucl_msgpack_parse_null (struct ucl_parser *parser,
		struct ucl_stack *container, size_t len, enum ucl_msgpack_format fmt,
		const unsigned char *pos, size_t remain);
static ssize_t ucl_msgpack_parse_ignore (struct ucl_parser *parser,
		struct ucl_stack *container, size_t len, enum ucl_msgpack_format fmt,
		const unsigned char *pos, size_t remain);

#define MSGPACK_FLAG_FIXED (1 << 0)
#define MSGPACK_FLAG_CONTAINER (1 << 1)
#define MSGPACK_FLAG_TYPEVALUE (1 << 2)
#define MSGPACK_FLAG_EXT (1 << 3)
#define MSGPACK_FLAG_ASSOC (1 << 4)
#define MSGPACK_FLAG_KEY (1 << 5)
#define MSGPACK_CONTAINER_BIT (1ULL << 62)

/*
 * Search tree packed in array
 */
struct ucl_msgpack_parser {
	uint8_t prefix;						/* Prefix byte					*/
	uint8_t prefixlen;					/* Length of prefix in bits		*/
	uint8_t fmt;						/* The desired format 			*/
	uint8_t len;						/* Length of the object
										  (either length bytes
										  or length of value in case
										  of fixed objects 				*/
	uint8_t flags;						/* Flags of the specified type	*/
	ucl_msgpack_parse_function func;	/* Parser function				*/
} parsers[] = {
	{
			0xa0,
			3,
			msgpack_fixstr,
			0,
			MSGPACK_FLAG_FIXED|MSGPACK_FLAG_KEY,
			ucl_msgpack_parse_string
	},
	{
			0x0,
			1,
			msgpack_positive_fixint,
			0,
			MSGPACK_FLAG_FIXED|MSGPACK_FLAG_TYPEVALUE,
			ucl_msgpack_parse_int
	},
	{
			0xe0,
			3,
			msgpack_negative_fixint,
			0,
			MSGPACK_FLAG_FIXED|MSGPACK_FLAG_TYPEVALUE,
			ucl_msgpack_parse_int
	},
	{
			0x80,
			4,
			msgpack_fixmap,
			0,
			MSGPACK_FLAG_FIXED|MSGPACK_FLAG_CONTAINER|MSGPACK_FLAG_ASSOC,
			ucl_msgpack_parse_map
	},
	{
			0x90,
			4,
			msgpack_fixarray,
			0,
			MSGPACK_FLAG_FIXED|MSGPACK_FLAG_CONTAINER,
			ucl_msgpack_parse_array
	},
	{
			0xd9,
			8,
			msgpack_str8,
			1,
			MSGPACK_FLAG_KEY,
			ucl_msgpack_parse_string
	},
	{
			0xc4,
			8,
			msgpack_bin8,
			1,
			MSGPACK_FLAG_KEY,
			ucl_msgpack_parse_string
	},
	{
			0xcf,
			8,
			msgpack_uint64,
			8,
			MSGPACK_FLAG_FIXED,
			ucl_msgpack_parse_int
	},
	{
			0xd3,
			8,
			msgpack_int64,
			8,
			MSGPACK_FLAG_FIXED,
			ucl_msgpack_parse_int
	},
	{
			0xce,
			8,
			msgpack_uint32,
			4,
			MSGPACK_FLAG_FIXED,
			ucl_msgpack_parse_int
	},
	{
			0xd2,
			8,
			msgpack_int32,
			4,
			MSGPACK_FLAG_FIXED,
			ucl_msgpack_parse_int
	},
	{
			0xcb,
			8,
			msgpack_float64,
			8,
			MSGPACK_FLAG_FIXED,
			ucl_msgpack_parse_float
	},
	{
			0xca,
			8,
			msgpack_float32,
			4,
			MSGPACK_FLAG_FIXED,
			ucl_msgpack_parse_float
	},
	{
			0xc2,
			8,
			msgpack_false,
			1,
			MSGPACK_FLAG_FIXED | MSGPACK_FLAG_TYPEVALUE,
			ucl_msgpack_parse_bool
	},
	{
			0xc3,
			8,
			msgpack_true,
			1,
			MSGPACK_FLAG_FIXED | MSGPACK_FLAG_TYPEVALUE,
			ucl_msgpack_parse_bool
	},
	{
			0xcc,
			8,
			msgpack_uint8,
			1,
			MSGPACK_FLAG_FIXED,
			ucl_msgpack_parse_int
	},
	{
			0xcd,
			8,
			msgpack_uint16,
			2,
			MSGPACK_FLAG_FIXED,
			ucl_msgpack_parse_int
	},
	{
			0xd0,
			8,
			msgpack_int8,
			1,
			MSGPACK_FLAG_FIXED,
			ucl_msgpack_parse_int
	},
	{
			0xd1,
			8,
			msgpack_int16,
			2,
			MSGPACK_FLAG_FIXED,
			ucl_msgpack_parse_int
	},
	{
			0xc0,
			8,
			msgpack_nil,
			0,
			MSGPACK_FLAG_FIXED | MSGPACK_FLAG_TYPEVALUE,
			ucl_msgpack_parse_null
	},
	{
			0xda,
			8,
			msgpack_str16,
			2,
			MSGPACK_FLAG_KEY,
			ucl_msgpack_parse_string
	},
	{
			0xdb,
			8,
			msgpack_str32,
			4,
			MSGPACK_FLAG_KEY,
			ucl_msgpack_parse_string
	},
	{
			0xc5,
			8,
			msgpack_bin16,
			2,
			MSGPACK_FLAG_KEY,
			ucl_msgpack_parse_string
	},
	{
			0xc6,
			8,
			msgpack_bin32,
			4,
			MSGPACK_FLAG_KEY,
			ucl_msgpack_parse_string
	},
	{
			0xdc,
			8,
			msgpack_array16,
			2,
			MSGPACK_FLAG_CONTAINER,
			ucl_msgpack_parse_array
	},
	{
			0xdd,
			8,
			msgpack_array32,
			4,
			MSGPACK_FLAG_CONTAINER,
			ucl_msgpack_parse_array
	},
	{
			0xde,
			8,
			msgpack_map16,
			2,
			MSGPACK_FLAG_CONTAINER|MSGPACK_FLAG_ASSOC,
			ucl_msgpack_parse_map
	},
	{
			0xdf,
			8,
			msgpack_map32,
			4,
			MSGPACK_FLAG_CONTAINER|MSGPACK_FLAG_ASSOC,
			ucl_msgpack_parse_map
	},
	{
			0xc7,
			8,
			msgpack_ext8,
			1,
			MSGPACK_FLAG_EXT,
			ucl_msgpack_parse_ignore
	},
	{
			0xc8,
			8,
			msgpack_ext16,
			2,
			MSGPACK_FLAG_EXT,
			ucl_msgpack_parse_ignore
	},
	{
			0xc9,
			8,
			msgpack_ext32,
			4,
			MSGPACK_FLAG_EXT,
			ucl_msgpack_parse_ignore
	},
	{
			0xd4,
			8,
			msgpack_fixext1,
			1,
			MSGPACK_FLAG_FIXED | MSGPACK_FLAG_EXT,
			ucl_msgpack_parse_ignore
	},
	{
			0xd5,
			8,
			msgpack_fixext2,
			2,
			MSGPACK_FLAG_FIXED | MSGPACK_FLAG_EXT,
			ucl_msgpack_parse_ignore
	},
	{
			0xd6,
			8,
			msgpack_fixext4,
			4,
			MSGPACK_FLAG_FIXED | MSGPACK_FLAG_EXT,
			ucl_msgpack_parse_ignore
	},
	{
			0xd7,
			8,
			msgpack_fixext8,
			8,
			MSGPACK_FLAG_FIXED | MSGPACK_FLAG_EXT,
			ucl_msgpack_parse_ignore
	},
	{
			0xd8,
			8,
			msgpack_fixext16,
			16,
			MSGPACK_FLAG_FIXED | MSGPACK_FLAG_EXT,
			ucl_msgpack_parse_ignore
	}
};

#undef MSGPACK_DEBUG_PARSER

static inline struct ucl_msgpack_parser *
ucl_msgpack_get_parser_from_type (unsigned char t)
{
	unsigned int i, shift, mask;

	for (i = 0; i < sizeof (parsers) / sizeof (parsers[0]); i ++) {
		shift = CHAR_BIT - parsers[i].prefixlen;
		mask = parsers[i].prefix >> shift;

		if (mask == (((unsigned int)t) >> shift)) {
			return &parsers[i];
		}
	}

	return NULL;
}

static inline struct ucl_stack *
ucl_msgpack_get_container (struct ucl_parser *parser,
		struct ucl_msgpack_parser *obj_parser, uint64_t len)
{
	struct ucl_stack *stack;

	assert (obj_parser != NULL);

	if (obj_parser->flags & MSGPACK_FLAG_CONTAINER) {
		assert ((len & MSGPACK_CONTAINER_BIT) == 0);
		/*
		 * Insert new container to the stack
		 */
		if (parser->stack == NULL) {
			parser->stack = calloc (1, sizeof (struct ucl_stack));

			if (parser->stack == NULL) {
				ucl_create_err (&parser->err, "no memory");
				return NULL;
			}
		}
		else {
			stack = calloc (1, sizeof (struct ucl_stack));

			if (stack == NULL) {
				ucl_create_err (&parser->err, "no memory");
				return NULL;
			}

			stack->next = parser->stack;
			parser->stack = stack;
		}

		parser->stack->level = len | MSGPACK_CONTAINER_BIT;

#ifdef MSGPACK_DEBUG_PARSER
		stack = parser->stack;
		while (stack) {
			fprintf(stderr, "+");
			stack = stack->next;
		}

		fprintf(stderr, "%s -> %d\n", obj_parser->flags & MSGPACK_FLAG_ASSOC ? "object" : "array", (int)len);
#endif
	}
	else {
		/*
		 * Get the current stack top
		 */
		if (parser->stack) {
			return parser->stack;
		}
		else {
			ucl_create_err (&parser->err, "bad top level object for msgpack");
			return NULL;
		}
	}

	return parser->stack;
}

static bool
ucl_msgpack_is_container_finished (struct ucl_stack *container)
{
	uint64_t level;

	assert (container != NULL);

	if (container->level & MSGPACK_CONTAINER_BIT) {
		level = container->level & ~MSGPACK_CONTAINER_BIT;

		if (level == 0) {
			return true;
		}
	}

	return false;
}

static bool
ucl_msgpack_insert_object (struct ucl_parser *parser,
		const unsigned char *key,
		size_t keylen, ucl_object_t *obj)
{
	uint64_t level;
	struct ucl_stack *container;

	container = parser->stack;
	assert (container != NULL);
	assert (container->level > 0);
	assert (obj != NULL);
	assert (container->obj != NULL);

	if (container->obj->type == UCL_ARRAY) {
		ucl_array_append (container->obj, obj);
	}
	else if (container->obj->type == UCL_OBJECT) {
		if (key == NULL || keylen == 0) {
			ucl_create_err (&parser->err, "cannot insert object with no key");
			return false;
		}

		obj->key = key;
		obj->keylen = keylen;

		if (!(parser->flags & UCL_PARSER_ZEROCOPY)) {
			ucl_copy_key_trash (obj);
		}

		ucl_parser_process_object_element (parser, obj);
	}
	else {
		ucl_create_err (&parser->err, "bad container type");
		return false;
	}

	if (container->level & MSGPACK_CONTAINER_BIT) {
		level = container->level & ~MSGPACK_CONTAINER_BIT;
		container->level = (level - 1) | MSGPACK_CONTAINER_BIT;
	}

	return true;
}

static struct ucl_stack *
ucl_msgpack_get_next_container (struct ucl_parser *parser)
{
	struct ucl_stack *cur = NULL;
	uint64_t level;

	cur = parser->stack;

	if (cur == NULL) {
		return NULL;
	}

	if (cur->level & MSGPACK_CONTAINER_BIT) {
		level = cur->level & ~MSGPACK_CONTAINER_BIT;

		if (level == 0) {
			/* We need to switch to the previous container */
			parser->stack = cur->next;
			parser->cur_obj = cur->obj;
			free (cur);

#ifdef MSGPACK_DEBUG_PARSER
			cur = parser->stack;
			while (cur) {
				fprintf(stderr, "-");
				cur = cur->next;
			}
			fprintf(stderr, "-%s -> %d\n", parser->cur_obj->type == UCL_OBJECT ? "object" : "array", (int)parser->cur_obj->len);
#endif

			return ucl_msgpack_get_next_container (parser);
		}
	}

	/*
	 * For UCL containers we don't know length, so we just insert the whole
	 * message pack blob into the top level container
	 */

	assert (cur->obj != NULL);

	return cur;
}

#define CONSUME_RET do {									\
	if (ret != -1) {										\
		p += ret;											\
		remain -= ret;										\
		obj_parser = NULL;									\
		assert (remain >= 0);								\
	}														\
	else {													\
		ucl_create_err (&parser->err,						\
			"cannot parse type %d of len %u",				\
			(int)obj_parser->fmt,							\
			(unsigned)len);									\
		return false;										\
	}														\
} while(0)

#define GET_NEXT_STATE do {									\
	container = ucl_msgpack_get_next_container (parser);	\
	if (container == NULL) {								\
		ucl_create_err (&parser->err,						\
					"empty container");						\
		return false;										\
	}														\
	next_state = container->obj->type == UCL_OBJECT ? 		\
					read_assoc_key : read_array_value;		\
} while(0)

static bool
ucl_msgpack_consume (struct ucl_parser *parser)
{
	const unsigned char *p, *end, *key = NULL;
	struct ucl_stack *container;
	enum e_msgpack_parser_state {
		read_type,
		start_assoc,
		start_array,
		read_assoc_key,
		read_assoc_value,
		finish_assoc_value,
		read_array_value,
		finish_array_value,
		error_state
	} state = read_type, next_state = error_state;
	struct ucl_msgpack_parser *obj_parser = NULL;
	uint64_t len = 0;
	ssize_t ret, remain, keylen = 0;
#ifdef MSGPACK_DEBUG_PARSER
	uint64_t i;
	enum e_msgpack_parser_state hist[256];
#endif

	p = parser->chunks->begin;
	remain = parser->chunks->remain;
	end = p + remain;


	while (p < end) {
#ifdef MSGPACK_DEBUG_PARSER
		hist[i++ % 256] = state;
#endif
		switch (state) {
		case read_type:
			obj_parser = ucl_msgpack_get_parser_from_type (*p);

			if (obj_parser == NULL) {
				ucl_create_err (&parser->err, "unknown msgpack format: %x",
						(unsigned int)*p);

				return false;
			}
			/* Now check length sanity */
			if (obj_parser->flags & MSGPACK_FLAG_FIXED) {
				if (obj_parser->len == 0) {
					/* We have an embedded size */
					len = *p & ~obj_parser->prefix;
				}
				else {
					if (remain < obj_parser->len) {
						ucl_create_err (&parser->err, "not enough data remain to "
								"read object's length: %u remain, %u needed",
								(unsigned)remain, obj_parser->len);

						return false;
					}

					len = obj_parser->len;
				}

				if (!(obj_parser->flags & MSGPACK_FLAG_TYPEVALUE)) {
					/* We must pass value as the second byte */
					if (remain > 0) {
						p ++;
						remain --;
					}
				}
				else {
					/* Len is irrelevant now */
					len = 0;
				}
			}
			else {
				/* Length is not embedded */
				if (remain < obj_parser->len) {
					ucl_create_err (&parser->err, "not enough data remain to "
							"read object's length: %u remain, %u needed",
							(unsigned)remain, obj_parser->len);

					return false;
				}

				p ++;
				remain --;

				switch (obj_parser->len) {
				case 1:
					len = *p;
					break;
				case 2:
					len = FROM_BE16 (*(uint16_t *)p);
					break;
				case 4:
					len = FROM_BE32 (*(uint32_t *)p);
					break;
				case 8:
					len = FROM_BE64 (*(uint64_t *)p);
					break;
				default:
					assert (0);
					break;
				}

				p += obj_parser->len;
				remain -= obj_parser->len;
			}

			if (obj_parser->flags & MSGPACK_FLAG_ASSOC) {
				/* We have just read the new associative map */
				state = start_assoc;
			}
			else if (obj_parser->flags & MSGPACK_FLAG_CONTAINER){
				state = start_array;
			}
			else {
				state = next_state;
			}

			break;
		case start_assoc:
			parser->cur_obj = ucl_object_new_full (UCL_OBJECT,
					parser->chunks->priority);
			/* Insert to the previous level container */
			if (parser->stack && !ucl_msgpack_insert_object (parser,
					key, keylen, parser->cur_obj)) {
				return false;
			}
			/* Get new container */
			container = ucl_msgpack_get_container (parser, obj_parser, len);

			if (container == NULL) {
				return false;
			}

			ret = obj_parser->func (parser, container, len, obj_parser->fmt,
					p, remain);
			CONSUME_RET;
			key = NULL;
			keylen = 0;

			if (len > 0) {
				state = read_type;
				next_state = read_assoc_key;
			}
			else {
				/* Empty object */
				state = finish_assoc_value;
			}
			break;

		case start_array:
			parser->cur_obj = ucl_object_new_full (UCL_ARRAY,
					parser->chunks->priority);
			/* Insert to the previous level container */
			if (parser->stack && !ucl_msgpack_insert_object (parser,
					key, keylen, parser->cur_obj)) {
				return false;
			}
			/* Get new container */
			container = ucl_msgpack_get_container (parser, obj_parser, len);

			if (container == NULL) {
				return false;
			}

			ret = obj_parser->func (parser, container, len, obj_parser->fmt,
								p, remain);
			CONSUME_RET;

			if (len > 0) {
				state = read_type;
				next_state = read_array_value;
			}
			else {
				/* Empty array */
				state = finish_array_value;
			}
			break;

		case read_array_value:
			/*
			 * p is now at the value start, len now contains length read and
			 * obj_parser contains the corresponding specific parser
			 */
			container = parser->stack;

			if (container == NULL) {
				return false;
			}

			ret = obj_parser->func (parser, container, len, obj_parser->fmt,
					p, remain);
			CONSUME_RET;


			/* Insert value to the container and check if we have finished array */
			if (!ucl_msgpack_insert_object (parser, NULL, 0,
					parser->cur_obj)) {
				return false;
			}

			if (ucl_msgpack_is_container_finished (container)) {
				state = finish_array_value;
			}
			else {
				/* Read more elements */
				state = read_type;
				next_state = read_array_value;
			}

			break;

		case read_assoc_key:
			/*
			 * Keys must have string type for ucl msgpack
			 */
			if (!(obj_parser->flags & MSGPACK_FLAG_KEY)) {
				ucl_create_err (&parser->err, "bad type for key: %u, expected "
						"string", (unsigned)obj_parser->fmt);

				return false;
			}

			key = p;
			keylen = len;

			if (keylen > remain || keylen == 0) {
				ucl_create_err (&parser->err, "too long or empty key");
				return false;
			}

			p += len;
			remain -= len;

			state = read_type;
			next_state = read_assoc_value;
			break;

		case read_assoc_value:
			/*
			 * p is now at the value start, len now contains length read and
			 * obj_parser contains the corresponding specific parser
			 */
			container = parser->stack;

			if (container == NULL) {
				return false;
			}

			ret = obj_parser->func (parser, container, len, obj_parser->fmt,
					p, remain);
			CONSUME_RET;

			assert (key != NULL && keylen > 0);

			if (!ucl_msgpack_insert_object (parser, key, keylen,
					parser->cur_obj)) {
				return false;
			}

			key = NULL;
			keylen = 0;

			if (ucl_msgpack_is_container_finished (container)) {
				state = finish_assoc_value;
			}
			else {
				/* Read more elements */
				state = read_type;
				next_state = read_assoc_key;
			}
			break;

		case finish_array_value:
		case finish_assoc_value:
			GET_NEXT_STATE;
			state = read_type;
			break;

		case error_state:
			ucl_create_err (&parser->err, "invalid state machine state");

			return false;
		}
	}

	/* Check the finishing state */
	switch (state) {
	case start_array:
	case start_assoc:
		/* Empty container at the end */
		if (len != 0) {
			ucl_create_err (&parser->err, "invalid non-empty container at the end");

			return false;
		}

		parser->cur_obj = ucl_object_new_full (
				state == start_array ? UCL_ARRAY : UCL_OBJECT,
				parser->chunks->priority);
		/* Insert to the previous level container */
		if (!ucl_msgpack_insert_object (parser,
				key, keylen, parser->cur_obj)) {
			return false;
		}
		/* Get new container */
		container = ucl_msgpack_get_container (parser, obj_parser, len);

		if (container == NULL) {
			return false;
		}

		ret = obj_parser->func (parser, container, len, obj_parser->fmt,
				p, remain);
		break;

	case read_array_value:
	case read_assoc_value:
		if (len != 0) {
			ucl_create_err (&parser->err, "unfinished value at the end");

			return false;
		}

		container = parser->stack;

		if (container == NULL) {
			return false;
		}

		ret = obj_parser->func (parser, container, len, obj_parser->fmt,
				p, remain);
		CONSUME_RET;


		/* Insert value to the container and check if we have finished array */
		if (!ucl_msgpack_insert_object (parser, NULL, 0,
				parser->cur_obj)) {
			return false;
		}
		break;
	case finish_array_value:
	case finish_assoc_value:
	case read_type:
		/* Valid finishing state */
		break;
	default:
		/* Invalid finishing state */
		ucl_create_err (&parser->err, "invalid state machine finishing state: %d",
				state);

		return false;
	}

	/* Rewind to the top level container */
	ucl_msgpack_get_next_container (parser);
	assert (parser->stack == NULL ||
			(parser->stack->level & MSGPACK_CONTAINER_BIT) == 0);

	return true;
}

bool
ucl_parse_msgpack (struct ucl_parser *parser)
{
	ucl_object_t *container = NULL;
	const unsigned char *p;
	bool ret;

	assert (parser != NULL);
	assert (parser->chunks != NULL);
	assert (parser->chunks->begin != NULL);
	assert (parser->chunks->remain != 0);

	p = parser->chunks->begin;

	if (parser->stack) {
		container = parser->stack->obj;
	}

	/*
	 * When we start parsing message pack chunk, we must ensure that we
	 * have either a valid container or the top object inside message pack is
	 * of container type
	 */
	if (container == NULL) {
		if ((*p & 0x80) != 0x80 && !(*p >= 0xdc && *p <= 0xdf)) {
			ucl_create_err (&parser->err, "bad top level object for msgpack");
			return false;
		}
	}

	ret = ucl_msgpack_consume (parser);

	if (ret && parser->top_obj == NULL) {
		parser->top_obj = parser->cur_obj;
	}

	return ret;
}

static ssize_t
ucl_msgpack_parse_map (struct ucl_parser *parser,
		struct ucl_stack *container, size_t len, enum ucl_msgpack_format fmt,
		const unsigned char *pos, size_t remain)
{
	container->obj = parser->cur_obj;

	return 0;
}

static ssize_t
ucl_msgpack_parse_array (struct ucl_parser *parser,
		struct ucl_stack *container, size_t len, enum ucl_msgpack_format fmt,
		const unsigned char *pos, size_t remain)
{
	container->obj = parser->cur_obj;

	return 0;
}

static ssize_t
ucl_msgpack_parse_string (struct ucl_parser *parser,
		struct ucl_stack *container, size_t len, enum ucl_msgpack_format fmt,
		const unsigned char *pos, size_t remain)
{
	ucl_object_t *obj;

	if (len > remain) {
		return -1;
	}

	obj = ucl_object_new_full (UCL_STRING, parser->chunks->priority);
	obj->value.sv = pos;
	obj->len = len;

	if (fmt >= msgpack_bin8 && fmt <= msgpack_bin32) {
		obj->flags |= UCL_OBJECT_BINARY;
	}

	if (!(parser->flags & UCL_PARSER_ZEROCOPY)) {
		if (obj->flags & UCL_OBJECT_BINARY) {
			obj->trash_stack[UCL_TRASH_VALUE] = malloc (len);

			if (obj->trash_stack[UCL_TRASH_VALUE] != NULL) {
				memcpy (obj->trash_stack[UCL_TRASH_VALUE], pos, len);
			}
		}
		else {
			ucl_copy_value_trash (obj);
		}
	}

	parser->cur_obj = obj;

	return len;
}

static ssize_t
ucl_msgpack_parse_int (struct ucl_parser *parser,
		struct ucl_stack *container, size_t len, enum ucl_msgpack_format fmt,
		const unsigned char *pos, size_t remain)
{
	ucl_object_t *obj;
	int8_t iv8;
	int16_t iv16;
	int32_t iv32;
	int64_t iv64;
	uint16_t uiv16;
	uint32_t uiv32;
	uint64_t uiv64;


	if (len > remain) {
		return -1;
	}

	obj = ucl_object_new_full (UCL_INT, parser->chunks->priority);

	switch (fmt) {
	case msgpack_positive_fixint:
		obj->value.iv = (*pos & 0x7f);
		len = 1;
		break;
	case msgpack_negative_fixint:
		obj->value.iv = - (*pos & 0x1f);
		len = 1;
		break;
	case msgpack_uint8:
		obj->value.iv = (unsigned char)*pos;
		len = 1;
		break;
	case msgpack_int8:
		memcpy (&iv8, pos, sizeof (iv8));
		obj->value.iv = iv8;
		len = 1;
		break;
	case msgpack_int16:
		memcpy (&iv16, pos, sizeof (iv16));
		iv16 = FROM_BE16 (iv16);
		obj->value.iv = iv16;
		len = 2;
		break;
	case msgpack_uint16:
		memcpy (&uiv16, pos, sizeof (uiv16));
		uiv16 = FROM_BE16 (uiv16);
		obj->value.iv = uiv16;
		len = 2;
		break;
	case msgpack_int32:
		memcpy (&iv32, pos, sizeof (iv32));
		iv32 = FROM_BE32 (iv32);
		obj->value.iv = iv32;
		len = 4;
		break;
	case msgpack_uint32:
		memcpy(&uiv32, pos, sizeof(uiv32));
		uiv32 = FROM_BE32(uiv32);
		obj->value.iv = uiv32;
		len = 4;
		break;
	case msgpack_int64:
		memcpy (&iv64, pos, sizeof (iv64));
		iv64 = FROM_BE64 (iv64);
		obj->value.iv = iv64;
		len = 8;
		break;
	case msgpack_uint64:
		memcpy(&uiv64, pos, sizeof(uiv64));
		uiv64 = FROM_BE64(uiv64);
		obj->value.iv = uiv64;
		len = 8;
		break;
	default:
		assert (0);
		break;
	}

	parser->cur_obj = obj;

	return len;
}

static ssize_t
ucl_msgpack_parse_float (struct ucl_parser *parser,
		struct ucl_stack *container, size_t len, enum ucl_msgpack_format fmt,
		const unsigned char *pos, size_t remain)
{
	ucl_object_t *obj;
	union {
		uint32_t i;
		float f;
	} d;
	uint64_t uiv64;

	if (len > remain) {
		return -1;
	}

	obj = ucl_object_new_full (UCL_FLOAT, parser->chunks->priority);

	switch (fmt) {
	case msgpack_float32:
		memcpy(&d.i, pos, sizeof(d.i));
		d.i = FROM_BE32(d.i);
		/* XXX: can be slow */
		obj->value.dv = d.f;
		len = 4;
		break;
	case msgpack_float64:
		memcpy(&uiv64, pos, sizeof(uiv64));
		uiv64 = FROM_BE64(uiv64);
		obj->value.iv = uiv64;
		len = 8;
		break;
	default:
		assert (0);
		break;
	}

	parser->cur_obj = obj;

	return len;
}

static ssize_t
ucl_msgpack_parse_bool (struct ucl_parser *parser,
		struct ucl_stack *container, size_t len, enum ucl_msgpack_format fmt,
		const unsigned char *pos, size_t remain)
{
	ucl_object_t *obj;

	if (len > remain) {
		return -1;
	}

	obj = ucl_object_new_full (UCL_BOOLEAN, parser->chunks->priority);

	switch (fmt) {
	case msgpack_true:
		obj->value.iv = true;
		break;
	case msgpack_false:
		obj->value.iv = false;
		break;
	default:
		assert (0);
		break;
	}

	parser->cur_obj = obj;

	return 1;
}

static ssize_t
ucl_msgpack_parse_null (struct ucl_parser *parser,
		struct ucl_stack *container, size_t len, enum ucl_msgpack_format fmt,
		const unsigned char *pos, size_t remain)
{
	ucl_object_t *obj;

	if (len > remain) {
		return -1;
	}

	obj = ucl_object_new_full (UCL_NULL, parser->chunks->priority);
	parser->cur_obj = obj;

	return 1;
}

static ssize_t
ucl_msgpack_parse_ignore (struct ucl_parser *parser,
		struct ucl_stack *container, size_t len, enum ucl_msgpack_format fmt,
		const unsigned char *pos, size_t remain)
{
	if (len > remain) {
		return -1;
	}

	switch (fmt) {
	case msgpack_fixext1:
		len = 2;
		break;
	case msgpack_fixext2:
		len = 3;
		break;
	case msgpack_fixext4:
		len = 5;
		break;
	case msgpack_fixext8:
		len = 9;
		break;
	case msgpack_fixext16:
		len = 17;
		break;
	case msgpack_ext8:
	case msgpack_ext16:
	case msgpack_ext32:
		len = len + 1;
		break;
	default:
		ucl_create_err (&parser->err, "bad type: %x", (unsigned)fmt);
		return -1;
	}

	return len;
}
