/* QLogic qed NIC Driver
 * Copyright (c) 2015 QLogic Corporation
 *
 * This software is available under the terms of the GNU General Public License
 * (GPL) Version 2, available from the file COPYING in the main directory of
 * this source tree.
 */

#include <linux/types.h>
#include <asm/byteorder.h>
#include <linux/io.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/string.h>
#include "qed.h"
#include "qed_hsi.h"
#include "qed_hw.h"
#include "qed_init_ops.h"
#include "qed_int.h"
#include "qed_mcp.h"
#include "qed_reg_addr.h"
#include "qed_sp.h"

struct qed_pi_info {
	qed_int_comp_cb_t	comp_cb;
	void			*cookie;
};

struct qed_sb_sp_info {
	struct qed_sb_info	sb_info;

	/* per protocol index data */
	struct qed_pi_info	pi_info_arr[PIS_PER_SB];
};

#define SB_ATTN_ALIGNED_SIZE(p_hwfn) \
	ALIGNED_TYPE_SIZE(struct atten_status_block, p_hwfn)

#define ATTN_STATE_BITS (0xfff)
#define ATTN_BITS_MASKABLE      (0x3ff)
struct qed_sb_attn_info {
	/* Virtual & Physical address of the SB */
	struct atten_status_block       *sb_attn;
	dma_addr_t		      sb_phys;

	/* Last seen running index */
	u16			     index;

	/* Previously asserted attentions, which are still unasserted */
	u16			     known_attn;

	/* Cleanup address for the link's general hw attention */
	u32			     mfw_attn_addr;
};

static inline u16 qed_attn_update_idx(struct qed_hwfn *p_hwfn,
				      struct qed_sb_attn_info   *p_sb_desc)
{
	u16     rc = 0;
	u16     index;

	/* Make certain HW write took affect */
	mmiowb();

	index = le16_to_cpu(p_sb_desc->sb_attn->sb_index);
	if (p_sb_desc->index != index) {
		p_sb_desc->index	= index;
		rc		      = QED_SB_ATT_IDX;
	}

	/* Make certain we got a consistent view with HW */
	mmiowb();

	return rc;
}

/**
 *  @brief qed_int_assertion - handles asserted attention bits
 *
 *  @param p_hwfn
 *  @param asserted_bits newly asserted bits
 *  @return int
 */
static int qed_int_assertion(struct qed_hwfn *p_hwfn,
			     u16 asserted_bits)
{
	struct qed_sb_attn_info *sb_attn_sw = p_hwfn->p_sb_attn;
	u32 igu_mask;

	/* Mask the source of the attention in the IGU */
	igu_mask = qed_rd(p_hwfn, p_hwfn->p_dpc_ptt,
			  IGU_REG_ATTENTION_ENABLE);
	DP_VERBOSE(p_hwfn, NETIF_MSG_INTR, "IGU mask: 0x%08x --> 0x%08x\n",
		   igu_mask, igu_mask & ~(asserted_bits & ATTN_BITS_MASKABLE));
	igu_mask &= ~(asserted_bits & ATTN_BITS_MASKABLE);
	qed_wr(p_hwfn, p_hwfn->p_dpc_ptt, IGU_REG_ATTENTION_ENABLE, igu_mask);

	DP_VERBOSE(p_hwfn, NETIF_MSG_INTR,
		   "inner known ATTN state: 0x%04x --> 0x%04x\n",
		   sb_attn_sw->known_attn,
		   sb_attn_sw->known_attn | asserted_bits);
	sb_attn_sw->known_attn |= asserted_bits;

	/* Handle MCP events */
	if (asserted_bits & 0x100) {
		qed_mcp_handle_events(p_hwfn, p_hwfn->p_dpc_ptt);
		/* Clean the MCP attention */
		qed_wr(p_hwfn, p_hwfn->p_dpc_ptt,
		       sb_attn_sw->mfw_attn_addr, 0);
	}

	DIRECT_REG_WR((u8 __iomem *)p_hwfn->regview +
		      GTT_BAR0_MAP_REG_IGU_CMD +
		      ((IGU_CMD_ATTN_BIT_SET_UPPER -
			IGU_CMD_INT_ACK_BASE) << 3),
		      (u32)asserted_bits);

	DP_VERBOSE(p_hwfn, NETIF_MSG_INTR, "set cmd IGU: 0x%04x\n",
		   asserted_bits);

	return 0;
}

/**
 * @brief - handles deassertion of previously asserted attentions.
 *
 * @param p_hwfn
 * @param deasserted_bits - newly deasserted bits
 * @return int
 *
 */
static int qed_int_deassertion(struct qed_hwfn  *p_hwfn,
			       u16 deasserted_bits)
{
	struct qed_sb_attn_info *sb_attn_sw = p_hwfn->p_sb_attn;
	u32 aeu_mask;

	if (deasserted_bits != 0x100)
		DP_ERR(p_hwfn, "Unexpected - non-link deassertion\n");

	/* Clear IGU indication for the deasserted bits */
	DIRECT_REG_WR((u8 __iomem *)p_hwfn->regview +
		      GTT_BAR0_MAP_REG_IGU_CMD +
		      ((IGU_CMD_ATTN_BIT_CLR_UPPER -
			IGU_CMD_INT_ACK_BASE) << 3),
		      ~((u32)deasserted_bits));

	/* Unmask deasserted attentions in IGU */
	aeu_mask = qed_rd(p_hwfn, p_hwfn->p_dpc_ptt,
			  IGU_REG_ATTENTION_ENABLE);
	aeu_mask |= (deasserted_bits & ATTN_BITS_MASKABLE);
	qed_wr(p_hwfn, p_hwfn->p_dpc_ptt, IGU_REG_ATTENTION_ENABLE, aeu_mask);

	/* Clear deassertion from inner state */
	sb_attn_sw->known_attn &= ~deasserted_bits;

	return 0;
}

