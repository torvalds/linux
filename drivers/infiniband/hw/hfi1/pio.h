#ifndef _PIO_H
#define _PIO_H
/*
 * Copyright(c) 2015-2017 Intel Corporation.
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

/* send context types */
#define SC_KERNEL 0
#define SC_VL15   1
#define SC_ACK    2
#define SC_USER   3	/* must be the last one: it may take all left */
#define SC_MAX    4	/* count of send context types */

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
	unsigned long sent_at;	/* buffer is sent when <= free */
	union mix carry;	/* pending unwritten bytes */
	u16 qw_written;		/* QW written so far */
	u8 carry_bytes;	/* number of valid bytes in carry */
};

/* cache line aligned pio buffer array */
union pio_shadow_ring {
	struct pio_buf pbuf;
} ____cacheline_aligned;

/* per-NUMA send context */
struct send_context {
	/* read-only after init */
	struct hfi1_devdata *dd;		/* device */
	union pio_shadow_ring *sr;	/* shadow ring */
	void __iomem *base_addr;	/* start of PIO memory */
	u32 __percpu *buffers_allocated;/* count of buffers allocated */
	u32 size;			/* context size, in bytes */

	int node;			/* context home node */
	u32 sr_size;			/* size of the shadow ring */
	u16 flags;			/* flags */
	u8  type;			/* context type */
	u8  sw_index;			/* software index number */
	u8  hw_context;			/* hardware context number */
	u8  group;			/* credit return group */

	/* allocator fields */
	spinlock_t alloc_lock ____cacheline_aligned_in_smp;
	u32 sr_head;			/* shadow ring head */
	unsigned long fill;		/* official alloc count */
	unsigned long alloc_free;	/* copy of free (less cache thrash) */
	u32 fill_wrap;			/* tracks fill within ring */
	u32 credits;			/* number of blocks in context */
	/* adding a new field here would make it part of this cacheline */

	/* releaser fields */
	spinlock_t release_lock ____cacheline_aligned_in_smp;
	u32 sr_tail;			/* shadow ring tail */
	unsigned long free;		/* official free count */
	volatile __le64 *hw_free;	/* HW free counter */
	/* list for PIO waiters */
	struct list_head piowait  ____cacheline_aligned_in_smp;
	spinlock_t credit_ctrl_lock ____cacheline_aligned_in_smp;
	u32 credit_intr_count;		/* count of credit intr users */
	u64 credit_ctrl;		/* cache for credit control */
	wait_queue_head_t halt_wait;    /* wait until kernel sees interrupt */
	struct work_struct halt_work;	/* halted context work queue entry */
};

/* send context flags */
#define SCF_ENABLED 0x01
#define SCF_IN_FREE 0x02
#define SCF_HALTED  0x04
#define SCF_FROZEN  0x08
#define SCF_LINK_DOWN 0x10

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
	dma_addr_t dma;
};

/* send context configuration sizes (one per type) */
struct sc_config_sizes {
	short int size;
	short int count;
};

