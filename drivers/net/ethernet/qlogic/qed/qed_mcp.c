// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
/* QLogic qed NIC Driver
 * Copyright (c) 2015-2017  QLogic Corporation
 * Copyright (c) 2019-2020 Marvell International Ltd.
 */

#include <linux/types.h>
#include <asm/byteorder.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/etherdevice.h>
#include "qed.h"
#include "qed_cxt.h"
#include "qed_dcbx.h"
#include "qed_hsi.h"
#include "qed_hw.h"
#include "qed_mcp.h"
#include "qed_reg_addr.h"
#include "qed_sriov.h"

#define GRCBASE_MCP     0xe00000

#define QED_MCP_RESP_ITER_US	10

#define QED_DRV_MB_MAX_RETRIES	(500 * 1000)	/* Account for 5 sec */
#define QED_MCP_RESET_RETRIES	(50 * 1000)	/* Account for 500 msec */

#define DRV_INNER_WR(_p_hwfn, _p_ptt, _ptr, _offset, _val)	     \
	qed_wr(_p_hwfn, _p_ptt, (_p_hwfn->mcp_info->_ptr + _offset), \
	       _val)

#define DRV_INNER_RD(_p_hwfn, _p_ptt, _ptr, _offset) \
	qed_rd(_p_hwfn, _p_ptt, (_p_hwfn->mcp_info->_ptr + _offset))

#define DRV_MB_WR(_p_hwfn, _p_ptt, _field, _val)  \
	DRV_INNER_WR(p_hwfn, _p_ptt, drv_mb_addr, \
		     offsetof(struct public_drv_mb, _field), _val)

#define DRV_MB_RD(_p_hwfn, _p_ptt, _field)	   \
	DRV_INNER_RD(_p_hwfn, _p_ptt, drv_mb_addr, \
		     offsetof(struct public_drv_mb, _field))

#define PDA_COMP (((FW_MAJOR_VERSION) + (FW_MINOR_VERSION << 8)) << \
		  DRV_ID_PDA_COMP_VER_SHIFT)

#define MCP_BYTES_PER_MBIT_SHIFT 17

bool qed_mcp_is_init(struct qed_hwfn *p_hwfn)
{
	if (!p_hwfn->mcp_info || !p_hwfn->mcp_info->public_base)
		return false;
	return true;
}

void qed_mcp_cmd_port_init(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt)
{
	u32 addr = SECTION_OFFSIZE_ADDR(p_hwfn->mcp_info->public_base,
					PUBLIC_PORT);
	u32 mfw_mb_offsize = qed_rd(p_hwfn, p_ptt, addr);

	p_hwfn->mcp_info->port_addr = SECTION_ADDR(mfw_mb_offsize,
						   MFW_PORT(p_hwfn));
	DP_VERBOSE(p_hwfn, QED_MSG_SP,
		   "port_addr = 0x%x, port_id 0x%02x\n",
		   p_hwfn->mcp_info->port_addr, MFW_PORT(p_hwfn));
}

void qed_mcp_read_mb(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt)
{
	u32 length = MFW_DRV_MSG_MAX_DWORDS(p_hwfn->mcp_info->mfw_mb_length);
	u32 tmp, i;

	if (!p_hwfn->mcp_info->public_base)
		return;

	for (i = 0; i < length; i++) {
		tmp = qed_rd(p_hwfn, p_ptt,
			     p_hwfn->mcp_info->mfw_mb_addr +
			     (i << 2) + sizeof(u32));

		/* The MB data is actually BE; Need to force it to cpu */
		((u32 *)p_hwfn->mcp_info->mfw_mb_cur)[i] =
			be32_to_cpu((__force __be32)tmp);
	}
}

struct qed_mcp_cmd_elem {
	struct list_head list;
	struct qed_mcp_mb_params *p_mb_params;
	u16 expected_seq_num;
	bool b_is_completed;
};

/* Must be called while cmd_lock is acquired */
static struct qed_mcp_cmd_elem *
qed_mcp_cmd_add_elem(struct qed_hwfn *p_hwfn,
		     struct qed_mcp_mb_params *p_mb_params,
		     u16 expected_seq_num)
{
	struct qed_mcp_cmd_elem *p_cmd_elem = NULL;

	p_cmd_elem = kzalloc(sizeof(*p_cmd_elem), GFP_ATOMIC);
	if (!p_cmd_elem)
		goto out;

	p_cmd_elem->p_mb_params = p_mb_params;
	p_cmd_elem->expected_seq_num = expected_seq_num;
	list_add(&p_cmd_elem->list, &p_hwfn->mcp_info->cmd_list);
out:
	return p_cmd_elem;
}

/* Must be called while cmd_lock is acquired */
static void qed_mcp_cmd_del_elem(struct qed_hwfn *p_hwfn,
				 struct qed_mcp_cmd_elem *p_cmd_elem)
{
	list_del(&p_cmd_elem->list);
	kfree(p_cmd_elem);
}

/* Must be called while cmd_lock is acquired */
static struct qed_mcp_cmd_elem *qed_mcp_cmd_get_elem(struct qed_hwfn *p_hwfn,
						     u16 seq_num)
{
	struct qed_mcp_cmd_elem *p_cmd_elem = NULL;

	list_for_each_entry(p_cmd_elem, &p_hwfn->mcp_info->cmd_list, list) {
		if (p_cmd_elem->expected_seq_num == seq_num)
			return p_cmd_elem;
	}

	return NULL;
}

int qed_mcp_free(struct qed_hwfn *p_hwfn)
{
	if (p_hwfn->mcp_info) {
		struct qed_mcp_cmd_elem *p_cmd_elem, *p_tmp;

		kfree(p_hwfn->mcp_info->mfw_mb_cur);
		kfree(p_hwfn->mcp_info->mfw_mb_shadow);

		spin_lock_bh(&p_hwfn->mcp_info->cmd_lock);
		list_for_each_entry_safe(p_cmd_elem,
					 p_tmp,
					 &p_hwfn->mcp_info->cmd_list, list) {
			qed_mcp_cmd_del_elem(p_hwfn, p_cmd_elem);
		}
		spin_unlock_bh(&p_hwfn->mcp_info->cmd_lock);
	}

	kfree(p_hwfn->mcp_info);
	p_hwfn->mcp_info = NULL;

	return 0;
}

/* Maximum of 1 sec to wait for the SHMEM ready indication */
#define QED_MCP_SHMEM_RDY_MAX_RETRIES	20
#define QED_MCP_SHMEM_RDY_ITER_MS	50

static int qed_load_mcp_offsets(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt)
{
	struct qed_mcp_info *p_info = p_hwfn->mcp_info;
	u8 cnt = QED_MCP_SHMEM_RDY_MAX_RETRIES;
	u8 msec = QED_MCP_SHMEM_RDY_ITER_MS;
	u32 drv_mb_offsize, mfw_mb_offsize;
	u32 mcp_pf_id = MCP_PF_ID(p_hwfn);

	p_info->public_base = qed_rd(p_hwfn, p_ptt, MISC_REG_SHARED_MEM_ADDR);
	if (!p_info->public_base) {
		DP_NOTICE(p_hwfn,
			  "The address of the MCP scratch-pad is not configured\n");
		return -EINVAL;
	}

	p_info->public_base |= GRCBASE_MCP;

	/* Get the MFW MB address and number of supported messages */
	mfw_mb_offsize = qed_rd(p_hwfn, p_ptt,
				SECTION_OFFSIZE_ADDR(p_info->public_base,
						     PUBLIC_MFW_MB));
	p_info->mfw_mb_addr = SECTION_ADDR(mfw_mb_offsize, mcp_pf_id);
	p_info->mfw_mb_length = (u16)qed_rd(p_hwfn, p_ptt,
					    p_info->mfw_mb_addr +
					    offsetof(struct public_mfw_mb,
						     sup_msgs));

	/* The driver can notify that there was an MCP reset, and might read the
	 * SHMEM values before the MFW has completed initializing them.
	 * To avoid this, the "sup_msgs" field in the MFW mailbox is used as a
	 * data ready indication.
	 */
	while (!p_info->mfw_mb_length && --cnt) {
		msleep(msec);
		p_info->mfw_mb_length =
			(u16)qed_rd(p_hwfn, p_ptt,
				    p_info->mfw_mb_addr +
				    offsetof(struct public_mfw_mb, sup_msgs));
	}

	if (!cnt) {
		DP_NOTICE(p_hwfn,
			  "Failed to get the SHMEM ready notification after %d msec\n",
			  QED_MCP_SHMEM_RDY_MAX_RETRIES * msec);
		return -EBUSY;
	}

	/* Calculate the driver and MFW mailbox address */
	drv_mb_offsize = qed_rd(p_hwfn, p_ptt,
				SECTION_OFFSIZE_ADDR(p_info->public_base,
						     PUBLIC_DRV_MB));
	p_info->drv_mb_addr = SECTION_ADDR(drv_mb_offsize, mcp_pf_id);
	DP_VERBOSE(p_hwfn, QED_MSG_SP,
		   "drv_mb_offsiz = 0x%x, drv_mb_addr = 0x%x mcp_pf_id = 0x%x\n",
		   drv_mb_offsize, p_info->drv_mb_addr, mcp_pf_id);

	/* Get the current driver mailbox sequence before sending
	 * the first command
	 */
	p_info->drv_mb_seq = DRV_MB_RD(p_hwfn, p_ptt, drv_mb_header) &
			     DRV_MSG_SEQ_NUMBER_MASK;

	/* Get current FW pulse sequence */
	p_info->drv_pulse_seq = DRV_MB_RD(p_hwfn, p_ptt, drv_pulse_mb) &
				DRV_PULSE_SEQ_MASK;

	p_info->mcp_hist = qed_rd(p_hwfn, p_ptt, MISCS_REG_GENERIC_POR_0);

	return 0;
}

int qed_mcp_cmd_init(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt)
{
	struct qed_mcp_info *p_info;
	u32 size;

	/* Allocate mcp_info structure */
	p_hwfn->mcp_info = kzalloc(sizeof(*p_hwfn->mcp_info), GFP_KERNEL);
	if (!p_hwfn->mcp_info)
		goto err;
	p_info = p_hwfn->mcp_info;

	/* Initialize the MFW spinlock */
	spin_lock_init(&p_info->cmd_lock);
	spin_lock_init(&p_info->link_lock);

	INIT_LIST_HEAD(&p_info->cmd_list);

	if (qed_load_mcp_offsets(p_hwfn, p_ptt) != 0) {
		DP_NOTICE(p_hwfn, "MCP is not initialized\n");
		/* Do not free mcp_info here, since public_base indicate that
		 * the MCP is not initialized
		 */
		return 0;
	}

	size = MFW_DRV_MSG_MAX_DWORDS(p_info->mfw_mb_length) * sizeof(u32);
	p_info->mfw_mb_cur = kzalloc(size, GFP_KERNEL);
	p_info->mfw_mb_shadow = kzalloc(size, GFP_KERNEL);
	if (!p_info->mfw_mb_cur || !p_info->mfw_mb_shadow)
		goto err;

	return 0;

err:
	qed_mcp_free(p_hwfn);
	return -ENOMEM;
}

static void qed_mcp_reread_offsets(struct qed_hwfn *p_hwfn,
				   struct qed_ptt *p_ptt)
{
	u32 generic_por_0 = qed_rd(p_hwfn, p_ptt, MISCS_REG_GENERIC_POR_0);

	/* Use MCP history register to check if MCP reset occurred between init
	 * time and now.
	 */
	if (p_hwfn->mcp_info->mcp_hist != generic_por_0) {
		DP_VERBOSE(p_hwfn,
			   QED_MSG_SP,
			   "Rereading MCP offsets [mcp_hist 0x%08x, generic_por_0 0x%08x]\n",
			   p_hwfn->mcp_info->mcp_hist, generic_por_0);

		qed_load_mcp_offsets(p_hwfn, p_ptt);
		qed_mcp_cmd_port_init(p_hwfn, p_ptt);
	}
}

int qed_mcp_reset(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt)
{
	u32 org_mcp_reset_seq, seq, delay = QED_MCP_RESP_ITER_US, cnt = 0;
	int rc = 0;

	if (p_hwfn->mcp_info->b_block_cmd) {
		DP_NOTICE(p_hwfn,
			  "The MFW is not responsive. Avoid sending MCP_RESET mailbox command.\n");
		return -EBUSY;
	}

	/* Ensure that only a single thread is accessing the mailbox */
	spin_lock_bh(&p_hwfn->mcp_info->cmd_lock);

	org_mcp_reset_seq = qed_rd(p_hwfn, p_ptt, MISCS_REG_GENERIC_POR_0);

	/* Set drv command along with the updated sequence */
	qed_mcp_reread_offsets(p_hwfn, p_ptt);
	seq = ++p_hwfn->mcp_info->drv_mb_seq;
	DRV_MB_WR(p_hwfn, p_ptt, drv_mb_header, (DRV_MSG_CODE_MCP_RESET | seq));

	do {
		/* Wait for MFW response */
		udelay(delay);
		/* Give the FW up to 500 second (50*1000*10usec) */
	} while ((org_mcp_reset_seq == qed_rd(p_hwfn, p_ptt,
					      MISCS_REG_GENERIC_POR_0)) &&
		 (cnt++ < QED_MCP_RESET_RETRIES));

	if (org_mcp_reset_seq !=
	    qed_rd(p_hwfn, p_ptt, MISCS_REG_GENERIC_POR_0)) {
		DP_VERBOSE(p_hwfn, QED_MSG_SP,
			   "MCP was reset after %d usec\n", cnt * delay);
	} else {
		DP_ERR(p_hwfn, "Failed to reset MCP\n");
		rc = -EAGAIN;
	}

	spin_unlock_bh(&p_hwfn->mcp_info->cmd_lock);

	return rc;
}

/* Must be called while cmd_lock is acquired */
static bool qed_mcp_has_pending_cmd(struct qed_hwfn *p_hwfn)
{
	struct qed_mcp_cmd_elem *p_cmd_elem;

	/* There is at most one pending command at a certain time, and if it
	 * exists - it is placed at the HEAD of the list.
	 */
	if (!list_empty(&p_hwfn->mcp_info->cmd_list)) {
		p_cmd_elem = list_first_entry(&p_hwfn->mcp_info->cmd_list,
					      struct qed_mcp_cmd_elem, list);
		return !p_cmd_elem->b_is_completed;
	}

	return false;
}

/* Must be called while cmd_lock is acquired */
static int
qed_mcp_update_pending_cmd(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt)
{
	struct qed_mcp_mb_params *p_mb_params;
	struct qed_mcp_cmd_elem *p_cmd_elem;
	u32 mcp_resp;
	u16 seq_num;

	mcp_resp = DRV_MB_RD(p_hwfn, p_ptt, fw_mb_header);
	seq_num = (u16)(mcp_resp & FW_MSG_SEQ_NUMBER_MASK);

	/* Return if no new non-handled response has been received */
	if (seq_num != p_hwfn->mcp_info->drv_mb_seq)
		return -EAGAIN;

	p_cmd_elem = qed_mcp_cmd_get_elem(p_hwfn, seq_num);
	if (!p_cmd_elem) {
		DP_ERR(p_hwfn,
		       "Failed to find a pending mailbox cmd that expects sequence number %d\n",
		       seq_num);
		return -EINVAL;
	}

	p_mb_params = p_cmd_elem->p_mb_params;

	/* Get the MFW response along with the sequence number */
	p_mb_params->mcp_resp = mcp_resp;

	/* Get the MFW param */
	p_mb_params->mcp_param = DRV_MB_RD(p_hwfn, p_ptt, fw_mb_param);

	/* Get the union data */
	if (p_mb_params->p_data_dst != NULL && p_mb_params->data_dst_size) {
		u32 union_data_addr = p_hwfn->mcp_info->drv_mb_addr +
				      offsetof(struct public_drv_mb,
					       union_data);
		qed_memcpy_from(p_hwfn, p_ptt, p_mb_params->p_data_dst,
				union_data_addr, p_mb_params->data_dst_size);
	}

	p_cmd_elem->b_is_completed = true;

	return 0;
}

/* Must be called while cmd_lock is acquired */
static void __qed_mcp_cmd_and_union(struct qed_hwfn *p_hwfn,
				    struct qed_ptt *p_ptt,
				    struct qed_mcp_mb_params *p_mb_params,
				    u16 seq_num)
{
	union drv_union_data union_data;
	u32 union_data_addr;

	/* Set the union data */
	union_data_addr = p_hwfn->mcp_info->drv_mb_addr +
			  offsetof(struct public_drv_mb, union_data);
	memset(&union_data, 0, sizeof(union_data));
	if (p_mb_params->p_data_src != NULL && p_mb_params->data_src_size)
		memcpy(&union_data, p_mb_params->p_data_src,
		       p_mb_params->data_src_size);
	qed_memcpy_to(p_hwfn, p_ptt, union_data_addr, &union_data,
		      sizeof(union_data));

	/* Set the drv param */
	DRV_MB_WR(p_hwfn, p_ptt, drv_mb_param, p_mb_params->param);

	/* Set the drv command along with the sequence number */
	DRV_MB_WR(p_hwfn, p_ptt, drv_mb_header, (p_mb_params->cmd | seq_num));

	DP_VERBOSE(p_hwfn, QED_MSG_SP,
		   "MFW mailbox: command 0x%08x param 0x%08x\n",
		   (p_mb_params->cmd | seq_num), p_mb_params->param);
}

static void qed_mcp_cmd_set_blocking(struct qed_hwfn *p_hwfn, bool block_cmd)
{
	p_hwfn->mcp_info->b_block_cmd = block_cmd;

	DP_INFO(p_hwfn, "%s sending of mailbox commands to the MFW\n",
		block_cmd ? "Block" : "Unblock");
}

static void qed_mcp_print_cpu_info(struct qed_hwfn *p_hwfn,
				   struct qed_ptt *p_ptt)
{
	u32 cpu_mode, cpu_state, cpu_pc_0, cpu_pc_1, cpu_pc_2;
	u32 delay = QED_MCP_RESP_ITER_US;

	cpu_mode = qed_rd(p_hwfn, p_ptt, MCP_REG_CPU_MODE);
	cpu_state = qed_rd(p_hwfn, p_ptt, MCP_REG_CPU_STATE);
	cpu_pc_0 = qed_rd(p_hwfn, p_ptt, MCP_REG_CPU_PROGRAM_COUNTER);
	udelay(delay);
	cpu_pc_1 = qed_rd(p_hwfn, p_ptt, MCP_REG_CPU_PROGRAM_COUNTER);
	udelay(delay);
	cpu_pc_2 = qed_rd(p_hwfn, p_ptt, MCP_REG_CPU_PROGRAM_COUNTER);

	DP_NOTICE(p_hwfn,
		  "MCP CPU info: mode 0x%08x, state 0x%08x, pc {0x%08x, 0x%08x, 0x%08x}\n",
		  cpu_mode, cpu_state, cpu_pc_0, cpu_pc_1, cpu_pc_2);
}

static int
_qed_mcp_cmd_and_union(struct qed_hwfn *p_hwfn,
		       struct qed_ptt *p_ptt,
		       struct qed_mcp_mb_params *p_mb_params,
		       u32 max_retries, u32 usecs)
{
	u32 cnt = 0, msecs = DIV_ROUND_UP(usecs, 1000);
	struct qed_mcp_cmd_elem *p_cmd_elem;
	u16 seq_num;
	int rc = 0;

	/* Wait until the mailbox is non-occupied */
	do {
		/* Exit the loop if there is no pending command, or if the
		 * pending command is completed during this iteration.
		 * The spinlock stays locked until the command is sent.
		 */

		spin_lock_bh(&p_hwfn->mcp_info->cmd_lock);

		if (!qed_mcp_has_pending_cmd(p_hwfn)) {
			spin_unlock_bh(&p_hwfn->mcp_info->cmd_lock);
			break;
		}

		rc = qed_mcp_update_pending_cmd(p_hwfn, p_ptt);
		if (!rc) {
			spin_unlock_bh(&p_hwfn->mcp_info->cmd_lock);
			break;
		} else if (rc != -EAGAIN) {
			goto err;
		}

		spin_unlock_bh(&p_hwfn->mcp_info->cmd_lock);

		if (QED_MB_FLAGS_IS_SET(p_mb_params, CAN_SLEEP))
			msleep(msecs);
		else
			udelay(usecs);
	} while (++cnt < max_retries);

	if (cnt >= max_retries) {
		DP_NOTICE(p_hwfn,
			  "The MFW mailbox is occupied by an uncompleted command. Failed to send command 0x%08x [param 0x%08x].\n",
			  p_mb_params->cmd, p_mb_params->param);
		return -EAGAIN;
	}

	spin_lock_bh(&p_hwfn->mcp_info->cmd_lock);

	/* Send the mailbox command */
	qed_mcp_reread_offsets(p_hwfn, p_ptt);
	seq_num = ++p_hwfn->mcp_info->drv_mb_seq;
	p_cmd_elem = qed_mcp_cmd_add_elem(p_hwfn, p_mb_params, seq_num);
	if (!p_cmd_elem) {
		rc = -ENOMEM;
		goto err;
	}

	__qed_mcp_cmd_and_union(p_hwfn, p_ptt, p_mb_params, seq_num);
	spin_unlock_bh(&p_hwfn->mcp_info->cmd_lock);

	/* Wait for the MFW response */
	do {
		/* Exit the loop if the command is already completed, or if the
		 * command is completed during this iteration.
		 * The spinlock stays locked until the list element is removed.
		 */

		if (QED_MB_FLAGS_IS_SET(p_mb_params, CAN_SLEEP))
			msleep(msecs);
		else
			udelay(usecs);

		spin_lock_bh(&p_hwfn->mcp_info->cmd_lock);

		if (p_cmd_elem->b_is_completed) {
			spin_unlock_bh(&p_hwfn->mcp_info->cmd_lock);
			break;
		}

		rc = qed_mcp_update_pending_cmd(p_hwfn, p_ptt);
		if (!rc) {
			spin_unlock_bh(&p_hwfn->mcp_info->cmd_lock);
			break;
		} else if (rc != -EAGAIN) {
			goto err;
		}

		spin_unlock_bh(&p_hwfn->mcp_info->cmd_lock);
	} while (++cnt < max_retries);

	if (cnt >= max_retries) {
		DP_NOTICE(p_hwfn,
			  "The MFW failed to respond to command 0x%08x [param 0x%08x].\n",
			  p_mb_params->cmd, p_mb_params->param);
		qed_mcp_print_cpu_info(p_hwfn, p_ptt);

		spin_lock_bh(&p_hwfn->mcp_info->cmd_lock);
		qed_mcp_cmd_del_elem(p_hwfn, p_cmd_elem);
		spin_unlock_bh(&p_hwfn->mcp_info->cmd_lock);

		if (!QED_MB_FLAGS_IS_SET(p_mb_params, AVOID_BLOCK))
			qed_mcp_cmd_set_blocking(p_hwfn, true);

		qed_hw_err_notify(p_hwfn, p_ptt,
				  QED_HW_ERR_MFW_RESP_FAIL, NULL);
		return -EAGAIN;
	}

	spin_lock_bh(&p_hwfn->mcp_info->cmd_lock);
	qed_mcp_cmd_del_elem(p_hwfn, p_cmd_elem);
	spin_unlock_bh(&p_hwfn->mcp_info->cmd_lock);

	DP_VERBOSE(p_hwfn,
		   QED_MSG_SP,
		   "MFW mailbox: response 0x%08x param 0x%08x [after %d.%03d ms]\n",
		   p_mb_params->mcp_resp,
		   p_mb_params->mcp_param,
		   (cnt * usecs) / 1000, (cnt * usecs) % 1000);

	/* Clear the sequence number from the MFW response */
	p_mb_params->mcp_resp &= FW_MSG_CODE_MASK;

	return 0;

err:
	spin_unlock_bh(&p_hwfn->mcp_info->cmd_lock);
	return rc;
}

