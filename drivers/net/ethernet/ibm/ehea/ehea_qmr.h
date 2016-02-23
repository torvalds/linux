/*
 *  linux/drivers/net/ethernet/ibm/ehea/ehea_qmr.h
 *
 *  eHEA ethernet device driver for IBM eServer System p
 *
 *  (C) Copyright IBM Corp. 2006
 *
 *  Authors:
 *       Christoph Raisch <raisch@de.ibm.com>
 *       Jan-Bernd Themann <themann@de.ibm.com>
 *       Thomas Klein <tklein@de.ibm.com>
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef __EHEA_QMR_H__
#define __EHEA_QMR_H__

#include <linux/prefetch.h>
#include "ehea.h"
#include "ehea_hw.h"

/*
 * page size of ehea hardware queues
 */

#define EHEA_PAGESHIFT         12
#define EHEA_PAGESIZE          (1UL << EHEA_PAGESHIFT)
#define EHEA_SECTSIZE          (1UL << 24)
#define EHEA_PAGES_PER_SECTION (EHEA_SECTSIZE >> EHEA_PAGESHIFT)
#define EHEA_HUGEPAGESHIFT     34
#define EHEA_HUGEPAGE_SIZE     (1UL << EHEA_HUGEPAGESHIFT)
#define EHEA_HUGEPAGE_PFN_MASK ((EHEA_HUGEPAGE_SIZE - 1) >> PAGE_SHIFT)

#if ((1UL << SECTION_SIZE_BITS) < EHEA_SECTSIZE)
#error eHEA module cannot work if kernel sectionsize < ehea sectionsize
#endif

/* Some abbreviations used here:
 *
 * WQE  - Work Queue Entry
 * SWQE - Send Work Queue Entry
 * RWQE - Receive Work Queue Entry
 * CQE  - Completion Queue Entry
 * EQE  - Event Queue Entry
 * MR   - Memory Region
 */

/* Use of WR_ID field for EHEA */
#define EHEA_WR_ID_COUNT   EHEA_BMASK_IBM(0, 19)
#define EHEA_WR_ID_TYPE    EHEA_BMASK_IBM(20, 23)
#define EHEA_SWQE2_TYPE    0x1
#define EHEA_SWQE3_TYPE    0x2
#define EHEA_RWQE2_TYPE    0x3
#define EHEA_RWQE3_TYPE    0x4
#define EHEA_WR_ID_INDEX   EHEA_BMASK_IBM(24, 47)
#define EHEA_WR_ID_REFILL  EHEA_BMASK_IBM(48, 63)

struct ehea_vsgentry {
	u64 vaddr;
	u32 l_key;
	u32 len;
};

/* maximum number of sg entries allowed in a WQE */
#define EHEA_MAX_WQE_SG_ENTRIES  	252
#define SWQE2_MAX_IMM            	(0xD0 - 0x30)
#define SWQE3_MAX_IMM            	224

/* tx control flags for swqe */
#define EHEA_SWQE_CRC                   0x8000
#define EHEA_SWQE_IP_CHECKSUM           0x4000
#define EHEA_SWQE_TCP_CHECKSUM          0x2000
#define EHEA_SWQE_TSO                   0x1000
#define EHEA_SWQE_SIGNALLED_COMPLETION  0x0800
#define EHEA_SWQE_VLAN_INSERT           0x0400
#define EHEA_SWQE_IMM_DATA_PRESENT      0x0200
#define EHEA_SWQE_DESCRIPTORS_PRESENT   0x0100
#define EHEA_SWQE_WRAP_CTL_REC          0x0080
#define EHEA_SWQE_WRAP_CTL_FORCE        0x0040
#define EHEA_SWQE_BIND                  0x0020
#define EHEA_SWQE_PURGE                 0x0010

/* sizeof(struct ehea_swqe) less the union */
#define SWQE_HEADER_SIZE		32

struct ehea_swqe {
	u64 wr_id;
	u16 tx_control;
	u16 vlan_tag;
	u8 reserved1;
	u8 ip_start;
	u8 ip_end;
	u8 immediate_data_length;
	u8 tcp_offset;
	u8 reserved2;
	u16 reserved2b;
	u8 wrap_tag;
	u8 descriptors;		/* number of valid descriptors in WQE */
	u16 reserved3;
	u16 reserved4;
	u16 mss;
	u32 reserved5;
	union {
		/*  Send WQE Format 1 */
		struct {
			struct ehea_vsgentry sg_list[EHEA_MAX_WQE_SG_ENTRIES];
		} no_immediate_data;

		/*  Send WQE Format 2 */
		struct {
			struct ehea_vsgentry sg_entry;
			/* 0x30 */
			u8 immediate_data[SWQE2_MAX_IMM];
			/* 0xd0 */
			struct ehea_vsgentry sg_list[EHEA_MAX_WQE_SG_ENTRIES-1];
		} immdata_desc __packed;

		/*  Send WQE Format 3 */
		struct {
			u8 immediate_data[SWQE3_MAX_IMM];
		} immdata_nodesc;
	} u;
};

