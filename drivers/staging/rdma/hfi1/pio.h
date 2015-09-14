#ifndef _PIO_H
#define _PIO_H
/*
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2015 Intel Corporation.
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
 * Copyright(c) 2015 Intel Corporation.
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


/* send context types */
#define SC_KERNEL 0
#define SC_ACK    1
#define SC_USER   2
#define SC_MAX    3

/* invalid send context index */
#define INVALID_SCI 0xff

/* PIO buffer release callback function */
typedef void (*pio_release_cb)(void *arg, int code);

/* PIO release codes - in bits, as there could more than one that apply */
#define PRC_OK		0	/* no known error */
#define PRC_STATUS_ERR	0x01	/* credit return due to status error */
#define PRC_PBC		0x02	/* credit return due to PBC */
#define PRC_THRESHOLD	0x04	/* credit return due to threshold */
#define PRC_FILL_ERR	0x08	/* credit return due fill error */
#define PRC_FORCE	0x10	/* credit return due credit force */
#define PRC_SC_DISABLE	0x20	/* clean-up after a context disable */

/* byte helper */
union mix {
	u64 val64;
	u32 val32[2];
	u8  val8[8];
};

/* an allocated PIO buffer */
struct pio_buf {
	struct send_context *sc;/* back pointer to owning send context */
	pio_release_cb cb;	/* called when the buffer is released */
	void *arg;		/* argument for cb */
	void __iomem *start;	/* buffer start address */
	void __iomem *end;	/* context end address */
	unsigned long size;	/* context size, in bytes */
	unsigned long sent_at;	/* buffer is sent when <= free */
	u32 block_count;	/* size of buffer, in blocks */
	u32 qw_written;		/* QW written so far */
	u32 carry_bytes;	/* number of valid bytes in carry */
	union mix carry;	/* pending unwritten bytes */
};

/* cache line aligned pio buffer array */
union pio_shadow_ring {
	struct pio_buf pbuf;
	u64 unused[16];		/* cache line spacer */
} ____cacheline_aligned;

/* per-NUMA send context */
struct send_context {
	/* read-only after init */
	struct hfi1_devdata *dd;		/* device */
	void __iomem *base_addr;	/* start of PIO memory */
	union pio_shadow_ring *sr;	/* shadow ring */
	volatile __le64 *hw_free;	/* HW free counter */
	struct work_struct halt_work;	/* halted context work queue entry */
	unsigned long flags;		/* flags */
	int node;			/* context home node */
	int type;			/* context type */
	u32 sw_index;			/* software index number */
	u32 hw_context;			/* hardware context number */
	u32 credits;			/* number of blocks in context */
	u32 sr_size;			/* size of the shadow ring */
	u32 group;			/* credit return group */
	/* allocator fields */
	spinlock_t alloc_lock ____cacheline_aligned_in_smp;
	unsigned long fill;		/* official alloc count */
	unsigned long alloc_free;	/* copy of free (less cache thrash) */
	u32 sr_head;			/* shadow ring head */
	/* releaser fields */
	spinlock_t release_lock ____cacheline_aligned_in_smp;
	unsigned long free;		/* official free count */
	u32 sr_tail;			/* shadow ring tail */
	/* list for PIO waiters */
	struct list_head piowait  ____cacheline_aligned_in_smp;
	spinlock_t credit_ctrl_lock ____cacheline_aligned_in_smp;
	u64 credit_ctrl;		/* cache for credit control */
	u32 credit_intr_count;		/* count of credit intr users */
	atomic_t buffers_allocated;	/* count of buffers allocated */
	wait_queue_head_t halt_wait;    /* wait until kernel sees interrupt */
};

/* send context flags */
#define SCF_ENABLED 0x01
#define SCF_IN_FREE 0x02
#define SCF_HALTED  0x04
#define SCF_FROZEN  0x08

struct send_context_info {
	struct send_context *sc;	/* allocated working context */
	u16 allocated;			/* has this been allocated? */
	u16 type;			/* context type */
	u16 base;			/* base in PIO array */
	u16 credits;			/* size in PIO array */
};

/* DMA credit return, index is always (context & 0x7) */
struct credit_return {
	volatile __le64 cr[8];
};

/* NUMA indexed credit return array */
struct credit_return_base {
	struct credit_return *va;
	dma_addr_t pa;
};

/* send context configuration sizes (one per type) */
struct sc_config_sizes {
	short int size;
	short int count;
};

/* send context functions */
int init_credit_return(struct hfi1_devdata *dd);
void free_credit_return(struct hfi1_devdata *dd);
int init_sc_pools_and_sizes(struct hfi1_devdata *dd);
int init_send_contexts(struct hfi1_devdata *dd);
int init_credit_return(struct hfi1_devdata *dd);
int init_pervl_scs(struct hfi1_devdata *dd);
struct send_context *sc_alloc(struct hfi1_devdata *dd, int type,
			      uint hdrqentsize, int numa);
void sc_free(struct send_context *sc);
int sc_enable(struct send_context *sc);
void sc_disable(struct send_context *sc);
int sc_restart(struct send_context *sc);
void sc_return_credits(struct send_context *sc);
void sc_flush(struct send_context *sc);
void sc_drop(struct send_context *sc);
void sc_stop(struct send_context *sc, int bit);
struct pio_buf *sc_buffer_alloc(struct send_context *sc, u32 dw_len,
			pio_release_cb cb, void *arg);
void sc_release_update(struct send_context *sc);
void sc_return_credits(struct send_context *sc);
void sc_group_release_update(struct hfi1_devdata *dd, u32 hw_context);
void sc_add_credit_return_intr(struct send_context *sc);
void sc_del_credit_return_intr(struct send_context *sc);
void sc_set_cr_threshold(struct send_context *sc, u32 new_threshold);
u32 sc_mtu_to_threshold(struct send_context *sc, u32 mtu, u32 hdrqentsize);
void hfi1_sc_wantpiobuf_intr(struct send_context *sc, u32 needint);
void sc_wait(struct hfi1_devdata *dd);
void set_pio_integrity(struct send_context *sc);

/* support functions */
void pio_reset_all(struct hfi1_devdata *dd);
void pio_freeze(struct hfi1_devdata *dd);
void pio_kernel_unfreeze(struct hfi1_devdata *dd);

/* global PIO send control operations */
#define PSC_GLOBAL_ENABLE 0
#define PSC_GLOBAL_DISABLE 1
#define PSC_GLOBAL_VLARB_ENABLE 2
#define PSC_GLOBAL_VLARB_DISABLE 3
#define PSC_CM_RESET 4
#define PSC_DATA_VL_ENABLE 5
#define PSC_DATA_VL_DISABLE 6

void __cm_reset(struct hfi1_devdata *dd, u64 sendctrl);
void pio_send_control(struct hfi1_devdata *dd, int op);


/* PIO copy routines */
void pio_copy(struct hfi1_devdata *dd, struct pio_buf *pbuf, u64 pbc,
	      const void *from, size_t count);
void seg_pio_copy_start(struct pio_buf *pbuf, u64 pbc,
					const void *from, size_t nbytes);
void seg_pio_copy_mid(struct pio_buf *pbuf, const void *from, size_t nbytes);
void seg_pio_copy_end(struct pio_buf *pbuf);

#endif /* _PIO_H */