static int qed_mcp_cmd_and_union(struct qed_hwfn *p_hwfn,
				 struct qed_ptt *p_ptt,
				 struct qed_mcp_mb_params *p_mb_params)
{
	size_t union_data_size = sizeof(union drv_union_data);
	u32 max_retries = QED_DRV_MB_MAX_RETRIES;
	u32 usecs = QED_MCP_RESP_ITER_US;

	/* MCP not initialized */
	if (!qed_mcp_is_init(p_hwfn)) {
		DP_NOTICE(p_hwfn, "MFW is not initialized!\n");
		return -EBUSY;
	}

	if (p_hwfn->mcp_info->b_block_cmd) {
		DP_NOTICE(p_hwfn,
			  "The MFW is not responsive. Avoid sending mailbox command 0x%08x [param 0x%08x].\n",
			  p_mb_params->cmd, p_mb_params->param);
		return -EBUSY;
	}

	if (p_mb_params->data_src_size > union_data_size ||
	    p_mb_params->data_dst_size > union_data_size) {
		DP_ERR(p_hwfn,
		       "The provided size is larger than the union data size [src_size %u, dst_size %u, union_data_size %zu]\n",
		       p_mb_params->data_src_size,
		       p_mb_params->data_dst_size, union_data_size);
		return -EINVAL;
	}

	if (QED_MB_FLAGS_IS_SET(p_mb_params, CAN_SLEEP)) {
		max_retries = DIV_ROUND_UP(max_retries, 1000);
		usecs *= 1000;
	}

	return _qed_mcp_cmd_and_union(p_hwfn, p_ptt, p_mb_params, max_retries,
				      usecs);
}

int qed_mcp_cmd(struct qed_hwfn *p_hwfn,
		struct qed_ptt *p_ptt,
		u32 cmd,
		u32 param,
		u32 *o_mcp_resp,
		u32 *o_mcp_param)
{
	struct qed_mcp_mb_params mb_params;
	int rc;

	memset(&mb_params, 0, sizeof(mb_params));
	mb_params.cmd = cmd;
	mb_params.param = param;

	rc = qed_mcp_cmd_and_union(p_hwfn, p_ptt, &mb_params);
	if (rc)
		return rc;

	*o_mcp_resp = mb_params.mcp_resp;
	*o_mcp_param = mb_params.mcp_param;

	return 0;
}

static int
qed_mcp_nvm_wr_cmd(struct qed_hwfn *p_hwfn,
		   struct qed_ptt *p_ptt,
		   u32 cmd,
		   u32 param,
		   u32 *o_mcp_resp,
		   u32 *o_mcp_param, u32 i_txn_size, u32 *i_buf)
{
	struct qed_mcp_mb_params mb_params;
	int rc;

	memset(&mb_params, 0, sizeof(mb_params));
	mb_params.cmd = cmd;
	mb_params.param = param;
	mb_params.p_data_src = i_buf;
	mb_params.data_src_size = (u8)i_txn_size;
	rc = qed_mcp_cmd_and_union(p_hwfn, p_ptt, &mb_params);
	if (rc)
		return rc;

	*o_mcp_resp = mb_params.mcp_resp;
	*o_mcp_param = mb_params.mcp_param;

	/* nvm_info needs to be updated */
	p_hwfn->nvm_info.valid = false;

	return 0;
}

int qed_mcp_nvm_rd_cmd(struct qed_hwfn *p_hwfn,
		       struct qed_ptt *p_ptt,
		       u32 cmd,
		       u32 param,
		       u32 *o_mcp_resp,
		       u32 *o_mcp_param, u32 *o_txn_size, u32 *o_buf)
{
	struct qed_mcp_mb_params mb_params;
	u8 raw_data[MCP_DRV_NVM_BUF_LEN];
	int rc;

	memset(&mb_params, 0, sizeof(mb_params));
	mb_params.cmd = cmd;
	mb_params.param = param;
	mb_params.p_data_dst = raw_data;

	/* Use the maximal value since the actual one is part of the response */
	mb_params.data_dst_size = MCP_DRV_NVM_BUF_LEN;

	rc = qed_mcp_cmd_and_union(p_hwfn, p_ptt, &mb_params);
	if (rc)
		return rc;

	*o_mcp_resp = mb_params.mcp_resp;
	*o_mcp_param = mb_params.mcp_param;

	*o_txn_size = *o_mcp_param;
	memcpy(o_buf, raw_data, *o_txn_size);

	return 0;
}

static bool
qed_mcp_can_force_load(u8 drv_role,
		       u8 exist_drv_role,
		       enum qed_override_force_load override_force_load)
{
	bool can_force_load = false;

	switch (override_force_load) {
	case QED_OVERRIDE_FORCE_LOAD_ALWAYS:
		can_force_load = true;
		break;
	case QED_OVERRIDE_FORCE_LOAD_NEVER:
		can_force_load = false;
		break;
	default:
		can_force_load = (drv_role == DRV_ROLE_OS &&
				  exist_drv_role == DRV_ROLE_PREBOOT) ||
				 (drv_role == DRV_ROLE_KDUMP &&
				  exist_drv_role == DRV_ROLE_OS);
		break;
	}

	return can_force_load;
}

static int qed_mcp_cancel_load_req(struct qed_hwfn *p_hwfn,
				   struct qed_ptt *p_ptt)
{
	u32 resp = 0, param = 0;
	int rc;

	rc = qed_mcp_cmd(p_hwfn, p_ptt, DRV_MSG_CODE_CANCEL_LOAD_REQ, 0,
			 &resp, &param);
	if (rc)
		DP_NOTICE(p_hwfn,
			  "Failed to send cancel load request, rc = %d\n", rc);

	return rc;
}

#define CONFIG_QEDE_BITMAP_IDX		BIT(0)
#define CONFIG_QED_SRIOV_BITMAP_IDX	BIT(1)
#define CONFIG_QEDR_BITMAP_IDX		BIT(2)
#define CONFIG_QEDF_BITMAP_IDX		BIT(4)
#define CONFIG_QEDI_BITMAP_IDX		BIT(5)
#define CONFIG_QED_LL2_BITMAP_IDX	BIT(6)

static u32 qed_get_config_bitmap(void)
{
	u32 config_bitmap = 0x0;

	if (IS_ENABLED(CONFIG_QEDE))
		config_bitmap |= CONFIG_QEDE_BITMAP_IDX;

	if (IS_ENABLED(CONFIG_QED_SRIOV))
		config_bitmap |= CONFIG_QED_SRIOV_BITMAP_IDX;

	if (IS_ENABLED(CONFIG_QED_RDMA))
		config_bitmap |= CONFIG_QEDR_BITMAP_IDX;

	if (IS_ENABLED(CONFIG_QED_FCOE))
		config_bitmap |= CONFIG_QEDF_BITMAP_IDX;

	if (IS_ENABLED(CONFIG_QED_ISCSI))
		config_bitmap |= CONFIG_QEDI_BITMAP_IDX;

	if (IS_ENABLED(CONFIG_QED_LL2))
		config_bitmap |= CONFIG_QED_LL2_BITMAP_IDX;

	return config_bitmap;
}

struct qed_load_req_in_params {
	u8 hsi_ver;
#define QED_LOAD_REQ_HSI_VER_DEFAULT	0
#define QED_LOAD_REQ_HSI_VER_1		1
	u32 drv_ver_0;
	u32 drv_ver_1;
	u32 fw_ver;
	u8 drv_role;
	u8 timeout_val;
	u8 force_cmd;
	bool avoid_eng_reset;
};

struct qed_load_req_out_params {
	u32 load_code;
	u32 exist_drv_ver_0;
	u32 exist_drv_ver_1;
	u32 exist_fw_ver;
	u8 exist_drv_role;
	u8 mfw_hsi_ver;
	bool drv_exists;
};

static int
__qed_mcp_load_req(struct qed_hwfn *p_hwfn,
		   struct qed_ptt *p_ptt,
		   struct qed_load_req_in_params *p_in_params,
		   struct qed_load_req_out_params *p_out_params)
{
	struct qed_mcp_mb_params mb_params;
	struct load_req_stc load_req;
	struct load_rsp_stc load_rsp;
	u32 hsi_ver;
	int rc;

	memset(&load_req, 0, sizeof(load_req));
	load_req.drv_ver_0 = p_in_params->drv_ver_0;
	load_req.drv_ver_1 = p_in_params->drv_ver_1;
	load_req.fw_ver = p_in_params->fw_ver;
	QED_MFW_SET_FIELD(load_req.misc0, LOAD_REQ_ROLE, p_in_params->drv_role);
	QED_MFW_SET_FIELD(load_req.misc0, LOAD_REQ_LOCK_TO,
			  p_in_params->timeout_val);
	QED_MFW_SET_FIELD(load_req.misc0, LOAD_REQ_FORCE,
			  p_in_params->force_cmd);
	QED_MFW_SET_FIELD(load_req.misc0, LOAD_REQ_FLAGS0,
			  p_in_params->avoid_eng_reset);

	hsi_ver = (p_in_params->hsi_ver == QED_LOAD_REQ_HSI_VER_DEFAULT) ?
		  DRV_ID_MCP_HSI_VER_CURRENT :
		  (p_in_params->hsi_ver << DRV_ID_MCP_HSI_VER_SHIFT);

	memset(&mb_params, 0, sizeof(mb_params));
	mb_params.cmd = DRV_MSG_CODE_LOAD_REQ;
	mb_params.param = PDA_COMP | hsi_ver | p_hwfn->cdev->drv_type;
	mb_params.p_data_src = &load_req;
	mb_params.data_src_size = sizeof(load_req);
	mb_params.p_data_dst = &load_rsp;
	mb_params.data_dst_size = sizeof(load_rsp);
	mb_params.flags = QED_MB_FLAG_CAN_SLEEP | QED_MB_FLAG_AVOID_BLOCK;

	DP_VERBOSE(p_hwfn, QED_MSG_SP,
		   "Load Request: param 0x%08x [init_hw %d, drv_type %d, hsi_ver %d, pda 0x%04x]\n",
		   mb_params.param,
		   QED_MFW_GET_FIELD(mb_params.param, DRV_ID_DRV_INIT_HW),
		   QED_MFW_GET_FIELD(mb_params.param, DRV_ID_DRV_TYPE),
		   QED_MFW_GET_FIELD(mb_params.param, DRV_ID_MCP_HSI_VER),
		   QED_MFW_GET_FIELD(mb_params.param, DRV_ID_PDA_COMP_VER));

	if (p_in_params->hsi_ver != QED_LOAD_REQ_HSI_VER_1) {
		DP_VERBOSE(p_hwfn, QED_MSG_SP,
			   "Load Request: drv_ver 0x%08x_0x%08x, fw_ver 0x%08x, misc0 0x%08x [role %d, timeout %d, force %d, flags0 0x%x]\n",
			   load_req.drv_ver_0,
			   load_req.drv_ver_1,
			   load_req.fw_ver,
			   load_req.misc0,
			   QED_MFW_GET_FIELD(load_req.misc0, LOAD_REQ_ROLE),
			   QED_MFW_GET_FIELD(load_req.misc0,
					     LOAD_REQ_LOCK_TO),
			   QED_MFW_GET_FIELD(load_req.misc0, LOAD_REQ_FORCE),
			   QED_MFW_GET_FIELD(load_req.misc0, LOAD_REQ_FLAGS0));
	}

	rc = qed_mcp_cmd_and_union(p_hwfn, p_ptt, &mb_params);
	if (rc) {
		DP_NOTICE(p_hwfn, "Failed to send load request, rc = %d\n", rc);
		return rc;
	}

	DP_VERBOSE(p_hwfn, QED_MSG_SP,
		   "Load Response: resp 0x%08x\n", mb_params.mcp_resp);
	p_out_params->load_code = mb_params.mcp_resp;

	if (p_in_params->hsi_ver != QED_LOAD_REQ_HSI_VER_1 &&
	    p_out_params->load_code != FW_MSG_CODE_DRV_LOAD_REFUSED_HSI_1) {
		DP_VERBOSE(p_hwfn,
			   QED_MSG_SP,
			   "Load Response: exist_drv_ver 0x%08x_0x%08x, exist_fw_ver 0x%08x, misc0 0x%08x [exist_role %d, mfw_hsi %d, flags0 0x%x]\n",
			   load_rsp.drv_ver_0,
			   load_rsp.drv_ver_1,
			   load_rsp.fw_ver,
			   load_rsp.misc0,
			   QED_MFW_GET_FIELD(load_rsp.misc0, LOAD_RSP_ROLE),
			   QED_MFW_GET_FIELD(load_rsp.misc0, LOAD_RSP_HSI),
			   QED_MFW_GET_FIELD(load_rsp.misc0, LOAD_RSP_FLAGS0));

		p_out_params->exist_drv_ver_0 = load_rsp.drv_ver_0;
		p_out_params->exist_drv_ver_1 = load_rsp.drv_ver_1;
		p_out_params->exist_fw_ver = load_rsp.fw_ver;
		p_out_params->exist_drv_role =
		    QED_MFW_GET_FIELD(load_rsp.misc0, LOAD_RSP_ROLE);
		p_out_params->mfw_hsi_ver =
		    QED_MFW_GET_FIELD(load_rsp.misc0, LOAD_RSP_HSI);
		p_out_params->drv_exists =
		    QED_MFW_GET_FIELD(load_rsp.misc0, LOAD_RSP_FLAGS0) &
		    LOAD_RSP_FLAGS0_DRV_EXISTS;
	}

	return 0;
}

static int eocre_get_mfw_drv_role(struct qed_hwfn *p_hwfn,
				  enum qed_drv_role drv_role,
				  u8 *p_mfw_drv_role)
{
	switch (drv_role) {
	case QED_DRV_ROLE_OS:
		*p_mfw_drv_role = DRV_ROLE_OS;
		break;
	case QED_DRV_ROLE_KDUMP:
		*p_mfw_drv_role = DRV_ROLE_KDUMP;
		break;
	default:
		DP_ERR(p_hwfn, "Unexpected driver role %d\n", drv_role);
		return -EINVAL;
	}

	return 0;
}

enum qed_load_req_force {
	QED_LOAD_REQ_FORCE_NONE,
	QED_LOAD_REQ_FORCE_PF,
	QED_LOAD_REQ_FORCE_ALL,
};

static void qed_get_mfw_force_cmd(struct qed_hwfn *p_hwfn,

				  enum qed_load_req_force force_cmd,
				  u8 *p_mfw_force_cmd)
{
	switch (force_cmd) {
	case QED_LOAD_REQ_FORCE_NONE:
		*p_mfw_force_cmd = LOAD_REQ_FORCE_NONE;
		break;
	case QED_LOAD_REQ_FORCE_PF:
		*p_mfw_force_cmd = LOAD_REQ_FORCE_PF;
		break;
	case QED_LOAD_REQ_FORCE_ALL:
		*p_mfw_force_cmd = LOAD_REQ_FORCE_ALL;
		break;
	}
}

int qed_mcp_load_req(struct qed_hwfn *p_hwfn,
		     struct qed_ptt *p_ptt,
		     struct qed_load_req_params *p_params)
{
	struct qed_load_req_out_params out_params;
	struct qed_load_req_in_params in_params;
	u8 mfw_drv_role, mfw_force_cmd;
	int rc;

	memset(&in_params, 0, sizeof(in_params));
	in_params.hsi_ver = QED_LOAD_REQ_HSI_VER_DEFAULT;
	in_params.drv_ver_0 = QED_VERSION;
	in_params.drv_ver_1 = qed_get_config_bitmap();
	in_params.fw_ver = STORM_FW_VERSION;
	rc = eocre_get_mfw_drv_role(p_hwfn, p_params->drv_role, &mfw_drv_role);
	if (rc)
		return rc;

	in_params.drv_role = mfw_drv_role;
	in_params.timeout_val = p_params->timeout_val;
	qed_get_mfw_force_cmd(p_hwfn,
			      QED_LOAD_REQ_FORCE_NONE, &mfw_force_cmd);

	in_params.force_cmd = mfw_force_cmd;
	in_params.avoid_eng_reset = p_params->avoid_eng_reset;

	memset(&out_params, 0, sizeof(out_params));
	rc = __qed_mcp_load_req(p_hwfn, p_ptt, &in_params, &out_params);
	if (rc)
		return rc;

	/* First handle cases where another load request should/might be sent:
	 * - MFW expects the old interface [HSI version = 1]
	 * - MFW responds that a force load request is required
	 */
	if (out_params.load_code == FW_MSG_CODE_DRV_LOAD_REFUSED_HSI_1) {
		DP_INFO(p_hwfn,
			"MFW refused a load request due to HSI > 1. Resending with HSI = 1\n");

		in_params.hsi_ver = QED_LOAD_REQ_HSI_VER_1;
		memset(&out_params, 0, sizeof(out_params));
		rc = __qed_mcp_load_req(p_hwfn, p_ptt, &in_params, &out_params);
		if (rc)
			return rc;
	} else if (out_params.load_code ==
		   FW_MSG_CODE_DRV_LOAD_REFUSED_REQUIRES_FORCE) {
		if (qed_mcp_can_force_load(in_params.drv_role,
					   out_params.exist_drv_role,
					   p_params->override_force_load)) {
			DP_INFO(p_hwfn,
				"A force load is required [{role, fw_ver, drv_ver}: loading={%d, 0x%08x, x%08x_0x%08x}, existing={%d, 0x%08x, 0x%08x_0x%08x}]\n",
				in_params.drv_role, in_params.fw_ver,
				in_params.drv_ver_0, in_params.drv_ver_1,
				out_params.exist_drv_role,
				out_params.exist_fw_ver,
				out_params.exist_drv_ver_0,
				out_params.exist_drv_ver_1);

			qed_get_mfw_force_cmd(p_hwfn,
					      QED_LOAD_REQ_FORCE_ALL,
					      &mfw_force_cmd);

			in_params.force_cmd = mfw_force_cmd;
			memset(&out_params, 0, sizeof(out_params));
			rc = __qed_mcp_load_req(p_hwfn, p_ptt, &in_params,
						&out_params);
			if (rc)
				return rc;
		} else {
			DP_NOTICE(p_hwfn,
				  "A force load is required [{role, fw_ver, drv_ver}: loading={%d, 0x%08x, x%08x_0x%08x}, existing={%d, 0x%08x, 0x%08x_0x%08x}] - Avoid\n",
				  in_params.drv_role, in_params.fw_ver,
				  in_params.drv_ver_0, in_params.drv_ver_1,
				  out_params.exist_drv_role,
				  out_params.exist_fw_ver,
				  out_params.exist_drv_ver_0,
				  out_params.exist_drv_ver_1);
			DP_NOTICE(p_hwfn,
				  "Avoid sending a force load request to prevent disruption of active PFs\n");

			qed_mcp_cancel_load_req(p_hwfn, p_ptt);
			return -EBUSY;
		}
	}

	/* Now handle the other types of responses.
	 * The "REFUSED_HSI_1" and "REFUSED_REQUIRES_FORCE" responses are not
	 * expected here after the additional revised load requests were sent.
	 */
	switch (out_params.load_code) {
	case FW_MSG_CODE_DRV_LOAD_ENGINE:
	case FW_MSG_CODE_DRV_LOAD_PORT:
	case FW_MSG_CODE_DRV_LOAD_FUNCTION:
		if (out_params.mfw_hsi_ver != QED_LOAD_REQ_HSI_VER_1 &&
		    out_params.drv_exists) {
			/* The role and fw/driver version match, but the PF is
			 * already loaded and has not been unloaded gracefully.
			 */
			DP_NOTICE(p_hwfn,
				  "PF is already loaded\n");
			return -EINVAL;
		}
		break;
	default:
		DP_NOTICE(p_hwfn,
			  "Unexpected refusal to load request [resp 0x%08x]. Aborting.\n",
			  out_params.load_code);
		return -EBUSY;
	}

	p_params->load_code = out_params.load_code;

	return 0;
}