static int qed_int_attentions(struct qed_hwfn *p_hwfn)
{
	struct qed_sb_attn_info *p_sb_attn_sw = p_hwfn->p_sb_attn;
	struct atten_status_block *p_sb_attn = p_sb_attn_sw->sb_attn;
	u32 attn_bits = 0, attn_acks = 0;
	u16 asserted_bits, deasserted_bits;
	__le16 index;
	int rc = 0;

	/* Read current attention bits/acks - safeguard against attentions
	 * by guaranting work on a synchronized timeframe
	 */
	do {
		index = p_sb_attn->sb_index;
		/* finish reading index before the loop condition */
		dma_rmb();
		attn_bits = le32_to_cpu(p_sb_attn->atten_bits);
		attn_acks = le32_to_cpu(p_sb_attn->atten_ack);
	} while (index != p_sb_attn->sb_index);
	p_sb_attn->sb_index = index;

	/* Attention / Deassertion are meaningful (and in correct state)
	 * only when they differ and consistent with known state - deassertion
	 * when previous attention & current ack, and assertion when current
	 * attention with no previous attention
	 */
	asserted_bits = (attn_bits & ~attn_acks & ATTN_STATE_BITS) &
		~p_sb_attn_sw->known_attn;
	deasserted_bits = (~attn_bits & attn_acks & ATTN_STATE_BITS) &
		p_sb_attn_sw->known_attn;

	if ((asserted_bits & ~0x100) || (deasserted_bits & ~0x100)) {
		DP_INFO(p_hwfn,
			"Attention: Index: 0x%04x, Bits: 0x%08x, Acks: 0x%08x, asserted: 0x%04x, De-asserted 0x%04x [Prev. known: 0x%04x]\n",
			index, attn_bits, attn_acks, asserted_bits,
			deasserted_bits, p_sb_attn_sw->known_attn);
	} else if (asserted_bits == 0x100) {
		DP_INFO(p_hwfn,
			"MFW indication via attention\n");
	} else {
		DP_VERBOSE(p_hwfn, NETIF_MSG_INTR,
			   "MFW indication [deassertion]\n");
	}

	if (asserted_bits) {
		rc = qed_int_assertion(p_hwfn, asserted_bits);
		if (rc)
			return rc;
	}

	if (deasserted_bits) {
		rc = qed_int_deassertion(p_hwfn, deasserted_bits);
		if (rc)
			return rc;
	}

	return rc;
}

static void qed_sb_ack_attn(struct qed_hwfn *p_hwfn,
			    void __iomem *igu_addr,
			    u32 ack_cons)
{
	struct igu_prod_cons_update igu_ack = { 0 };

	igu_ack.sb_id_and_flags =
		((ack_cons << IGU_PROD_CONS_UPDATE_SB_INDEX_SHIFT) |
		 (1 << IGU_PROD_CONS_UPDATE_UPDATE_FLAG_SHIFT) |
		 (IGU_INT_NOP << IGU_PROD_CONS_UPDATE_ENABLE_INT_SHIFT) |
		 (IGU_SEG_ACCESS_ATTN <<
		  IGU_PROD_CONS_UPDATE_SEGMENT_ACCESS_SHIFT));

	DIRECT_REG_WR(igu_addr, igu_ack.sb_id_and_flags);

	/* Both segments (interrupts & acks) are written to same place address;
	 * Need to guarantee all commands will be received (in-order) by HW.
	 */
	mmiowb();
	barrier();
}

void qed_int_sp_dpc(unsigned long hwfn_cookie)
{
	struct qed_hwfn *p_hwfn = (struct qed_hwfn *)hwfn_cookie;
	struct qed_pi_info *pi_info = NULL;
	struct qed_sb_attn_info *sb_attn;
	struct qed_sb_info *sb_info;
	int arr_size;
	u16 rc = 0;

	if (!p_hwfn->p_sp_sb) {
		DP_ERR(p_hwfn->cdev, "DPC called - no p_sp_sb\n");
		return;
	}

	sb_info = &p_hwfn->p_sp_sb->sb_info;
	arr_size = ARRAY_SIZE(p_hwfn->p_sp_sb->pi_info_arr);
	if (!sb_info) {
		DP_ERR(p_hwfn->cdev,
		       "Status block is NULL - cannot ack interrupts\n");
		return;
	}

	if (!p_hwfn->p_sb_attn) {
		DP_ERR(p_hwfn->cdev, "DPC called - no p_sb_attn");
		return;
	}
	sb_attn = p_hwfn->p_sb_attn;

	DP_VERBOSE(p_hwfn, NETIF_MSG_INTR, "DPC Called! (hwfn %p %d)\n",
		   p_hwfn, p_hwfn->my_id);

	/* Disable ack for def status block. Required both for msix +
	 * inta in non-mask mode, in inta does no harm.
	 */
	qed_sb_ack(sb_info, IGU_INT_DISABLE, 0);

	/* Gather Interrupts/Attentions information */
	if (!sb_info->sb_virt) {
		DP_ERR(
			p_hwfn->cdev,
			"Interrupt Status block is NULL - cannot check for new interrupts!\n");
	} else {
		u32 tmp_index = sb_info->sb_ack;

		rc = qed_sb_update_sb_idx(sb_info);
		DP_VERBOSE(p_hwfn->cdev, NETIF_MSG_INTR,
			   "Interrupt indices: 0x%08x --> 0x%08x\n",
			   tmp_index, sb_info->sb_ack);
	}

	if (!sb_attn || !sb_attn->sb_attn) {
		DP_ERR(
			p_hwfn->cdev,
			"Attentions Status block is NULL - cannot check for new attentions!\n");
	} else {
		u16 tmp_index = sb_attn->index;

		rc |= qed_attn_update_idx(p_hwfn, sb_attn);
		DP_VERBOSE(p_hwfn->cdev, NETIF_MSG_INTR,
			   "Attention indices: 0x%08x --> 0x%08x\n",
			   tmp_index, sb_attn->index);
	}

	/* Check if we expect interrupts at this time. if not just ack them */
	if (!(rc & QED_SB_EVENT_MASK)) {
		qed_sb_ack(sb_info, IGU_INT_ENABLE, 1);
		return;
	}

	/* Check the validity of the DPC ptt. If not ack interrupts and fail */
	if (!p_hwfn->p_dpc_ptt) {
		DP_NOTICE(p_hwfn->cdev, "Failed to allocate PTT\n");
		qed_sb_ack(sb_info, IGU_INT_ENABLE, 1);
		return;
	}

	if (rc & QED_SB_ATT_IDX)
		qed_int_attentions(p_hwfn);

	if (rc & QED_SB_IDX) {
		int pi;

		/* Look for a free index */
		for (pi = 0; pi < arr_size; pi++) {
			pi_info = &p_hwfn->p_sp_sb->pi_info_arr[pi];
			if (pi_info->comp_cb)
				pi_info->comp_cb(p_hwfn, pi_info->cookie);
		}
	}

	if (sb_attn && (rc & QED_SB_ATT_IDX))
		/* This should be done before the interrupts are enabled,
		 * since otherwise a new attention will be generated.
		 */
		qed_sb_ack_attn(p_hwfn, sb_info->igu_addr, sb_attn->index);

	qed_sb_ack(sb_info, IGU_INT_ENABLE, 1);
}

