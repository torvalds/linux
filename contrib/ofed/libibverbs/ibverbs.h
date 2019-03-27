/*
 * Copyright (c) 2004, 2005 Topspin Communications.  All rights reserved.
 * Copyright (c) 2007 Cisco Systems, Inc.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
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

#ifndef IB_VERBS_H
#define IB_VERBS_H

#include <pthread.h>

#include <infiniband/driver.h>

#define INIT		__attribute__((constructor))

#define DEFAULT_ABI	"IBVERBS_1.1"

#define symver(name, api, ver) asm(".symver " #name "," #api "@" #ver)
#define default_symver(name, api)                                              \
	asm(".symver " #name "," #api "@@" DEFAULT_ABI)
#define private_symver(name, api)                                              \
	asm(".symver " #name "," #api "@@IBVERBS_PRIVATE_14")

#define PFX		"libibverbs: "

struct ibv_abi_compat_v2 {
	struct ibv_comp_channel	channel;
	pthread_mutex_t		in_use;
};

extern int abi_ver;

int ibverbs_init(struct ibv_device ***list);

struct verbs_ex_private {
	struct ibv_cq_ex *(*create_cq_ex)(struct ibv_context *context,
					  struct ibv_cq_init_attr_ex *init_attr);
};

#define IBV_INIT_CMD(cmd, size, opcode)					\
	do {								\
		if (abi_ver > 2)					\
			(cmd)->command = IB_USER_VERBS_CMD_##opcode;	\
		else							\
			(cmd)->command = IB_USER_VERBS_CMD_##opcode##_V2; \
		(cmd)->in_words  = (size) / 4;				\
		(cmd)->out_words = 0;					\
	} while (0)

#define IBV_INIT_CMD_RESP(cmd, size, opcode, out, outsize)		\
	do {								\
		if (abi_ver > 2)					\
			(cmd)->command = IB_USER_VERBS_CMD_##opcode;	\
		else							\
			(cmd)->command = IB_USER_VERBS_CMD_##opcode##_V2; \
		(cmd)->in_words  = (size) / 4;				\
		(cmd)->out_words = (outsize) / 4;			\
		(cmd)->response  = (uintptr_t) (out);			\
	} while (0)

#define IBV_INIT_CMD_RESP_EX_V(cmd, cmd_size, size, opcode, out, resp_size,\
		outsize)						   \
	do {                                                               \
		size_t c_size = cmd_size - sizeof(struct ex_hdr);	   \
		if (abi_ver > 2)					   \
			(cmd)->hdr.command = IB_USER_VERBS_CMD_##opcode;   \
		else							   \
			(cmd)->hdr.command =				   \
				IB_USER_VERBS_CMD_##opcode##_V2;	   \
		(cmd)->hdr.in_words  = ((c_size) / 8);                     \
		(cmd)->hdr.out_words = ((resp_size) / 8);                  \
		(cmd)->hdr.provider_in_words   = (((size) - (cmd_size))/8);\
		(cmd)->hdr.provider_out_words  =			   \
			     (((outsize) - (resp_size)) / 8);              \
		(cmd)->hdr.response  = (uintptr_t) (out);                  \
		(cmd)->hdr.reserved = 0;				   \
	} while (0)

#define IBV_INIT_CMD_RESP_EX_VCMD(cmd, cmd_size, size, opcode, out, outsize) \
	IBV_INIT_CMD_RESP_EX_V(cmd, cmd_size, size, opcode, out,	     \
			sizeof(*(out)), outsize)

#define IBV_INIT_CMD_RESP_EX(cmd, size, opcode, out, outsize)		     \
	IBV_INIT_CMD_RESP_EX_V(cmd, sizeof(*(cmd)), size, opcode, out,    \
			sizeof(*(out)), outsize)

#define IBV_INIT_CMD_EX(cmd, size, opcode)				     \
	IBV_INIT_CMD_RESP_EX_V(cmd, sizeof(*(cmd)), size, opcode, NULL, 0, 0)

#endif /* IB_VERBS_H */