int qed_mcp_load_done(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt)
{
	u32 resp = 0, param = 0;
	int rc;

	rc = qed_mcp_cmd(p_hwfn, p_ptt, DRV_MSG_CODE_LOAD_DONE, 0, &resp,
			 &param);
	if (rc) {
		DP_NOTICE(p_hwfn,
			  "Failed to send a LOAD_DONE command, rc = %d\n", rc);
		return rc;
	}

	/* Check if there is a DID mismatch between nvm-cfg/efuse */
	if (param & FW_MB_PARAM_LOAD_DONE_DID_EFUSE_ERROR)
		DP_NOTICE(p_hwfn,
			  "warning: device configuration is not supported on this board type. The device may not function as expected.\n");

	return 0;
}

int qed_mcp_unload_req(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt)
{
	struct qed_mcp_mb_params mb_params;
	u32 wol_param;

	switch (p_hwfn->cdev->wol_config) {
	case QED_OV_WOL_DISABLED:
		wol_param = DRV_MB_PARAM_UNLOAD_WOL_DISABLED;
		break;
	case QED_OV_WOL_ENABLED:
		wol_param = DRV_MB_PARAM_UNLOAD_WOL_ENABLED;
		break;
	default:
		DP_NOTICE(p_hwfn,
			  "Unknown WoL configuration %02x\n",
			  p_hwfn->cdev->wol_config);
		fallthrough;
	case QED_OV_WOL_DEFAULT:
		wol_param = DRV_MB_PARAM_UNLOAD_WOL_MCP;
	}

	memset(&mb_params, 0, sizeof(mb_params));
	mb_params.cmd = DRV_MSG_CODE_UNLOAD_REQ;
	mb_params.param = wol_param;
	mb_params.flags = QED_MB_FLAG_CAN_SLEEP | QED_MB_FLAG_AVOID_BLOCK;

	return qed_mcp_cmd_and_union(p_hwfn, p_ptt, &mb_params);
}

int qed_mcp_unload_done(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt)
{
	struct qed_mcp_mb_params mb_params;
	struct mcp_mac wol_mac;

	memset(&mb_params, 0, sizeof(mb_params));
	mb_params.cmd = DRV_MSG_CODE_UNLOAD_DONE;

	/* Set the primary MAC if WoL is enabled */
	if (p_hwfn->cdev->wol_config == QED_OV_WOL_ENABLED) {
		u8 *p_mac = p_hwfn->cdev->wol_mac;

		memset(&wol_mac, 0, sizeof(wol_mac));
		wol_mac.mac_upper = p_mac[0] << 8 | p_mac[1];
		wol_mac.mac_lower = p_mac[2] << 24 | p_mac[3] << 16 |
				    p_mac[4] << 8 | p_mac[5];

		DP_VERBOSE(p_hwfn,
			   (QED_MSG_SP | NETIF_MSG_IFDOWN),
			   "Setting WoL MAC: %pM --> [%08x,%08x]\n",
			   p_mac, wol_mac.mac_upper, wol_mac.mac_lower);

		mb_params.p_data_src = &wol_mac;
		mb_params.data_src_size = sizeof(wol_mac);
	}

	return qed_mcp_cmd_and_union(p_hwfn, p_ptt, &mb_params);
}

static void qed_mcp_handle_vf_flr(struct qed_hwfn *p_hwfn,
				  struct qed_ptt *p_ptt)
{
	u32 addr = SECTION_OFFSIZE_ADDR(p_hwfn->mcp_info->public_base,
					PUBLIC_PATH);
	u32 mfw_path_offsize = qed_rd(p_hwfn, p_ptt, addr);
	u32 path_addr = SECTION_ADDR(mfw_path_offsize,
				     QED_PATH_ID(p_hwfn));
	u32 disabled_vfs[VF_MAX_STATIC / 32];
	int i;

	DP_VERBOSE(p_hwfn,
		   QED_MSG_SP,
		   "Reading Disabled VF information from [offset %08x], path_addr %08x\n",
		   mfw_path_offsize, path_addr);

	for (i = 0; i < (VF_MAX_STATIC / 32); i++) {
		disabled_vfs[i] = qed_rd(p_hwfn, p_ptt,
					 path_addr +
					 offsetof(struct public_path,
						  mcp_vf_disabled) +
					 sizeof(u32) * i);
		DP_VERBOSE(p_hwfn, (QED_MSG_SP | QED_MSG_IOV),
			   "FLR-ed VFs [%08x,...,%08x] - %08x\n",
			   i * 32, (i + 1) * 32 - 1, disabled_vfs[i]);
	}

	if (qed_iov_mark_vf_flr(p_hwfn, disabled_vfs))
		qed_schedule_iov(p_hwfn, QED_IOV_WQ_FLR_FLAG);
}

int qed_mcp_ack_vf_flr(struct qed_hwfn *p_hwfn,
		       struct qed_ptt *p_ptt, u32 *vfs_to_ack)
{
	u32 addr = SECTION_OFFSIZE_ADDR(p_hwfn->mcp_info->public_base,
					PUBLIC_FUNC);
	u32 mfw_func_offsize = qed_rd(p_hwfn, p_ptt, addr);
	u32 func_addr = SECTION_ADDR(mfw_func_offsize,
				     MCP_PF_ID(p_hwfn));
	struct qed_mcp_mb_params mb_params;
	int rc;
	int i;

	for (i = 0; i < (VF_MAX_STATIC / 32); i++)
		DP_VERBOSE(p_hwfn, (QED_MSG_SP | QED_MSG_IOV),
			   "Acking VFs [%08x,...,%08x] - %08x\n",
			   i * 32, (i + 1) * 32 - 1, vfs_to_ack[i]);

	memset(&mb_params, 0, sizeof(mb_params));
	mb_params.cmd = DRV_MSG_CODE_VF_DISABLED_DONE;
	mb_params.p_data_src = vfs_to_ack;
	mb_params.data_src_size = VF_MAX_STATIC / 8;
	rc = qed_mcp_cmd_and_union(p_hwfn, p_ptt, &mb_params);
	if (rc) {
		DP_NOTICE(p_hwfn, "Failed to pass ACK for VF flr to MFW\n");
		return -EBUSY;
	}

	/* Clear the ACK bits */
	for (i = 0; i < (VF_MAX_STATIC / 32); i++)
		qed_wr(p_hwfn, p_ptt,
		       func_addr +
		       offsetof(struct public_func, drv_ack_vf_disabled) +
		       i * sizeof(u32), 0);

	return rc;
}

static void qed_mcp_handle_transceiver_change(struct qed_hwfn *p_hwfn,
					      struct qed_ptt *p_ptt)
{
	u32 transceiver_state;

	transceiver_state = qed_rd(p_hwfn, p_ptt,
				   p_hwfn->mcp_info->port_addr +
				   offsetof(struct public_port,
					    transceiver_data));

	DP_VERBOSE(p_hwfn,
		   (NETIF_MSG_HW | QED_MSG_SP),
		   "Received transceiver state update [0x%08x] from mfw [Addr 0x%x]\n",
		   transceiver_state,
		   (u32)(p_hwfn->mcp_info->port_addr +
			  offsetof(struct public_port, transceiver_data)));

	transceiver_state = GET_FIELD(transceiver_state,
				      ETH_TRANSCEIVER_STATE);

	if (transceiver_state == ETH_TRANSCEIVER_STATE_PRESENT)
		DP_NOTICE(p_hwfn, "Transceiver is present.\n");
	else
		DP_NOTICE(p_hwfn, "Transceiver is unplugged.\n");
}

static void qed_mcp_read_eee_config(struct qed_hwfn *p_hwfn,
				    struct qed_ptt *p_ptt,
				    struct qed_mcp_link_state *p_link)
{
	u32 eee_status, val;

	p_link->eee_adv_caps = 0;
	p_link->eee_lp_adv_caps = 0;
	eee_status = qed_rd(p_hwfn,
			    p_ptt,
			    p_hwfn->mcp_info->port_addr +
			    offsetof(struct public_port, eee_status));
	p_link->eee_active = !!(eee_status & EEE_ACTIVE_BIT);
	val = (eee_status & EEE_LD_ADV_STATUS_MASK) >> EEE_LD_ADV_STATUS_OFFSET;
	if (val & EEE_1G_ADV)
		p_link->eee_adv_caps |= QED_EEE_1G_ADV;
	if (val & EEE_10G_ADV)
		p_link->eee_adv_caps |= QED_EEE_10G_ADV;
	val = (eee_status & EEE_LP_ADV_STATUS_MASK) >> EEE_LP_ADV_STATUS_OFFSET;
	if (val & EEE_1G_ADV)
		p_link->eee_lp_adv_caps |= QED_EEE_1G_ADV;
	if (val & EEE_10G_ADV)
		p_link->eee_lp_adv_caps |= QED_EEE_10G_ADV;
}

static u32 qed_mcp_get_shmem_func(struct qed_hwfn *p_hwfn,
				  struct qed_ptt *p_ptt,
				  struct public_func *p_data, int pfid)
{
	u32 addr = SECTION_OFFSIZE_ADDR(p_hwfn->mcp_info->public_base,
					PUBLIC_FUNC);
	u32 mfw_path_offsize = qed_rd(p_hwfn, p_ptt, addr);
	u32 func_addr;
	u32 i, size;

	func_addr = SECTION_ADDR(mfw_path_offsize, pfid);
	memset(p_data, 0, sizeof(*p_data));

	size = min_t(u32, sizeof(*p_data), QED_SECTION_SIZE(mfw_path_offsize));
	for (i = 0; i < size / sizeof(u32); i++)
		((u32 *)p_data)[i] = qed_rd(p_hwfn, p_ptt,
					    func_addr + (i << 2));
	return size;
}

static void qed_read_pf_bandwidth(struct qed_hwfn *p_hwfn,
				  struct public_func *p_shmem_info)
{
	struct qed_mcp_function_info *p_info;

	p_info = &p_hwfn->mcp_info->func_info;

	p_info->bandwidth_min = QED_MFW_GET_FIELD(p_shmem_info->config,
						  FUNC_MF_CFG_MIN_BW);
	if (p_info->bandwidth_min < 1 || p_info->bandwidth_min > 100) {
		DP_INFO(p_hwfn,
			"bandwidth minimum out of bounds [%02x]. Set to 1\n",
			p_info->bandwidth_min);
		p_info->bandwidth_min = 1;
	}

	p_info->bandwidth_max = QED_MFW_GET_FIELD(p_shmem_info->config,
						  FUNC_MF_CFG_MAX_BW);
	if (p_info->bandwidth_max < 1 || p_info->bandwidth_max > 100) {
		DP_INFO(p_hwfn,
			"bandwidth maximum out of bounds [%02x]. Set to 100\n",
			p_info->bandwidth_max);
		p_info->bandwidth_max = 100;
	}
}

static void qed_mcp_handle_link_change(struct qed_hwfn *p_hwfn,
				       struct qed_ptt *p_ptt, bool b_reset)
{
	struct qed_mcp_link_state *p_link;
	u8 max_bw, min_bw;
	u32 status = 0;

	/* Prevent SW/attentions from doing this at the same time */
	spin_lock_bh(&p_hwfn->mcp_info->link_lock);

	p_link = &p_hwfn->mcp_info->link_output;
	memset(p_link, 0, sizeof(*p_link));
	if (!b_reset) {
		status = qed_rd(p_hwfn, p_ptt,
				p_hwfn->mcp_info->port_addr +
				offsetof(struct public_port, link_status));
		DP_VERBOSE(p_hwfn, (NETIF_MSG_LINK | QED_MSG_SP),
			   "Received link update [0x%08x] from mfw [Addr 0x%x]\n",
			   status,
			   (u32)(p_hwfn->mcp_info->port_addr +
				 offsetof(struct public_port, link_status)));
	} else {
		DP_VERBOSE(p_hwfn, NETIF_MSG_LINK,
			   "Resetting link indications\n");
		goto out;
	}

	if (p_hwfn->b_drv_link_init) {
		/* Link indication with modern MFW arrives as per-PF
		 * indication.
		 */
		if (p_hwfn->mcp_info->capabilities &
		    FW_MB_PARAM_FEATURE_SUPPORT_VLINK) {
			struct public_func shmem_info;

			qed_mcp_get_shmem_func(p_hwfn, p_ptt, &shmem_info,
					       MCP_PF_ID(p_hwfn));
			p_link->link_up = !!(shmem_info.status &
					     FUNC_STATUS_VIRTUAL_LINK_UP);
			qed_read_pf_bandwidth(p_hwfn, &shmem_info);
			DP_VERBOSE(p_hwfn, NETIF_MSG_LINK,
				   "Virtual link_up = %d\n", p_link->link_up);
		} else {
			p_link->link_up = !!(status & LINK_STATUS_LINK_UP);
			DP_VERBOSE(p_hwfn, NETIF_MSG_LINK,
				   "Physical link_up = %d\n", p_link->link_up);
		}
	} else {
		p_link->link_up = false;
	}

	p_link->full_duplex = true;
	switch ((status & LINK_STATUS_SPEED_AND_DUPLEX_MASK)) {
	case LINK_STATUS_SPEED_AND_DUPLEX_100G:
		p_link->speed = 100000;
		break;
	case LINK_STATUS_SPEED_AND_DUPLEX_50G:
		p_link->speed = 50000;
		break;
	case LINK_STATUS_SPEED_AND_DUPLEX_40G:
		p_link->speed = 40000;
		break;
	case LINK_STATUS_SPEED_AND_DUPLEX_25G:
		p_link->speed = 25000;
		break;
	case LINK_STATUS_SPEED_AND_DUPLEX_20G:
		p_link->speed = 20000;
		break;
	case LINK_STATUS_SPEED_AND_DUPLEX_10G:
		p_link->speed = 10000;
		break;
	case LINK_STATUS_SPEED_AND_DUPLEX_1000THD:
		p_link->full_duplex = false;
		fallthrough;
	case LINK_STATUS_SPEED_AND_DUPLEX_1000TFD:
		p_link->speed = 1000;
		break;
	default:
		p_link->speed = 0;
		p_link->link_up = 0;
	}

	if (p_link->link_up && p_link->speed)
		p_link->line_speed = p_link->speed;
	else
		p_link->line_speed = 0;

	max_bw = p_hwfn->mcp_info->func_info.bandwidth_max;
	min_bw = p_hwfn->mcp_info->func_info.bandwidth_min;

	/* Max bandwidth configuration */
	__qed_configure_pf_max_bandwidth(p_hwfn, p_ptt, p_link, max_bw);

	/* Min bandwidth configuration */
	__qed_configure_pf_min_bandwidth(p_hwfn, p_ptt, p_link, min_bw);
	qed_configure_vp_wfq_on_link_change(p_hwfn->cdev, p_ptt,
					    p_link->min_pf_rate);

	p_link->an = !!(status & LINK_STATUS_AUTO_NEGOTIATE_ENABLED);
	p_link->an_complete = !!(status &
				 LINK_STATUS_AUTO_NEGOTIATE_COMPLETE);
	p_link->parallel_detection = !!(status &
					LINK_STATUS_PARALLEL_DETECTION_USED);
	p_link->pfc_enabled = !!(status & LINK_STATUS_PFC_ENABLED);

	p_link->partner_adv_speed |=
		(status & LINK_STATUS_LINK_PARTNER_1000TFD_CAPABLE) ?
		QED_LINK_PARTNER_SPEED_1G_FD : 0;
	p_link->partner_adv_speed |=
		(status & LINK_STATUS_LINK_PARTNER_1000THD_CAPABLE) ?
		QED_LINK_PARTNER_SPEED_1G_HD : 0;
	p_link->partner_adv_speed |=
		(status & LINK_STATUS_LINK_PARTNER_10G_CAPABLE) ?
		QED_LINK_PARTNER_SPEED_10G : 0;
	p_link->partner_adv_speed |=
		(status & LINK_STATUS_LINK_PARTNER_20G_CAPABLE) ?
		QED_LINK_PARTNER_SPEED_20G : 0;
	p_link->partner_adv_speed |=
		(status & LINK_STATUS_LINK_PARTNER_25G_CAPABLE) ?
		QED_LINK_PARTNER_SPEED_25G : 0;
	p_link->partner_adv_speed |=
		(status & LINK_STATUS_LINK_PARTNER_40G_CAPABLE) ?
		QED_LINK_PARTNER_SPEED_40G : 0;
	p_link->partner_adv_speed |=
		(status & LINK_STATUS_LINK_PARTNER_50G_CAPABLE) ?
		QED_LINK_PARTNER_SPEED_50G : 0;
	p_link->partner_adv_speed |=
		(status & LINK_STATUS_LINK_PARTNER_100G_CAPABLE) ?
		QED_LINK_PARTNER_SPEED_100G : 0;

	p_link->partner_tx_flow_ctrl_en =
		!!(status & LINK_STATUS_TX_FLOW_CONTROL_ENABLED);
	p_link->partner_rx_flow_ctrl_en =
		!!(status & LINK_STATUS_RX_FLOW_CONTROL_ENABLED);

	switch (status & LINK_STATUS_LINK_PARTNER_FLOW_CONTROL_MASK) {
	case LINK_STATUS_LINK_PARTNER_SYMMETRIC_PAUSE:
		p_link->partner_adv_pause = QED_LINK_PARTNER_SYMMETRIC_PAUSE;
		break;
	case LINK_STATUS_LINK_PARTNER_ASYMMETRIC_PAUSE:
		p_link->partner_adv_pause = QED_LINK_PARTNER_ASYMMETRIC_PAUSE;
		break;
	case LINK_STATUS_LINK_PARTNER_BOTH_PAUSE:
		p_link->partner_adv_pause = QED_LINK_PARTNER_BOTH_PAUSE;
		break;
	default:
		p_link->partner_adv_pause = 0;
	}

	p_link->sfp_tx_fault = !!(status & LINK_STATUS_SFP_TX_FAULT);

	if (p_hwfn->mcp_info->capabilities & FW_MB_PARAM_FEATURE_SUPPORT_EEE)
		qed_mcp_read_eee_config(p_hwfn, p_ptt, p_link);

	if (p_hwfn->mcp_info->capabilities &
	    FW_MB_PARAM_FEATURE_SUPPORT_FEC_CONTROL) {
		switch (status & LINK_STATUS_FEC_MODE_MASK) {
		case LINK_STATUS_FEC_MODE_NONE:
			p_link->fec_active = QED_FEC_MODE_NONE;
			break;
		case LINK_STATUS_FEC_MODE_FIRECODE_CL74:
			p_link->fec_active = QED_FEC_MODE_FIRECODE;
			break;
		case LINK_STATUS_FEC_MODE_RS_CL91:
			p_link->fec_active = QED_FEC_MODE_RS;
			break;
		default:
			p_link->fec_active = QED_FEC_MODE_AUTO;
		}
	} else {
		p_link->fec_active = QED_FEC_MODE_UNSUPPORTED;
	}

	qed_link_update(p_hwfn, p_ptt);
out:
	spin_unlock_bh(&p_hwfn->mcp_info->link_lock);
}

