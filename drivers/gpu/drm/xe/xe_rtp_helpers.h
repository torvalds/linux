/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef _XE_RTP_HELPERS_
#define _XE_RTP_HELPERS_

#ifndef _XE_RTP_INCLUDE_PRIVATE_HELPERS
#error "This header is supposed to be included by xe_rtp.h only"
#endif

/*
 * Helper macros - not to be used outside this header.
 */
#define _XE_ESC(...) __VA_ARGS__
#define _XE_COUNT_ARGS(...) _XE_ESC(__XE_COUNT_ARGS(__VA_ARGS__, 5, 4, 3, 2, 1,))
#define __XE_COUNT_ARGS(_, _5, _4, _3, _2, X_, ...) X_

#define _XE_FIRST(...) _XE_ESC(__XE_FIRST(__VA_ARGS__,))
#define __XE_FIRST(x_, ...) x_
#define _XE_TUPLE_TAIL(...) _XE_ESC(__XE_TUPLE_TAIL(__VA_ARGS__))
#define __XE_TUPLE_TAIL(x_, ...) (__VA_ARGS__)

#define _XE_DROP_FIRST(x_, ...) __VA_ARGS__

#define _XE_RTP_CONCAT(a, b) __XE_RTP_CONCAT(a, b)
#define __XE_RTP_CONCAT(a, b) XE_RTP_ ## a ## b

#define __XE_RTP_PASTE_SEP_COMMA		,
#define __XE_RTP_PASTE_SEP_BITWISE_OR		|

/*
 * XE_RTP_PASTE_FOREACH - Paste XE_RTP_<@prefix_> on each element of the tuple
 * @args, with the end result separated by @sep_. @sep must be one of the
 * previously declared macros __XE_RTP_PASTE_SEP_*, or declared with such
 * prefix.
 *
 * Examples:
 *
 * 1) XE_RTP_PASTE_FOREACH(TEST_, COMMA, (FOO, BAR))
 *    expands to:
 *
 *	XE_RTP_TEST_FOO , XE_RTP_TEST_BAR
 *
 * 2) XE_RTP_PASTE_FOREACH(TEST2_, COMMA, (FOO))
 *    expands to:
 *
 *	XE_RTP_TEST2_FOO
 *
 * 3) XE_RTP_PASTE_FOREACH(TEST3, BITWISE_OR, (FOO, BAR))
 *    expands to:
 *
 *	XE_RTP_TEST3_FOO | XE_RTP_TEST3_BAR
 *
 * 4) #define __XE_RTP_PASTE_SEP_MY_SEP	BANANA
 *    XE_RTP_PASTE_FOREACH(TEST_, MY_SEP, (FOO, BAR))
 *    expands to:
 *
 *	XE_RTP_TEST_FOO BANANA XE_RTP_TEST_BAR
 */
#define XE_RTP_PASTE_FOREACH(prefix_, sep_, args_) _XE_ESC(_XE_RTP_CONCAT(PASTE_, _XE_COUNT_ARGS args_)(prefix_, sep_, args_))
#define XE_RTP_PASTE_1(prefix_, sep_, args_) _XE_RTP_CONCAT(prefix_, _XE_FIRST args_)
#define XE_RTP_PASTE_2(prefix_, sep_, args_) _XE_RTP_CONCAT(prefix_, _XE_FIRST args_) __XE_RTP_PASTE_SEP_ ## sep_ XE_RTP_PASTE_1(prefix_, sep_, _XE_TUPLE_TAIL args_)
#define XE_RTP_PASTE_3(prefix_, sep_, args_) _XE_RTP_CONCAT(prefix_, _XE_FIRST args_) __XE_RTP_PASTE_SEP_ ## sep_ XE_RTP_PASTE_2(prefix_, sep_, _XE_TUPLE_TAIL args_)
#define XE_RTP_PASTE_4(prefix_, sep_, args_) _XE_RTP_CONCAT(prefix_, _XE_FIRST args_) __XE_RTP_PASTE_SEP_ ## sep_ XE_RTP_PASTE_3(prefix_, sep_, _XE_TUPLE_TAIL args_)

/*
 * XE_RTP_DROP_CAST - Drop cast to convert a compound statement to a initializer
 *
 * Example:
 *
 *	#define foo(a_)	((struct foo){ .a = a_ })
 *	XE_RTP_DROP_CAST(foo(10))
 *	expands to:
 *
 *	{ .a = 10 }
 */
#define XE_RTP_DROP_CAST(...) _XE_ESC(_XE_DROP_FIRST _XE_ESC __VA_ARGS__)

#endif
