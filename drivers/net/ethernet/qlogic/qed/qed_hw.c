// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
/* QLogic qed NIC Driver
 * Copyright (c) 2015-2017  QLogic Corporation
 * Copyright (c) 2019-2020 Marvell International Ltd.
 */

#include <linux/types.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/qed/qed_chain.h>
#include "qed.h"
#include "qed_hsi.h"
#include "qed_hw.h"
#include "qed_reg_addr.h"
#include "qed_sriov.h"

#define QED_BAR_ACQUIRE_TIMEOUT_USLEEP_CNT	1000
#define QED_BAR_ACQUIRE_TIMEOUT_USLEEP		1000
#define QED_BAR_ACQUIRE_TIMEOUT_UDELAY_CNT	100000
#define QED_BAR_ACQUIRE_TIMEOUT_UDELAY		10

/* Invalid values */
#define QED_BAR_INVALID_OFFSET          (cpu_to_le32(-1))

struct qed_ptt {
	struct list_head	list_entry;
	unsigned int		idx;
	struct pxp_ptt_entry	pxp;
	u8			hwfn_id;
};

struct qed_ptt_pool {
	struct list_head	free_list;
	spinlock_t		lock; /* ptt synchronized access */
	struct qed_ptt		ptts[PXP_EXTERNAL_BAR_PF_WINDOW_NUM];
};

int qed_ptt_pool_alloc(struct qed_hwfn *p_hwfn)
{
	struct qed_ptt_pool *p_pool = kmalloc(sizeof(*p_pool), GFP_KERNEL);
	int i;

	if (!p_pool)
		return -ENOMEM;

	INIT_LIST_HEAD(&p_pool->free_list);
	for (i = 0; i < PXP_EXTERNAL_BAR_PF_WINDOW_NUM; i++) {
		p_pool->ptts[i].idx = i;
		p_pool->ptts[i].pxp.offset = QED_BAR_INVALID_OFFSET;
		p_pool->ptts[i].pxp.pretend.control = 0;
		p_pool->ptts[i].hwfn_id = p_hwfn->my_id;
		if (i >= RESERVED_PTT_MAX)
			list_add(&p_pool->ptts[i].list_entry,
				 &p_pool->free_list);
	}

	p_hwfn->p_ptt_pool = p_pool;
	spin_lock_init(&p_pool->lock);

	return 0;
}

void qed_ptt_pool_free(struct qed_hwfn *p_hwfn)
{
	kfree(p_hwfn->p_ptt_pool);
	p_hwfn->p_ptt_pool = NULL;
}

struct qed_ptt *qed_ptt_acquire(struct qed_hwfn *p_hwfn)
{
	return qed_ptt_acquire_context(p_hwfn, false);
}

struct qed_ptt *qed_ptt_acquire_context(struct qed_hwfn *p_hwfn, bool is_atomic)
{
	struct qed_ptt *p_ptt;
	unsigned int i, count;

	if (is_atomic)
		count = QED_BAR_ACQUIRE_TIMEOUT_UDELAY_CNT;
	else
		count = QED_BAR_ACQUIRE_TIMEOUT_USLEEP_CNT;

	/* Take the free PTT from the list */
	for (i = 0; i < count; i++) {
		spin_lock_bh(&p_hwfn->p_ptt_pool->lock);

		if (!list_empty(&p_hwfn->p_ptt_pool->free_list)) {
			p_ptt = list_first_entry(&p_hwfn->p_ptt_pool->free_list,
						 struct qed_ptt, list_entry);
			list_del(&p_ptt->list_entry);

			spin_unlock_bh(&p_hwfn->p_ptt_pool->lock);

			DP_VERBOSE(p_hwfn, NETIF_MSG_HW,
				   "allocated ptt %d\n", p_ptt->idx);
			return p_ptt;
		}

		spin_unlock_bh(&p_hwfn->p_ptt_pool->lock);

		if (is_atomic)
			udelay(QED_BAR_ACQUIRE_TIMEOUT_UDELAY);
		else
			usleep_range(QED_BAR_ACQUIRE_TIMEOUT_USLEEP,
				     QED_BAR_ACQUIRE_TIMEOUT_USLEEP * 2);
	}

	DP_NOTICE(p_hwfn, "PTT acquire timeout - failed to allocate PTT\n");
	return NULL;
}

void qed_ptt_release(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt)
{
	spin_lock_bh(&p_hwfn->p_ptt_pool->lock);
	list_add(&p_ptt->list_entry, &p_hwfn->p_ptt_pool->free_list);
	spin_unlock_bh(&p_hwfn->p_ptt_pool->lock);
}

u32 qed_ptt_get_hw_addr(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt)
{
	/* The HW is using DWORDS and we need to translate it to Bytes */
	return le32_to_cpu(p_ptt->pxp.offset) << 2;
}