int qed_mcp_set_link(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt, bool b_up)
{
	struct qed_mcp_link_params *params = &p_hwfn->mcp_info->link_input;
	struct qed_mcp_mb_params mb_params;
	struct eth_phy_cfg phy_cfg;
	u32 cmd, fec_bit = 0;
	u32 val, ext_speed;
	int rc = 0;

	/* Set the shmem configuration according to params */
	memset(&phy_cfg, 0, sizeof(phy_cfg));
	cmd = b_up ? DRV_MSG_CODE_INIT_PHY : DRV_MSG_CODE_LINK_RESET;
	if (!params->speed.autoneg)
		phy_cfg.speed = params->speed.forced_speed;
	phy_cfg.pause |= (params->pause.autoneg) ? ETH_PAUSE_AUTONEG : 0;
	phy_cfg.pause |= (params->pause.forced_rx) ? ETH_PAUSE_RX : 0;
	phy_cfg.pause |= (params->pause.forced_tx) ? ETH_PAUSE_TX : 0;
	phy_cfg.adv_speed = params->speed.advertised_speeds;
	phy_cfg.loopback_mode = params->loopback_mode;

	/* There are MFWs that share this capability regardless of whether
	 * this is feasible or not. And given that at the very least adv_caps
	 * would be set internally by qed, we want to make sure LFA would
	 * still work.
	 */
	if ((p_hwfn->mcp_info->capabilities &
	     FW_MB_PARAM_FEATURE_SUPPORT_EEE) && params->eee.enable) {
		phy_cfg.eee_cfg |= EEE_CFG_EEE_ENABLED;
		if (params->eee.tx_lpi_enable)
			phy_cfg.eee_cfg |= EEE_CFG_TX_LPI;
		if (params->eee.adv_caps & QED_EEE_1G_ADV)
			phy_cfg.eee_cfg |= EEE_CFG_ADV_SPEED_1G;
		if (params->eee.adv_caps & QED_EEE_10G_ADV)
			phy_cfg.eee_cfg |= EEE_CFG_ADV_SPEED_10G;
		phy_cfg.eee_cfg |= (params->eee.tx_lpi_timer <<
				    EEE_TX_TIMER_USEC_OFFSET) &
				   EEE_TX_TIMER_USEC_MASK;
	}

	if (p_hwfn->mcp_info->capabilities &
	    FW_MB_PARAM_FEATURE_SUPPORT_FEC_CONTROL) {
		if (params->fec & QED_FEC_MODE_NONE)
			fec_bit |= FEC_FORCE_MODE_NONE;
		else if (params->fec & QED_FEC_MODE_FIRECODE)
			fec_bit |= FEC_FORCE_MODE_FIRECODE;
		else if (params->fec & QED_FEC_MODE_RS)
			fec_bit |= FEC_FORCE_MODE_RS;
		else if (params->fec & QED_FEC_MODE_AUTO)
			fec_bit |= FEC_FORCE_MODE_AUTO;

		SET_MFW_FIELD(phy_cfg.fec_mode, FEC_FORCE_MODE, fec_bit);
	}

	if (p_hwfn->mcp_info->capabilities &
	    FW_MB_PARAM_FEATURE_SUPPORT_EXT_SPEED_FEC_CONTROL) {
		ext_speed = 0;
		if (params->ext_speed.autoneg)
			ext_speed |= ETH_EXT_SPEED_AN;

		val = params->ext_speed.forced_speed;
		if (val & QED_EXT_SPEED_1G)
			ext_speed |= ETH_EXT_SPEED_1G;
		if (val & QED_EXT_SPEED_10G)
			ext_speed |= ETH_EXT_SPEED_10G;
		if (val & QED_EXT_SPEED_20G)
			ext_speed |= ETH_EXT_SPEED_20G;
		if (val & QED_EXT_SPEED_25G)
			ext_speed |= ETH_EXT_SPEED_25G;
		if (val & QED_EXT_SPEED_40G)
			ext_speed |= ETH_EXT_SPEED_40G;
		if (val & QED_EXT_SPEED_50G_R)
			ext_speed |= ETH_EXT_SPEED_50G_BASE_R;
		if (val & QED_EXT_SPEED_50G_R2)
			ext_speed |= ETH_EXT_SPEED_50G_BASE_R2;
		if (val & QED_EXT_SPEED_100G_R2)
			ext_speed |= ETH_EXT_SPEED_100G_BASE_R2;
		if (val & QED_EXT_SPEED_100G_R4)
			ext_speed |= ETH_EXT_SPEED_100G_BASE_R4;
		if (val & QED_EXT_SPEED_100G_P4)
			ext_speed |= ETH_EXT_SPEED_100G_BASE_P4;

		SET_MFW_FIELD(phy_cfg.extended_speed, ETH_EXT_SPEED,
			      ext_speed);

		ext_speed = 0;

		val = params->ext_speed.advertised_speeds;
		if (val & QED_EXT_SPEED_MASK_1G)
			ext_speed |= ETH_EXT_ADV_SPEED_1G;
		if (val & QED_EXT_SPEED_MASK_10G)
			ext_speed |= ETH_EXT_ADV_SPEED_10G;
		if (val & QED_EXT_SPEED_MASK_20G)
			ext_speed |= ETH_EXT_ADV_SPEED_20G;
		if (val & QED_EXT_SPEED_MASK_25G)
			ext_speed |= ETH_EXT_ADV_SPEED_25G;
		if (val & QED_EXT_SPEED_MASK_40G)
			ext_speed |= ETH_EXT_ADV_SPEED_40G;
		if (val & QED_EXT_SPEED_MASK_50G_R)
			ext_speed |= ETH_EXT_ADV_SPEED_50G_BASE_R;
		if (val & QED_EXT_SPEED_MASK_50G_R2)
			ext_speed |= ETH_EXT_ADV_SPEED_50G_BASE_R2;
		if (val & QED_EXT_SPEED_MASK_100G_R2)
			ext_speed |= ETH_EXT_ADV_SPEED_100G_BASE_R2;
		if (val & QED_EXT_SPEED_MASK_100G_R4)
			ext_speed |= ETH_EXT_ADV_SPEED_100G_BASE_R4;
		if (val & QED_EXT_SPEED_MASK_100G_P4)
			ext_speed |= ETH_EXT_ADV_SPEED_100G_BASE_P4;

		phy_cfg.extended_speed |= ext_speed;

		SET_MFW_FIELD(phy_cfg.fec_mode, FEC_EXTENDED_MODE,
			      params->ext_fec_mode);
	}

	p_hwfn->b_drv_link_init = b_up;

	if (b_up) {
		DP_VERBOSE(p_hwfn, NETIF_MSG_LINK,
			   "Configuring Link: Speed 0x%08x, Pause 0x%08x, Adv. Speed 0x%08x, Loopback 0x%08x, FEC 0x%08x, Ext. Speed 0x%08x\n",
			   phy_cfg.speed, phy_cfg.pause, phy_cfg.adv_speed,
			   phy_cfg.loopback_mode, phy_cfg.fec_mode,
			   phy_cfg.extended_speed);
	} else {
		DP_VERBOSE(p_hwfn, NETIF_MSG_LINK, "Resetting link\n");
	}

	memset(&mb_params, 0, sizeof(mb_params));
	mb_params.cmd = cmd;
	mb_params.p_data_src = &phy_cfg;
	mb_params.data_src_size = sizeof(phy_cfg);
	rc = qed_mcp_cmd_and_union(p_hwfn, p_ptt, &mb_params);

	/* if mcp fails to respond we must abort */
	if (rc) {
		DP_ERR(p_hwfn, "MCP response failure, aborting\n");
		return rc;
	}

	/* Mimic link-change attention, done for several reasons:
	 *  - On reset, there's no guarantee MFW would trigger
	 *    an attention.
	 *  - On initialization, older MFWs might not indicate link change
	 *    during LFA, so we'll never get an UP indication.
	 */
	qed_mcp_handle_link_change(p_hwfn, p_ptt, !b_up);

	return 0;
}

u32 qed_get_process_kill_counter(struct qed_hwfn *p_hwfn,
				 struct qed_ptt *p_ptt)
{
	u32 path_offsize_addr, path_offsize, path_addr, proc_kill_cnt;

	if (IS_VF(p_hwfn->cdev))
		return -EINVAL;

	path_offsize_addr = SECTION_OFFSIZE_ADDR(p_hwfn->mcp_info->public_base,
						 PUBLIC_PATH);
	path_offsize = qed_rd(p_hwfn, p_ptt, path_offsize_addr);
	path_addr = SECTION_ADDR(path_offsize, QED_PATH_ID(p_hwfn));

	proc_kill_cnt = qed_rd(p_hwfn, p_ptt,
			       path_addr +
			       offsetof(struct public_path, process_kill)) &
			PROCESS_KILL_COUNTER_MASK;

	return proc_kill_cnt;
}

static void qed_mcp_handle_process_kill(struct qed_hwfn *p_hwfn,
					struct qed_ptt *p_ptt)
{
	struct qed_dev *cdev = p_hwfn->cdev;
	u32 proc_kill_cnt;

	/* Prevent possible attentions/interrupts during the recovery handling
	 * and till its load phase, during which they will be re-enabled.
	 */
	qed_int_igu_disable_int(p_hwfn, p_ptt);

	DP_NOTICE(p_hwfn, "Received a process kill indication\n");

	/* The following operations should be done once, and thus in CMT mode
	 * are carried out by only the first HW function.
	 */
	if (p_hwfn != QED_LEADING_HWFN(cdev))
		return;

	if (cdev->recov_in_prog) {
		DP_NOTICE(p_hwfn,
			  "Ignoring the indication since a recovery process is already in progress\n");
		return;
	}

	cdev->recov_in_prog = true;

	proc_kill_cnt = qed_get_process_kill_counter(p_hwfn, p_ptt);
	DP_NOTICE(p_hwfn, "Process kill counter: %d\n", proc_kill_cnt);

	qed_schedule_recovery_handler(p_hwfn);
}

static void qed_mcp_send_protocol_stats(struct qed_hwfn *p_hwfn,
					struct qed_ptt *p_ptt,
					enum MFW_DRV_MSG_TYPE type)
{
	enum qed_mcp_protocol_type stats_type;
	union qed_mcp_protocol_stats stats;
	struct qed_mcp_mb_params mb_params;
	u32 hsi_param;

	switch (type) {
	case MFW_DRV_MSG_GET_LAN_STATS:
		stats_type = QED_MCP_LAN_STATS;
		hsi_param = DRV_MSG_CODE_STATS_TYPE_LAN;
		break;
	case MFW_DRV_MSG_GET_FCOE_STATS:
		stats_type = QED_MCP_FCOE_STATS;
		hsi_param = DRV_MSG_CODE_STATS_TYPE_FCOE;
		break;
	case MFW_DRV_MSG_GET_ISCSI_STATS:
		stats_type = QED_MCP_ISCSI_STATS;
		hsi_param = DRV_MSG_CODE_STATS_TYPE_ISCSI;
		break;
	case MFW_DRV_MSG_GET_RDMA_STATS:
		stats_type = QED_MCP_RDMA_STATS;
		hsi_param = DRV_MSG_CODE_STATS_TYPE_RDMA;
		break;
	default:
		DP_NOTICE(p_hwfn, "Invalid protocol type %d\n", type);
		return;
	}

	qed_get_protocol_stats(p_hwfn->cdev, stats_type, &stats);

	memset(&mb_params, 0, sizeof(mb_params));
	mb_params.cmd = DRV_MSG_CODE_GET_STATS;
	mb_params.param = hsi_param;
	mb_params.p_data_src = &stats;
	mb_params.data_src_size = sizeof(stats);
	qed_mcp_cmd_and_union(p_hwfn, p_ptt, &mb_params);
}

static void qed_mcp_update_bw(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt)
{
	struct qed_mcp_function_info *p_info;
	struct public_func shmem_info;
	u32 resp = 0, param = 0;

	qed_mcp_get_shmem_func(p_hwfn, p_ptt, &shmem_info, MCP_PF_ID(p_hwfn));

	qed_read_pf_bandwidth(p_hwfn, &shmem_info);

	p_info = &p_hwfn->mcp_info->func_info;

	qed_configure_pf_min_bandwidth(p_hwfn->cdev, p_info->bandwidth_min);
	qed_configure_pf_max_bandwidth(p_hwfn->cdev, p_info->bandwidth_max);

	/* Acknowledge the MFW */
	qed_mcp_cmd(p_hwfn, p_ptt, DRV_MSG_CODE_BW_UPDATE_ACK, 0, &resp,
		    &param);
}

static void qed_mcp_update_stag(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt)
{
	struct public_func shmem_info;
	u32 resp = 0, param = 0;

	qed_mcp_get_shmem_func(p_hwfn, p_ptt, &shmem_info, MCP_PF_ID(p_hwfn));

	p_hwfn->mcp_info->func_info.ovlan = (u16)shmem_info.ovlan_stag &
						 FUNC_MF_CFG_OV_STAG_MASK;
	p_hwfn->hw_info.ovlan = p_hwfn->mcp_info->func_info.ovlan;
	if (test_bit(QED_MF_OVLAN_CLSS, &p_hwfn->cdev->mf_bits)) {
		if (p_hwfn->hw_info.ovlan != QED_MCP_VLAN_UNSET) {
			qed_wr(p_hwfn, p_ptt, NIG_REG_LLH_FUNC_TAG_VALUE,
			       p_hwfn->hw_info.ovlan);
			qed_wr(p_hwfn, p_ptt, NIG_REG_LLH_FUNC_TAG_EN, 1);

			/* Configure DB to add external vlan to EDPM packets */
			qed_wr(p_hwfn, p_ptt, DORQ_REG_TAG1_OVRD_MODE, 1);
			qed_wr(p_hwfn, p_ptt, DORQ_REG_PF_EXT_VID_BB_K2,
			       p_hwfn->hw_info.ovlan);
		} else {
			qed_wr(p_hwfn, p_ptt, NIG_REG_LLH_FUNC_TAG_EN, 0);
			qed_wr(p_hwfn, p_ptt, NIG_REG_LLH_FUNC_TAG_VALUE, 0);
			qed_wr(p_hwfn, p_ptt, DORQ_REG_TAG1_OVRD_MODE, 0);
			qed_wr(p_hwfn, p_ptt, DORQ_REG_PF_EXT_VID_BB_K2, 0);
		}

		qed_sp_pf_update_stag(p_hwfn);
	}

	DP_VERBOSE(p_hwfn, QED_MSG_SP, "ovlan = %d hw_mode = 0x%x\n",
		   p_hwfn->mcp_info->func_info.ovlan, p_hwfn->hw_info.hw_mode);

	/* Acknowledge the MFW */
	qed_mcp_cmd(p_hwfn, p_ptt, DRV_MSG_CODE_S_TAG_UPDATE_ACK, 0,
		    &resp, &param);
}

static void qed_mcp_handle_fan_failure(struct qed_hwfn *p_hwfn,
				       struct qed_ptt *p_ptt)
{
	/* A single notification should be sent to upper driver in CMT mode */
	if (p_hwfn != QED_LEADING_HWFN(p_hwfn->cdev))
		return;

	qed_hw_err_notify(p_hwfn, p_ptt, QED_HW_ERR_FAN_FAIL,
			  "Fan failure was detected on the network interface card and it's going to be shut down.\n");
}

struct qed_mdump_cmd_params {
	u32 cmd;
	void *p_data_src;
	u8 data_src_size;
	void *p_data_dst;
	u8 data_dst_size;
	u32 mcp_resp;
};

static int
qed_mcp_mdump_cmd(struct qed_hwfn *p_hwfn,
		  struct qed_ptt *p_ptt,
		  struct qed_mdump_cmd_params *p_mdump_cmd_params)
{
	struct qed_mcp_mb_params mb_params;
	int rc;

	memset(&mb_params, 0, sizeof(mb_params));
	mb_params.cmd = DRV_MSG_CODE_MDUMP_CMD;
	mb_params.param = p_mdump_cmd_params->cmd;
	mb_params.p_data_src = p_mdump_cmd_params->p_data_src;
	mb_params.data_src_size = p_mdump_cmd_params->data_src_size;
	mb_params.p_data_dst = p_mdump_cmd_params->p_data_dst;
	mb_params.data_dst_size = p_mdump_cmd_params->data_dst_size;
	rc = qed_mcp_cmd_and_union(p_hwfn, p_ptt, &mb_params);
	if (rc)
		return rc;

	p_mdump_cmd_params->mcp_resp = mb_params.mcp_resp;

	if (p_mdump_cmd_params->mcp_resp == FW_MSG_CODE_MDUMP_INVALID_CMD) {
		DP_INFO(p_hwfn,
			"The mdump sub command is unsupported by the MFW [mdump_cmd 0x%x]\n",
			p_mdump_cmd_params->cmd);
		rc = -EOPNOTSUPP;
	} else if (p_mdump_cmd_params->mcp_resp == FW_MSG_CODE_UNSUPPORTED) {
		DP_INFO(p_hwfn,
			"The mdump command is not supported by the MFW\n");
		rc = -EOPNOTSUPP;
	}

	return rc;
}

static int qed_mcp_mdump_ack(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt)
{
	struct qed_mdump_cmd_params mdump_cmd_params;

	memset(&mdump_cmd_params, 0, sizeof(mdump_cmd_params));
	mdump_cmd_params.cmd = DRV_MSG_CODE_MDUMP_ACK;

	return qed_mcp_mdump_cmd(p_hwfn, p_ptt, &mdump_cmd_params);
}

int
qed_mcp_mdump_get_retain(struct qed_hwfn *p_hwfn,
			 struct qed_ptt *p_ptt,
			 struct mdump_retain_data_stc *p_mdump_retain)
{
	struct qed_mdump_cmd_params mdump_cmd_params;
	int rc;

	memset(&mdump_cmd_params, 0, sizeof(mdump_cmd_params));
	mdump_cmd_params.cmd = DRV_MSG_CODE_MDUMP_GET_RETAIN;
	mdump_cmd_params.p_data_dst = p_mdump_retain;
	mdump_cmd_params.data_dst_size = sizeof(*p_mdump_retain);

	rc = qed_mcp_mdump_cmd(p_hwfn, p_ptt, &mdump_cmd_params);
	if (rc)
		return rc;

	if (mdump_cmd_params.mcp_resp != FW_MSG_CODE_OK) {
		DP_INFO(p_hwfn,
			"Failed to get the mdump retained data [mcp_resp 0x%x]\n",
			mdump_cmd_params.mcp_resp);
		return -EINVAL;
	}

	return 0;
}

static void qed_mcp_handle_critical_error(struct qed_hwfn *p_hwfn,
					  struct qed_ptt *p_ptt)
{
	struct mdump_retain_data_stc mdump_retain;
	int rc;

	/* In CMT mode - no need for more than a single acknowledgment to the
	 * MFW, and no more than a single notification to the upper driver.
	 */
	if (p_hwfn != QED_LEADING_HWFN(p_hwfn->cdev))
		return;

	rc = qed_mcp_mdump_get_retain(p_hwfn, p_ptt, &mdump_retain);
	if (rc == 0 && mdump_retain.valid)
		DP_NOTICE(p_hwfn,
			  "The MFW notified that a critical error occurred in the device [epoch 0x%08x, pf 0x%x, status 0x%08x]\n",
			  mdump_retain.epoch,
			  mdump_retain.pf, mdump_retain.status);
	else
		DP_NOTICE(p_hwfn,
			  "The MFW notified that a critical error occurred in the device\n");

	DP_NOTICE(p_hwfn,
		  "Acknowledging the notification to not allow the MFW crash dump [driver debug data collection is preferable]\n");
	qed_mcp_mdump_ack(p_hwfn, p_ptt);

	qed_hw_err_notify(p_hwfn, p_ptt, QED_HW_ERR_HW_ATTN, NULL);
}

void qed_mcp_read_ufp_config(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt)
{
	struct public_func shmem_info;
	u32 port_cfg, val;

	if (!test_bit(QED_MF_UFP_SPECIFIC, &p_hwfn->cdev->mf_bits))
		return;

	memset(&p_hwfn->ufp_info, 0, sizeof(p_hwfn->ufp_info));
	port_cfg = qed_rd(p_hwfn, p_ptt, p_hwfn->mcp_info->port_addr +
			  offsetof(struct public_port, oem_cfg_port));
	val = (port_cfg & OEM_CFG_CHANNEL_TYPE_MASK) >>
		OEM_CFG_CHANNEL_TYPE_OFFSET;
	if (val != OEM_CFG_CHANNEL_TYPE_STAGGED)
		DP_NOTICE(p_hwfn,
			  "Incorrect UFP Channel type  %d port_id 0x%02x\n",
			  val, MFW_PORT(p_hwfn));

	val = (port_cfg & OEM_CFG_SCHED_TYPE_MASK) >> OEM_CFG_SCHED_TYPE_OFFSET;
	if (val == OEM_CFG_SCHED_TYPE_ETS) {
		p_hwfn->ufp_info.mode = QED_UFP_MODE_ETS;
	} else if (val == OEM_CFG_SCHED_TYPE_VNIC_BW) {
		p_hwfn->ufp_info.mode = QED_UFP_MODE_VNIC_BW;
	} else {
		p_hwfn->ufp_info.mode = QED_UFP_MODE_UNKNOWN;
		DP_NOTICE(p_hwfn,
			  "Unknown UFP scheduling mode %d port_id 0x%02x\n",
			  val, MFW_PORT(p_hwfn));
	}

	qed_mcp_get_shmem_func(p_hwfn, p_ptt, &shmem_info, MCP_PF_ID(p_hwfn));
	val = (shmem_info.oem_cfg_func & OEM_CFG_FUNC_TC_MASK) >>
		OEM_CFG_FUNC_TC_OFFSET;
	p_hwfn->ufp_info.tc = (u8)val;
	val = (shmem_info.oem_cfg_func & OEM_CFG_FUNC_HOST_PRI_CTRL_MASK) >>
		OEM_CFG_FUNC_HOST_PRI_CTRL_OFFSET;
	if (val == OEM_CFG_FUNC_HOST_PRI_CTRL_VNIC) {
		p_hwfn->ufp_info.pri_type = QED_UFP_PRI_VNIC;
	} else if (val == OEM_CFG_FUNC_HOST_PRI_CTRL_OS) {
		p_hwfn->ufp_info.pri_type = QED_UFP_PRI_OS;
	} else {
		p_hwfn->ufp_info.pri_type = QED_UFP_PRI_UNKNOWN;
		DP_NOTICE(p_hwfn,
			  "Unknown Host priority control %d port_id 0x%02x\n",
			  val, MFW_PORT(p_hwfn));
	}

	DP_NOTICE(p_hwfn,
		  "UFP shmem config: mode = %d tc = %d pri_type = %d port_id 0x%02x\n",
		  p_hwfn->ufp_info.mode, p_hwfn->ufp_info.tc,
		  p_hwfn->ufp_info.pri_type, MFW_PORT(p_hwfn));
}

static int
qed_mcp_handle_ufp_event(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt)
{
	qed_mcp_read_ufp_config(p_hwfn, p_ptt);

	if (p_hwfn->ufp_info.mode == QED_UFP_MODE_VNIC_BW) {
		p_hwfn->qm_info.ooo_tc = p_hwfn->ufp_info.tc;
		qed_hw_info_set_offload_tc(&p_hwfn->hw_info,
					   p_hwfn->ufp_info.tc);

		qed_qm_reconf(p_hwfn, p_ptt);
	} else if (p_hwfn->ufp_info.mode == QED_UFP_MODE_ETS) {
		/* Merge UFP TC with the dcbx TC data */
		qed_dcbx_mib_update_event(p_hwfn, p_ptt,
					  QED_DCBX_OPERATIONAL_MIB);
	} else {
		DP_ERR(p_hwfn, "Invalid sched type, discard the UFP config\n");
		return -EINVAL;
	}

	/* update storm FW with negotiation results */
	qed_sp_pf_update_ufp(p_hwfn);

	/* update stag pcp value */
	qed_sp_pf_update_stag(p_hwfn);

	return 0;
}

int qed_mcp_handle_events(struct qed_hwfn *p_hwfn,
			  struct qed_ptt *p_ptt)
{
	struct qed_mcp_info *info = p_hwfn->mcp_info;
	int rc = 0;
	bool found = false;
	u16 i;

	DP_VERBOSE(p_hwfn, QED_MSG_SP, "Received message from MFW\n");

	/* Read Messages from MFW */
	qed_mcp_read_mb(p_hwfn, p_ptt);

