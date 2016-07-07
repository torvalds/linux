/* QLogic qed NIC Driver
 * Copyright (c) 2015 QLogic Corporation
 *
 * This software is available under the terms of the GNU General Public License
 * (GPL) Version 2, available from the file COPYING in the main directory of
 * this source tree.
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

#define QED_BAR_ACQUIRE_TIMEOUT 1000

/* Invalid values */
#define QED_BAR_INVALID_OFFSET          (cpu_to_le32(-1))

struct qed_ptt {
	struct list_head	list_entry;
	unsigned int		idx;
	struct pxp_ptt_entry	pxp;
};

struct qed_ptt_pool {
	struct list_head	free_list;
	spinlock_t		lock; /* ptt synchronized access */
	struct qed_ptt		ptts[PXP_EXTERNAL_BAR_PF_WINDOW_NUM];
};

int qed_ptt_pool_alloc(struct qed_hwfn *p_hwfn)
{
	struct qed_ptt_pool *p_pool = kmalloc(sizeof(*p_pool),
					      GFP_KERNEL);
	int i;

	if (!p_pool)
		return -ENOMEM;

	INIT_LIST_HEAD(&p_pool->free_list);
	for (i = 0; i < PXP_EXTERNAL_BAR_PF_WINDOW_NUM; i++) {
		p_pool->ptts[i].idx = i;
		p_pool->ptts[i].pxp.offset = QED_BAR_INVALID_OFFSET;
		p_pool->ptts[i].pxp.pretend.control = 0;
		if (i >= RESERVED_PTT_MAX)
			list_add(&p_pool->ptts[i].list_entry,
				 &p_pool->free_list);
	}

	p_hwfn->p_ptt_pool = p_pool;
	spin_lock_init(&p_pool->lock);

	return 0;
}

void qed_ptt_invalidate(struct qed_hwfn *p_hwfn)
{
	struct qed_ptt *p_ptt;
	int i;

	for (i = 0; i < PXP_EXTERNAL_BAR_PF_WINDOW_NUM; i++) {
		p_ptt = &p_hwfn->p_ptt_pool->ptts[i];
		p_ptt->pxp.offset = QED_BAR_INVALID_OFFSET;
	}
}

void qed_ptt_pool_free(struct qed_hwfn *p_hwfn)
{
	kfree(p_hwfn->p_ptt_pool);
	p_hwfn->p_ptt_pool = NULL;
}

struct qed_ptt *qed_ptt_acquire(struct qed_hwfn *p_hwfn)
{
	struct qed_ptt *p_ptt;
	unsigned int i;

	/* Take the free PTT from the list */
	for (i = 0; i < QED_BAR_ACQUIRE_TIMEOUT; i++) {
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
		usleep_range(1000, 2000);
	}

	DP_NOTICE(p_hwfn, "PTT acquire timeout - failed to allocate PTT\n");
	return NULL;
}

void qed_ptt_release(struct qed_hwfn *p_hwfn,
		     struct qed_ptt *p_ptt)
{
	spin_lock_bh(&p_hwfn->p_ptt_pool->lock);
	list_add(&p_ptt->list_entry, &p_hwfn->p_ptt_pool->free_list);
	spin_unlock_bh(&p_hwfn->p_ptt_pool->lock);
}

u32 qed_ptt_get_hw_addr(struct qed_hwfn *p_hwfn,
			struct qed_ptt *p_ptt)
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
		     struct qed_ptt *p_ptt,
		     u32 new_hw_addr)
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
		       struct qed_ptt *p_ptt,
		       u32 hw_addr)
{
	u32 win_hw_addr = qed_ptt_get_hw_addr(p_hwfn, p_ptt);
	u32 offset;

	offset = hw_addr - win_hw_addr;

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
			  void *addr,
			  u32 hw_addr,
			  size_t n,
			  bool to_device)
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
		     struct qed_ptt *p_ptt,
		     void *dest, u32 hw_addr, size_t n)
{
	DP_VERBOSE(p_hwfn, NETIF_MSG_HW,
		   "hw_addr 0x%x, dest %p hw_addr 0x%x, size %lu\n",
		   hw_addr, dest, hw_addr, (unsigned long)n);

	qed_memcpy_hw(p_hwfn, p_ptt, dest, hw_addr, n, false);
}

void qed_memcpy_to(struct qed_hwfn *p_hwfn,
		   struct qed_ptt *p_ptt,
		   u32 hw_addr, void *src, size_t n)
{
	DP_VERBOSE(p_hwfn, NETIF_MSG_HW,
		   "hw_addr 0x%x, hw_addr 0x%x, src %p size %lu\n",
		   hw_addr, hw_addr, src, (unsigned long)n);

	qed_memcpy_hw(p_hwfn, p_ptt, src, hw_addr, n, true);
}