static u32 qed_ptt_config_addr(struct qed_ptt *p_ptt)
{
	return PXP_PF_WINDOW_ADMIN_PER_PF_START +
	       p_ptt->idx * sizeof(struct pxp_ptt_entry);
}

u32 qed_ptt_get_bar_addr(struct qed_ptt *p_ptt)
{
	return PXP_EXTERNAL_BAR_PF_WINDOW_START +
	       p_ptt->idx * PXP_EXTERNAL_BAR_PF_WINDOW_SINGLE_SIZE;
}

void qed_ptt_set_win(struct qed_hwfn *p_hwfn,
		     struct qed_ptt *p_ptt, u32 new_hw_addr)
{
	u32 prev_hw_addr;

	prev_hw_addr = qed_ptt_get_hw_addr(p_hwfn, p_ptt);

	if (new_hw_addr == prev_hw_addr)
		return;

	/* Update PTT entery in admin window */
	DP_VERBOSE(p_hwfn, NETIF_MSG_HW,
		   "Updating PTT entry %d to offset 0x%x\n",
		   p_ptt->idx, new_hw_addr);

	/* The HW is using DWORDS and the address is in Bytes */
	p_ptt->pxp.offset = cpu_to_le32(new_hw_addr >> 2);

	REG_WR(p_hwfn,
	       qed_ptt_config_addr(p_ptt) +
	       offsetof(struct pxp_ptt_entry, offset),
	       le32_to_cpu(p_ptt->pxp.offset));
}

static u32 qed_set_ptt(struct qed_hwfn *p_hwfn,
		       struct qed_ptt *p_ptt, u32 hw_addr)
{
	u32 win_hw_addr = qed_ptt_get_hw_addr(p_hwfn, p_ptt);
	u32 offset;

	offset = hw_addr - win_hw_addr;

	if (p_ptt->hwfn_id != p_hwfn->my_id)
		DP_NOTICE(p_hwfn,
			  "ptt[%d] of hwfn[%02x] is used by hwfn[%02x]!\n",
			  p_ptt->idx, p_ptt->hwfn_id, p_hwfn->my_id);

	/* Verify the address is within the window */
	if (hw_addr < win_hw_addr ||
	    offset >= PXP_EXTERNAL_BAR_PF_WINDOW_SINGLE_SIZE) {
		qed_ptt_set_win(p_hwfn, p_ptt, hw_addr);
		offset = 0;
	}

	return qed_ptt_get_bar_addr(p_ptt) + offset;
}

struct qed_ptt *qed_get_reserved_ptt(struct qed_hwfn *p_hwfn,
				     enum reserved_ptts ptt_idx)
{
	if (ptt_idx >= RESERVED_PTT_MAX) {
		DP_NOTICE(p_hwfn,
			  "Requested PTT %d is out of range\n", ptt_idx);
		return NULL;
	}

	return &p_hwfn->p_ptt_pool->ptts[ptt_idx];
}

void qed_wr(struct qed_hwfn *p_hwfn,
	    struct qed_ptt *p_ptt,
	    u32 hw_addr, u32 val)
{
	u32 bar_addr = qed_set_ptt(p_hwfn, p_ptt, hw_addr);

	REG_WR(p_hwfn, bar_addr, val);
	DP_VERBOSE(p_hwfn, NETIF_MSG_HW,
		   "bar_addr 0x%x, hw_addr 0x%x, val 0x%x\n",
		   bar_addr, hw_addr, val);
}

u32 qed_rd(struct qed_hwfn *p_hwfn,
	   struct qed_ptt *p_ptt,
	   u32 hw_addr)
{
	u32 bar_addr = qed_set_ptt(p_hwfn, p_ptt, hw_addr);
	u32 val = REG_RD(p_hwfn, bar_addr);

	DP_VERBOSE(p_hwfn, NETIF_MSG_HW,
		   "bar_addr 0x%x, hw_addr 0x%x, val 0x%x\n",
		   bar_addr, hw_addr, val);

	return val;
}

static void qed_memcpy_hw(struct qed_hwfn *p_hwfn,
			  struct qed_ptt *p_ptt,
			  void *addr, u32 hw_addr, size_t n, bool to_device)
{
	u32 dw_count, *host_addr, hw_offset;
	size_t quota, done = 0;
	u32 __iomem *reg_addr;

	while (done < n) {
		quota = min_t(size_t, n - done,
			      PXP_EXTERNAL_BAR_PF_WINDOW_SINGLE_SIZE);

		if (IS_PF(p_hwfn->cdev)) {
			qed_ptt_set_win(p_hwfn, p_ptt, hw_addr + done);
			hw_offset = qed_ptt_get_bar_addr(p_ptt);
		} else {
			hw_offset = hw_addr + done;
		}

		dw_count = quota / 4;
		host_addr = (u32 *)((u8 *)addr + done);
		reg_addr = (u32 __iomem *)REG_ADDR(p_hwfn, hw_offset);
		if (to_device)
			while (dw_count--)
				DIRECT_REG_WR(reg_addr++, *host_addr++);
		else
			while (dw_count--)
				*host_addr++ = DIRECT_REG_RD(reg_addr++);

		done += quota;
	}
}