struct ehea_rwqe {
	u64 wr_id;		/* work request ID */
	u8 reserved1[5];
	u8 data_segments;
	u16 reserved2;
	u64 reserved3;
	u64 reserved4;
	struct ehea_vsgentry sg_list[EHEA_MAX_WQE_SG_ENTRIES];
};

#define EHEA_CQE_VLAN_TAG_XTRACT   0x0400

#define EHEA_CQE_TYPE_RQ           0x60
#define EHEA_CQE_STAT_ERR_MASK     0x700F
#define EHEA_CQE_STAT_FAT_ERR_MASK 0xF
#define EHEA_CQE_BLIND_CKSUM       0x8000
#define EHEA_CQE_STAT_ERR_TCP      0x4000
#define EHEA_CQE_STAT_ERR_IP       0x2000
#define EHEA_CQE_STAT_ERR_CRC      0x1000

/* Defines which bad send cqe stati lead to a port reset */
#define EHEA_CQE_STAT_RESET_MASK   0x0002

struct ehea_cqe {
	u64 wr_id;		/* work request ID from WQE */
	u8 type;
	u8 valid;
	u16 status;
	u16 reserved1;
	u16 num_bytes_transfered;
	u16 vlan_tag;
	u16 inet_checksum_value;
	u8 reserved2;
	u8 header_length;
	u16 reserved3;
	u16 page_offset;
	u16 wqe_count;
	u32 qp_token;
	u32 timestamp;
	u32 reserved4;
	u64 reserved5[3];
};

#define EHEA_EQE_VALID           EHEA_BMASK_IBM(0, 0)
#define EHEA_EQE_IS_CQE          EHEA_BMASK_IBM(1, 1)
#define EHEA_EQE_IDENTIFIER      EHEA_BMASK_IBM(2, 7)
#define EHEA_EQE_QP_CQ_NUMBER    EHEA_BMASK_IBM(8, 31)
#define EHEA_EQE_QP_TOKEN        EHEA_BMASK_IBM(32, 63)
#define EHEA_EQE_CQ_TOKEN        EHEA_BMASK_IBM(32, 63)
#define EHEA_EQE_KEY             EHEA_BMASK_IBM(32, 63)
#define EHEA_EQE_PORT_NUMBER     EHEA_BMASK_IBM(56, 63)
#define EHEA_EQE_EQ_NUMBER       EHEA_BMASK_IBM(48, 63)
#define EHEA_EQE_SM_ID           EHEA_BMASK_IBM(48, 63)
#define EHEA_EQE_SM_MECH_NUMBER  EHEA_BMASK_IBM(48, 55)
#define EHEA_EQE_SM_PORT_NUMBER  EHEA_BMASK_IBM(56, 63)

#define EHEA_AER_RESTYPE_QP  0x8
#define EHEA_AER_RESTYPE_CQ  0x4
#define EHEA_AER_RESTYPE_EQ  0x3

/* Defines which affiliated errors lead to a port reset */
#define EHEA_AER_RESET_MASK   0xFFFFFFFFFEFFFFFFULL
#define EHEA_AERR_RESET_MASK  0xFFFFFFFFFFFFFFFFULL

struct ehea_eqe {
	u64 entry;
};

#define ERROR_DATA_LENGTH  EHEA_BMASK_IBM(52, 63)
#define ERROR_DATA_TYPE    EHEA_BMASK_IBM(0, 7)

static inline void *hw_qeit_calc(struct hw_queue *queue, u64 q_offset)
{
	struct ehea_page *current_page;

	if (q_offset >= queue->queue_length)
		q_offset -= queue->queue_length;
	current_page = (queue->queue_pages)[q_offset >> EHEA_PAGESHIFT];
	return &current_page->entries[q_offset & (EHEA_PAGESIZE - 1)];
}

static inline void *hw_qeit_get(struct hw_queue *queue)
{
	return hw_qeit_calc(queue, queue->current_q_offset);
}

static inline void hw_qeit_inc(struct hw_queue *queue)
{
	queue->current_q_offset += queue->qe_size;
	if (queue->current_q_offset >= queue->queue_length) {
		queue->current_q_offset = 0;
		/* toggle the valid flag */
		queue->toggle_state = (~queue->toggle_state) & 1;
	}
}

static inline void *hw_qeit_get_inc(struct hw_queue *queue)
{
	void *retvalue = hw_qeit_get(queue);
	hw_qeit_inc(queue);
	return retvalue;
}

static inline void *hw_qeit_get_inc_valid(struct hw_queue *queue)
{
	struct ehea_cqe *retvalue = hw_qeit_get(queue);
	u8 valid = retvalue->valid;
	void *pref;

	if ((valid >> 7) == (queue->toggle_state & 1)) {
		/* this is a good one */
		hw_qeit_inc(queue);
		pref = hw_qeit_calc(queue, queue->current_q_offset);
		prefetch(pref);
		prefetch(pref + 128);
	} else
		retvalue = NULL;
	return retvalue;
}