static void qed_int_sb_attn_free(struct qed_hwfn *p_hwfn)
{
	struct qed_dev *cdev   = p_hwfn->cdev;
	struct qed_sb_attn_info *p_sb   = p_hwfn->p_sb_attn;

	if (p_sb) {
		if (p_sb->sb_attn)
			dma_free_coherent(&cdev->pdev->dev,
					  SB_ATTN_ALIGNED_SIZE(p_hwfn),
					  p_sb->sb_attn,
					  p_sb->sb_phys);
		kfree(p_sb);
	}
}

static void qed_int_sb_attn_setup(struct qed_hwfn *p_hwfn,
				  struct qed_ptt *p_ptt)
{
	struct qed_sb_attn_info *sb_info = p_hwfn->p_sb_attn;

	memset(sb_info->sb_attn, 0, sizeof(*sb_info->sb_attn));

	sb_info->index = 0;
	sb_info->known_attn = 0;

	/* Configure Attention Status Block in IGU */
	qed_wr(p_hwfn, p_ptt, IGU_REG_ATTN_MSG_ADDR_L,
	       lower_32_bits(p_hwfn->p_sb_attn->sb_phys));
	qed_wr(p_hwfn, p_ptt, IGU_REG_ATTN_MSG_ADDR_H,
	       upper_32_bits(p_hwfn->p_sb_attn->sb_phys));
}

static void qed_int_sb_attn_init(struct qed_hwfn *p_hwfn,
				 struct qed_ptt *p_ptt,
				 void *sb_virt_addr,
				 dma_addr_t sb_phy_addr)
{
	struct qed_sb_attn_info *sb_info = p_hwfn->p_sb_attn;

	sb_info->sb_attn = sb_virt_addr;
	sb_info->sb_phys = sb_phy_addr;

	/* Set the address of cleanup for the mcp attention */
	sb_info->mfw_attn_addr = (p_hwfn->rel_pf_id << 3) +
				 MISC_REG_AEU_GENERAL_ATTN_0;

	qed_int_sb_attn_setup(p_hwfn, p_ptt);
}

static int qed_int_sb_attn_alloc(struct qed_hwfn *p_hwfn,
				 struct qed_ptt *p_ptt)
{
	struct qed_dev *cdev = p_hwfn->cdev;
	struct qed_sb_attn_info *p_sb;
	void *p_virt;
	dma_addr_t p_phys = 0;

	/* SB struct */
	p_sb = kmalloc(sizeof(*p_sb), GFP_ATOMIC);
	if (!p_sb) {
		DP_NOTICE(cdev, "Failed to allocate `struct qed_sb_attn_info'\n");
		return -ENOMEM;
	}

	/* SB ring  */
	p_virt = dma_alloc_coherent(&cdev->pdev->dev,
				    SB_ATTN_ALIGNED_SIZE(p_hwfn),
				    &p_phys, GFP_KERNEL);

	if (!p_virt) {
		DP_NOTICE(cdev, "Failed to allocate status block (attentions)\n");
		kfree(p_sb);
		return -ENOMEM;
	}

	/* Attention setup */
	p_hwfn->p_sb_attn = p_sb;
	qed_int_sb_attn_init(p_hwfn, p_ptt, p_virt, p_phys);

	return 0;
}

/* coalescing timeout = timeset << (timer_res + 1) */
#define QED_CAU_DEF_RX_USECS 24
#define QED_CAU_DEF_TX_USECS 48