void qed_fid_pretend(struct qed_hwfn *p_hwfn,
		     struct qed_ptt *p_ptt,
		     u16 fid)
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
		      struct qed_ptt *p_ptt,
		      u8 port_id)
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

void qed_port_unpretend(struct qed_hwfn *p_hwfn,
			struct qed_ptt *p_ptt)
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

u32 qed_vfid_to_concrete(struct qed_hwfn *p_hwfn, u8 vfid)
{
	u32 concrete_fid = 0;

	SET_FIELD(concrete_fid, PXP_CONCRETE_FID_PFID, p_hwfn->rel_pf_id);
	SET_FIELD(concrete_fid, PXP_CONCRETE_FID_VFID, vfid);
	SET_FIELD(concrete_fid, PXP_CONCRETE_FID_VFVALID, 1);

	return concrete_fid;
}

/* DMAE */
static void qed_dmae_opcode(struct qed_hwfn *p_hwfn,
			    const u8 is_src_type_grc,
			    const u8 is_dst_type_grc,
			    struct qed_dmae_params *p_params)
{
	u16 opcode_b = 0;
	u32 opcode = 0;

	/* Whether the source is the PCIe or the GRC.
	 * 0- The source is the PCIe
	 * 1- The source is the GRC.
	 */
	opcode |= (is_src_type_grc ? DMAE_CMD_SRC_MASK_GRC
				   : DMAE_CMD_SRC_MASK_PCIE) <<
		   DMAE_CMD_SRC_SHIFT;
	opcode |= ((p_hwfn->rel_pf_id & DMAE_CMD_SRC_PF_ID_MASK) <<
		   DMAE_CMD_SRC_PF_ID_SHIFT);

	/* The destination of the DMA can be: 0-None 1-PCIe 2-GRC 3-None */
	opcode |= (is_dst_type_grc ? DMAE_CMD_DST_MASK_GRC
				   : DMAE_CMD_DST_MASK_PCIE) <<
		   DMAE_CMD_DST_SHIFT;
	opcode |= ((p_hwfn->rel_pf_id & DMAE_CMD_DST_PF_ID_MASK) <<
		   DMAE_CMD_DST_PF_ID_SHIFT);

	/* Whether to write a completion word to the completion destination:
	 * 0-Do not write a completion word
	 * 1-Write the completion word
	 */
	opcode |= (DMAE_CMD_COMP_WORD_EN_MASK << DMAE_CMD_COMP_WORD_EN_SHIFT);
	opcode |= (DMAE_CMD_SRC_ADDR_RESET_MASK <<
		   DMAE_CMD_SRC_ADDR_RESET_SHIFT);

	if (p_params->flags & QED_DMAE_FLAG_COMPLETION_DST)
		opcode |= (1 << DMAE_CMD_COMP_FUNC_SHIFT);

	opcode |= (DMAE_CMD_ENDIANITY << DMAE_CMD_ENDIANITY_MODE_SHIFT);

	opcode |= ((p_hwfn->port_id) << DMAE_CMD_PORT_ID_SHIFT);

	/* reset source address in next go */
	opcode |= (DMAE_CMD_SRC_ADDR_RESET_MASK <<
		   DMAE_CMD_SRC_ADDR_RESET_SHIFT);

	/* reset dest address in next go */
	opcode |= (DMAE_CMD_DST_ADDR_RESET_MASK <<
		   DMAE_CMD_DST_ADDR_RESET_SHIFT);

	/* SRC/DST VFID: all 1's - pf, otherwise VF id */
	if (p_params->flags & QED_DMAE_FLAG_VF_SRC) {
		opcode |= 1 << DMAE_CMD_SRC_VF_ID_VALID_SHIFT;
		opcode_b |= p_params->src_vfid << DMAE_CMD_SRC_VF_ID_SHIFT;
	} else {
		opcode_b |= DMAE_CMD_SRC_VF_ID_MASK <<
			    DMAE_CMD_SRC_VF_ID_SHIFT;
	}

	if (p_params->flags & QED_DMAE_FLAG_VF_DST) {
		opcode |= 1 << DMAE_CMD_DST_VF_ID_VALID_SHIFT;
		opcode_b |= p_params->dst_vfid << DMAE_CMD_DST_VF_ID_SHIFT;
	} else {
		opcode_b |= DMAE_CMD_DST_VF_ID_MASK << DMAE_CMD_DST_VF_ID_SHIFT;
	}