static inline void *hw_qeit_get_valid(struct hw_queue *queue)
{
	struct ehea_cqe *retvalue = hw_qeit_get(queue);
	void *pref;
	u8 valid;

	pref = hw_qeit_calc(queue, queue->current_q_offset);
	prefetch(pref);
	prefetch(pref + 128);
	prefetch(pref + 256);
	valid = retvalue->valid;
	if (!((valid >> 7) == (queue->toggle_state & 1)))
		retvalue = NULL;
	return retvalue;
}

static inline void *hw_qeit_reset(struct hw_queue *queue)
{
	queue->current_q_offset = 0;
	return hw_qeit_get(queue);
}

static inline void *hw_qeit_eq_get_inc(struct hw_queue *queue)
{
	u64 last_entry_in_q = queue->queue_length - queue->qe_size;
	void *retvalue;

	retvalue = hw_qeit_get(queue);
	queue->current_q_offset += queue->qe_size;
	if (queue->current_q_offset > last_entry_in_q) {
		queue->current_q_offset = 0;
		queue->toggle_state = (~queue->toggle_state) & 1;
	}
	return retvalue;
}

static inline void *hw_eqit_eq_get_inc_valid(struct hw_queue *queue)
{
	void *retvalue = hw_qeit_get(queue);
	u32 qe = *(u8 *)retvalue;
	if ((qe >> 7) == (queue->toggle_state & 1))
		hw_qeit_eq_get_inc(queue);
	else
		retvalue = NULL;
	return retvalue;
}

static inline struct ehea_rwqe *ehea_get_next_rwqe(struct ehea_qp *qp,
						   int rq_nr)
{
	struct hw_queue *queue;

	if (rq_nr == 1)
		queue = &qp->hw_rqueue1;
	else if (rq_nr == 2)
		queue = &qp->hw_rqueue2;
	else
		queue = &qp->hw_rqueue3;

	return hw_qeit_get_inc(queue);
}

static inline struct ehea_swqe *ehea_get_swqe(struct ehea_qp *my_qp,
					      int *wqe_index)
{
	struct hw_queue *queue = &my_qp->hw_squeue;
	struct ehea_swqe *wqe_p;

	*wqe_index = (queue->current_q_offset) >> (7 + EHEA_SG_SQ);
	wqe_p = hw_qeit_get_inc(&my_qp->hw_squeue);

	return wqe_p;
}

static inline void ehea_post_swqe(struct ehea_qp *my_qp, struct ehea_swqe *swqe)
{
	iosync();
	ehea_update_sqa(my_qp, 1);
}

static inline struct ehea_cqe *ehea_poll_rq1(struct ehea_qp *qp, int *wqe_index)
{
	struct hw_queue *queue = &qp->hw_rqueue1;

	*wqe_index = (queue->current_q_offset) >> (7 + EHEA_SG_RQ1);
	return hw_qeit_get_valid(queue);
}

static inline void ehea_inc_cq(struct ehea_cq *cq)
{
	hw_qeit_inc(&cq->hw_queue);
}

static inline void ehea_inc_rq1(struct ehea_qp *qp)
{
	hw_qeit_inc(&qp->hw_rqueue1);
}

static inline struct ehea_cqe *ehea_poll_cq(struct ehea_cq *my_cq)
{
	return hw_qeit_get_valid(&my_cq->hw_queue);
}

#define EHEA_CQ_REGISTER_ORIG 0
#define EHEA_EQ_REGISTER_ORIG 0

enum ehea_eq_type {
	EHEA_EQ = 0,		/* event queue              */
	EHEA_NEQ		/* notification event queue */
};

struct ehea_eq *ehea_create_eq(struct ehea_adapter *adapter,
			       enum ehea_eq_type type,
			       const u32 length, const u8 eqe_gen);

int ehea_destroy_eq(struct ehea_eq *eq);

struct ehea_eqe *ehea_poll_eq(struct ehea_eq *eq);

struct ehea_cq *ehea_create_cq(struct ehea_adapter *adapter, int cqe,
			       u64 eq_handle, u32 cq_token);

int ehea_destroy_cq(struct ehea_cq *cq);

struct ehea_qp *ehea_create_qp(struct ehea_adapter *adapter, u32 pd,
			       struct ehea_qp_init_attr *init_attr);

int ehea_destroy_qp(struct ehea_qp *qp);

int ehea_reg_kernel_mr(struct ehea_adapter *adapter, struct ehea_mr *mr);

int ehea_gen_smr(struct ehea_adapter *adapter, struct ehea_mr *old_mr,
		 struct ehea_mr *shared_mr);

int ehea_rem_mr(struct ehea_mr *mr);

u64 ehea_error_data(struct ehea_adapter *adapter, u64 res_handle,
		    u64 *aer, u64 *aerr);

int ehea_add_sect_bmap(unsigned long pfn, unsigned long nr_pages);
int ehea_rem_sect_bmap(unsigned long pfn, unsigned long nr_pages);
int ehea_create_busmap(void);
void ehea_destroy_busmap(void);
u64 ehea_map_vaddr(void *caddr);

#endif	/* __EHEA_QMR_H__ */