void qed_init_cau_sb_entry(struct qed_hwfn *p_hwfn,
			   struct cau_sb_entry *p_sb_entry,
			   u8 pf_id,
			   u16 vf_number,
			   u8 vf_valid)
{
	u32 cau_state;

	memset(p_sb_entry, 0, sizeof(*p_sb_entry));

	SET_FIELD(p_sb_entry->params, CAU_SB_ENTRY_PF_NUMBER, pf_id);
	SET_FIELD(p_sb_entry->params, CAU_SB_ENTRY_VF_NUMBER, vf_number);
	SET_FIELD(p_sb_entry->params, CAU_SB_ENTRY_VF_VALID, vf_valid);
	SET_FIELD(p_sb_entry->params, CAU_SB_ENTRY_SB_TIMESET0, 0x7F);
	SET_FIELD(p_sb_entry->params, CAU_SB_ENTRY_SB_TIMESET1, 0x7F);

	/* setting the time resultion to a fixed value ( = 1) */
	SET_FIELD(p_sb_entry->params, CAU_SB_ENTRY_TIMER_RES0,
		  QED_CAU_DEF_RX_TIMER_RES);
	SET_FIELD(p_sb_entry->params, CAU_SB_ENTRY_TIMER_RES1,
		  QED_CAU_DEF_TX_TIMER_RES);

	cau_state = CAU_HC_DISABLE_STATE;

	if (p_hwfn->cdev->int_coalescing_mode == QED_COAL_MODE_ENABLE) {
		cau_state = CAU_HC_ENABLE_STATE;
		if (!p_hwfn->cdev->rx_coalesce_usecs)
			p_hwfn->cdev->rx_coalesce_usecs =
				QED_CAU_DEF_RX_USECS;
		if (!p_hwfn->cdev->tx_coalesce_usecs)
			p_hwfn->cdev->tx_coalesce_usecs =
				QED_CAU_DEF_TX_USECS;
	}

	SET_FIELD(p_sb_entry->data, CAU_SB_ENTRY_STATE0, cau_state);
	SET_FIELD(p_sb_entry->data, CAU_SB_ENTRY_STATE1, cau_state);
}

void qed_int_cau_conf_sb(struct qed_hwfn *p_hwfn,
			 struct qed_ptt *p_ptt,
			 dma_addr_t sb_phys,
			 u16 igu_sb_id,
			 u16 vf_number,
			 u8 vf_valid)
{
	struct cau_sb_entry sb_entry;
	u32 val;

	qed_init_cau_sb_entry(p_hwfn, &sb_entry, p_hwfn->rel_pf_id,
			      vf_number, vf_valid);

	if (p_hwfn->hw_init_done) {
		val = CAU_REG_SB_ADDR_MEMORY + igu_sb_id * sizeof(u64);
		qed_wr(p_hwfn, p_ptt, val, lower_32_bits(sb_phys));
		qed_wr(p_hwfn, p_ptt, val + sizeof(u32),
		       upper_32_bits(sb_phys));

		val = CAU_REG_SB_VAR_MEMORY + igu_sb_id * sizeof(u64);
		qed_wr(p_hwfn, p_ptt, val, sb_entry.data);
		qed_wr(p_hwfn, p_ptt, val + sizeof(u32), sb_entry.params);
	} else {
		/* Initialize Status Block Address */
		STORE_RT_REG_AGG(p_hwfn,
				 CAU_REG_SB_ADDR_MEMORY_RT_OFFSET +
				 igu_sb_id * 2,
				 sb_phys);

		STORE_RT_REG_AGG(p_hwfn,
				 CAU_REG_SB_VAR_MEMORY_RT_OFFSET +
				 igu_sb_id * 2,
				 sb_entry);
	}

	/* Configure pi coalescing if set */
	if (p_hwfn->cdev->int_coalescing_mode == QED_COAL_MODE_ENABLE) {
		u8 timeset = p_hwfn->cdev->rx_coalesce_usecs >>
			     (QED_CAU_DEF_RX_TIMER_RES + 1);
		u8 num_tc = 1, i;

		qed_int_cau_conf_pi(p_hwfn, p_ptt, igu_sb_id, RX_PI,
				    QED_COAL_RX_STATE_MACHINE,
				    timeset);

		timeset = p_hwfn->cdev->tx_coalesce_usecs >>
			  (QED_CAU_DEF_TX_TIMER_RES + 1);

		for (i = 0; i < num_tc; i++) {
			qed_int_cau_conf_pi(p_hwfn, p_ptt,
					    igu_sb_id, TX_PI(i),
					    QED_COAL_TX_STATE_MACHINE,
					    timeset);
		}
	}
}

void qed_int_cau_conf_pi(struct qed_hwfn *p_hwfn,
			 struct qed_ptt *p_ptt,
			 u16 igu_sb_id,
			 u32 pi_index,
			 enum qed_coalescing_fsm coalescing_fsm,
			 u8 timeset)
{
	struct cau_pi_entry pi_entry;
	u32 sb_offset;
	u32 pi_offset;

	sb_offset = igu_sb_id * PIS_PER_SB;
	memset(&pi_entry, 0, sizeof(struct cau_pi_entry));

	SET_FIELD(pi_entry.prod, CAU_PI_ENTRY_PI_TIMESET, timeset);
	if (coalescing_fsm == QED_COAL_RX_STATE_MACHINE)
		SET_FIELD(pi_entry.prod, CAU_PI_ENTRY_FSM_SEL, 0);
	else
		SET_FIELD(pi_entry.prod, CAU_PI_ENTRY_FSM_SEL, 1);

	pi_offset = sb_offset + pi_index;
	if (p_hwfn->hw_init_done) {
		qed_wr(p_hwfn, p_ptt,
		       CAU_REG_PI_MEMORY + pi_offset * sizeof(u32),
		       *((u32 *)&(pi_entry)));
	} else {
		STORE_RT_REG(p_hwfn,
			     CAU_REG_PI_MEMORY_RT_OFFSET + pi_offset,
			     *((u32 *)&(pi_entry)));
	}
}

void qed_int_sb_setup(struct qed_hwfn *p_hwfn,
		      struct qed_ptt *p_ptt,
		      struct qed_sb_info *sb_info)
{
	/* zero status block and ack counter */
	sb_info->sb_ack = 0;
	memset(sb_info->sb_virt, 0, sizeof(*sb_info->sb_virt));