	p_hwfn->dmae_info.p_dmae_cmd->opcode = cpu_to_le32(opcode);
	p_hwfn->dmae_info.p_dmae_cmd->opcode_b = cpu_to_le16(opcode_b);
}

u32 qed_dmae_idx_to_go_cmd(u8 idx)
{
	/* All the DMAE 'go' registers form an array in internal memory */
	return DMAE_REG_GO_C0 + (idx << 2);
}

static int
qed_dmae_post_command(struct qed_hwfn *p_hwfn,
		      struct qed_ptt *p_ptt)
{
	struct dmae_cmd *command = p_hwfn->dmae_info.p_dmae_cmd;
	u8 idx_cmd = p_hwfn->dmae_info.channel, i;
	int qed_status = 0;

	/* verify address is not NULL */
	if ((((command->dst_addr_lo == 0) && (command->dst_addr_hi == 0)) ||
	     ((command->src_addr_lo == 0) && (command->src_addr_hi == 0)))) {
		DP_NOTICE(p_hwfn,
			  "source or destination address 0 idx_cmd=%d\n"
			  "opcode = [0x%08x,0x%04x] len=0x%x src=0x%x:%x dst=0x%x:%x\n",
			   idx_cmd,
			   le32_to_cpu(command->opcode),
			   le16_to_cpu(command->opcode_b),
			   le16_to_cpu(command->length),
			   le32_to_cpu(command->src_addr_hi),
			   le32_to_cpu(command->src_addr_lo),
			   le32_to_cpu(command->dst_addr_hi),
			   le32_to_cpu(command->dst_addr_lo));

		return -EINVAL;
	}

	DP_VERBOSE(p_hwfn,
		   NETIF_MSG_HW,
		   "Posting DMAE command [idx %d]: opcode = [0x%08x,0x%04x] len=0x%x src=0x%x:%x dst=0x%x:%x\n",
		   idx_cmd,
		   le32_to_cpu(command->opcode),
		   le16_to_cpu(command->opcode_b),
		   le16_to_cpu(command->length),
		   le32_to_cpu(command->src_addr_hi),
		   le32_to_cpu(command->src_addr_lo),
		   le32_to_cpu(command->dst_addr_hi),
		   le32_to_cpu(command->dst_addr_lo));

	/* Copy the command to DMAE - need to do it before every call
	 * for source/dest address no reset.
	 * The first 9 DWs are the command registers, the 10 DW is the
	 * GO register, and the rest are result registers
	 * (which are read only by the client).
	 */
	for (i = 0; i < DMAE_CMD_SIZE; i++) {
		u32 data = (i < DMAE_CMD_SIZE_TO_FILL) ?
			   *(((u32 *)command) + i) : 0;

		qed_wr(p_hwfn, p_ptt,
		       DMAE_REG_CMD_MEM +
		       (idx_cmd * DMAE_CMD_SIZE * sizeof(u32)) +
		       (i * sizeof(u32)), data);
	}

	qed_wr(p_hwfn, p_ptt,
	       qed_dmae_idx_to_go_cmd(idx_cmd),
	       DMAE_GO_VALUE);

	return qed_status;
}

