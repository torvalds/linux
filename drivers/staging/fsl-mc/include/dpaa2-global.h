/*
 * Copyright 2014-2016 Freescale Semiconductor Inc.
 * Copyright 2016 NXP
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Freescale Semiconductor nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * ALTERNATIVELY, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") as published by the Free Software
 * Foundation, either version 2 of that License or (at your option) any
 * later version.
 *
 * THIS SOFTWARE IS PROVIDED BY Freescale Semiconductor ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL Freescale Semiconductor BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef __FSL_DPAA2_GLOBAL_H
#define __FSL_DPAA2_GLOBAL_H

#include <linux/types.h>
#include <linux/cpumask.h>
#include "dpaa2-fd.h"

struct dpaa2_dq {
	union {
		struct common {
			u8 verb;
			u8 reserved[63];
		} common;
		struct dq {
			u8 verb;
			u8 stat;
			__le16 seqnum;
			__le16 oprid;
			u8 reserved;
			u8 tok;
			__le32 fqid;
			u32 reserved2;
			__le32 fq_byte_cnt;
			__le32 fq_frm_cnt;
			__le64 fqd_ctx;
			u8 fd[32];
		} dq;
		struct scn {
			u8 verb;
			u8 stat;
			u8 state;
			u8 reserved;
			__le32 rid_tok;
			__le64 ctx;
		} scn;
	};
};

/* Parsing frame dequeue results */
/* FQ empty */
#define DPAA2_DQ_STAT_FQEMPTY       0x80
/* FQ held active */
#define DPAA2_DQ_STAT_HELDACTIVE    0x40
/* FQ force eligible */
#define DPAA2_DQ_STAT_FORCEELIGIBLE 0x20
/* valid frame */
#define DPAA2_DQ_STAT_VALIDFRAME    0x10
/* FQ ODP enable */
#define DPAA2_DQ_STAT_ODPVALID      0x04
/* volatile dequeue */
#define DPAA2_DQ_STAT_VOLATILE      0x02
/* volatile dequeue command is expired */
#define DPAA2_DQ_STAT_EXPIRED       0x01

#define DQ_FQID_MASK		0x00FFFFFF
#define DQ_FRAME_COUNT_MASK	0x00FFFFFF

/**
 * dpaa2_dq_flags() - Get the stat field of dequeue response
 * @dq: the dequeue result.
 */
static inline u32 dpaa2_dq_flags(const struct dpaa2_dq *dq)
{
	return dq->dq.stat;
}

/**
 * dpaa2_dq_is_pull() - Check whether the dq response is from a pull
 *                      command.
 * @dq: the dequeue result
 *
 * Return 1 for volatile(pull) dequeue, 0 for static dequeue.
 */
static inline int dpaa2_dq_is_pull(const struct dpaa2_dq *dq)
{
	return (int)(dpaa2_dq_flags(dq) & DPAA2_DQ_STAT_VOLATILE);
}

/**
 * dpaa2_dq_is_pull_complete() - Check whether the pull command is completed.
 * @dq: the dequeue result
 *
 * Return boolean.
 */
static inline bool dpaa2_dq_is_pull_complete(const struct dpaa2_dq *dq)
{
	return !!(dpaa2_dq_flags(dq) & DPAA2_DQ_STAT_EXPIRED);
}

/**
 * dpaa2_dq_seqnum() - Get the seqnum field in dequeue response
 * @dq: the dequeue result
 *
 * seqnum is valid only if VALIDFRAME flag is TRUE
 *
 * Return seqnum.
 */
static inline u16 dpaa2_dq_seqnum(const struct dpaa2_dq *dq)
{
	return le16_to_cpu(dq->dq.seqnum);
}

/**
 * dpaa2_dq_odpid() - Get the odpid field in dequeue response
 * @dq: the dequeue result
 *
 * odpid is valid only if ODPVALID flag is TRUE.
 *
 * Return odpid.
 */
static inline u16 dpaa2_dq_odpid(const struct dpaa2_dq *dq)
{
	return le16_to_cpu(dq->dq.oprid);
}

/**
 * dpaa2_dq_fqid() - Get the fqid in dequeue response
 * @dq: the dequeue result
 *
 * Return fqid.
 */
static inline u32 dpaa2_dq_fqid(const struct dpaa2_dq *dq)
{
	return le32_to_cpu(dq->dq.fqid) & DQ_FQID_MASK;
}

/**
 * dpaa2_dq_byte_count() - Get the byte count in dequeue response
 * @dq: the dequeue result
 *
 * Return the byte count remaining in the FQ.
 */
static inline u32 dpaa2_dq_byte_count(const struct dpaa2_dq *dq)
{
	return le32_to_cpu(dq->dq.fq_byte_cnt);
}

/**
 * dpaa2_dq_frame_count() - Get the frame count in dequeue response
 * @dq: the dequeue result
 *
 * Return the frame count remaining in the FQ.
 */
static inline u32 dpaa2_dq_frame_count(const struct dpaa2_dq *dq)
{
	return le32_to_cpu(dq->dq.fq_frm_cnt) & DQ_FRAME_COUNT_MASK;
}

/**
 * dpaa2_dq_fd_ctx() - Get the frame queue context in dequeue response
 * @dq: the dequeue result
 *
 * Return the frame queue context.
 */
static inline u64 dpaa2_dq_fqd_ctx(const struct dpaa2_dq *dq)
{
	return le64_to_cpu(dq->dq.fqd_ctx);
}

/**
 * dpaa2_dq_fd() - Get the frame descriptor in dequeue response
 * @dq: the dequeue result
 *
 * Return the frame descriptor.
 */
static inline const struct dpaa2_fd *dpaa2_dq_fd(const struct dpaa2_dq *dq)
{
	return (const struct dpaa2_fd *)&dq->dq.fd[0];
}

#endif /* __FSL_DPAA2_GLOBAL_H */