	/* Compare current messages to old ones */
	for (i = 0; i < info->mfw_mb_length; i++) {
		if (info->mfw_mb_cur[i] == info->mfw_mb_shadow[i])
			continue;

		found = true;

		DP_VERBOSE(p_hwfn, NETIF_MSG_LINK,
			   "Msg [%d] - old CMD 0x%02x, new CMD 0x%02x\n",
			   i, info->mfw_mb_shadow[i], info->mfw_mb_cur[i]);

		switch (i) {
		case MFW_DRV_MSG_LINK_CHANGE:
			qed_mcp_handle_link_change(p_hwfn, p_ptt, false);
			break;
		case MFW_DRV_MSG_VF_DISABLED:
			qed_mcp_handle_vf_flr(p_hwfn, p_ptt);
			break;
		case MFW_DRV_MSG_LLDP_DATA_UPDATED:
			qed_dcbx_mib_update_event(p_hwfn, p_ptt,
						  QED_DCBX_REMOTE_LLDP_MIB);
			break;
		case MFW_DRV_MSG_DCBX_REMOTE_MIB_UPDATED:
			qed_dcbx_mib_update_event(p_hwfn, p_ptt,
						  QED_DCBX_REMOTE_MIB);
			break;
		case MFW_DRV_MSG_DCBX_OPERATIONAL_MIB_UPDATED:
			qed_dcbx_mib_update_event(p_hwfn, p_ptt,
						  QED_DCBX_OPERATIONAL_MIB);
			break;
		case MFW_DRV_MSG_OEM_CFG_UPDATE:
			qed_mcp_handle_ufp_event(p_hwfn, p_ptt);
			break;
		case MFW_DRV_MSG_TRANSCEIVER_STATE_CHANGE:
			qed_mcp_handle_transceiver_change(p_hwfn, p_ptt);
			break;
		case MFW_DRV_MSG_ERROR_RECOVERY:
			qed_mcp_handle_process_kill(p_hwfn, p_ptt);
			break;
		case MFW_DRV_MSG_GET_LAN_STATS:
		case MFW_DRV_MSG_GET_FCOE_STATS:
		case MFW_DRV_MSG_GET_ISCSI_STATS:
		case MFW_DRV_MSG_GET_RDMA_STATS:
			qed_mcp_send_protocol_stats(p_hwfn, p_ptt, i);
			break;
		case MFW_DRV_MSG_BW_UPDATE:
			qed_mcp_update_bw(p_hwfn, p_ptt);
			break;
		case MFW_DRV_MSG_S_TAG_UPDATE:
			qed_mcp_update_stag(p_hwfn, p_ptt);
			break;
		case MFW_DRV_MSG_FAILURE_DETECTED:
			qed_mcp_handle_fan_failure(p_hwfn, p_ptt);
			break;
		case MFW_DRV_MSG_CRITICAL_ERROR_OCCURRED:
			qed_mcp_handle_critical_error(p_hwfn, p_ptt);
			break;
		case MFW_DRV_MSG_GET_TLV_REQ:
			qed_mfw_tlv_req(p_hwfn);
			break;
		default:
			DP_INFO(p_hwfn, "Unimplemented MFW message %d\n", i);
			rc = -EINVAL;
		}
	}

	/* ACK everything */
	for (i = 0; i < MFW_DRV_MSG_MAX_DWORDS(info->mfw_mb_length); i++) {
		__be32 val = cpu_to_be32(((u32 *)info->mfw_mb_cur)[i]);

		/* MFW expect answer in BE, so we force write in that format */
		qed_wr(p_hwfn, p_ptt,
		       info->mfw_mb_addr + sizeof(u32) +
		       MFW_DRV_MSG_MAX_DWORDS(info->mfw_mb_length) *
		       sizeof(u32) + i * sizeof(u32),
		       (__force u32)val);
	}

	if (!found) {
		DP_NOTICE(p_hwfn,
			  "Received an MFW message indication but no new message!\n");
		rc = -EINVAL;
	}

	/* Copy the new mfw messages into the shadow */
	memcpy(info->mfw_mb_shadow, info->mfw_mb_cur, info->mfw_mb_length);

	return rc;
}

int qed_mcp_get_mfw_ver(struct qed_hwfn *p_hwfn,
			struct qed_ptt *p_ptt,
			u32 *p_mfw_ver, u32 *p_running_bundle_id)
{
	u32 global_offsize;

	if (IS_VF(p_hwfn->cdev)) {
		if (p_hwfn->vf_iov_info) {
			struct pfvf_acquire_resp_tlv *p_resp;

			p_resp = &p_hwfn->vf_iov_info->acquire_resp;
			*p_mfw_ver = p_resp->pfdev_info.mfw_ver;
			return 0;
		} else {
			DP_VERBOSE(p_hwfn,
				   QED_MSG_IOV,
				   "VF requested MFW version prior to ACQUIRE\n");
			return -EINVAL;
		}
	}

	global_offsize = qed_rd(p_hwfn, p_ptt,
				SECTION_OFFSIZE_ADDR(p_hwfn->
						     mcp_info->public_base,
						     PUBLIC_GLOBAL));
	*p_mfw_ver =
	    qed_rd(p_hwfn, p_ptt,
		   SECTION_ADDR(global_offsize,
				0) + offsetof(struct public_global, mfw_ver));

	if (p_running_bundle_id != NULL) {
		*p_running_bundle_id = qed_rd(p_hwfn, p_ptt,
					      SECTION_ADDR(global_offsize, 0) +
					      offsetof(struct public_global,
						       running_bundle_id));
	}

	return 0;
}

int qed_mcp_get_mbi_ver(struct qed_hwfn *p_hwfn,
			struct qed_ptt *p_ptt, u32 *p_mbi_ver)
{
	u32 nvm_cfg_addr, nvm_cfg1_offset, mbi_ver_addr;

	if (IS_VF(p_hwfn->cdev))
		return -EINVAL;

	/* Read the address of the nvm_cfg */
	nvm_cfg_addr = qed_rd(p_hwfn, p_ptt, MISC_REG_GEN_PURP_CR0);
	if (!nvm_cfg_addr) {
		DP_NOTICE(p_hwfn, "Shared memory not initialized\n");
		return -EINVAL;
	}

	/* Read the offset of nvm_cfg1 */
	nvm_cfg1_offset = qed_rd(p_hwfn, p_ptt, nvm_cfg_addr + 4);

	mbi_ver_addr = MCP_REG_SCRATCH + nvm_cfg1_offset +
		       offsetof(struct nvm_cfg1, glob) +
		       offsetof(struct nvm_cfg1_glob, mbi_version);
	*p_mbi_ver = qed_rd(p_hwfn, p_ptt,
			    mbi_ver_addr) &
		     (NVM_CFG1_GLOB_MBI_VERSION_0_MASK |
		      NVM_CFG1_GLOB_MBI_VERSION_1_MASK |
		      NVM_CFG1_GLOB_MBI_VERSION_2_MASK);

	return 0;
}

int qed_mcp_get_media_type(struct qed_hwfn *p_hwfn,
			   struct qed_ptt *p_ptt, u32 *p_media_type)
{
	*p_media_type = MEDIA_UNSPECIFIED;

	if (IS_VF(p_hwfn->cdev))
		return -EINVAL;

	if (!qed_mcp_is_init(p_hwfn)) {
		DP_NOTICE(p_hwfn, "MFW is not initialized!\n");
		return -EBUSY;
	}

	if (!p_ptt) {
		*p_media_type = MEDIA_UNSPECIFIED;
		return -EINVAL;
	}

	*p_media_type = qed_rd(p_hwfn, p_ptt,
			       p_hwfn->mcp_info->port_addr +
			       offsetof(struct public_port,
					media_type));

	return 0;
}

int qed_mcp_get_transceiver_data(struct qed_hwfn *p_hwfn,
				 struct qed_ptt *p_ptt,
				 u32 *p_transceiver_state,
				 u32 *p_transceiver_type)
{
	u32 transceiver_info;

	*p_transceiver_type = ETH_TRANSCEIVER_TYPE_NONE;
	*p_transceiver_state = ETH_TRANSCEIVER_STATE_UPDATING;

	if (IS_VF(p_hwfn->cdev))
		return -EINVAL;

	if (!qed_mcp_is_init(p_hwfn)) {
		DP_NOTICE(p_hwfn, "MFW is not initialized!\n");
		return -EBUSY;
	}

	transceiver_info = qed_rd(p_hwfn, p_ptt,
				  p_hwfn->mcp_info->port_addr +
				  offsetof(struct public_port,
					   transceiver_data));

	*p_transceiver_state = (transceiver_info &
				ETH_TRANSCEIVER_STATE_MASK) >>
				ETH_TRANSCEIVER_STATE_OFFSET;

	if (*p_transceiver_state == ETH_TRANSCEIVER_STATE_PRESENT)
		*p_transceiver_type = (transceiver_info &
				       ETH_TRANSCEIVER_TYPE_MASK) >>
				       ETH_TRANSCEIVER_TYPE_OFFSET;
	else
		*p_transceiver_type = ETH_TRANSCEIVER_TYPE_UNKNOWN;

	return 0;
}
static bool qed_is_transceiver_ready(u32 transceiver_state,
				     u32 transceiver_type)
{
	if ((transceiver_state & ETH_TRANSCEIVER_STATE_PRESENT) &&
	    ((transceiver_state & ETH_TRANSCEIVER_STATE_UPDATING) == 0x0) &&
	    (transceiver_type != ETH_TRANSCEIVER_TYPE_NONE))
		return true;

	return false;
}

int qed_mcp_trans_speed_mask(struct qed_hwfn *p_hwfn,
			     struct qed_ptt *p_ptt, u32 *p_speed_mask)
{
	u32 transceiver_type, transceiver_state;
	int ret;

	ret = qed_mcp_get_transceiver_data(p_hwfn, p_ptt, &transceiver_state,
					   &transceiver_type);
	if (ret)
		return ret;

	if (qed_is_transceiver_ready(transceiver_state, transceiver_type) ==
				     false)
		return -EINVAL;

	switch (transceiver_type) {
	case ETH_TRANSCEIVER_TYPE_1G_LX:
	case ETH_TRANSCEIVER_TYPE_1G_SX:
	case ETH_TRANSCEIVER_TYPE_1G_PCC:
	case ETH_TRANSCEIVER_TYPE_1G_ACC:
	case ETH_TRANSCEIVER_TYPE_1000BASET:
		*p_speed_mask = NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_1G;
		break;
	case ETH_TRANSCEIVER_TYPE_10G_SR:
	case ETH_TRANSCEIVER_TYPE_10G_LR:
	case ETH_TRANSCEIVER_TYPE_10G_LRM:
	case ETH_TRANSCEIVER_TYPE_10G_ER:
	case ETH_TRANSCEIVER_TYPE_10G_PCC:
	case ETH_TRANSCEIVER_TYPE_10G_ACC:
	case ETH_TRANSCEIVER_TYPE_4x10G:
		*p_speed_mask = NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_10G;
		break;
	case ETH_TRANSCEIVER_TYPE_40G_LR4:
	case ETH_TRANSCEIVER_TYPE_40G_SR4:
	case ETH_TRANSCEIVER_TYPE_MULTI_RATE_10G_40G_SR:
	case ETH_TRANSCEIVER_TYPE_MULTI_RATE_10G_40G_LR:
		*p_speed_mask = NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_40G |
		    NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_10G;
		break;
	case ETH_TRANSCEIVER_TYPE_100G_AOC:
	case ETH_TRANSCEIVER_TYPE_100G_SR4:
	case ETH_TRANSCEIVER_TYPE_100G_LR4:
	case ETH_TRANSCEIVER_TYPE_100G_ER4:
	case ETH_TRANSCEIVER_TYPE_100G_ACC:
		*p_speed_mask =
		    NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_BB_100G |
		    NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_25G;
		break;
	case ETH_TRANSCEIVER_TYPE_25G_SR:
	case ETH_TRANSCEIVER_TYPE_25G_LR:
	case ETH_TRANSCEIVER_TYPE_25G_AOC:
	case ETH_TRANSCEIVER_TYPE_25G_ACC_S:
	case ETH_TRANSCEIVER_TYPE_25G_ACC_M:
	case ETH_TRANSCEIVER_TYPE_25G_ACC_L:
		*p_speed_mask = NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_25G;
		break;
	case ETH_TRANSCEIVER_TYPE_25G_CA_N:
	case ETH_TRANSCEIVER_TYPE_25G_CA_S:
	case ETH_TRANSCEIVER_TYPE_25G_CA_L:
	case ETH_TRANSCEIVER_TYPE_4x25G_CR:
		*p_speed_mask = NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_25G |
		    NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_10G |
		    NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_1G;
		break;
	case ETH_TRANSCEIVER_TYPE_MULTI_RATE_10G_25G_SR:
	case ETH_TRANSCEIVER_TYPE_MULTI_RATE_10G_25G_LR:
		*p_speed_mask = NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_25G |
				NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_10G;
		break;
	case ETH_TRANSCEIVER_TYPE_40G_CR4:
	case ETH_TRANSCEIVER_TYPE_MULTI_RATE_10G_40G_CR:
		*p_speed_mask = NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_40G |
		    NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_10G |
		    NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_1G;
		break;
	case ETH_TRANSCEIVER_TYPE_100G_CR4:
	case ETH_TRANSCEIVER_TYPE_MULTI_RATE_40G_100G_CR:
		*p_speed_mask =
		    NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_BB_100G |
		    NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_50G |
		    NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_40G |
		    NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_25G |
		    NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_20G |
		    NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_10G |
		    NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_1G;
		break;
	case ETH_TRANSCEIVER_TYPE_MULTI_RATE_40G_100G_SR:
	case ETH_TRANSCEIVER_TYPE_MULTI_RATE_40G_100G_LR:
	case ETH_TRANSCEIVER_TYPE_MULTI_RATE_40G_100G_AOC:
		*p_speed_mask =
		    NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_BB_100G |
		    NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_40G |
		    NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_25G |
		    NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_10G;
		break;
	case ETH_TRANSCEIVER_TYPE_XLPPI:
		*p_speed_mask = NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_40G;
		break;
	case ETH_TRANSCEIVER_TYPE_10G_BASET:
	case ETH_TRANSCEIVER_TYPE_MULTI_RATE_1G_10G_SR:
	case ETH_TRANSCEIVER_TYPE_MULTI_RATE_1G_10G_LR:
		*p_speed_mask = NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_10G |
				NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_1G;
		break;
	default:
		DP_INFO(p_hwfn, "Unknown transceiver type 0x%x\n",
			transceiver_type);
		*p_speed_mask = 0xff;
		break;
	}

	return 0;
}

int qed_mcp_get_board_config(struct qed_hwfn *p_hwfn,
			     struct qed_ptt *p_ptt, u32 *p_board_config)
{
	u32 nvm_cfg_addr, nvm_cfg1_offset, port_cfg_addr;

	if (IS_VF(p_hwfn->cdev))
		return -EINVAL;

	if (!qed_mcp_is_init(p_hwfn)) {
		DP_NOTICE(p_hwfn, "MFW is not initialized!\n");
		return -EBUSY;
	}
	if (!p_ptt) {
		*p_board_config = NVM_CFG1_PORT_PORT_TYPE_UNDEFINED;
		return -EINVAL;
	}

	nvm_cfg_addr = qed_rd(p_hwfn, p_ptt, MISC_REG_GEN_PURP_CR0);
	nvm_cfg1_offset = qed_rd(p_hwfn, p_ptt, nvm_cfg_addr + 4);
	port_cfg_addr = MCP_REG_SCRATCH + nvm_cfg1_offset +
			offsetof(struct nvm_cfg1, port[MFW_PORT(p_hwfn)]);
	*p_board_config = qed_rd(p_hwfn, p_ptt,
				 port_cfg_addr +
				 offsetof(struct nvm_cfg1_port,
					  board_cfg));

	return 0;
}

/* Old MFW has a global configuration for all PFs regarding RDMA support */
static void
qed_mcp_get_shmem_proto_legacy(struct qed_hwfn *p_hwfn,
			       enum qed_pci_personality *p_proto)
{
	/* There wasn't ever a legacy MFW that published iwarp.
	 * So at this point, this is either plain l2 or RoCE.
	 */
	if (test_bit(QED_DEV_CAP_ROCE, &p_hwfn->hw_info.device_capabilities))
		*p_proto = QED_PCI_ETH_ROCE;
	else
		*p_proto = QED_PCI_ETH;

	DP_VERBOSE(p_hwfn, NETIF_MSG_IFUP,
		   "According to Legacy capabilities, L2 personality is %08x\n",
		   (u32) *p_proto);
}

static int
qed_mcp_get_shmem_proto_mfw(struct qed_hwfn *p_hwfn,
			    struct qed_ptt *p_ptt,
			    enum qed_pci_personality *p_proto)
{
	u32 resp = 0, param = 0;
	int rc;

	rc = qed_mcp_cmd(p_hwfn, p_ptt,
			 DRV_MSG_CODE_GET_PF_RDMA_PROTOCOL, 0, &resp, &param);
	if (rc)
		return rc;
	if (resp != FW_MSG_CODE_OK) {
		DP_VERBOSE(p_hwfn, NETIF_MSG_IFUP,
			   "MFW lacks support for command; Returns %08x\n",
			   resp);
		return -EINVAL;
	}

	switch (param) {
	case FW_MB_PARAM_GET_PF_RDMA_NONE:
		*p_proto = QED_PCI_ETH;
		break;
	case FW_MB_PARAM_GET_PF_RDMA_ROCE:
		*p_proto = QED_PCI_ETH_ROCE;
		break;
	case FW_MB_PARAM_GET_PF_RDMA_IWARP:
		*p_proto = QED_PCI_ETH_IWARP;
		break;
	case FW_MB_PARAM_GET_PF_RDMA_BOTH:
		*p_proto = QED_PCI_ETH_RDMA;
		break;
	default:
		DP_NOTICE(p_hwfn,
			  "MFW answers GET_PF_RDMA_PROTOCOL but param is %08x\n",
			  param);
		return -EINVAL;
	}

	DP_VERBOSE(p_hwfn,
		   NETIF_MSG_IFUP,
		   "According to capabilities, L2 personality is %08x [resp %08x param %08x]\n",
		   (u32) *p_proto, resp, param);
	return 0;
}

static int
qed_mcp_get_shmem_proto(struct qed_hwfn *p_hwfn,
			struct public_func *p_info,
			struct qed_ptt *p_ptt,
			enum qed_pci_personality *p_proto)
{
	int rc = 0;

	switch (p_info->config & FUNC_MF_CFG_PROTOCOL_MASK) {
	case FUNC_MF_CFG_PROTOCOL_ETHERNET:
		if (!IS_ENABLED(CONFIG_QED_RDMA))
			*p_proto = QED_PCI_ETH;
		else if (qed_mcp_get_shmem_proto_mfw(p_hwfn, p_ptt, p_proto))
			qed_mcp_get_shmem_proto_legacy(p_hwfn, p_proto);
		break;
	case FUNC_MF_CFG_PROTOCOL_ISCSI:
		*p_proto = QED_PCI_ISCSI;
		break;
	case FUNC_MF_CFG_PROTOCOL_NVMETCP:
		*p_proto = QED_PCI_NVMETCP;
		break;
	case FUNC_MF_CFG_PROTOCOL_FCOE:
		*p_proto = QED_PCI_FCOE;
		break;
	case FUNC_MF_CFG_PROTOCOL_ROCE:
		DP_NOTICE(p_hwfn, "RoCE personality is not a valid value!\n");
		fallthrough;
	default:
		rc = -EINVAL;
	}

	return rc;
}

int qed_mcp_fill_shmem_func_info(struct qed_hwfn *p_hwfn,
				 struct qed_ptt *p_ptt)
{
	struct qed_mcp_function_info *info;
	struct public_func shmem_info;

	qed_mcp_get_shmem_func(p_hwfn, p_ptt, &shmem_info, MCP_PF_ID(p_hwfn));
	info = &p_hwfn->mcp_info->func_info;

	info->pause_on_host = (shmem_info.config &
			       FUNC_MF_CFG_PAUSE_ON_HOST_RING) ? 1 : 0;

	if (qed_mcp_get_shmem_proto(p_hwfn, &shmem_info, p_ptt,
				    &info->protocol)) {
		DP_ERR(p_hwfn, "Unknown personality %08x\n",
		       (u32)(shmem_info.config & FUNC_MF_CFG_PROTOCOL_MASK));
		return -EINVAL;
	}

	qed_read_pf_bandwidth(p_hwfn, &shmem_info);

	if (shmem_info.mac_upper || shmem_info.mac_lower) {
		info->mac[0] = (u8)(shmem_info.mac_upper >> 8);
		info->mac[1] = (u8)(shmem_info.mac_upper);
		info->mac[2] = (u8)(shmem_info.mac_lower >> 24);
		info->mac[3] = (u8)(shmem_info.mac_lower >> 16);
		info->mac[4] = (u8)(shmem_info.mac_lower >> 8);
		info->mac[5] = (u8)(shmem_info.mac_lower);

		/* Store primary MAC for later possible WoL */
		memcpy(&p_hwfn->cdev->wol_mac, info->mac, ETH_ALEN);
	} else {
		DP_NOTICE(p_hwfn, "MAC is 0 in shmem\n");
	}

	info->wwn_port = (u64)shmem_info.fcoe_wwn_port_name_lower |
			 (((u64)shmem_info.fcoe_wwn_port_name_upper) << 32);
	info->wwn_node = (u64)shmem_info.fcoe_wwn_node_name_lower |
			 (((u64)shmem_info.fcoe_wwn_node_name_upper) << 32);

	info->ovlan = (u16)(shmem_info.ovlan_stag & FUNC_MF_CFG_OV_STAG_MASK);

	info->mtu = (u16)shmem_info.mtu_size;

	p_hwfn->hw_info.b_wol_support = QED_WOL_SUPPORT_NONE;
	p_hwfn->cdev->wol_config = (u8)QED_OV_WOL_DEFAULT;
	if (qed_mcp_is_init(p_hwfn)) {
		u32 resp = 0, param = 0;
		int rc;

		rc = qed_mcp_cmd(p_hwfn, p_ptt,
				 DRV_MSG_CODE_OS_WOL, 0, &resp, &param);
		if (rc)
			return rc;
		if (resp == FW_MSG_CODE_OS_WOL_SUPPORTED)
			p_hwfn->hw_info.b_wol_support = QED_WOL_SUPPORT_PME;
	}