	qed_int_cau_conf_sb(p_hwfn, p_ptt, sb_info->sb_phys,
			    sb_info->igu_sb_id, 0, 0);
}

/**
 * @brief qed_get_igu_sb_id - given a sw sb_id return the
 *        igu_sb_id
 *
 * @param p_hwfn
 * @param sb_id
 *
 * @return u16
 */
static u16 qed_get_igu_sb_id(struct qed_hwfn *p_hwfn,
			     u16 sb_id)
{
	u16 igu_sb_id;

	/* Assuming continuous set of IGU SBs dedicated for given PF */
	if (sb_id == QED_SP_SB_ID)
		igu_sb_id = p_hwfn->hw_info.p_igu_info->igu_dsb_id;
	else
		igu_sb_id = sb_id + p_hwfn->hw_info.p_igu_info->igu_base_sb;

	DP_VERBOSE(p_hwfn, NETIF_MSG_INTR, "SB [%s] index is 0x%04x\n",
		   (sb_id == QED_SP_SB_ID) ? "DSB" : "non-DSB", igu_sb_id);

	return igu_sb_id;
}

int qed_int_sb_init(struct qed_hwfn *p_hwfn,
		    struct qed_ptt *p_ptt,
		    struct qed_sb_info *sb_info,
		    void *sb_virt_addr,
		    dma_addr_t sb_phy_addr,
		    u16 sb_id)
{
	sb_info->sb_virt = sb_virt_addr;
	sb_info->sb_phys = sb_phy_addr;

	sb_info->igu_sb_id = qed_get_igu_sb_id(p_hwfn, sb_id);

	if (sb_id != QED_SP_SB_ID) {
		p_hwfn->sbs_info[sb_id] = sb_info;
		p_hwfn->num_sbs++;
	}

	sb_info->cdev = p_hwfn->cdev;

	/* The igu address will hold the absolute address that needs to be
	 * written to for a specific status block
	 */
	sb_info->igu_addr = (u8 __iomem *)p_hwfn->regview +
					  GTT_BAR0_MAP_REG_IGU_CMD +
					  (sb_info->igu_sb_id << 3);

	sb_info->flags |= QED_SB_INFO_INIT;

	qed_int_sb_setup(p_hwfn, p_ptt, sb_info);

	return 0;
}

int qed_int_sb_release(struct qed_hwfn *p_hwfn,
		       struct qed_sb_info *sb_info,
		       u16 sb_id)
{
	if (sb_id == QED_SP_SB_ID) {
		DP_ERR(p_hwfn, "Do Not free sp sb using this function");
		return -EINVAL;
	}

	/* zero status block and ack counter */
	sb_info->sb_ack = 0;
	memset(sb_info->sb_virt, 0, sizeof(*sb_info->sb_virt));

	p_hwfn->sbs_info[sb_id] = NULL;
	p_hwfn->num_sbs--;

	return 0;
}

static void qed_int_sp_sb_free(struct qed_hwfn *p_hwfn)
{
	struct qed_sb_sp_info *p_sb = p_hwfn->p_sp_sb;

	if (p_sb) {
		if (p_sb->sb_info.sb_virt)
			dma_free_coherent(&p_hwfn->cdev->pdev->dev,
					  SB_ALIGNED_SIZE(p_hwfn),
					  p_sb->sb_info.sb_virt,
					  p_sb->sb_info.sb_phys);
		kfree(p_sb);
	}
}

static int qed_int_sp_sb_alloc(struct qed_hwfn *p_hwfn,
			       struct qed_ptt *p_ptt)
{
	struct qed_sb_sp_info *p_sb;
	dma_addr_t p_phys = 0;
	void *p_virt;

	/* SB struct */
	p_sb = kmalloc(sizeof(*p_sb), GFP_ATOMIC);
	if (!p_sb) {
		DP_NOTICE(p_hwfn, "Failed to allocate `struct qed_sb_info'\n");
		return -ENOMEM;
	}

	/* SB ring  */
	p_virt = dma_alloc_coherent(&p_hwfn->cdev->pdev->dev,
				    SB_ALIGNED_SIZE(p_hwfn),
				    &p_phys, GFP_KERNEL);
	if (!p_virt) {
		DP_NOTICE(p_hwfn, "Failed to allocate status block\n");
		kfree(p_sb);
		return -ENOMEM;
	}

	/* Status Block setup */
	p_hwfn->p_sp_sb = p_sb;
	qed_int_sb_init(p_hwfn, p_ptt, &p_sb->sb_info, p_virt,
			p_phys, QED_SP_SB_ID);

	memset(p_sb->pi_info_arr, 0, sizeof(p_sb->pi_info_arr));

	return 0;
}

static void qed_int_sp_sb_setup(struct qed_hwfn *p_hwfn,
				struct qed_ptt *p_ptt)
{
	if (!p_hwfn)
		return;

	if (p_hwfn->p_sp_sb)
		qed_int_sb_setup(p_hwfn, p_ptt, &p_hwfn->p_sp_sb->sb_info);
	else
		DP_NOTICE(p_hwfn->cdev,
			  "Failed to setup Slow path status block - NULL pointer\n");

	if (p_hwfn->p_sb_attn)
		qed_int_sb_attn_setup(p_hwfn, p_ptt);
	else
		DP_NOTICE(p_hwfn->cdev,
			  "Failed to setup attentions status block - NULL pointer\n");
}