void qed_memcpy_from(struct qed_hwfn *p_hwfn,
		     struct qed_ptt *p_ptt, void *dest, u32 hw_addr, size_t n)
{
	DP_VERBOSE(p_hwfn, NETIF_MSG_HW,
		   "hw_addr 0x%x, dest %p hw_addr 0x%x, size %lu\n",
		   hw_addr, dest, hw_addr, (unsigned long)n);

	qed_memcpy_hw(p_hwfn, p_ptt, dest, hw_addr, n, false);
}

void qed_memcpy_to(struct qed_hwfn *p_hwfn,
		   struct qed_ptt *p_ptt, u32 hw_addr, void *src, size_t n)
{
	DP_VERBOSE(p_hwfn, NETIF_MSG_HW,
		   "hw_addr 0x%x, hw_addr 0x%x, src %p size %lu\n",
		   hw_addr, hw_addr, src, (unsigned long)n);

	qed_memcpy_hw(p_hwfn, p_ptt, src, hw_addr, n, true);
}

void qed_fid_pretend(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt, u16 fid)
{
	u16 control = 0;

	SET_FIELD(control, PXP_PRETEND_CMD_IS_CONCRETE, 1);
	SET_FIELD(control, PXP_PRETEND_CMD_PRETEND_FUNCTION, 1);

	/* Every pretend undos previous pretends, including
	 * previous port pretend.
	 */
	SET_FIELD(control, PXP_PRETEND_CMD_PORT, 0);
	SET_FIELD(control, PXP_PRETEND_CMD_USE_PORT, 0);
	SET_FIELD(control, PXP_PRETEND_CMD_PRETEND_PORT, 1);

	if (!GET_FIELD(fid, PXP_CONCRETE_FID_VFVALID))
		fid = GET_FIELD(fid, PXP_CONCRETE_FID_PFID);

	p_ptt->pxp.pretend.control = cpu_to_le16(control);
	p_ptt->pxp.pretend.fid.concrete_fid.fid = cpu_to_le16(fid);

	REG_WR(p_hwfn,
	       qed_ptt_config_addr(p_ptt) +
	       offsetof(struct pxp_ptt_entry, pretend),
	       *(u32 *)&p_ptt->pxp.pretend);
}

void qed_port_pretend(struct qed_hwfn *p_hwfn,
		      struct qed_ptt *p_ptt, u8 port_id)
{
	u16 control = 0;

	SET_FIELD(control, PXP_PRETEND_CMD_PORT, port_id);
	SET_FIELD(control, PXP_PRETEND_CMD_USE_PORT, 1);
	SET_FIELD(control, PXP_PRETEND_CMD_PRETEND_PORT, 1);

	p_ptt->pxp.pretend.control = cpu_to_le16(control);

	REG_WR(p_hwfn,
	       qed_ptt_config_addr(p_ptt) +
	       offsetof(struct pxp_ptt_entry, pretend),
	       *(u32 *)&p_ptt->pxp.pretend);
}

void qed_port_unpretend(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt)
{
	u16 control = 0;

	SET_FIELD(control, PXP_PRETEND_CMD_PORT, 0);
	SET_FIELD(control, PXP_PRETEND_CMD_USE_PORT, 0);
	SET_FIELD(control, PXP_PRETEND_CMD_PRETEND_PORT, 1);

	p_ptt->pxp.pretend.control = cpu_to_le16(control);

	REG_WR(p_hwfn,
	       qed_ptt_config_addr(p_ptt) +
	       offsetof(struct pxp_ptt_entry, pretend),
	       *(u32 *)&p_ptt->pxp.pretend);
}

void qed_port_fid_pretend(struct qed_hwfn *p_hwfn,
			  struct qed_ptt *p_ptt, u8 port_id, u16 fid)
{
	u16 control = 0;

	SET_FIELD(control, PXP_PRETEND_CMD_PORT, port_id);
	SET_FIELD(control, PXP_PRETEND_CMD_USE_PORT, 1);
	SET_FIELD(control, PXP_PRETEND_CMD_PRETEND_PORT, 1);
	SET_FIELD(control, PXP_PRETEND_CMD_IS_CONCRETE, 1);
	SET_FIELD(control, PXP_PRETEND_CMD_PRETEND_FUNCTION, 1);
	if (!GET_FIELD(fid, PXP_CONCRETE_FID_VFVALID))
		fid = GET_FIELD(fid, PXP_CONCRETE_FID_PFID);
	p_ptt->pxp.pretend.control = cpu_to_le16(control);
	p_ptt->pxp.pretend.fid.concrete_fid.fid = cpu_to_le16(fid);
	REG_WR(p_hwfn,
	       qed_ptt_config_addr(p_ptt) +
	       offsetof(struct pxp_ptt_entry, pretend),
	       *(u32 *)&p_ptt->pxp.pretend);
}

