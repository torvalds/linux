#ifndef _HFI1_USER_EXP_RCV_H
#define _HFI1_USER_EXP_RCV_H
/*
 * Copyright(c) 2015 - 2017 Intel Corporation.
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * BSD LICENSE
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  - Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  - Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  - Neither the name of Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include "hfi.h"

#define EXP_TID_TIDLEN_MASK   0x7FFULL
#define EXP_TID_TIDLEN_SHIFT  0
#define EXP_TID_TIDCTRL_MASK  0x3ULL
#define EXP_TID_TIDCTRL_SHIFT 20
#define EXP_TID_TIDIDX_MASK   0x3FFULL
#define EXP_TID_TIDIDX_SHIFT  22
#define EXP_TID_GET(tid, field)	\
	(((tid) >> EXP_TID_TID##field##_SHIFT) & EXP_TID_TID##field##_MASK)

#define EXP_TID_SET(field, value)			\
	(((value) & EXP_TID_TID##field##_MASK) <<	\
	 EXP_TID_TID##field##_SHIFT)
#define EXP_TID_CLEAR(tid, field) ({					\
		(tid) &= ~(EXP_TID_TID##field##_MASK <<			\
			   EXP_TID_TID##field##_SHIFT);			\
		})
#define EXP_TID_RESET(tid, field, value) do {				\
		EXP_TID_CLEAR(tid, field);				\
		(tid) |= EXP_TID_SET(field, (value));			\
	} while (0)

void hfi1_user_exp_rcv_grp_free(struct hfi1_ctxtdata *uctxt);
int hfi1_user_exp_rcv_grp_init(struct hfi1_filedata *fd);
int hfi1_user_exp_rcv_init(struct hfi1_filedata *fd);
void hfi1_user_exp_rcv_free(struct hfi1_filedata *fd);
int hfi1_user_exp_rcv_setup(struct hfi1_filedata *fd,
			    struct hfi1_tid_info *tinfo);
int hfi1_user_exp_rcv_clear(struct hfi1_filedata *fd,
			    struct hfi1_tid_info *tinfo);
int hfi1_user_exp_rcv_invalid(struct hfi1_filedata *fd,
			      struct hfi1_tid_info *tinfo);

#endif /* _HFI1_USER_EXP_RCV_H */