int qed_int_register_cb(struct qed_hwfn *p_hwfn,
			qed_int_comp_cb_t comp_cb,
			void *cookie,
			u8 *sb_idx,
			__le16 **p_fw_cons)
{
	struct qed_sb_sp_info *p_sp_sb = p_hwfn->p_sp_sb;
	int qed_status = -ENOMEM;
	u8 pi;

	/* Look for a free index */
	for (pi = 0; pi < ARRAY_SIZE(p_sp_sb->pi_info_arr); pi++) {
		if (!p_sp_sb->pi_info_arr[pi].comp_cb) {
			p_sp_sb->pi_info_arr[pi].comp_cb = comp_cb;
			p_sp_sb->pi_info_arr[pi].cookie = cookie;
			*sb_idx = pi;
			*p_fw_cons = &p_sp_sb->sb_info.sb_virt->pi_array[pi];
			qed_status = 0;
			break;
		}
	}

	return qed_status;
}

int qed_int_unregister_cb(struct qed_hwfn *p_hwfn, u8 pi)
{
	struct qed_sb_sp_info *p_sp_sb = p_hwfn->p_sp_sb;
	int qed_status = -ENOMEM;

	if (p_sp_sb->pi_info_arr[pi].comp_cb) {
		p_sp_sb->pi_info_arr[pi].comp_cb = NULL;
		p_sp_sb->pi_info_arr[pi].cookie = NULL;
		qed_status = 0;
	}

	return qed_status;
}

u16 qed_int_get_sp_sb_id(struct qed_hwfn *p_hwfn)
{
	return p_hwfn->p_sp_sb->sb_info.igu_sb_id;
}

void qed_int_igu_enable_int(struct qed_hwfn *p_hwfn,
			    struct qed_ptt *p_ptt,
			    enum qed_int_mode int_mode)
{
	u32 igu_pf_conf = IGU_PF_CONF_FUNC_EN | IGU_PF_CONF_ATTN_BIT_EN;

	p_hwfn->cdev->int_mode = int_mode;
	switch (p_hwfn->cdev->int_mode) {
	case QED_INT_MODE_INTA:
		igu_pf_conf |= IGU_PF_CONF_INT_LINE_EN;
		igu_pf_conf |= IGU_PF_CONF_SINGLE_ISR_EN;
		break;

	case QED_INT_MODE_MSI:
		igu_pf_conf |= IGU_PF_CONF_MSI_MSIX_EN;
		igu_pf_conf |= IGU_PF_CONF_SINGLE_ISR_EN;
		break;

	case QED_INT_MODE_MSIX:
		igu_pf_conf |= IGU_PF_CONF_MSI_MSIX_EN;
		break;
	case QED_INT_MODE_POLL:
		break;
	}

	qed_wr(p_hwfn, p_ptt, IGU_REG_PF_CONFIGURATION, igu_pf_conf);
}

int qed_int_igu_enable(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt,
		       enum qed_int_mode int_mode)
{
	int rc, i;

	/* Mask non-link attentions */
	for (i = 0; i < 9; i++)
		qed_wr(p_hwfn, p_ptt,
		       MISC_REG_AEU_ENABLE1_IGU_OUT_0 + (i << 2), 0);

	/* Configure AEU signal change to produce attentions for link */
	qed_wr(p_hwfn, p_ptt, IGU_REG_LEADING_EDGE_LATCH, 0xfff);
	qed_wr(p_hwfn, p_ptt, IGU_REG_TRAILING_EDGE_LATCH, 0xfff);

	/* Flush the writes to IGU */
	mmiowb();

	/* Unmask AEU signals toward IGU */
	qed_wr(p_hwfn, p_ptt, MISC_REG_AEU_MASK_ATTN_IGU, 0xff);
	if ((int_mode != QED_INT_MODE_INTA) || IS_LEAD_HWFN(p_hwfn)) {
		rc = qed_slowpath_irq_req(p_hwfn);
		if (rc != 0) {
			DP_NOTICE(p_hwfn, "Slowpath IRQ request failed\n");
			return -EINVAL;
		}
		p_hwfn->b_int_requested = true;
	}
	/* Enable interrupt Generation */
	qed_int_igu_enable_int(p_hwfn, p_ptt, int_mode);
	p_hwfn->b_int_enabled = 1;

	return rc;
}

void qed_int_igu_disable_int(struct qed_hwfn *p_hwfn,
			     struct qed_ptt *p_ptt)
{
	p_hwfn->b_int_enabled = 0;

	qed_wr(p_hwfn, p_ptt, IGU_REG_PF_CONFIGURATION, 0);
}

#define IGU_CLEANUP_SLEEP_LENGTH                (1000)
void qed_int_igu_cleanup_sb(struct qed_hwfn *p_hwfn,
			    struct qed_ptt *p_ptt,
			    u32 sb_id,
			    bool cleanup_set,
			    u16 opaque_fid
			    )
{
	u32 pxp_addr = IGU_CMD_INT_ACK_BASE + sb_id;
	u32 sleep_cnt = IGU_CLEANUP_SLEEP_LENGTH;
	u32 data = 0;
	u32 cmd_ctrl = 0;
	u32 val = 0;
	u32 sb_bit = 0;
	u32 sb_bit_addr = 0;

	/* Set the data field */
	SET_FIELD(data, IGU_CLEANUP_CLEANUP_SET, cleanup_set ? 1 : 0);
	SET_FIELD(data, IGU_CLEANUP_CLEANUP_TYPE, 0);
	SET_FIELD(data, IGU_CLEANUP_COMMAND_TYPE, IGU_COMMAND_TYPE_SET);

	/* Set the control register */
	SET_FIELD(cmd_ctrl, IGU_CTRL_REG_PXP_ADDR, pxp_addr);
	SET_FIELD(cmd_ctrl, IGU_CTRL_REG_FID, opaque_fid);
	SET_FIELD(cmd_ctrl, IGU_CTRL_REG_TYPE, IGU_CTRL_CMD_TYPE_WR);

	qed_wr(p_hwfn, p_ptt, IGU_REG_COMMAND_REG_32LSB_DATA, data);

	barrier();

	qed_wr(p_hwfn, p_ptt, IGU_REG_COMMAND_REG_CTRL, cmd_ctrl);

	/* Flush the write to IGU */
	mmiowb();

	/* calculate where to read the status bit from */
	sb_bit = 1 << (sb_id % 32);
	sb_bit_addr = sb_id / 32 * sizeof(u32);

	sb_bit_addr += IGU_REG_CLEANUP_STATUS_0;

	/* Now wait for the command to complete */
	do {
		val = qed_rd(p_hwfn, p_ptt, sb_bit_addr);

		if ((val & sb_bit) == (cleanup_set ? sb_bit : 0))
			break;

		usleep_range(5000, 10000);
	} while (--sleep_cnt);

	if (!sleep_cnt)
		DP_NOTICE(p_hwfn,
			  "Timeout waiting for clear status 0x%08x [for sb %d]\n",
			  val, sb_id);
}

