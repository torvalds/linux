/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef _XE_RTP_HELPERS_
#define _XE_RTP_HELPERS_

#ifndef _XE_RTP_INCLUDE_PRIVATE_HELPERS
#error "This header is supposed to be included by xe_rtp.h only"
#endif

#include "xe_args.h"

/*
 * Helper macros - not to be used outside this header.
 */
#define _XE_ESC(...) __VA_ARGS__

#define _XE_TUPLE_TAIL(...) (DROP_FIRST_ARG(__VA_ARGS__))

#define _XE_RTP_CONCAT(a, b) CONCATENATE(XE_RTP_, CONCATENATE(a, b))

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
#define XE_RTP_PASTE_FOREACH(prefix_, sep_, args_) _XE_RTP_CONCAT(PASTE_, COUNT_ARGS args_)(prefix_, sep_, args_)
#define XE_RTP_PASTE_1(prefix_, sep_, args_) _XE_RTP_CONCAT(prefix_, FIRST_ARG args_)
#define XE_RTP_PASTE_2(prefix_, sep_, args_) _XE_RTP_CONCAT(prefix_, FIRST_ARG args_) __XE_RTP_PASTE_SEP_ ## sep_ XE_RTP_PASTE_1(prefix_, sep_, _XE_TUPLE_TAIL args_)
#define XE_RTP_PASTE_3(prefix_, sep_, args_) _XE_RTP_CONCAT(prefix_, FIRST_ARG args_) __XE_RTP_PASTE_SEP_ ## sep_ XE_RTP_PASTE_2(prefix_, sep_, _XE_TUPLE_TAIL args_)
#define XE_RTP_PASTE_4(prefix_, sep_, args_) _XE_RTP_CONCAT(prefix_, FIRST_ARG args_) __XE_RTP_PASTE_SEP_ ## sep_ XE_RTP_PASTE_3(prefix_, sep_, _XE_TUPLE_TAIL args_)

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
#define XE_RTP_DROP_CAST(...) _XE_ESC(DROP_FIRST_ARG _XE_ESC __VA_ARGS__)

#endif