	DP_VERBOSE(p_hwfn, (QED_MSG_SP | NETIF_MSG_IFUP),
		   "Read configuration from shmem: pause_on_host %02x protocol %02x BW [%02x - %02x] MAC %pM wwn port %llx node %llx ovlan %04x wol %02x\n",
		info->pause_on_host, info->protocol,
		info->bandwidth_min, info->bandwidth_max,
		info->mac,
		info->wwn_port, info->wwn_node,
		info->ovlan, (u8)p_hwfn->hw_info.b_wol_support);

	return 0;
}

struct qed_mcp_link_params
*qed_mcp_get_link_params(struct qed_hwfn *p_hwfn)
{
	if (!p_hwfn || !p_hwfn->mcp_info)
		return NULL;
	return &p_hwfn->mcp_info->link_input;
}

struct qed_mcp_link_state
*qed_mcp_get_link_state(struct qed_hwfn *p_hwfn)
{
	if (!p_hwfn || !p_hwfn->mcp_info)
		return NULL;
	return &p_hwfn->mcp_info->link_output;
}

struct qed_mcp_link_capabilities
*qed_mcp_get_link_capabilities(struct qed_hwfn *p_hwfn)
{
	if (!p_hwfn || !p_hwfn->mcp_info)
		return NULL;
	return &p_hwfn->mcp_info->link_capabilities;
}

int qed_mcp_drain(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt)
{
	u32 resp = 0, param = 0;
	int rc;

	rc = qed_mcp_cmd(p_hwfn, p_ptt,
			 DRV_MSG_CODE_NIG_DRAIN, 1000, &resp, &param);

	/* Wait for the drain to complete before returning */
	msleep(1020);

	return rc;
}

int qed_mcp_get_flash_size(struct qed_hwfn *p_hwfn,
			   struct qed_ptt *p_ptt, u32 *p_flash_size)
{
	u32 flash_size;

	if (IS_VF(p_hwfn->cdev))
		return -EINVAL;

	flash_size = qed_rd(p_hwfn, p_ptt, MCP_REG_NVM_CFG4);
	flash_size = (flash_size & MCP_REG_NVM_CFG4_FLASH_SIZE) >>
		      MCP_REG_NVM_CFG4_FLASH_SIZE_SHIFT;
	flash_size = (1 << (flash_size + MCP_BYTES_PER_MBIT_SHIFT));

	*p_flash_size = flash_size;

	return 0;
}

int qed_start_recovery_process(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt)
{
	struct qed_dev *cdev = p_hwfn->cdev;

	if (cdev->recov_in_prog) {
		DP_NOTICE(p_hwfn,
			  "Avoid triggering a recovery since such a process is already in progress\n");
		return -EAGAIN;
	}

	DP_NOTICE(p_hwfn, "Triggering a recovery process\n");
	qed_wr(p_hwfn, p_ptt, MISC_REG_AEU_GENERAL_ATTN_35, 0x1);

	return 0;
}

#define QED_RECOVERY_PROLOG_SLEEP_MS    100

int qed_recovery_prolog(struct qed_dev *cdev)
{
	struct qed_hwfn *p_hwfn = QED_LEADING_HWFN(cdev);
	struct qed_ptt *p_ptt = p_hwfn->p_main_ptt;
	int rc;

	/* Allow ongoing PCIe transactions to complete */
	msleep(QED_RECOVERY_PROLOG_SLEEP_MS);

	/* Clear the PF's internal FID_enable in the PXP */
	rc = qed_pglueb_set_pfid_enable(p_hwfn, p_ptt, false);
	if (rc)
		DP_NOTICE(p_hwfn,
			  "qed_pglueb_set_pfid_enable() failed. rc = %d.\n",
			  rc);

	return rc;
}

static int
qed_mcp_config_vf_msix_bb(struct qed_hwfn *p_hwfn,
			  struct qed_ptt *p_ptt, u8 vf_id, u8 num)
{
	u32 resp = 0, param = 0, rc_param = 0;
	int rc;

	/* Only Leader can configure MSIX, and need to take CMT into account */
	if (!IS_LEAD_HWFN(p_hwfn))
		return 0;
	num *= p_hwfn->cdev->num_hwfns;

	param |= (vf_id << DRV_MB_PARAM_CFG_VF_MSIX_VF_ID_SHIFT) &
		 DRV_MB_PARAM_CFG_VF_MSIX_VF_ID_MASK;
	param |= (num << DRV_MB_PARAM_CFG_VF_MSIX_SB_NUM_SHIFT) &
		 DRV_MB_PARAM_CFG_VF_MSIX_SB_NUM_MASK;

	rc = qed_mcp_cmd(p_hwfn, p_ptt, DRV_MSG_CODE_CFG_VF_MSIX, param,
			 &resp, &rc_param);

	if (resp != FW_MSG_CODE_DRV_CFG_VF_MSIX_DONE) {
		DP_NOTICE(p_hwfn, "VF[%d]: MFW failed to set MSI-X\n", vf_id);
		rc = -EINVAL;
	} else {
		DP_VERBOSE(p_hwfn, QED_MSG_IOV,
			   "Requested 0x%02x MSI-x interrupts from VF 0x%02x\n",
			   num, vf_id);
	}

	return rc;
}

static int
qed_mcp_config_vf_msix_ah(struct qed_hwfn *p_hwfn,
			  struct qed_ptt *p_ptt, u8 num)
{
	u32 resp = 0, param = num, rc_param = 0;
	int rc;

	rc = qed_mcp_cmd(p_hwfn, p_ptt, DRV_MSG_CODE_CFG_PF_VFS_MSIX,
			 param, &resp, &rc_param);

	if (resp != FW_MSG_CODE_DRV_CFG_PF_VFS_MSIX_DONE) {
		DP_NOTICE(p_hwfn, "MFW failed to set MSI-X for VFs\n");
		rc = -EINVAL;
	} else {
		DP_VERBOSE(p_hwfn, QED_MSG_IOV,
			   "Requested 0x%02x MSI-x interrupts for VFs\n", num);
	}

	return rc;
}

int qed_mcp_config_vf_msix(struct qed_hwfn *p_hwfn,
			   struct qed_ptt *p_ptt, u8 vf_id, u8 num)
{
	if (QED_IS_BB(p_hwfn->cdev))
		return qed_mcp_config_vf_msix_bb(p_hwfn, p_ptt, vf_id, num);
	else
		return qed_mcp_config_vf_msix_ah(p_hwfn, p_ptt, num);
}

int
qed_mcp_send_drv_version(struct qed_hwfn *p_hwfn,
			 struct qed_ptt *p_ptt,
			 struct qed_mcp_drv_version *p_ver)
{
	struct qed_mcp_mb_params mb_params;
	struct drv_version_stc drv_version;
	__be32 val;
	u32 i;
	int rc;

	memset(&drv_version, 0, sizeof(drv_version));
	drv_version.version = p_ver->version;
	for (i = 0; i < (MCP_DRV_VER_STR_SIZE - 4) / sizeof(u32); i++) {
		val = cpu_to_be32(*((u32 *)&p_ver->name[i * sizeof(u32)]));
		*(__be32 *)&drv_version.name[i * sizeof(u32)] = val;
	}

	memset(&mb_params, 0, sizeof(mb_params));
	mb_params.cmd = DRV_MSG_CODE_SET_VERSION;
	mb_params.p_data_src = &drv_version;
	mb_params.data_src_size = sizeof(drv_version);
	rc = qed_mcp_cmd_and_union(p_hwfn, p_ptt, &mb_params);
	if (rc)
		DP_ERR(p_hwfn, "MCP response failure, aborting\n");

	return rc;
}

/* A maximal 100 msec waiting time for the MCP to halt */
#define QED_MCP_HALT_SLEEP_MS		10
#define QED_MCP_HALT_MAX_RETRIES	10

int qed_mcp_halt(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt)
{
	u32 resp = 0, param = 0, cpu_state, cnt = 0;
	int rc;

	rc = qed_mcp_cmd(p_hwfn, p_ptt, DRV_MSG_CODE_MCP_HALT, 0, &resp,
			 &param);
	if (rc) {
		DP_ERR(p_hwfn, "MCP response failure, aborting\n");
		return rc;
	}

	do {
		msleep(QED_MCP_HALT_SLEEP_MS);
		cpu_state = qed_rd(p_hwfn, p_ptt, MCP_REG_CPU_STATE);
		if (cpu_state & MCP_REG_CPU_STATE_SOFT_HALTED)
			break;
	} while (++cnt < QED_MCP_HALT_MAX_RETRIES);

	if (cnt == QED_MCP_HALT_MAX_RETRIES) {
		DP_NOTICE(p_hwfn,
			  "Failed to halt the MCP [CPU_MODE = 0x%08x, CPU_STATE = 0x%08x]\n",
			  qed_rd(p_hwfn, p_ptt, MCP_REG_CPU_MODE), cpu_state);
		return -EBUSY;
	}

	qed_mcp_cmd_set_blocking(p_hwfn, true);

	return 0;
}

#define QED_MCP_RESUME_SLEEP_MS	10

int qed_mcp_resume(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt)
{
	u32 cpu_mode, cpu_state;

	qed_wr(p_hwfn, p_ptt, MCP_REG_CPU_STATE, 0xffffffff);

	cpu_mode = qed_rd(p_hwfn, p_ptt, MCP_REG_CPU_MODE);
	cpu_mode &= ~MCP_REG_CPU_MODE_SOFT_HALT;
	qed_wr(p_hwfn, p_ptt, MCP_REG_CPU_MODE, cpu_mode);
	msleep(QED_MCP_RESUME_SLEEP_MS);
	cpu_state = qed_rd(p_hwfn, p_ptt, MCP_REG_CPU_STATE);

	if (cpu_state & MCP_REG_CPU_STATE_SOFT_HALTED) {
		DP_NOTICE(p_hwfn,
			  "Failed to resume the MCP [CPU_MODE = 0x%08x, CPU_STATE = 0x%08x]\n",
			  cpu_mode, cpu_state);
		return -EBUSY;
	}

	qed_mcp_cmd_set_blocking(p_hwfn, false);

	return 0;
}

int qed_mcp_ov_update_current_config(struct qed_hwfn *p_hwfn,
				     struct qed_ptt *p_ptt,
				     enum qed_ov_client client)
{
	u32 resp = 0, param = 0;
	u32 drv_mb_param;
	int rc;

	switch (client) {
	case QED_OV_CLIENT_DRV:
		drv_mb_param = DRV_MB_PARAM_OV_CURR_CFG_OS;
		break;
	case QED_OV_CLIENT_USER:
		drv_mb_param = DRV_MB_PARAM_OV_CURR_CFG_OTHER;
		break;
	case QED_OV_CLIENT_VENDOR_SPEC:
		drv_mb_param = DRV_MB_PARAM_OV_CURR_CFG_VENDOR_SPEC;
		break;
	default:
		DP_NOTICE(p_hwfn, "Invalid client type %d\n", client);
		return -EINVAL;
	}

	rc = qed_mcp_cmd(p_hwfn, p_ptt, DRV_MSG_CODE_OV_UPDATE_CURR_CFG,
			 drv_mb_param, &resp, &param);
	if (rc)
		DP_ERR(p_hwfn, "MCP response failure, aborting\n");

	return rc;
}

int qed_mcp_ov_update_driver_state(struct qed_hwfn *p_hwfn,
				   struct qed_ptt *p_ptt,
				   enum qed_ov_driver_state drv_state)
{
	u32 resp = 0, param = 0;
	u32 drv_mb_param;
	int rc;

	switch (drv_state) {
	case QED_OV_DRIVER_STATE_NOT_LOADED:
		drv_mb_param = DRV_MSG_CODE_OV_UPDATE_DRIVER_STATE_NOT_LOADED;
		break;
	case QED_OV_DRIVER_STATE_DISABLED:
		drv_mb_param = DRV_MSG_CODE_OV_UPDATE_DRIVER_STATE_DISABLED;
		break;
	case QED_OV_DRIVER_STATE_ACTIVE:
		drv_mb_param = DRV_MSG_CODE_OV_UPDATE_DRIVER_STATE_ACTIVE;
		break;
	default:
		DP_NOTICE(p_hwfn, "Invalid driver state %d\n", drv_state);
		return -EINVAL;
	}

	rc = qed_mcp_cmd(p_hwfn, p_ptt, DRV_MSG_CODE_OV_UPDATE_DRIVER_STATE,
			 drv_mb_param, &resp, &param);
	if (rc)
		DP_ERR(p_hwfn, "Failed to send driver state\n");

	return rc;
}

int qed_mcp_ov_update_mtu(struct qed_hwfn *p_hwfn,
			  struct qed_ptt *p_ptt, u16 mtu)
{
	u32 resp = 0, param = 0;
	u32 drv_mb_param;
	int rc;

	drv_mb_param = (u32)mtu << DRV_MB_PARAM_OV_MTU_SIZE_SHIFT;
	rc = qed_mcp_cmd(p_hwfn, p_ptt, DRV_MSG_CODE_OV_UPDATE_MTU,
			 drv_mb_param, &resp, &param);
	if (rc)
		DP_ERR(p_hwfn, "Failed to send mtu value, rc = %d\n", rc);

	return rc;
}

int qed_mcp_ov_update_mac(struct qed_hwfn *p_hwfn,
			  struct qed_ptt *p_ptt, u8 *mac)
{
	struct qed_mcp_mb_params mb_params;
	u32 mfw_mac[2];
	int rc;

	memset(&mb_params, 0, sizeof(mb_params));
	mb_params.cmd = DRV_MSG_CODE_SET_VMAC;
	mb_params.param = DRV_MSG_CODE_VMAC_TYPE_MAC <<
			  DRV_MSG_CODE_VMAC_TYPE_SHIFT;
	mb_params.param |= MCP_PF_ID(p_hwfn);

	/* MCP is BE, and on LE platforms PCI would swap access to SHMEM
	 * in 32-bit granularity.
	 * So the MAC has to be set in native order [and not byte order],
	 * otherwise it would be read incorrectly by MFW after swap.
	 */
	mfw_mac[0] = mac[0] << 24 | mac[1] << 16 | mac[2] << 8 | mac[3];
	mfw_mac[1] = mac[4] << 24 | mac[5] << 16;

	mb_params.p_data_src = (u8 *)mfw_mac;
	mb_params.data_src_size = 8;
	rc = qed_mcp_cmd_and_union(p_hwfn, p_ptt, &mb_params);
	if (rc)
		DP_ERR(p_hwfn, "Failed to send mac address, rc = %d\n", rc);

	/* Store primary MAC for later possible WoL */
	memcpy(p_hwfn->cdev->wol_mac, mac, ETH_ALEN);

	return rc;
}

int qed_mcp_ov_update_wol(struct qed_hwfn *p_hwfn,
			  struct qed_ptt *p_ptt, enum qed_ov_wol wol)
{
	u32 resp = 0, param = 0;
	u32 drv_mb_param;
	int rc;

	if (p_hwfn->hw_info.b_wol_support == QED_WOL_SUPPORT_NONE) {
		DP_VERBOSE(p_hwfn, QED_MSG_SP,
			   "Can't change WoL configuration when WoL isn't supported\n");
		return -EINVAL;
	}

	switch (wol) {
	case QED_OV_WOL_DEFAULT:
		drv_mb_param = DRV_MB_PARAM_WOL_DEFAULT;
		break;
	case QED_OV_WOL_DISABLED:
		drv_mb_param = DRV_MB_PARAM_WOL_DISABLED;
		break;
	case QED_OV_WOL_ENABLED:
		drv_mb_param = DRV_MB_PARAM_WOL_ENABLED;
		break;
	default:
		DP_ERR(p_hwfn, "Invalid wol state %d\n", wol);
		return -EINVAL;
	}

	rc = qed_mcp_cmd(p_hwfn, p_ptt, DRV_MSG_CODE_OV_UPDATE_WOL,
			 drv_mb_param, &resp, &param);
	if (rc)
		DP_ERR(p_hwfn, "Failed to send wol mode, rc = %d\n", rc);

	/* Store the WoL update for a future unload */
	p_hwfn->cdev->wol_config = (u8)wol;

	return rc;
}

int qed_mcp_ov_update_eswitch(struct qed_hwfn *p_hwfn,
			      struct qed_ptt *p_ptt,
			      enum qed_ov_eswitch eswitch)
{
	u32 resp = 0, param = 0;
	u32 drv_mb_param;
	int rc;

	switch (eswitch) {
	case QED_OV_ESWITCH_NONE:
		drv_mb_param = DRV_MB_PARAM_ESWITCH_MODE_NONE;
		break;
	case QED_OV_ESWITCH_VEB:
		drv_mb_param = DRV_MB_PARAM_ESWITCH_MODE_VEB;
		break;
	case QED_OV_ESWITCH_VEPA:
		drv_mb_param = DRV_MB_PARAM_ESWITCH_MODE_VEPA;
		break;
	default:
		DP_ERR(p_hwfn, "Invalid eswitch mode %d\n", eswitch);
		return -EINVAL;
	}

	rc = qed_mcp_cmd(p_hwfn, p_ptt, DRV_MSG_CODE_OV_UPDATE_ESWITCH_MODE,
			 drv_mb_param, &resp, &param);
	if (rc)
		DP_ERR(p_hwfn, "Failed to send eswitch mode, rc = %d\n", rc);

	return rc;
}

int qed_mcp_set_led(struct qed_hwfn *p_hwfn,
		    struct qed_ptt *p_ptt, enum qed_led_mode mode)
{
	u32 resp = 0, param = 0, drv_mb_param;
	int rc;

	switch (mode) {
	case QED_LED_MODE_ON:
		drv_mb_param = DRV_MB_PARAM_SET_LED_MODE_ON;
		break;
	case QED_LED_MODE_OFF:
		drv_mb_param = DRV_MB_PARAM_SET_LED_MODE_OFF;
		break;
	case QED_LED_MODE_RESTORE:
		drv_mb_param = DRV_MB_PARAM_SET_LED_MODE_OPER;
		break;
	default:
		DP_NOTICE(p_hwfn, "Invalid LED mode %d\n", mode);
		return -EINVAL;
	}

	rc = qed_mcp_cmd(p_hwfn, p_ptt, DRV_MSG_CODE_SET_LED_MODE,
			 drv_mb_param, &resp, &param);

	return rc;
}

int qed_mcp_mask_parities(struct qed_hwfn *p_hwfn,
			  struct qed_ptt *p_ptt, u32 mask_parities)
{
	u32 resp = 0, param = 0;
	int rc;

	rc = qed_mcp_cmd(p_hwfn, p_ptt, DRV_MSG_CODE_MASK_PARITIES,
			 mask_parities, &resp, &param);

	if (rc) {
		DP_ERR(p_hwfn,
		       "MCP response failure for mask parities, aborting\n");
	} else if (resp != FW_MSG_CODE_OK) {
		DP_ERR(p_hwfn,
		       "MCP did not acknowledge mask parity request. Old MFW?\n");
		rc = -EINVAL;
	}

	return rc;
}

int qed_mcp_nvm_read(struct qed_dev *cdev, u32 addr, u8 *p_buf, u32 len)
{
	u32 bytes_left = len, offset = 0, bytes_to_copy, read_len = 0;
	struct qed_hwfn *p_hwfn = QED_LEADING_HWFN(cdev);
	u32 resp = 0, resp_param = 0;
	struct qed_ptt *p_ptt;
	int rc = 0;

	p_ptt = qed_ptt_acquire(p_hwfn);
	if (!p_ptt)
		return -EBUSY;

	while (bytes_left > 0) {
		bytes_to_copy = min_t(u32, bytes_left, MCP_DRV_NVM_BUF_LEN);

		rc = qed_mcp_nvm_rd_cmd(p_hwfn, p_ptt,
					DRV_MSG_CODE_NVM_READ_NVRAM,
					addr + offset +
					(bytes_to_copy <<
					 DRV_MB_PARAM_NVM_LEN_OFFSET),
					&resp, &resp_param,
					&read_len,
					(u32 *)(p_buf + offset));

		if (rc || (resp != FW_MSG_CODE_NVM_OK)) {
			DP_NOTICE(cdev, "MCP command rc = %d\n", rc);
			break;
		}

		/* This can be a lengthy process, and it's possible scheduler
		 * isn't preemptable. Sleep a bit to prevent CPU hogging.
		 */
		if (bytes_left % 0x1000 <
		    (bytes_left - read_len) % 0x1000)
			usleep_range(1000, 2000);

		offset += read_len;
		bytes_left -= read_len;
	}

	cdev->mcp_nvm_resp = resp;
	qed_ptt_release(p_hwfn, p_ptt);

	return rc;
}

int qed_mcp_nvm_resp(struct qed_dev *cdev, u8 *p_buf)
{
	struct qed_hwfn *p_hwfn = QED_LEADING_HWFN(cdev);
	struct qed_ptt *p_ptt;

	p_ptt = qed_ptt_acquire(p_hwfn);
	if (!p_ptt)
		return -EBUSY;

	memcpy(p_buf, &cdev->mcp_nvm_resp, sizeof(cdev->mcp_nvm_resp));
	qed_ptt_release(p_hwfn, p_ptt);

	return 0;
}