void qed_int_igu_init_pure_rt_single(struct qed_hwfn *p_hwfn,
				     struct qed_ptt *p_ptt,
				     u32 sb_id,
				     u16 opaque,
				     bool b_set)
{
	int pi;

	/* Set */
	if (b_set)
		qed_int_igu_cleanup_sb(p_hwfn, p_ptt, sb_id, 1, opaque);

	/* Clear */
	qed_int_igu_cleanup_sb(p_hwfn, p_ptt, sb_id, 0, opaque);

	/* Clear the CAU for the SB */
	for (pi = 0; pi < 12; pi++)
		qed_wr(p_hwfn, p_ptt,
		       CAU_REG_PI_MEMORY + (sb_id * 12 + pi) * 4, 0);
}

void qed_int_igu_init_pure_rt(struct qed_hwfn *p_hwfn,
			      struct qed_ptt *p_ptt,
			      bool b_set,
			      bool b_slowpath)
{
	u32 igu_base_sb = p_hwfn->hw_info.p_igu_info->igu_base_sb;
	u32 igu_sb_cnt = p_hwfn->hw_info.p_igu_info->igu_sb_cnt;
	u32 sb_id = 0;
	u32 val = 0;

	val = qed_rd(p_hwfn, p_ptt, IGU_REG_BLOCK_CONFIGURATION);
	val |= IGU_REG_BLOCK_CONFIGURATION_VF_CLEANUP_EN;
	val &= ~IGU_REG_BLOCK_CONFIGURATION_PXP_TPH_INTERFACE_EN;
	qed_wr(p_hwfn, p_ptt, IGU_REG_BLOCK_CONFIGURATION, val);

	DP_VERBOSE(p_hwfn, NETIF_MSG_INTR,
		   "IGU cleaning SBs [%d,...,%d]\n",
		   igu_base_sb, igu_base_sb + igu_sb_cnt - 1);

	for (sb_id = igu_base_sb; sb_id < igu_base_sb + igu_sb_cnt; sb_id++)
		qed_int_igu_init_pure_rt_single(p_hwfn, p_ptt, sb_id,
						p_hwfn->hw_info.opaque_fid,
						b_set);

	if (b_slowpath) {
		sb_id = p_hwfn->hw_info.p_igu_info->igu_dsb_id;
		DP_VERBOSE(p_hwfn, NETIF_MSG_INTR,
			   "IGU cleaning slowpath SB [%d]\n", sb_id);
		qed_int_igu_init_pure_rt_single(p_hwfn, p_ptt, sb_id,
						p_hwfn->hw_info.opaque_fid,
						b_set);
	}
}

int qed_int_igu_read_cam(struct qed_hwfn *p_hwfn,
			 struct qed_ptt *p_ptt)
{
	struct qed_igu_info *p_igu_info;
	struct qed_igu_block *blk;
	u32 val;
	u16 sb_id;
	u16 prev_sb_id = 0xFF;

	p_hwfn->hw_info.p_igu_info = kzalloc(sizeof(*p_igu_info), GFP_ATOMIC);

	if (!p_hwfn->hw_info.p_igu_info)
		return -ENOMEM;

	p_igu_info = p_hwfn->hw_info.p_igu_info;

	/* Initialize base sb / sb cnt for PFs */
	p_igu_info->igu_base_sb		= 0xffff;
	p_igu_info->igu_sb_cnt		= 0;
	p_igu_info->igu_dsb_id		= 0xffff;
	p_igu_info->igu_base_sb_iov	= 0xffff;

	for (sb_id = 0; sb_id < QED_MAPPING_MEMORY_SIZE(p_hwfn->cdev);
	     sb_id++) {
		blk = &p_igu_info->igu_map.igu_blocks[sb_id];

		val = qed_rd(p_hwfn, p_ptt,
			     IGU_REG_MAPPING_MEMORY + sizeof(u32) * sb_id);

		/* stop scanning when hit first invalid PF entry */
		if (!GET_FIELD(val, IGU_MAPPING_LINE_VALID) &&
		    GET_FIELD(val, IGU_MAPPING_LINE_PF_VALID))
			break;

		blk->status = QED_IGU_STATUS_VALID;
		blk->function_id = GET_FIELD(val,
					     IGU_MAPPING_LINE_FUNCTION_NUMBER);
		blk->is_pf = GET_FIELD(val, IGU_MAPPING_LINE_PF_VALID);
		blk->vector_number = GET_FIELD(val,
					       IGU_MAPPING_LINE_VECTOR_NUMBER);

		DP_VERBOSE(p_hwfn, NETIF_MSG_INTR,
			   "IGU_BLOCK[sb_id]:%x:func_id = %d is_pf = %d vector_num = 0x%x\n",
			   val, blk->function_id, blk->is_pf,
			   blk->vector_number);

		if (blk->is_pf) {
			if (blk->function_id == p_hwfn->rel_pf_id) {
				blk->status |= QED_IGU_STATUS_PF;

				if (blk->vector_number == 0) {
					if (p_igu_info->igu_dsb_id == 0xffff)
						p_igu_info->igu_dsb_id = sb_id;
				} else {
					if (p_igu_info->igu_base_sb ==
					    0xffff) {
						p_igu_info->igu_base_sb = sb_id;
					} else if (prev_sb_id != sb_id - 1) {
						DP_NOTICE(p_hwfn->cdev,
							  "consecutive igu vectors for HWFN %x broken",
							  p_hwfn->rel_pf_id);
						break;
					}
					prev_sb_id = sb_id;
					/* we don't count the default */
					(p_igu_info->igu_sb_cnt)++;
				}
			}
		}
	}