u32 qed_vfid_to_concrete(struct qed_hwfn *p_hwfn, u8 vfid)
{
	u32 concrete_fid = 0;

	SET_FIELD(concrete_fid, PXP_CONCRETE_FID_PFID, p_hwfn->rel_pf_id);
	SET_FIELD(concrete_fid, PXP_CONCRETE_FID_VFID, vfid);
	SET_FIELD(concrete_fid, PXP_CONCRETE_FID_VFVALID, 1);

	return concrete_fid;
}

/* DMAE */
#define QED_DMAE_FLAGS_IS_SET(params, flag) \
	((params) != NULL && GET_FIELD((params)->flags, QED_DMAE_PARAMS_##flag))

static void qed_dmae_opcode(struct qed_hwfn *p_hwfn,
			    const u8 is_src_type_grc,
			    const u8 is_dst_type_grc,
			    struct qed_dmae_params *p_params)
{
	u8 src_pfid, dst_pfid, port_id;
	u16 opcode_b = 0;
	u32 opcode = 0;

	/* Whether the source is the PCIe or the GRC.
	 * 0- The source is the PCIe
	 * 1- The source is the GRC.
	 */
	SET_FIELD(opcode, DMAE_CMD_SRC,
		  (is_src_type_grc ? dmae_cmd_src_grc : dmae_cmd_src_pcie));
	src_pfid = QED_DMAE_FLAGS_IS_SET(p_params, SRC_PF_VALID) ?
	    p_params->src_pfid : p_hwfn->rel_pf_id;
	SET_FIELD(opcode, DMAE_CMD_SRC_PF_ID, src_pfid);

	/* The destination of the DMA can be: 0-None 1-PCIe 2-GRC 3-None */
	SET_FIELD(opcode, DMAE_CMD_DST,
		  (is_dst_type_grc ? dmae_cmd_dst_grc : dmae_cmd_dst_pcie));
	dst_pfid = QED_DMAE_FLAGS_IS_SET(p_params, DST_PF_VALID) ?
	    p_params->dst_pfid : p_hwfn->rel_pf_id;
	SET_FIELD(opcode, DMAE_CMD_DST_PF_ID, dst_pfid);


	/* Whether to write a completion word to the completion destination:
	 * 0-Do not write a completion word
	 * 1-Write the completion word
	 */
	SET_FIELD(opcode, DMAE_CMD_COMP_WORD_EN, 1);
	SET_FIELD(opcode, DMAE_CMD_SRC_ADDR_RESET, 1);

	if (QED_DMAE_FLAGS_IS_SET(p_params, COMPLETION_DST))
		SET_FIELD(opcode, DMAE_CMD_COMP_FUNC, 1);

	/* swapping mode 3 - big endian */
	SET_FIELD(opcode, DMAE_CMD_ENDIANITY_MODE, DMAE_CMD_ENDIANITY);

	port_id = (QED_DMAE_FLAGS_IS_SET(p_params, PORT_VALID)) ?
	    p_params->port_id : p_hwfn->port_id;
	SET_FIELD(opcode, DMAE_CMD_PORT_ID, port_id);

	/* reset source address in next go */
	SET_FIELD(opcode, DMAE_CMD_SRC_ADDR_RESET, 1);

	/* reset dest address in next go */
	SET_FIELD(opcode, DMAE_CMD_DST_ADDR_RESET, 1);

	/* SRC/DST VFID: all 1's - pf, otherwise VF id */
	if (QED_DMAE_FLAGS_IS_SET(p_params, SRC_VF_VALID)) {
		SET_FIELD(opcode, DMAE_CMD_SRC_VF_ID_VALID, 1);
		SET_FIELD(opcode_b, DMAE_CMD_SRC_VF_ID, p_params->src_vfid);
	} else {
		SET_FIELD(opcode_b, DMAE_CMD_SRC_VF_ID, 0xFF);
	}
	if (QED_DMAE_FLAGS_IS_SET(p_params, DST_VF_VALID)) {
		SET_FIELD(opcode, DMAE_CMD_DST_VF_ID_VALID, 1);
		SET_FIELD(opcode_b, DMAE_CMD_DST_VF_ID, p_params->dst_vfid);
	} else {
		SET_FIELD(opcode_b, DMAE_CMD_DST_VF_ID, 0xFF);
	}

	p_hwfn->dmae_info.p_dmae_cmd->opcode = cpu_to_le32(opcode);
	p_hwfn->dmae_info.p_dmae_cmd->opcode_b = cpu_to_le16(opcode_b);
}

u32 qed_dmae_idx_to_go_cmd(u8 idx)
{
	/* All the DMAE 'go' registers form an array in internal memory */
	return DMAE_REG_GO_C0 + (idx << 2);
}

static int qed_dmae_post_command(struct qed_hwfn *p_hwfn,
				 struct qed_ptt *p_ptt)
{
	struct dmae_cmd *p_command = p_hwfn->dmae_info.p_dmae_cmd;
	u8 idx_cmd = p_hwfn->dmae_info.channel, i;
	int qed_status = 0;

	/* verify address is not NULL */
	if ((((!p_command->dst_addr_lo) && (!p_command->dst_addr_hi)) ||
	     ((!p_command->src_addr_lo) && (!p_command->src_addr_hi)))) {
		DP_NOTICE(p_hwfn,
			  "source or destination address 0 idx_cmd=%d\n"
			  "opcode = [0x%08x,0x%04x] len=0x%x src=0x%x:%x dst=0x%x:%x\n",
			  idx_cmd,
			  le32_to_cpu(p_command->opcode),
			  le16_to_cpu(p_command->opcode_b),
			  le16_to_cpu(p_command->length_dw),
			  le32_to_cpu(p_command->src_addr_hi),
			  le32_to_cpu(p_command->src_addr_lo),
			  le32_to_cpu(p_command->dst_addr_hi),
			  le32_to_cpu(p_command->dst_addr_lo));

		return -EINVAL;
	}

	DP_VERBOSE(p_hwfn,
		   NETIF_MSG_HW,
		   "Posting DMAE command [idx %d]: opcode = [0x%08x,0x%04x] len=0x%x src=0x%x:%x dst=0x%x:%x\n",
		   idx_cmd,
		   le32_to_cpu(p_command->opcode),
		   le16_to_cpu(p_command->opcode_b),
		   le16_to_cpu(p_command->length_dw),
		   le32_to_cpu(p_command->src_addr_hi),
		   le32_to_cpu(p_command->src_addr_lo),
		   le32_to_cpu(p_command->dst_addr_hi),
		   le32_to_cpu(p_command->dst_addr_lo));

	/* Copy the command to DMAE - need to do it before every call
	 * for source/dest address no reset.
	 * The first 9 DWs are the command registers, the 10 DW is the
	 * GO register, and the rest are result registers
	 * (which are read only by the client).
	 */
	for (i = 0; i < DMAE_CMD_SIZE; i++) {
		u32 data = (i < DMAE_CMD_SIZE_TO_FILL) ?
			   *(((u32 *)p_command) + i) : 0;

		qed_wr(p_hwfn, p_ptt,
		       DMAE_REG_CMD_MEM +
		       (idx_cmd * DMAE_CMD_SIZE * sizeof(u32)) +
		       (i * sizeof(u32)), data);
	}

	qed_wr(p_hwfn, p_ptt, qed_dmae_idx_to_go_cmd(idx_cmd), DMAE_GO_VALUE);

	return qed_status;
}

int qed_dmae_info_alloc(struct qed_hwfn *p_hwfn)
{
	dma_addr_t *p_addr = &p_hwfn->dmae_info.completion_word_phys_addr;
	struct dmae_cmd **p_cmd = &p_hwfn->dmae_info.p_dmae_cmd;
	u32 **p_buff = &p_hwfn->dmae_info.p_intermediate_buffer;
	u32 **p_comp = &p_hwfn->dmae_info.p_completion_word;

	*p_comp = dma_alloc_coherent(&p_hwfn->cdev->pdev->dev,
				     sizeof(u32), p_addr, GFP_KERNEL);
	if (!*p_comp)
		goto err;

	p_addr = &p_hwfn->dmae_info.dmae_cmd_phys_addr;
	*p_cmd = dma_alloc_coherent(&p_hwfn->cdev->pdev->dev,
				    sizeof(struct dmae_cmd),
				    p_addr, GFP_KERNEL);
	if (!*p_cmd)
		goto err;

	p_addr = &p_hwfn->dmae_info.intermediate_buffer_phys_addr;
	*p_buff = dma_alloc_coherent(&p_hwfn->cdev->pdev->dev,
				     sizeof(u32) * DMAE_MAX_RW_SIZE,
				     p_addr, GFP_KERNEL);
	if (!*p_buff)
		goto err;

	p_hwfn->dmae_info.channel = p_hwfn->rel_pf_id;

	return 0;
err:
	qed_dmae_info_free(p_hwfn);
	return -ENOMEM;
}

void qed_dmae_info_free(struct qed_hwfn *p_hwfn)
{
	dma_addr_t p_phys;

	/* Just make sure no one is in the middle */
	mutex_lock(&p_hwfn->dmae_info.mutex);

	if (p_hwfn->dmae_info.p_completion_word) {
		p_phys = p_hwfn->dmae_info.completion_word_phys_addr;
		dma_free_coherent(&p_hwfn->cdev->pdev->dev,
				  sizeof(u32),
				  p_hwfn->dmae_info.p_completion_word, p_phys);
		p_hwfn->dmae_info.p_completion_word = NULL;
	}

	if (p_hwfn->dmae_info.p_dmae_cmd) {
		p_phys = p_hwfn->dmae_info.dmae_cmd_phys_addr;
		dma_free_coherent(&p_hwfn->cdev->pdev->dev,
				  sizeof(struct dmae_cmd),
				  p_hwfn->dmae_info.p_dmae_cmd, p_phys);
		p_hwfn->dmae_info.p_dmae_cmd = NULL;
	}

	if (p_hwfn->dmae_info.p_intermediate_buffer) {
		p_phys = p_hwfn->dmae_info.intermediate_buffer_phys_addr;
		dma_free_coherent(&p_hwfn->cdev->pdev->dev,
				  sizeof(u32) * DMAE_MAX_RW_SIZE,
				  p_hwfn->dmae_info.p_intermediate_buffer,
				  p_phys);
		p_hwfn->dmae_info.p_intermediate_buffer = NULL;
	}

	mutex_unlock(&p_hwfn->dmae_info.mutex);
}

static int qed_dmae_operation_wait(struct qed_hwfn *p_hwfn)
{
	u32 wait_cnt_limit = 10000, wait_cnt = 0;
	int qed_status = 0;

	barrier();
	while (*p_hwfn->dmae_info.p_completion_word != DMAE_COMPLETION_VAL) {
		udelay(DMAE_MIN_WAIT_TIME);
		cond_resched();
		if (++wait_cnt > wait_cnt_limit) {
			DP_NOTICE(p_hwfn->cdev,
				  "Timed-out waiting for operation to complete. Completion word is 0x%08x expected 0x%08x.\n",
				  *p_hwfn->dmae_info.p_completion_word,
				 DMAE_COMPLETION_VAL);
			qed_status = -EBUSY;
			break;
		}

		/* to sync the completion_word since we are not
		 * using the volatile keyword for p_completion_word
		 */
		barrier();
	}

	if (qed_status == 0)
		*p_hwfn->dmae_info.p_completion_word = 0;

	return qed_status;
}

static int qed_dmae_execute_sub_operation(struct qed_hwfn *p_hwfn,
					  struct qed_ptt *p_ptt,
					  u64 src_addr,
					  u64 dst_addr,
					  u8 src_type,
					  u8 dst_type,
					  u32 length_dw)
{
	dma_addr_t phys = p_hwfn->dmae_info.intermediate_buffer_phys_addr;
	struct dmae_cmd *cmd = p_hwfn->dmae_info.p_dmae_cmd;
	int qed_status = 0;

	switch (src_type) {
	case QED_DMAE_ADDRESS_GRC:
	case QED_DMAE_ADDRESS_HOST_PHYS:
		cmd->src_addr_hi = cpu_to_le32(upper_32_bits(src_addr));
		cmd->src_addr_lo = cpu_to_le32(lower_32_bits(src_addr));
		break;
	/* for virtual source addresses we use the intermediate buffer. */
	case QED_DMAE_ADDRESS_HOST_VIRT:
		cmd->src_addr_hi = cpu_to_le32(upper_32_bits(phys));
		cmd->src_addr_lo = cpu_to_le32(lower_32_bits(phys));
		memcpy(&p_hwfn->dmae_info.p_intermediate_buffer[0],
		       (void *)(uintptr_t)src_addr,
		       length_dw * sizeof(u32));
		break;
	default:
		return -EINVAL;
	}

	switch (dst_type) {
	case QED_DMAE_ADDRESS_GRC:
	case QED_DMAE_ADDRESS_HOST_PHYS:
		cmd->dst_addr_hi = cpu_to_le32(upper_32_bits(dst_addr));
		cmd->dst_addr_lo = cpu_to_le32(lower_32_bits(dst_addr));
		break;
	/* for virtual source addresses we use the intermediate buffer. */
	case QED_DMAE_ADDRESS_HOST_VIRT:
		cmd->dst_addr_hi = cpu_to_le32(upper_32_bits(phys));
		cmd->dst_addr_lo = cpu_to_le32(lower_32_bits(phys));
		break;
	default:
		return -EINVAL;
	}

	cmd->length_dw = cpu_to_le16((u16)length_dw);

	qed_dmae_post_command(p_hwfn, p_ptt);

	qed_status = qed_dmae_operation_wait(p_hwfn);

	if (qed_status) {
		DP_NOTICE(p_hwfn,
			  "qed_dmae_host2grc: Wait Failed. source_addr 0x%llx, grc_addr 0x%llx, size_in_dwords 0x%x\n",
			  src_addr, dst_addr, length_dw);
		return qed_status;
	}

	if (dst_type == QED_DMAE_ADDRESS_HOST_VIRT)
		memcpy((void *)(uintptr_t)(dst_addr),
		       &p_hwfn->dmae_info.p_intermediate_buffer[0],
		       length_dw * sizeof(u32));

	return 0;
}

static int qed_dmae_execute_command(struct qed_hwfn *p_hwfn,
				    struct qed_ptt *p_ptt,
				    u64 src_addr, u64 dst_addr,
				    u8 src_type, u8 dst_type,
				    u32 size_in_dwords,
				    struct qed_dmae_params *p_params)
{
	dma_addr_t phys = p_hwfn->dmae_info.completion_word_phys_addr;
	u16 length_cur = 0, i = 0, cnt_split = 0, length_mod = 0;
	struct dmae_cmd *cmd = p_hwfn->dmae_info.p_dmae_cmd;
	u64 src_addr_split = 0, dst_addr_split = 0;
	u16 length_limit = DMAE_MAX_RW_SIZE;
	int qed_status = 0;
	u32 offset = 0;

	if (p_hwfn->cdev->recov_in_prog) {
		DP_VERBOSE(p_hwfn,
			   NETIF_MSG_HW,
			   "Recovery is in progress. Avoid DMAE transaction [{src: addr 0x%llx, type %d}, {dst: addr 0x%llx, type %d}, size %d].\n",
			   src_addr, src_type, dst_addr, dst_type,
			   size_in_dwords);

		/* Let the flow complete w/o any error handling */
		return 0;
	}

	qed_dmae_opcode(p_hwfn,
			(src_type == QED_DMAE_ADDRESS_GRC),
			(dst_type == QED_DMAE_ADDRESS_GRC),
			p_params);

	cmd->comp_addr_lo = cpu_to_le32(lower_32_bits(phys));
	cmd->comp_addr_hi = cpu_to_le32(upper_32_bits(phys));
	cmd->comp_val = cpu_to_le32(DMAE_COMPLETION_VAL);

	/* Check if the grc_addr is valid like < MAX_GRC_OFFSET */
	cnt_split = size_in_dwords / length_limit;
	length_mod = size_in_dwords % length_limit;

	src_addr_split = src_addr;
	dst_addr_split = dst_addr;

	for (i = 0; i <= cnt_split; i++) {
		offset = length_limit * i;

		if (!QED_DMAE_FLAGS_IS_SET(p_params, RW_REPL_SRC)) {
			if (src_type == QED_DMAE_ADDRESS_GRC)
				src_addr_split = src_addr + offset;
			else
				src_addr_split = src_addr + (offset * 4);
		}

		if (dst_type == QED_DMAE_ADDRESS_GRC)
			dst_addr_split = dst_addr + offset;
		else
			dst_addr_split = dst_addr + (offset * 4);

		length_cur = (cnt_split == i) ? length_mod : length_limit;

		/* might be zero on last iteration */
		if (!length_cur)
			continue;

		qed_status = qed_dmae_execute_sub_operation(p_hwfn,
							    p_ptt,
							    src_addr_split,
							    dst_addr_split,
							    src_type,
							    dst_type,
							    length_cur);
		if (qed_status) {
			qed_hw_err_notify(p_hwfn, p_ptt, QED_HW_ERR_DMAE_FAIL,
					  "qed_dmae_execute_sub_operation Failed with error 0x%x. source_addr 0x%llx, destination addr 0x%llx, size_in_dwords 0x%x\n",
					  qed_status, src_addr,
					  dst_addr, length_cur);
			break;
		}
	}

	return qed_status;
}

int qed_dmae_host2grc(struct qed_hwfn *p_hwfn,
		      struct qed_ptt *p_ptt,
		      u64 source_addr, u32 grc_addr, u32 size_in_dwords,
		      struct qed_dmae_params *p_params)
{
	u32 grc_addr_in_dw = grc_addr / sizeof(u32);
	int rc;


	mutex_lock(&p_hwfn->dmae_info.mutex);

	rc = qed_dmae_execute_command(p_hwfn, p_ptt, source_addr,
				      grc_addr_in_dw,
				      QED_DMAE_ADDRESS_HOST_VIRT,
				      QED_DMAE_ADDRESS_GRC,
				      size_in_dwords, p_params);

	mutex_unlock(&p_hwfn->dmae_info.mutex);

	return rc;
}

int qed_dmae_grc2host(struct qed_hwfn *p_hwfn,
		      struct qed_ptt *p_ptt,
		      u32 grc_addr,
		      dma_addr_t dest_addr, u32 size_in_dwords,
		      struct qed_dmae_params *p_params)
{
	u32 grc_addr_in_dw = grc_addr / sizeof(u32);
	int rc;


	mutex_lock(&p_hwfn->dmae_info.mutex);

	rc = qed_dmae_execute_command(p_hwfn, p_ptt, grc_addr_in_dw,
				      dest_addr, QED_DMAE_ADDRESS_GRC,
				      QED_DMAE_ADDRESS_HOST_VIRT,
				      size_in_dwords, p_params);

	mutex_unlock(&p_hwfn->dmae_info.mutex);

	return rc;
}

int qed_dmae_host2host(struct qed_hwfn *p_hwfn,
		       struct qed_ptt *p_ptt,
		       dma_addr_t source_addr,
		       dma_addr_t dest_addr,
		       u32 size_in_dwords, struct qed_dmae_params *p_params)
{
	int rc;

	mutex_lock(&(p_hwfn->dmae_info.mutex));

	rc = qed_dmae_execute_command(p_hwfn, p_ptt, source_addr,
				      dest_addr,
				      QED_DMAE_ADDRESS_HOST_PHYS,
				      QED_DMAE_ADDRESS_HOST_PHYS,
				      size_in_dwords, p_params);

	mutex_unlock(&(p_hwfn->dmae_info.mutex));

	return rc;
}

void qed_hw_err_notify(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt,
		       enum qed_hw_err_type err_type, const char *fmt, ...)
{
	char buf[QED_HW_ERR_MAX_STR_SIZE];
	va_list vl;
	int len;

	if (fmt) {
		va_start(vl, fmt);
		len = vsnprintf(buf, QED_HW_ERR_MAX_STR_SIZE, fmt, vl);
		va_end(vl);

		if (len > QED_HW_ERR_MAX_STR_SIZE - 1)
			len = QED_HW_ERR_MAX_STR_SIZE - 1;

		DP_NOTICE(p_hwfn, "%s", buf);
	}

	/* Fan failure cannot be masked by handling of another HW error */
	if (p_hwfn->cdev->recov_in_prog &&
	    err_type != QED_HW_ERR_FAN_FAIL) {
		DP_VERBOSE(p_hwfn,
			   NETIF_MSG_DRV,
			   "Recovery is in progress. Avoid notifying about HW error %d.\n",
			   err_type);
		return;
	}

	qed_hw_error_occurred(p_hwfn, err_type);

	if (fmt)
		qed_mcp_send_raw_debug_data(p_hwfn, p_ptt, buf, len);
}

int qed_dmae_sanity(struct qed_hwfn *p_hwfn,
		    struct qed_ptt *p_ptt, const char *phase)
{
	u32 size = PAGE_SIZE / 2, val;
	int rc = 0;
	dma_addr_t p_phys;
	void *p_virt;
	u32 *p_tmp;

	p_virt = dma_alloc_coherent(&p_hwfn->cdev->pdev->dev,
				    2 * size, &p_phys, GFP_KERNEL);
	if (!p_virt) {
		DP_NOTICE(p_hwfn,
			  "DMAE sanity [%s]: failed to allocate memory\n",
			  phase);
		return -ENOMEM;
	}

	/* Fill the bottom half of the allocated memory with a known pattern */
	for (p_tmp = (u32 *)p_virt;
	     p_tmp < (u32 *)((u8 *)p_virt + size); p_tmp++) {
		/* Save the address itself as the value */
		val = (u32)(uintptr_t)p_tmp;
		*p_tmp = val;
	}

	/* Zero the top half of the allocated memory */
	memset((u8 *)p_virt + size, 0, size);

	DP_VERBOSE(p_hwfn,
		   QED_MSG_SP,
		   "DMAE sanity [%s]: src_addr={phys 0x%llx, virt %p}, dst_addr={phys 0x%llx, virt %p}, size 0x%x\n",
		   phase,
		   (u64)p_phys,
		   p_virt, (u64)(p_phys + size), (u8 *)p_virt + size, size);

	rc = qed_dmae_host2host(p_hwfn, p_ptt, p_phys, p_phys + size,
				size / 4, NULL);
	if (rc) {
		DP_NOTICE(p_hwfn,
			  "DMAE sanity [%s]: qed_dmae_host2host() failed. rc = %d.\n",
			  phase, rc);
		goto out;
	}

	/* Verify that the top half of the allocated memory has the pattern */
	for (p_tmp = (u32 *)((u8 *)p_virt + size);
	     p_tmp < (u32 *)((u8 *)p_virt + (2 * size)); p_tmp++) {
		/* The corresponding address in the bottom half */
		val = (u32)(uintptr_t)p_tmp - size;

		if (*p_tmp != val) {
			DP_NOTICE(p_hwfn,
				  "DMAE sanity [%s]: addr={phys 0x%llx, virt %p}, read_val 0x%08x, expected_val 0x%08x\n",
				  phase,
				  (u64)p_phys + ((u8 *)p_tmp - (u8 *)p_virt),
				  p_tmp, *p_tmp, val);
			rc = -EINVAL;
			goto out;
		}
	}

out:
	dma_free_coherent(&p_hwfn->cdev->pdev->dev, 2 * size, p_virt, p_phys);
	return rc;
}
