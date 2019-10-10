/*
 *
 * (C) COPYRIGHT 2019 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 * SPDX-License-Identifier: GPL-2.0
 *
 */

#include "mali_kbase_mipe_proto.h"

/**
 * This header generates MIPE tracepoint declaration BLOB at
 * compile time.
 *
 * Before including this header, the following parameters
 * must be defined:
 *
 * MIPE_HEADER_BLOB_VAR_NAME: the name of the variable
 * where the result BLOB will be stored.
 *
 * MIPE_HEADER_TP_LIST: the list of tracepoints to process.
 * It should be defined as follows:
 * #define MIPE_HEADER_TP_LIST \
 *     TP_DESC(FIRST_TRACEPOINT, "Some description", "@II", "first_arg,second_arg") \
 *     TP_DESC(SECOND_TRACEPOINT, "Some description", "@II", "first_arg,second_arg") \
 *     etc.
 * Where the first argument is tracepoints name, the second
 * argument is a short tracepoint description, the third argument
 * argument types (see MIPE documentation), and the fourth argument
 * is comma separated argument names.
 *
 * MIPE_HEADER_TP_LIST_COUNT: number of entries in MIPE_HEADER_TP_LIST.
 *
 * MIPE_HEADER_PKT_CLASS: MIPE packet class.
 */

#if !defined(MIPE_HEADER_BLOB_VAR_NAME)
#error "MIPE_HEADER_BLOB_VAR_NAME must be defined!"
#endif

#if !defined(MIPE_HEADER_TP_LIST)
#error "MIPE_HEADER_TP_LIST must be defined!"
#endif

#if !defined(MIPE_HEADER_TP_LIST_COUNT)
#error "MIPE_HEADER_TP_LIST_COUNT must be defined!"
#endif

#if !defined(MIPE_HEADER_PKT_CLASS)
#error "MIPE_HEADER_PKT_CLASS must be defined!"
#endif

static const struct {
	u32 _mipe_w0;
	u32 _mipe_w1;
	u8  _protocol_version;
	u8  _pointer_size;
	u32 _tp_count;
#define TP_DESC(name, desc, arg_types, arg_names)       \
	struct {                                        \
		u32  _name;                             \
		u32  _size_string_name;                 \
		char _string_name[sizeof(#name)];       \
		u32  _size_desc;                        \
		char _desc[sizeof(desc)];               \
		u32  _size_arg_types;                   \
		char _arg_types[sizeof(arg_types)];     \
		u32  _size_arg_names;                   \
		char _arg_names[sizeof(arg_names)];     \
	} __attribute__ ((__packed__)) __ ## name;

	MIPE_HEADER_TP_LIST
#undef TP_DESC

} __attribute__ ((__packed__)) MIPE_HEADER_BLOB_VAR_NAME = {
	._mipe_w0 = MIPE_PACKET_HEADER_W0(
		TL_PACKET_FAMILY_TL,
		MIPE_HEADER_PKT_CLASS,
		TL_PACKET_TYPE_HEADER,
		1),
	._mipe_w1 = MIPE_PACKET_HEADER_W1(
		sizeof(MIPE_HEADER_BLOB_VAR_NAME) - PACKET_HEADER_SIZE,
		0),
	._protocol_version = SWTRACE_VERSION,
	._pointer_size = sizeof(void *),
	._tp_count = MIPE_HEADER_TP_LIST_COUNT,
#define TP_DESC(name, desc, arg_types, arg_names)       \
	.__ ## name = {                                 \
		._name = name,                          \
		._size_string_name = sizeof(#name),     \
		._string_name = #name,                  \
		._size_desc = sizeof(desc),             \
		._desc = desc,                          \
		._size_arg_types = sizeof(arg_types),   \
		._arg_types = arg_types,                \
		._size_arg_names = sizeof(arg_names),   \
		._arg_names = arg_names                 \
	},
	MIPE_HEADER_TP_LIST
#undef TP_DESC
};

#undef MIPE_HEADER_BLOB_VAR_NAME
#undef MIPE_HEADER_TP_LIST
#undef MIPE_HEADER_TP_LIST_COUNT
#undef MIPE_HEADER_PKT_CLASS