int qed_dmae_info_alloc(struct qed_hwfn *p_hwfn)
{
	dma_addr_t *p_addr = &p_hwfn->dmae_info.completion_word_phys_addr;
	struct dmae_cmd **p_cmd = &p_hwfn->dmae_info.p_dmae_cmd;
	u32 **p_buff = &p_hwfn->dmae_info.p_intermediate_buffer;
	u32 **p_comp = &p_hwfn->dmae_info.p_completion_word;

	*p_comp = dma_alloc_coherent(&p_hwfn->cdev->pdev->dev,
				     sizeof(u32),
				     p_addr,
				     GFP_KERNEL);
	if (!*p_comp) {
		DP_NOTICE(p_hwfn, "Failed to allocate `p_completion_word'\n");
		goto err;
	}

	p_addr = &p_hwfn->dmae_info.dmae_cmd_phys_addr;
	*p_cmd = dma_alloc_coherent(&p_hwfn->cdev->pdev->dev,
				    sizeof(struct dmae_cmd),
				    p_addr, GFP_KERNEL);
	if (!*p_cmd) {
		DP_NOTICE(p_hwfn, "Failed to allocate `struct dmae_cmd'\n");
		goto err;
	}

	p_addr = &p_hwfn->dmae_info.intermediate_buffer_phys_addr;
	*p_buff = dma_alloc_coherent(&p_hwfn->cdev->pdev->dev,
				     sizeof(u32) * DMAE_MAX_RW_SIZE,
				     p_addr, GFP_KERNEL);
	if (!*p_buff) {
		DP_NOTICE(p_hwfn, "Failed to allocate `intermediate_buffer'\n");
		goto err;
	}

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
				  p_hwfn->dmae_info.p_completion_word,
				  p_phys);
		p_hwfn->dmae_info.p_completion_word = NULL;
	}

	if (p_hwfn->dmae_info.p_dmae_cmd) {
		p_phys = p_hwfn->dmae_info.dmae_cmd_phys_addr;
		dma_free_coherent(&p_hwfn->cdev->pdev->dev,
				  sizeof(struct dmae_cmd),
				  p_hwfn->dmae_info.p_dmae_cmd,
				  p_phys);
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
	u32 wait_cnt = 0;
	u32 wait_cnt_limit = 10000;

	int qed_status = 0;

	barrier();
	while (*p_hwfn->dmae_info.p_completion_word != DMAE_COMPLETION_VAL) {
		udelay(DMAE_MIN_WAIT_TIME);
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
					  u32 length)
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
		       length * sizeof(u32));
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

	cmd->length = cpu_to_le16((u16)length);

	qed_dmae_post_command(p_hwfn, p_ptt);

	qed_status = qed_dmae_operation_wait(p_hwfn);

	if (qed_status) {
		DP_NOTICE(p_hwfn,
			  "qed_dmae_host2grc: Wait Failed. source_addr 0x%llx, grc_addr 0x%llx, size_in_dwords 0x%x\n",
			  src_addr,
			  dst_addr,
			  length);
		return qed_status;
	}

	if (dst_type == QED_DMAE_ADDRESS_HOST_VIRT)
		memcpy((void *)(uintptr_t)(dst_addr),
		       &p_hwfn->dmae_info.p_intermediate_buffer[0],
		       length * sizeof(u32));

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

		if (!(p_params->flags & QED_DMAE_FLAG_RW_REPL_SRC)) {
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
			DP_NOTICE(p_hwfn,
				  "qed_dmae_execute_sub_operation Failed with error 0x%x. source_addr 0x%llx, destination addr 0x%llx, size_in_dwords 0x%x\n",
				  qed_status,
				  src_addr,
				  dst_addr,
				  length_cur);
			break;
		}
	}

	return qed_status;
}

int qed_dmae_host2grc(struct qed_hwfn *p_hwfn,
		      struct qed_ptt *p_ptt,
		      u64 source_addr,
		      u32 grc_addr,
		      u32 size_in_dwords,
		      u32 flags)
{
	u32 grc_addr_in_dw = grc_addr / sizeof(u32);
	struct qed_dmae_params params;
	int rc;

	memset(&params, 0, sizeof(struct qed_dmae_params));
	params.flags = flags;

	mutex_lock(&p_hwfn->dmae_info.mutex);

	rc = qed_dmae_execute_command(p_hwfn, p_ptt, source_addr,
				      grc_addr_in_dw,
				      QED_DMAE_ADDRESS_HOST_VIRT,
				      QED_DMAE_ADDRESS_GRC,
				      size_in_dwords, &params);

	mutex_unlock(&p_hwfn->dmae_info.mutex);

	return rc;
}

int
qed_dmae_host2host(struct qed_hwfn *p_hwfn,
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

u16 qed_get_qm_pq(struct qed_hwfn *p_hwfn,
		  enum protocol_type proto,
		  union qed_qm_pq_params *p_params)
{
	u16 pq_id = 0;

	if ((proto == PROTOCOLID_CORE || proto == PROTOCOLID_ETH) &&
	    !p_params) {
		DP_NOTICE(p_hwfn,
			  "Protocol %d received NULL PQ params\n",
			  proto);
		return 0;
	}

	switch (proto) {
	case PROTOCOLID_CORE:
		if (p_params->core.tc == LB_TC)
			pq_id = p_hwfn->qm_info.pure_lb_pq;
		else
			pq_id = p_hwfn->qm_info.offload_pq;
		break;
	case PROTOCOLID_ETH:
		pq_id = p_params->eth.tc;
		if (p_params->eth.is_vf)
			pq_id += p_hwfn->qm_info.vf_queues_offset +
				 p_params->eth.vf_id;
		break;
	default:
		pq_id = 0;
	}

	pq_id = CM_TX_PQ_BASE + pq_id + RESC_START(p_hwfn, QED_PQ);

	return pq_id;
}