int qed_mcp_nvm_write(struct qed_dev *cdev,
		      u32 cmd, u32 addr, u8 *p_buf, u32 len)
{
	u32 buf_idx = 0, buf_size, nvm_cmd, nvm_offset, resp = 0, param;
	struct qed_hwfn *p_hwfn = QED_LEADING_HWFN(cdev);
	struct qed_ptt *p_ptt;
	int rc = -EINVAL;

	p_ptt = qed_ptt_acquire(p_hwfn);
	if (!p_ptt)
		return -EBUSY;

	switch (cmd) {
	case QED_PUT_FILE_BEGIN:
		nvm_cmd = DRV_MSG_CODE_NVM_PUT_FILE_BEGIN;
		break;
	case QED_PUT_FILE_DATA:
		nvm_cmd = DRV_MSG_CODE_NVM_PUT_FILE_DATA;
		break;
	case QED_NVM_WRITE_NVRAM:
		nvm_cmd = DRV_MSG_CODE_NVM_WRITE_NVRAM;
		break;
	default:
		DP_NOTICE(p_hwfn, "Invalid nvm write command 0x%x\n", cmd);
		rc = -EINVAL;
		goto out;
	}

	buf_size = min_t(u32, (len - buf_idx), MCP_DRV_NVM_BUF_LEN);
	while (buf_idx < len) {
		if (cmd == QED_PUT_FILE_BEGIN)
			nvm_offset = addr;
		else
			nvm_offset = ((buf_size <<
				       DRV_MB_PARAM_NVM_LEN_OFFSET) | addr) +
				       buf_idx;
		rc = qed_mcp_nvm_wr_cmd(p_hwfn, p_ptt, nvm_cmd, nvm_offset,
					&resp, &param, buf_size,
					(u32 *)&p_buf[buf_idx]);
		if (rc) {
			DP_NOTICE(cdev, "nvm write failed, rc = %d\n", rc);
			resp = FW_MSG_CODE_ERROR;
			break;
		}

		if (resp != FW_MSG_CODE_OK &&
		    resp != FW_MSG_CODE_NVM_OK &&
		    resp != FW_MSG_CODE_NVM_PUT_FILE_FINISH_OK) {
			DP_NOTICE(cdev,
				  "nvm write failed, resp = 0x%08x\n", resp);
			rc = -EINVAL;
			break;
		}

		/* This can be a lengthy process, and it's possible scheduler
		 * isn't pre-emptable. Sleep a bit to prevent CPU hogging.
		 */
		if (buf_idx % 0x1000 > (buf_idx + buf_size) % 0x1000)
			usleep_range(1000, 2000);

		/* For MBI upgrade, MFW response includes the next buffer offset
		 * to be delivered to MFW.
		 */
		if (param && cmd == QED_PUT_FILE_DATA) {
			buf_idx = QED_MFW_GET_FIELD(param,
					FW_MB_PARAM_NVM_PUT_FILE_REQ_OFFSET);
			buf_size = QED_MFW_GET_FIELD(param,
					 FW_MB_PARAM_NVM_PUT_FILE_REQ_SIZE);
		} else {
			buf_idx += buf_size;
			buf_size = min_t(u32, (len - buf_idx),
					 MCP_DRV_NVM_BUF_LEN);
		}
	}

	cdev->mcp_nvm_resp = resp;
out:
	qed_ptt_release(p_hwfn, p_ptt);

	return rc;
}

int qed_mcp_phy_sfp_read(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt,
			 u32 port, u32 addr, u32 offset, u32 len, u8 *p_buf)
{
	u32 bytes_left, bytes_to_copy, buf_size, nvm_offset = 0;
	u32 resp, param;
	int rc;

	nvm_offset |= (port << DRV_MB_PARAM_TRANSCEIVER_PORT_OFFSET) &
		       DRV_MB_PARAM_TRANSCEIVER_PORT_MASK;
	nvm_offset |= (addr << DRV_MB_PARAM_TRANSCEIVER_I2C_ADDRESS_OFFSET) &
		       DRV_MB_PARAM_TRANSCEIVER_I2C_ADDRESS_MASK;

	addr = offset;
	offset = 0;
	bytes_left = len;
	while (bytes_left > 0) {
		bytes_to_copy = min_t(u32, bytes_left,
				      MAX_I2C_TRANSACTION_SIZE);
		nvm_offset &= (DRV_MB_PARAM_TRANSCEIVER_I2C_ADDRESS_MASK |
			       DRV_MB_PARAM_TRANSCEIVER_PORT_MASK);
		nvm_offset |= ((addr + offset) <<
			       DRV_MB_PARAM_TRANSCEIVER_OFFSET_OFFSET) &
			       DRV_MB_PARAM_TRANSCEIVER_OFFSET_MASK;
		nvm_offset |= (bytes_to_copy <<
			       DRV_MB_PARAM_TRANSCEIVER_SIZE_OFFSET) &
			       DRV_MB_PARAM_TRANSCEIVER_SIZE_MASK;
		rc = qed_mcp_nvm_rd_cmd(p_hwfn, p_ptt,
					DRV_MSG_CODE_TRANSCEIVER_READ,
					nvm_offset, &resp, &param, &buf_size,
					(u32 *)(p_buf + offset));
		if (rc) {
			DP_NOTICE(p_hwfn,
				  "Failed to send a transceiver read command to the MFW. rc = %d.\n",
				  rc);
			return rc;
		}

		if (resp == FW_MSG_CODE_TRANSCEIVER_NOT_PRESENT)
			return -ENODEV;
		else if (resp != FW_MSG_CODE_TRANSCEIVER_DIAG_OK)
			return -EINVAL;

		offset += buf_size;
		bytes_left -= buf_size;
	}

	return 0;
}

int qed_mcp_bist_register_test(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt)
{
	u32 drv_mb_param = 0, rsp, param;
	int rc = 0;

	drv_mb_param = (DRV_MB_PARAM_BIST_REGISTER_TEST <<
			DRV_MB_PARAM_BIST_TEST_INDEX_SHIFT);

	rc = qed_mcp_cmd(p_hwfn, p_ptt, DRV_MSG_CODE_BIST_TEST,
			 drv_mb_param, &rsp, &param);

	if (rc)
		return rc;

	if (((rsp & FW_MSG_CODE_MASK) != FW_MSG_CODE_OK) ||
	    (param != DRV_MB_PARAM_BIST_RC_PASSED))
		rc = -EAGAIN;

	return rc;
}

int qed_mcp_bist_clock_test(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt)
{
	u32 drv_mb_param, rsp, param;
	int rc = 0;

	drv_mb_param = (DRV_MB_PARAM_BIST_CLOCK_TEST <<
			DRV_MB_PARAM_BIST_TEST_INDEX_SHIFT);

	rc = qed_mcp_cmd(p_hwfn, p_ptt, DRV_MSG_CODE_BIST_TEST,
			 drv_mb_param, &rsp, &param);

	if (rc)
		return rc;

	if (((rsp & FW_MSG_CODE_MASK) != FW_MSG_CODE_OK) ||
	    (param != DRV_MB_PARAM_BIST_RC_PASSED))
		rc = -EAGAIN;

	return rc;
}

int qed_mcp_bist_nvm_get_num_images(struct qed_hwfn *p_hwfn,
				    struct qed_ptt *p_ptt,
				    u32 *num_images)
{
	u32 drv_mb_param = 0, rsp;
	int rc = 0;

	drv_mb_param = (DRV_MB_PARAM_BIST_NVM_TEST_NUM_IMAGES <<
			DRV_MB_PARAM_BIST_TEST_INDEX_SHIFT);

	rc = qed_mcp_cmd(p_hwfn, p_ptt, DRV_MSG_CODE_BIST_TEST,
			 drv_mb_param, &rsp, num_images);
	if (rc)
		return rc;

	if (((rsp & FW_MSG_CODE_MASK) != FW_MSG_CODE_OK))
		rc = -EINVAL;

	return rc;
}

int qed_mcp_bist_nvm_get_image_att(struct qed_hwfn *p_hwfn,
				   struct qed_ptt *p_ptt,
				   struct bist_nvm_image_att *p_image_att,
				   u32 image_index)
{
	u32 buf_size = 0, param, resp = 0, resp_param = 0;
	int rc;

	param = DRV_MB_PARAM_BIST_NVM_TEST_IMAGE_BY_INDEX <<
		DRV_MB_PARAM_BIST_TEST_INDEX_SHIFT;
	param |= image_index << DRV_MB_PARAM_BIST_TEST_IMAGE_INDEX_SHIFT;

	rc = qed_mcp_nvm_rd_cmd(p_hwfn, p_ptt,
				DRV_MSG_CODE_BIST_TEST, param,
				&resp, &resp_param,
				&buf_size,
				(u32 *)p_image_att);
	if (rc)
		return rc;

	if (((resp & FW_MSG_CODE_MASK) != FW_MSG_CODE_OK) ||
	    (p_image_att->return_code != 1))
		rc = -EINVAL;

	return rc;
}

int qed_mcp_nvm_info_populate(struct qed_hwfn *p_hwfn)
{
	struct qed_nvm_image_info nvm_info;
	struct qed_ptt *p_ptt;
	int rc;
	u32 i;

	if (p_hwfn->nvm_info.valid)
		return 0;

	p_ptt = qed_ptt_acquire(p_hwfn);
	if (!p_ptt) {
		DP_ERR(p_hwfn, "failed to acquire ptt\n");
		return -EBUSY;
	}

	/* Acquire from MFW the amount of available images */
	nvm_info.num_images = 0;
	rc = qed_mcp_bist_nvm_get_num_images(p_hwfn,
					     p_ptt, &nvm_info.num_images);
	if (rc == -EOPNOTSUPP) {
		DP_INFO(p_hwfn, "DRV_MSG_CODE_BIST_TEST is not supported\n");
		goto out;
	} else if (rc || !nvm_info.num_images) {
		DP_ERR(p_hwfn, "Failed getting number of images\n");
		goto err0;
	}

	nvm_info.image_att = kmalloc_array(nvm_info.num_images,
					   sizeof(struct bist_nvm_image_att),
					   GFP_KERNEL);
	if (!nvm_info.image_att) {
		rc = -ENOMEM;
		goto err0;
	}

	/* Iterate over images and get their attributes */
	for (i = 0; i < nvm_info.num_images; i++) {
		rc = qed_mcp_bist_nvm_get_image_att(p_hwfn, p_ptt,
						    &nvm_info.image_att[i], i);
		if (rc) {
			DP_ERR(p_hwfn,
			       "Failed getting image index %d attributes\n", i);
			goto err1;
		}

		DP_VERBOSE(p_hwfn, QED_MSG_SP, "image index %d, size %x\n", i,
			   nvm_info.image_att[i].len);
	}
out:
	/* Update hwfn's nvm_info */
	if (nvm_info.num_images) {
		p_hwfn->nvm_info.num_images = nvm_info.num_images;
		kfree(p_hwfn->nvm_info.image_att);
		p_hwfn->nvm_info.image_att = nvm_info.image_att;
		p_hwfn->nvm_info.valid = true;
	}

	qed_ptt_release(p_hwfn, p_ptt);
	return 0;

err1:
	kfree(nvm_info.image_att);
err0:
	qed_ptt_release(p_hwfn, p_ptt);
	return rc;
}

void qed_mcp_nvm_info_free(struct qed_hwfn *p_hwfn)
{
	kfree(p_hwfn->nvm_info.image_att);
	p_hwfn->nvm_info.image_att = NULL;
	p_hwfn->nvm_info.valid = false;
}

int
qed_mcp_get_nvm_image_att(struct qed_hwfn *p_hwfn,
			  enum qed_nvm_images image_id,
			  struct qed_nvm_image_att *p_image_att)
{
	enum nvm_image_type type;
	u32 i;

	/* Translate image_id into MFW definitions */
	switch (image_id) {
	case QED_NVM_IMAGE_ISCSI_CFG:
		type = NVM_TYPE_ISCSI_CFG;
		break;
	case QED_NVM_IMAGE_FCOE_CFG:
		type = NVM_TYPE_FCOE_CFG;
		break;
	case QED_NVM_IMAGE_MDUMP:
		type = NVM_TYPE_MDUMP;
		break;
	case QED_NVM_IMAGE_NVM_CFG1:
		type = NVM_TYPE_NVM_CFG1;
		break;
	case QED_NVM_IMAGE_DEFAULT_CFG:
		type = NVM_TYPE_DEFAULT_CFG;
		break;
	case QED_NVM_IMAGE_NVM_META:
		type = NVM_TYPE_META;
		break;
	default:
		DP_NOTICE(p_hwfn, "Unknown request of image_id %08x\n",
			  image_id);
		return -EINVAL;
	}

	qed_mcp_nvm_info_populate(p_hwfn);
	for (i = 0; i < p_hwfn->nvm_info.num_images; i++)
		if (type == p_hwfn->nvm_info.image_att[i].image_type)
			break;
	if (i == p_hwfn->nvm_info.num_images) {
		DP_VERBOSE(p_hwfn, QED_MSG_STORAGE,
			   "Failed to find nvram image of type %08x\n",
			   image_id);
		return -ENOENT;
	}

	p_image_att->start_addr = p_hwfn->nvm_info.image_att[i].nvm_start_addr;
	p_image_att->length = p_hwfn->nvm_info.image_att[i].len;

	return 0;
}

int qed_mcp_get_nvm_image(struct qed_hwfn *p_hwfn,
			  enum qed_nvm_images image_id,
			  u8 *p_buffer, u32 buffer_len)
{
	struct qed_nvm_image_att image_att;
	int rc;

	memset(p_buffer, 0, buffer_len);

	rc = qed_mcp_get_nvm_image_att(p_hwfn, image_id, &image_att);
	if (rc)
		return rc;

	/* Validate sizes - both the image's and the supplied buffer's */
	if (image_att.length <= 4) {
		DP_VERBOSE(p_hwfn, QED_MSG_STORAGE,
			   "Image [%d] is too small - only %d bytes\n",
			   image_id, image_att.length);
		return -EINVAL;
	}

	if (image_att.length > buffer_len) {
		DP_VERBOSE(p_hwfn,
			   QED_MSG_STORAGE,
			   "Image [%d] is too big - %08x bytes where only %08x are available\n",
			   image_id, image_att.length, buffer_len);
		return -ENOMEM;
	}

	return qed_mcp_nvm_read(p_hwfn->cdev, image_att.start_addr,
				p_buffer, image_att.length);
}

static enum resource_id_enum qed_mcp_get_mfw_res_id(enum qed_resources res_id)
{
	enum resource_id_enum mfw_res_id = RESOURCE_NUM_INVALID;

	switch (res_id) {
	case QED_SB:
		mfw_res_id = RESOURCE_NUM_SB_E;
		break;
	case QED_L2_QUEUE:
		mfw_res_id = RESOURCE_NUM_L2_QUEUE_E;
		break;
	case QED_VPORT:
		mfw_res_id = RESOURCE_NUM_VPORT_E;
		break;
	case QED_RSS_ENG:
		mfw_res_id = RESOURCE_NUM_RSS_ENGINES_E;
		break;
	case QED_PQ:
		mfw_res_id = RESOURCE_NUM_PQ_E;
		break;
	case QED_RL:
		mfw_res_id = RESOURCE_NUM_RL_E;
		break;
	case QED_MAC:
	case QED_VLAN:
		/* Each VFC resource can accommodate both a MAC and a VLAN */
		mfw_res_id = RESOURCE_VFC_FILTER_E;
		break;
	case QED_ILT:
		mfw_res_id = RESOURCE_ILT_E;
		break;
	case QED_LL2_RAM_QUEUE:
		mfw_res_id = RESOURCE_LL2_QUEUE_E;
		break;
	case QED_LL2_CTX_QUEUE:
		mfw_res_id = RESOURCE_LL2_CQS_E;
		break;
	case QED_RDMA_CNQ_RAM:
	case QED_CMDQS_CQS:
		/* CNQ/CMDQS are the same resource */
		mfw_res_id = RESOURCE_CQS_E;
		break;
	case QED_RDMA_STATS_QUEUE:
		mfw_res_id = RESOURCE_RDMA_STATS_QUEUE_E;
		break;
	case QED_BDQ:
		mfw_res_id = RESOURCE_BDQ_E;
		break;
	default:
		break;
	}

	return mfw_res_id;
}

#define QED_RESC_ALLOC_VERSION_MAJOR    2
#define QED_RESC_ALLOC_VERSION_MINOR    0
#define QED_RESC_ALLOC_VERSION				     \
	((QED_RESC_ALLOC_VERSION_MAJOR <<		     \
	  DRV_MB_PARAM_RESOURCE_ALLOC_VERSION_MAJOR_SHIFT) | \
	 (QED_RESC_ALLOC_VERSION_MINOR <<		     \
	  DRV_MB_PARAM_RESOURCE_ALLOC_VERSION_MINOR_SHIFT))

struct qed_resc_alloc_in_params {
	u32 cmd;
	enum qed_resources res_id;
	u32 resc_max_val;
};

struct qed_resc_alloc_out_params {
	u32 mcp_resp;
	u32 mcp_param;
	u32 resc_num;
	u32 resc_start;
	u32 vf_resc_num;
	u32 vf_resc_start;
	u32 flags;
};

static int
qed_mcp_resc_allocation_msg(struct qed_hwfn *p_hwfn,
			    struct qed_ptt *p_ptt,
			    struct qed_resc_alloc_in_params *p_in_params,
			    struct qed_resc_alloc_out_params *p_out_params)
{
	struct qed_mcp_mb_params mb_params;
	struct resource_info mfw_resc_info;
	int rc;

	memset(&mfw_resc_info, 0, sizeof(mfw_resc_info));

	mfw_resc_info.res_id = qed_mcp_get_mfw_res_id(p_in_params->res_id);
	if (mfw_resc_info.res_id == RESOURCE_NUM_INVALID) {
		DP_ERR(p_hwfn,
		       "Failed to match resource %d [%s] with the MFW resources\n",
		       p_in_params->res_id,
		       qed_hw_get_resc_name(p_in_params->res_id));
		return -EINVAL;
	}

	switch (p_in_params->cmd) {
	case DRV_MSG_SET_RESOURCE_VALUE_MSG:
		mfw_resc_info.size = p_in_params->resc_max_val;
		fallthrough;
	case DRV_MSG_GET_RESOURCE_ALLOC_MSG:
		break;
	default:
		DP_ERR(p_hwfn, "Unexpected resource alloc command [0x%08x]\n",
		       p_in_params->cmd);
		return -EINVAL;
	}

	memset(&mb_params, 0, sizeof(mb_params));
	mb_params.cmd = p_in_params->cmd;
	mb_params.param = QED_RESC_ALLOC_VERSION;
	mb_params.p_data_src = &mfw_resc_info;
	mb_params.data_src_size = sizeof(mfw_resc_info);
	mb_params.p_data_dst = mb_params.p_data_src;
	mb_params.data_dst_size = mb_params.data_src_size;

	DP_VERBOSE(p_hwfn,
		   QED_MSG_SP,
		   "Resource message request: cmd 0x%08x, res_id %d [%s], hsi_version %d.%d, val 0x%x\n",
		   p_in_params->cmd,
		   p_in_params->res_id,
		   qed_hw_get_resc_name(p_in_params->res_id),
		   QED_MFW_GET_FIELD(mb_params.param,
				     DRV_MB_PARAM_RESOURCE_ALLOC_VERSION_MAJOR),
		   QED_MFW_GET_FIELD(mb_params.param,
				     DRV_MB_PARAM_RESOURCE_ALLOC_VERSION_MINOR),
		   p_in_params->resc_max_val);

	rc = qed_mcp_cmd_and_union(p_hwfn, p_ptt, &mb_params);
	if (rc)
		return rc;

	p_out_params->mcp_resp = mb_params.mcp_resp;
	p_out_params->mcp_param = mb_params.mcp_param;
	p_out_params->resc_num = mfw_resc_info.size;
	p_out_params->resc_start = mfw_resc_info.offset;
	p_out_params->vf_resc_num = mfw_resc_info.vf_size;
	p_out_params->vf_resc_start = mfw_resc_info.vf_offset;
	p_out_params->flags = mfw_resc_info.flags;

	DP_VERBOSE(p_hwfn,
		   QED_MSG_SP,
		   "Resource message response: mfw_hsi_version %d.%d, num 0x%x, start 0x%x, vf_num 0x%x, vf_start 0x%x, flags 0x%08x\n",
		   QED_MFW_GET_FIELD(p_out_params->mcp_param,
				     FW_MB_PARAM_RESOURCE_ALLOC_VERSION_MAJOR),
		   QED_MFW_GET_FIELD(p_out_params->mcp_param,
				     FW_MB_PARAM_RESOURCE_ALLOC_VERSION_MINOR),
		   p_out_params->resc_num,
		   p_out_params->resc_start,
		   p_out_params->vf_resc_num,
		   p_out_params->vf_resc_start, p_out_params->flags);

	return 0;
}

int
qed_mcp_set_resc_max_val(struct qed_hwfn *p_hwfn,
			 struct qed_ptt *p_ptt,
			 enum qed_resources res_id,
			 u32 resc_max_val, u32 *p_mcp_resp)
{
	struct qed_resc_alloc_out_params out_params;
	struct qed_resc_alloc_in_params in_params;
	int rc;

	memset(&in_params, 0, sizeof(in_params));
	in_params.cmd = DRV_MSG_SET_RESOURCE_VALUE_MSG;
	in_params.res_id = res_id;
	in_params.resc_max_val = resc_max_val;
	memset(&out_params, 0, sizeof(out_params));
	rc = qed_mcp_resc_allocation_msg(p_hwfn, p_ptt, &in_params,
					 &out_params);
	if (rc)
		return rc;

	*p_mcp_resp = out_params.mcp_resp;

	return 0;
}

int
qed_mcp_get_resc_info(struct qed_hwfn *p_hwfn,
		      struct qed_ptt *p_ptt,
		      enum qed_resources res_id,
		      u32 *p_mcp_resp, u32 *p_resc_num, u32 *p_resc_start)
{
	struct qed_resc_alloc_out_params out_params;
	struct qed_resc_alloc_in_params in_params;
	int rc;

	memset(&in_params, 0, sizeof(in_params));
	in_params.cmd = DRV_MSG_GET_RESOURCE_ALLOC_MSG;
	in_params.res_id = res_id;
	memset(&out_params, 0, sizeof(out_params));
	rc = qed_mcp_resc_allocation_msg(p_hwfn, p_ptt, &in_params,
					 &out_params);
	if (rc)
		return rc;

	*p_mcp_resp = out_params.mcp_resp;

	if (*p_mcp_resp == FW_MSG_CODE_RESOURCE_ALLOC_OK) {
		*p_resc_num = out_params.resc_num;
		*p_resc_start = out_params.resc_start;
	}

	return 0;
}