/*
 * The diagram below details the relationship of the mapping structures
 *
 * Since the mapping now allows for non-uniform send contexts per vl, the
 * number of send contexts for a vl is either the vl_scontexts[vl] or
 * a computation based on num_kernel_send_contexts/num_vls:
 *
 * For example:
 * nactual = vl_scontexts ? vl_scontexts[vl] : num_kernel_send_contexts/num_vls
 *
 * n = roundup to next highest power of 2 using nactual
 *
 * In the case where there are num_kernel_send_contexts/num_vls doesn't divide
 * evenly, the extras are added from the last vl downward.
 *
 * For the case where n > nactual, the send contexts are assigned
 * in a round robin fashion wrapping back to the first send context
 * for a particular vl.
 *
 *               dd->pio_map
 *                    |                                   pio_map_elem[0]
 *                    |                                +--------------------+
 *                    v                                |       mask         |
 *               pio_vl_map                            |--------------------|
 *      +--------------------------+                   | ksc[0] -> sc 1     |
 *      |    list (RCU)            |                   |--------------------|
 *      |--------------------------|                 ->| ksc[1] -> sc 2     |
 *      |    mask                  |              --/  |--------------------|
 *      |--------------------------|            -/     |        *           |
 *      |    actual_vls (max 8)    |          -/       |--------------------|
 *      |--------------------------|       --/         | ksc[n-1] -> sc n   |
 *      |    vls (max 8)           |     -/            +--------------------+
 *      |--------------------------|  --/
 *      |    map[0]                |-/
 *      |--------------------------|                   +--------------------+
 *      |    map[1]                |---                |       mask         |
 *      |--------------------------|   \----           |--------------------|
 *      |           *              |        \--        | ksc[0] -> sc 1+n   |
 *      |           *              |           \----   |--------------------|
 *      |           *              |                \->| ksc[1] -> sc 2+n   |
 *      |--------------------------|                   |--------------------|
 *      |   map[vls - 1]           |-                  |         *          |
 *      +--------------------------+ \-                |--------------------|
 *                                     \-              | ksc[m-1] -> sc m+n |
 *                                       \             +--------------------+
 *                                        \-
 *                                          \
 *                                           \-        +----------------------+
 *                                             \-      |       mask           |
 *                                               \     |----------------------|
 *                                                \-   | ksc[0] -> sc 1+m+n   |
 *                                                  \- |----------------------|
 *                                                    >| ksc[1] -> sc 2+m+n   |
 *                                                     |----------------------|
 *                                                     |         *            |
 *                                                     |----------------------|
 *                                                     | ksc[o-1] -> sc o+m+n |
 *                                                     +----------------------+
 *
 */

/* Initial number of send contexts per VL */
#define INIT_SC_PER_VL 2

/*
 * struct pio_map_elem - mapping for a vl
 * @mask - selector mask
 * @ksc - array of kernel send contexts for this vl
 *
 * The mask is used to "mod" the selector to
 * produce index into the trailing array of
 * kscs
 */
struct pio_map_elem {
	u32 mask;
	struct send_context *ksc[0];
};

/*
 * struct pio_vl_map - mapping for a vl
 * @list - rcu head for free callback
 * @mask - vl mask to "mod" the vl to produce an index to map array
 * @actual_vls - number of vls
 * @vls - numbers of vls rounded to next power of 2
 * @map - array of pio_map_elem entries
 *
 * This is the parent mapping structure. The trailing members of the
 * struct point to pio_map_elem entries, which in turn point to an
 * array of kscs for that vl.
 */
struct pio_vl_map {
	struct rcu_head list;
	u32 mask;
	u8 actual_vls;
	u8 vls;
	struct pio_map_elem *map[0];
};

int pio_map_init(struct hfi1_devdata *dd, u8 port, u8 num_vls,
		 u8 *vl_scontexts);
void free_pio_map(struct hfi1_devdata *dd);
struct send_context *pio_select_send_context_vl(struct hfi1_devdata *dd,
						u32 selector, u8 vl);
struct send_context *pio_select_send_context_sc(struct hfi1_devdata *dd,
						u32 selector, u8 sc5);

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
u32 sc_percent_to_threshold(struct send_context *sc, u32 percent);
u32 sc_mtu_to_threshold(struct send_context *sc, u32 mtu, u32 hdrqentsize);
void hfi1_sc_wantpiobuf_intr(struct send_context *sc, u32 needint);
void sc_wait(struct hfi1_devdata *dd);
void set_pio_integrity(struct send_context *sc);

/* support functions */
void pio_reset_all(struct hfi1_devdata *dd);
void pio_freeze(struct hfi1_devdata *dd);
void pio_kernel_unfreeze(struct hfi1_devdata *dd);
void pio_kernel_linkup(struct hfi1_devdata *dd);

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

void seqfile_dump_sci(struct seq_file *s, u32 i,
		      struct send_context_info *sci);

#endif /* _PIO_H */