	DP_VERBOSE(p_hwfn, NETIF_MSG_INTR,
		   "IGU igu_base_sb=0x%x igu_sb_cnt=%d igu_dsb_id=0x%x\n",
		   p_igu_info->igu_base_sb,
		   p_igu_info->igu_sb_cnt,
		   p_igu_info->igu_dsb_id);

	if (p_igu_info->igu_base_sb == 0xffff ||
	    p_igu_info->igu_dsb_id == 0xffff ||
	    p_igu_info->igu_sb_cnt == 0) {
		DP_NOTICE(p_hwfn,
			  "IGU CAM returned invalid values igu_base_sb=0x%x igu_sb_cnt=%d igu_dsb_id=0x%x\n",
			   p_igu_info->igu_base_sb,
			   p_igu_info->igu_sb_cnt,
			   p_igu_info->igu_dsb_id);
		return -EINVAL;
	}

	return 0;
}

/**
 * @brief Initialize igu runtime registers
 *
 * @param p_hwfn
 */
void qed_int_igu_init_rt(struct qed_hwfn *p_hwfn)
{
	u32 igu_pf_conf = 0;

	igu_pf_conf |= IGU_PF_CONF_FUNC_EN;

	STORE_RT_REG(p_hwfn, IGU_REG_PF_CONFIGURATION_RT_OFFSET, igu_pf_conf);
}

u64 qed_int_igu_read_sisr_reg(struct qed_hwfn *p_hwfn)
{
	u64 intr_status = 0;
	u32 intr_status_lo = 0;
	u32 intr_status_hi = 0;
	u32 lsb_igu_cmd_addr = IGU_REG_SISR_MDPC_WMASK_LSB_UPPER -
			       IGU_CMD_INT_ACK_BASE;
	u32 msb_igu_cmd_addr = IGU_REG_SISR_MDPC_WMASK_MSB_UPPER -
			       IGU_CMD_INT_ACK_BASE;

	intr_status_lo = REG_RD(p_hwfn,
				GTT_BAR0_MAP_REG_IGU_CMD +
				lsb_igu_cmd_addr * 8);
	intr_status_hi = REG_RD(p_hwfn,
				GTT_BAR0_MAP_REG_IGU_CMD +
				msb_igu_cmd_addr * 8);
	intr_status = ((u64)intr_status_hi << 32) + (u64)intr_status_lo;

	return intr_status;
}

static void qed_int_sp_dpc_setup(struct qed_hwfn *p_hwfn)
{
	tasklet_init(p_hwfn->sp_dpc,
		     qed_int_sp_dpc, (unsigned long)p_hwfn);
	p_hwfn->b_sp_dpc_enabled = true;
}

static int qed_int_sp_dpc_alloc(struct qed_hwfn *p_hwfn)
{
	p_hwfn->sp_dpc = kmalloc(sizeof(*p_hwfn->sp_dpc), GFP_ATOMIC);
	if (!p_hwfn->sp_dpc)
		return -ENOMEM;

	return 0;
}

static void qed_int_sp_dpc_free(struct qed_hwfn *p_hwfn)
{
	kfree(p_hwfn->sp_dpc);
}

int qed_int_alloc(struct qed_hwfn *p_hwfn,
		  struct qed_ptt *p_ptt)
{
	int rc = 0;

	rc = qed_int_sp_dpc_alloc(p_hwfn);
	if (rc) {
		DP_ERR(p_hwfn->cdev, "Failed to allocate sp dpc mem\n");
		return rc;
	}
	rc = qed_int_sp_sb_alloc(p_hwfn, p_ptt);
	if (rc) {
		DP_ERR(p_hwfn->cdev, "Failed to allocate sp sb mem\n");
		return rc;
	}
	rc = qed_int_sb_attn_alloc(p_hwfn, p_ptt);
	if (rc) {
		DP_ERR(p_hwfn->cdev, "Failed to allocate sb attn mem\n");
		return rc;
	}
	return rc;
}

void qed_int_free(struct qed_hwfn *p_hwfn)
{
	qed_int_sp_sb_free(p_hwfn);
	qed_int_sb_attn_free(p_hwfn);
	qed_int_sp_dpc_free(p_hwfn);
}

void qed_int_setup(struct qed_hwfn *p_hwfn,
		   struct qed_ptt *p_ptt)
{
	qed_int_sp_sb_setup(p_hwfn, p_ptt);
	qed_int_sp_dpc_setup(p_hwfn);
}

int qed_int_get_num_sbs(struct qed_hwfn *p_hwfn,
			int *p_iov_blks)
{
	struct qed_igu_info *info = p_hwfn->hw_info.p_igu_info;

	if (!info)
		return 0;

	if (p_iov_blks)
		*p_iov_blks = info->free_blks;

	return info->igu_sb_cnt;
}

void qed_int_disable_post_isr_release(struct qed_dev *cdev)
{
	int i;

	for_each_hwfn(cdev, i)
		cdev->hwfns[i].b_int_requested = false;
}