int qed_mcp_initiate_pf_flr(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt)
{
	u32 mcp_resp, mcp_param;

	return qed_mcp_cmd(p_hwfn, p_ptt, DRV_MSG_CODE_INITIATE_PF_FLR, 0,
			   &mcp_resp, &mcp_param);
}

static int qed_mcp_resource_cmd(struct qed_hwfn *p_hwfn,
				struct qed_ptt *p_ptt,
				u32 param, u32 *p_mcp_resp, u32 *p_mcp_param)
{
	int rc;

	rc = qed_mcp_cmd(p_hwfn, p_ptt, DRV_MSG_CODE_RESOURCE_CMD, param,
			 p_mcp_resp, p_mcp_param);
	if (rc)
		return rc;

	if (*p_mcp_resp == FW_MSG_CODE_UNSUPPORTED) {
		DP_INFO(p_hwfn,
			"The resource command is unsupported by the MFW\n");
		return -EINVAL;
	}

	if (*p_mcp_param == RESOURCE_OPCODE_UNKNOWN_CMD) {
		u8 opcode = QED_MFW_GET_FIELD(param, RESOURCE_CMD_REQ_OPCODE);

		DP_NOTICE(p_hwfn,
			  "The resource command is unknown to the MFW [param 0x%08x, opcode %d]\n",
			  param, opcode);
		return -EINVAL;
	}

	return rc;
}

static int
__qed_mcp_resc_lock(struct qed_hwfn *p_hwfn,
		    struct qed_ptt *p_ptt,
		    struct qed_resc_lock_params *p_params)
{
	u32 param = 0, mcp_resp, mcp_param;
	u8 opcode;
	int rc;

	switch (p_params->timeout) {
	case QED_MCP_RESC_LOCK_TO_DEFAULT:
		opcode = RESOURCE_OPCODE_REQ;
		p_params->timeout = 0;
		break;
	case QED_MCP_RESC_LOCK_TO_NONE:
		opcode = RESOURCE_OPCODE_REQ_WO_AGING;
		p_params->timeout = 0;
		break;
	default:
		opcode = RESOURCE_OPCODE_REQ_W_AGING;
		break;
	}

	QED_MFW_SET_FIELD(param, RESOURCE_CMD_REQ_RESC, p_params->resource);
	QED_MFW_SET_FIELD(param, RESOURCE_CMD_REQ_OPCODE, opcode);
	QED_MFW_SET_FIELD(param, RESOURCE_CMD_REQ_AGE, p_params->timeout);

	DP_VERBOSE(p_hwfn,
		   QED_MSG_SP,
		   "Resource lock request: param 0x%08x [age %d, opcode %d, resource %d]\n",
		   param, p_params->timeout, opcode, p_params->resource);

	/* Attempt to acquire the resource */
	rc = qed_mcp_resource_cmd(p_hwfn, p_ptt, param, &mcp_resp, &mcp_param);
	if (rc)
		return rc;

	/* Analyze the response */
	p_params->owner = QED_MFW_GET_FIELD(mcp_param, RESOURCE_CMD_RSP_OWNER);
	opcode = QED_MFW_GET_FIELD(mcp_param, RESOURCE_CMD_RSP_OPCODE);

	DP_VERBOSE(p_hwfn,
		   QED_MSG_SP,
		   "Resource lock response: mcp_param 0x%08x [opcode %d, owner %d]\n",
		   mcp_param, opcode, p_params->owner);

	switch (opcode) {
	case RESOURCE_OPCODE_GNT:
		p_params->b_granted = true;
		break;
	case RESOURCE_OPCODE_BUSY:
		p_params->b_granted = false;
		break;
	default:
		DP_NOTICE(p_hwfn,
			  "Unexpected opcode in resource lock response [mcp_param 0x%08x, opcode %d]\n",
			  mcp_param, opcode);
		return -EINVAL;
	}

	return 0;
}

int
qed_mcp_resc_lock(struct qed_hwfn *p_hwfn,
		  struct qed_ptt *p_ptt, struct qed_resc_lock_params *p_params)
{
	u32 retry_cnt = 0;
	int rc;

	do {
		/* No need for an interval before the first iteration */
		if (retry_cnt) {
			if (p_params->sleep_b4_retry) {
				u16 retry_interval_in_ms =
				    DIV_ROUND_UP(p_params->retry_interval,
						 1000);

				msleep(retry_interval_in_ms);
			} else {
				udelay(p_params->retry_interval);
			}
		}

		rc = __qed_mcp_resc_lock(p_hwfn, p_ptt, p_params);
		if (rc)
			return rc;

		if (p_params->b_granted)
			break;
	} while (retry_cnt++ < p_params->retry_num);

	return 0;
}

int
qed_mcp_resc_unlock(struct qed_hwfn *p_hwfn,
		    struct qed_ptt *p_ptt,
		    struct qed_resc_unlock_params *p_params)
{
	u32 param = 0, mcp_resp, mcp_param;
	u8 opcode;
	int rc;

	opcode = p_params->b_force ? RESOURCE_OPCODE_FORCE_RELEASE
				   : RESOURCE_OPCODE_RELEASE;
	QED_MFW_SET_FIELD(param, RESOURCE_CMD_REQ_RESC, p_params->resource);
	QED_MFW_SET_FIELD(param, RESOURCE_CMD_REQ_OPCODE, opcode);

	DP_VERBOSE(p_hwfn, QED_MSG_SP,
		   "Resource unlock request: param 0x%08x [opcode %d, resource %d]\n",
		   param, opcode, p_params->resource);

	/* Attempt to release the resource */
	rc = qed_mcp_resource_cmd(p_hwfn, p_ptt, param, &mcp_resp, &mcp_param);
	if (rc)
		return rc;

	/* Analyze the response */
	opcode = QED_MFW_GET_FIELD(mcp_param, RESOURCE_CMD_RSP_OPCODE);

	DP_VERBOSE(p_hwfn, QED_MSG_SP,
		   "Resource unlock response: mcp_param 0x%08x [opcode %d]\n",
		   mcp_param, opcode);

	switch (opcode) {
	case RESOURCE_OPCODE_RELEASED_PREVIOUS:
		DP_INFO(p_hwfn,
			"Resource unlock request for an already released resource [%d]\n",
			p_params->resource);
		fallthrough;
	case RESOURCE_OPCODE_RELEASED:
		p_params->b_released = true;
		break;
	case RESOURCE_OPCODE_WRONG_OWNER:
		p_params->b_released = false;
		break;
	default:
		DP_NOTICE(p_hwfn,
			  "Unexpected opcode in resource unlock response [mcp_param 0x%08x, opcode %d]\n",
			  mcp_param, opcode);
		return -EINVAL;
	}

	return 0;
}

void qed_mcp_resc_lock_default_init(struct qed_resc_lock_params *p_lock,
				    struct qed_resc_unlock_params *p_unlock,
				    enum qed_resc_lock
				    resource, bool b_is_permanent)
{
	if (p_lock) {
		memset(p_lock, 0, sizeof(*p_lock));

		/* Permanent resources don't require aging, and there's no
		 * point in trying to acquire them more than once since it's
		 * unexpected another entity would release them.
		 */
		if (b_is_permanent) {
			p_lock->timeout = QED_MCP_RESC_LOCK_TO_NONE;
		} else {
			p_lock->retry_num = QED_MCP_RESC_LOCK_RETRY_CNT_DFLT;
			p_lock->retry_interval =
			    QED_MCP_RESC_LOCK_RETRY_VAL_DFLT;
			p_lock->sleep_b4_retry = true;
		}

		p_lock->resource = resource;
	}

	if (p_unlock) {
		memset(p_unlock, 0, sizeof(*p_unlock));
		p_unlock->resource = resource;
	}
}

bool qed_mcp_is_smart_an_supported(struct qed_hwfn *p_hwfn)
{
	return !!(p_hwfn->mcp_info->capabilities &
		  FW_MB_PARAM_FEATURE_SUPPORT_SMARTLINQ);
}

int qed_mcp_get_capabilities(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt)
{
	u32 mcp_resp;
	int rc;

	rc = qed_mcp_cmd(p_hwfn, p_ptt, DRV_MSG_CODE_GET_MFW_FEATURE_SUPPORT,
			 0, &mcp_resp, &p_hwfn->mcp_info->capabilities);
	if (!rc)
		DP_VERBOSE(p_hwfn, (QED_MSG_SP | NETIF_MSG_PROBE),
			   "MFW supported features: %08x\n",
			   p_hwfn->mcp_info->capabilities);

	return rc;
}

int qed_mcp_set_capabilities(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt)
{
	u32 mcp_resp, mcp_param, features;

	features = DRV_MB_PARAM_FEATURE_SUPPORT_PORT_EEE |
		   DRV_MB_PARAM_FEATURE_SUPPORT_FUNC_VLINK |
		   DRV_MB_PARAM_FEATURE_SUPPORT_PORT_FEC_CONTROL;

	if (QED_IS_E5(p_hwfn->cdev))
		features |=
		    DRV_MB_PARAM_FEATURE_SUPPORT_PORT_EXT_SPEED_FEC_CONTROL;

	return qed_mcp_cmd(p_hwfn, p_ptt, DRV_MSG_CODE_FEATURE_SUPPORT,
			   features, &mcp_resp, &mcp_param);
}

int qed_mcp_get_engine_config(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt)
{
	struct qed_mcp_mb_params mb_params = {0};
	struct qed_dev *cdev = p_hwfn->cdev;
	u8 fir_valid, l2_valid;
	int rc;

	mb_params.cmd = DRV_MSG_CODE_GET_ENGINE_CONFIG;
	rc = qed_mcp_cmd_and_union(p_hwfn, p_ptt, &mb_params);
	if (rc)
		return rc;

	if (mb_params.mcp_resp == FW_MSG_CODE_UNSUPPORTED) {
		DP_INFO(p_hwfn,
			"The get_engine_config command is unsupported by the MFW\n");
		return -EOPNOTSUPP;
	}

	fir_valid = QED_MFW_GET_FIELD(mb_params.mcp_param,
				      FW_MB_PARAM_ENG_CFG_FIR_AFFIN_VALID);
	if (fir_valid)
		cdev->fir_affin =
		    QED_MFW_GET_FIELD(mb_params.mcp_param,
				      FW_MB_PARAM_ENG_CFG_FIR_AFFIN_VALUE);

	l2_valid = QED_MFW_GET_FIELD(mb_params.mcp_param,
				     FW_MB_PARAM_ENG_CFG_L2_AFFIN_VALID);
	if (l2_valid)
		cdev->l2_affin_hint =
		    QED_MFW_GET_FIELD(mb_params.mcp_param,
				      FW_MB_PARAM_ENG_CFG_L2_AFFIN_VALUE);

	DP_INFO(p_hwfn,
		"Engine affinity config: FIR={valid %hhd, value %hhd}, L2_hint={valid %hhd, value %hhd}\n",
		fir_valid, cdev->fir_affin, l2_valid, cdev->l2_affin_hint);

	return 0;
}

int qed_mcp_get_ppfid_bitmap(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt)
{
	struct qed_mcp_mb_params mb_params = {0};
	struct qed_dev *cdev = p_hwfn->cdev;
	int rc;

	mb_params.cmd = DRV_MSG_CODE_GET_PPFID_BITMAP;
	rc = qed_mcp_cmd_and_union(p_hwfn, p_ptt, &mb_params);
	if (rc)
		return rc;

	if (mb_params.mcp_resp == FW_MSG_CODE_UNSUPPORTED) {
		DP_INFO(p_hwfn,
			"The get_ppfid_bitmap command is unsupported by the MFW\n");
		return -EOPNOTSUPP;
	}

	cdev->ppfid_bitmap = QED_MFW_GET_FIELD(mb_params.mcp_param,
					       FW_MB_PARAM_PPFID_BITMAP);

	DP_VERBOSE(p_hwfn, QED_MSG_SP, "PPFID bitmap 0x%hhx\n",
		   cdev->ppfid_bitmap);

	return 0;
}

int qed_mcp_nvm_get_cfg(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt,
			u16 option_id, u8 entity_id, u16 flags, u8 *p_buf,
			u32 *p_len)
{
	u32 mb_param = 0, resp, param;
	int rc;

	QED_MFW_SET_FIELD(mb_param, DRV_MB_PARAM_NVM_CFG_OPTION_ID, option_id);
	if (flags & QED_NVM_CFG_OPTION_INIT)
		QED_MFW_SET_FIELD(mb_param,
				  DRV_MB_PARAM_NVM_CFG_OPTION_INIT, 1);
	if (flags & QED_NVM_CFG_OPTION_FREE)
		QED_MFW_SET_FIELD(mb_param,
				  DRV_MB_PARAM_NVM_CFG_OPTION_FREE, 1);
	if (flags & QED_NVM_CFG_OPTION_ENTITY_SEL) {
		QED_MFW_SET_FIELD(mb_param,
				  DRV_MB_PARAM_NVM_CFG_OPTION_ENTITY_SEL, 1);
		QED_MFW_SET_FIELD(mb_param,
				  DRV_MB_PARAM_NVM_CFG_OPTION_ENTITY_ID,
				  entity_id);
	}

	rc = qed_mcp_nvm_rd_cmd(p_hwfn, p_ptt,
				DRV_MSG_CODE_GET_NVM_CFG_OPTION,
				mb_param, &resp, &param, p_len, (u32 *)p_buf);

	return rc;
}

int qed_mcp_nvm_set_cfg(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt,
			u16 option_id, u8 entity_id, u16 flags, u8 *p_buf,
			u32 len)
{
	u32 mb_param = 0, resp, param;

	QED_MFW_SET_FIELD(mb_param, DRV_MB_PARAM_NVM_CFG_OPTION_ID, option_id);
	if (flags & QED_NVM_CFG_OPTION_ALL)
		QED_MFW_SET_FIELD(mb_param,
				  DRV_MB_PARAM_NVM_CFG_OPTION_ALL, 1);
	if (flags & QED_NVM_CFG_OPTION_INIT)
		QED_MFW_SET_FIELD(mb_param,
				  DRV_MB_PARAM_NVM_CFG_OPTION_INIT, 1);
	if (flags & QED_NVM_CFG_OPTION_COMMIT)
		QED_MFW_SET_FIELD(mb_param,
				  DRV_MB_PARAM_NVM_CFG_OPTION_COMMIT, 1);
	if (flags & QED_NVM_CFG_OPTION_FREE)
		QED_MFW_SET_FIELD(mb_param,
				  DRV_MB_PARAM_NVM_CFG_OPTION_FREE, 1);
	if (flags & QED_NVM_CFG_OPTION_ENTITY_SEL) {
		QED_MFW_SET_FIELD(mb_param,
				  DRV_MB_PARAM_NVM_CFG_OPTION_ENTITY_SEL, 1);
		QED_MFW_SET_FIELD(mb_param,
				  DRV_MB_PARAM_NVM_CFG_OPTION_ENTITY_ID,
				  entity_id);
	}

	return qed_mcp_nvm_wr_cmd(p_hwfn, p_ptt,
				  DRV_MSG_CODE_SET_NVM_CFG_OPTION,
				  mb_param, &resp, &param, len, (u32 *)p_buf);
}

#define QED_MCP_DBG_DATA_MAX_SIZE               MCP_DRV_NVM_BUF_LEN
#define QED_MCP_DBG_DATA_MAX_HEADER_SIZE        sizeof(u32)
#define QED_MCP_DBG_DATA_MAX_PAYLOAD_SIZE \
	(QED_MCP_DBG_DATA_MAX_SIZE - QED_MCP_DBG_DATA_MAX_HEADER_SIZE)

static int
__qed_mcp_send_debug_data(struct qed_hwfn *p_hwfn,
			  struct qed_ptt *p_ptt, u8 *p_buf, u8 size)
{
	struct qed_mcp_mb_params mb_params;
	int rc;

	if (size > QED_MCP_DBG_DATA_MAX_SIZE) {
		DP_ERR(p_hwfn,
		       "Debug data size is %d while it should not exceed %d\n",
		       size, QED_MCP_DBG_DATA_MAX_SIZE);
		return -EINVAL;
	}

	memset(&mb_params, 0, sizeof(mb_params));
	mb_params.cmd = DRV_MSG_CODE_DEBUG_DATA_SEND;
	SET_MFW_FIELD(mb_params.param, DRV_MSG_CODE_DEBUG_DATA_SEND_SIZE, size);
	mb_params.p_data_src = p_buf;
	mb_params.data_src_size = size;
	rc = qed_mcp_cmd_and_union(p_hwfn, p_ptt, &mb_params);
	if (rc)
		return rc;

	if (mb_params.mcp_resp == FW_MSG_CODE_UNSUPPORTED) {
		DP_INFO(p_hwfn,
			"The DEBUG_DATA_SEND command is unsupported by the MFW\n");
		return -EOPNOTSUPP;
	} else if (mb_params.mcp_resp == (u32)FW_MSG_CODE_DEBUG_NOT_ENABLED) {
		DP_INFO(p_hwfn, "The DEBUG_DATA_SEND command is not enabled\n");
		return -EBUSY;
	} else if (mb_params.mcp_resp != (u32)FW_MSG_CODE_DEBUG_DATA_SEND_OK) {
		DP_NOTICE(p_hwfn,
			  "Failed to send debug data to the MFW [resp 0x%08x]\n",
			  mb_params.mcp_resp);
		return -EINVAL;
	}

	return 0;
}

enum qed_mcp_dbg_data_type {
	QED_MCP_DBG_DATA_TYPE_RAW,
};

/* Header format: [31:28] PFID, [27:20] flags, [19:12] type, [11:0] S/N */
#define QED_MCP_DBG_DATA_HDR_SN_OFFSET  0
#define QED_MCP_DBG_DATA_HDR_SN_MASK            0x00000fff
#define QED_MCP_DBG_DATA_HDR_TYPE_OFFSET        12
#define QED_MCP_DBG_DATA_HDR_TYPE_MASK  0x000ff000
#define QED_MCP_DBG_DATA_HDR_FLAGS_OFFSET       20
#define QED_MCP_DBG_DATA_HDR_FLAGS_MASK 0x0ff00000
#define QED_MCP_DBG_DATA_HDR_PF_OFFSET  28
#define QED_MCP_DBG_DATA_HDR_PF_MASK            0xf0000000

#define QED_MCP_DBG_DATA_HDR_FLAGS_FIRST        0x1
#define QED_MCP_DBG_DATA_HDR_FLAGS_LAST 0x2

static int
qed_mcp_send_debug_data(struct qed_hwfn *p_hwfn,
			struct qed_ptt *p_ptt,
			enum qed_mcp_dbg_data_type type, u8 *p_buf, u32 size)
{
	u8 raw_data[QED_MCP_DBG_DATA_MAX_SIZE], *p_tmp_buf = p_buf;
	u32 tmp_size = size, *p_header, *p_payload;
	u8 flags = 0;
	u16 seq;
	int rc;

	p_header = (u32 *)raw_data;
	p_payload = (u32 *)(raw_data + QED_MCP_DBG_DATA_MAX_HEADER_SIZE);

	seq = (u16)atomic_inc_return(&p_hwfn->mcp_info->dbg_data_seq);

	/* First chunk is marked as 'first' */
	flags |= QED_MCP_DBG_DATA_HDR_FLAGS_FIRST;

	*p_header = 0;
	SET_MFW_FIELD(*p_header, QED_MCP_DBG_DATA_HDR_SN, seq);
	SET_MFW_FIELD(*p_header, QED_MCP_DBG_DATA_HDR_TYPE, type);
	SET_MFW_FIELD(*p_header, QED_MCP_DBG_DATA_HDR_FLAGS, flags);
	SET_MFW_FIELD(*p_header, QED_MCP_DBG_DATA_HDR_PF, p_hwfn->abs_pf_id);

	while (tmp_size > QED_MCP_DBG_DATA_MAX_PAYLOAD_SIZE) {
		memcpy(p_payload, p_tmp_buf, QED_MCP_DBG_DATA_MAX_PAYLOAD_SIZE);
		rc = __qed_mcp_send_debug_data(p_hwfn, p_ptt, raw_data,
					       QED_MCP_DBG_DATA_MAX_SIZE);
		if (rc)
			return rc;

		/* Clear the 'first' marking after sending the first chunk */
		if (p_tmp_buf == p_buf) {
			flags &= ~QED_MCP_DBG_DATA_HDR_FLAGS_FIRST;
			SET_MFW_FIELD(*p_header, QED_MCP_DBG_DATA_HDR_FLAGS,
				      flags);
		}

		p_tmp_buf += QED_MCP_DBG_DATA_MAX_PAYLOAD_SIZE;
		tmp_size -= QED_MCP_DBG_DATA_MAX_PAYLOAD_SIZE;
	}

	/* Last chunk is marked as 'last' */
	flags |= QED_MCP_DBG_DATA_HDR_FLAGS_LAST;
	SET_MFW_FIELD(*p_header, QED_MCP_DBG_DATA_HDR_FLAGS, flags);
	memcpy(p_payload, p_tmp_buf, tmp_size);

	/* Casting the left size to u8 is ok since at this point it is <= 32 */
	return __qed_mcp_send_debug_data(p_hwfn, p_ptt, raw_data,
					 (u8)(QED_MCP_DBG_DATA_MAX_HEADER_SIZE +
					 tmp_size));
}

int
qed_mcp_send_raw_debug_data(struct qed_hwfn *p_hwfn,
			    struct qed_ptt *p_ptt, u8 *p_buf, u32 size)
{
	return qed_mcp_send_debug_data(p_hwfn, p_ptt,
				       QED_MCP_DBG_DATA_TYPE_RAW, p_buf, size);
}
