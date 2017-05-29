/* QLogic qed NIC Driver
 * Copyright (c) 2015-2017  QLogic Corporation
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
 *        disclaimer in the documentation and /or other materials
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
#include "qed_dcbx.h"
#include "qed_hsi.h"
#include "qed_hw.h"
#include "qed_mcp.h"
#include "qed_reg_addr.h"
#include "qed_sriov.h"

#define CHIP_MCP_RESP_ITER_US 10

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

static int qed_load_mcp_offsets(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt)
{
	struct qed_mcp_info *p_info = p_hwfn->mcp_info;
	u32 drv_mb_offsize, mfw_mb_offsize;
	u32 mcp_pf_id = MCP_PF_ID(p_hwfn);

	p_info->public_base = qed_rd(p_hwfn, p_ptt, MISC_REG_SHARED_MEM_ADDR);
	if (!p_info->public_base)
		return 0;

	p_info->public_base |= GRCBASE_MCP;

	/* Calculate the driver and MFW mailbox address */
	drv_mb_offsize = qed_rd(p_hwfn, p_ptt,
				SECTION_OFFSIZE_ADDR(p_info->public_base,
						     PUBLIC_DRV_MB));
	p_info->drv_mb_addr = SECTION_ADDR(drv_mb_offsize, mcp_pf_id);
	DP_VERBOSE(p_hwfn, QED_MSG_SP,
		   "drv_mb_offsiz = 0x%x, drv_mb_addr = 0x%x mcp_pf_id = 0x%x\n",
		   drv_mb_offsize, p_info->drv_mb_addr, mcp_pf_id);

	/* Set the MFW MB address */
	mfw_mb_offsize = qed_rd(p_hwfn, p_ptt,
				SECTION_OFFSIZE_ADDR(p_info->public_base,
						     PUBLIC_MFW_MB));
	p_info->mfw_mb_addr = SECTION_ADDR(mfw_mb_offsize, mcp_pf_id);
	p_info->mfw_mb_length =	(u16)qed_rd(p_hwfn, p_ptt, p_info->mfw_mb_addr);

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
	if (!p_info->mfw_mb_shadow || !p_info->mfw_mb_addr)
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
	u32 org_mcp_reset_seq, seq, delay = CHIP_MCP_RESP_ITER_US, cnt = 0;
	int rc = 0;

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

static int
_qed_mcp_cmd_and_union(struct qed_hwfn *p_hwfn,
		       struct qed_ptt *p_ptt,
		       struct qed_mcp_mb_params *p_mb_params,
		       u32 max_retries, u32 delay)
{
	struct qed_mcp_cmd_elem *p_cmd_elem;
	u32 cnt = 0;
	u16 seq_num;
	int rc = 0;

	/* Wait until the mailbox is non-occupied */
	do {
		/* Exit the loop if there is no pending command, or if the
		 * pending command is completed during this iteration.
		 * The spinlock stays locked until the command is sent.
		 */

		spin_lock_bh(&p_hwfn->mcp_info->cmd_lock);

		if (!qed_mcp_has_pending_cmd(p_hwfn))
			break;

		rc = qed_mcp_update_pending_cmd(p_hwfn, p_ptt);
		if (!rc)
			break;
		else if (rc != -EAGAIN)
			goto err;

		spin_unlock_bh(&p_hwfn->mcp_info->cmd_lock);
		udelay(delay);
	} while (++cnt < max_retries);

	if (cnt >= max_retries) {
		DP_NOTICE(p_hwfn,
			  "The MFW mailbox is occupied by an uncompleted command. Failed to send command 0x%08x [param 0x%08x].\n",
			  p_mb_params->cmd, p_mb_params->param);
		return -EAGAIN;
	}

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

		udelay(delay);
		spin_lock_bh(&p_hwfn->mcp_info->cmd_lock);

		if (p_cmd_elem->b_is_completed)
			break;

		rc = qed_mcp_update_pending_cmd(p_hwfn, p_ptt);
		if (!rc)
			break;
		else if (rc != -EAGAIN)
			goto err;

		spin_unlock_bh(&p_hwfn->mcp_info->cmd_lock);
	} while (++cnt < max_retries);

	if (cnt >= max_retries) {
		DP_NOTICE(p_hwfn,
			  "The MFW failed to respond to command 0x%08x [param 0x%08x].\n",
			  p_mb_params->cmd, p_mb_params->param);

		spin_lock_bh(&p_hwfn->mcp_info->cmd_lock);
		qed_mcp_cmd_del_elem(p_hwfn, p_cmd_elem);
		spin_unlock_bh(&p_hwfn->mcp_info->cmd_lock);

		return -EAGAIN;
	}

	qed_mcp_cmd_del_elem(p_hwfn, p_cmd_elem);
	spin_unlock_bh(&p_hwfn->mcp_info->cmd_lock);

	DP_VERBOSE(p_hwfn,
		   QED_MSG_SP,
		   "MFW mailbox: response 0x%08x param 0x%08x [after %d.%03d ms]\n",
		   p_mb_params->mcp_resp,
		   p_mb_params->mcp_param,
		   (cnt * delay) / 1000, (cnt * delay) % 1000);

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
	u32 delay = CHIP_MCP_RESP_ITER_US;

	/* MCP not initialized */
	if (!qed_mcp_is_init(p_hwfn)) {
		DP_NOTICE(p_hwfn, "MFW is not initialized!\n");
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

	return _qed_mcp_cmd_and_union(p_hwfn, p_ptt, p_mb_params, max_retries,
				      delay);
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

int qed_mcp_unload_req(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt)
{
	u32 wol_param, mcp_resp, mcp_param;

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
		/* Fallthrough */
	case QED_OV_WOL_DEFAULT:
		wol_param = DRV_MB_PARAM_UNLOAD_WOL_MCP;
	}

	return qed_mcp_cmd(p_hwfn, p_ptt, DRV_MSG_CODE_UNLOAD_REQ, wol_param,
			   &mcp_resp, &mcp_param);
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

	if (p_hwfn->b_drv_link_init)
		p_link->link_up = !!(status & LINK_STATUS_LINK_UP);
	else
		p_link->link_up = false;

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
	/* Fall-through */
	case LINK_STATUS_SPEED_AND_DUPLEX_1000TFD:
		p_link->speed = 1000;
		break;
	default:
		p_link->speed = 0;
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

	qed_link_update(p_hwfn);
out:
	spin_unlock_bh(&p_hwfn->mcp_info->link_lock);
}

int qed_mcp_set_link(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt, bool b_up)
{
	struct qed_mcp_link_params *params = &p_hwfn->mcp_info->link_input;
	struct qed_mcp_mb_params mb_params;
	struct eth_phy_cfg phy_cfg;
	int rc = 0;
	u32 cmd;

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

	p_hwfn->b_drv_link_init = b_up;

	if (b_up) {
		DP_VERBOSE(p_hwfn, NETIF_MSG_LINK,
			   "Configuring Link: Speed 0x%08x, Pause 0x%08x, adv_speed 0x%08x, loopback 0x%08x, features 0x%08x\n",
			   phy_cfg.speed,
			   phy_cfg.pause,
			   phy_cfg.adv_speed,
			   phy_cfg.loopback_mode,
			   phy_cfg.feature_config_flags);
	} else {
		DP_VERBOSE(p_hwfn, NETIF_MSG_LINK,
			   "Resetting link\n");
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

static void qed_read_pf_bandwidth(struct qed_hwfn *p_hwfn,
				  struct public_func *p_shmem_info)
{
	struct qed_mcp_function_info *p_info;

	p_info = &p_hwfn->mcp_info->func_info;

	p_info->bandwidth_min = (p_shmem_info->config &
				 FUNC_MF_CFG_MIN_BW_MASK) >>
					FUNC_MF_CFG_MIN_BW_SHIFT;
	if (p_info->bandwidth_min < 1 || p_info->bandwidth_min > 100) {
		DP_INFO(p_hwfn,
			"bandwidth minimum out of bounds [%02x]. Set to 1\n",
			p_info->bandwidth_min);
		p_info->bandwidth_min = 1;
	}

	p_info->bandwidth_max = (p_shmem_info->config &
				 FUNC_MF_CFG_MAX_BW_MASK) >>
					FUNC_MF_CFG_MAX_BW_SHIFT;
	if (p_info->bandwidth_max < 1 || p_info->bandwidth_max > 100) {
		DP_INFO(p_hwfn,
			"bandwidth maximum out of bounds [%02x]. Set to 100\n",
			p_info->bandwidth_max);
		p_info->bandwidth_max = 100;
	}
}

static u32 qed_mcp_get_shmem_func(struct qed_hwfn *p_hwfn,
				  struct qed_ptt *p_ptt,
				  struct public_func *p_data, int pfid)
{
	u32 addr = SECTION_OFFSIZE_ADDR(p_hwfn->mcp_info->public_base,
					PUBLIC_FUNC);
	u32 mfw_path_offsize = qed_rd(p_hwfn, p_ptt, addr);
	u32 func_addr = SECTION_ADDR(mfw_path_offsize, pfid);
	u32 i, size;

	memset(p_data, 0, sizeof(*p_data));

	size = min_t(u32, sizeof(*p_data), QED_SECTION_SIZE(mfw_path_offsize));
	for (i = 0; i < size / sizeof(u32); i++)
		((u32 *)p_data)[i] = qed_rd(p_hwfn, p_ptt,
					    func_addr + (i << 2));
	return size;
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
		case MFW_DRV_MSG_TRANSCEIVER_STATE_CHANGE:
			qed_mcp_handle_transceiver_change(p_hwfn, p_ptt);
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

int qed_mcp_get_media_type(struct qed_dev *cdev, u32 *p_media_type)
{
	struct qed_hwfn *p_hwfn = &cdev->hwfns[0];
	struct qed_ptt  *p_ptt;

	if (IS_VF(cdev))
		return -EINVAL;

	if (!qed_mcp_is_init(p_hwfn)) {
		DP_NOTICE(p_hwfn, "MFW is not initialized!\n");
		return -EBUSY;
	}

	*p_media_type = MEDIA_UNSPECIFIED;

	p_ptt = qed_ptt_acquire(p_hwfn);
	if (!p_ptt)
		return -EBUSY;

	*p_media_type = qed_rd(p_hwfn, p_ptt, p_hwfn->mcp_info->port_addr +
			       offsetof(struct public_port, media_type));

	qed_ptt_release(p_hwfn, p_ptt);

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
	case FW_MB_PARAM_GET_PF_RDMA_BOTH:
		DP_NOTICE(p_hwfn,
			  "Current day drivers don't support RoCE & iWARP. Default to RoCE-only\n");
		*p_proto = QED_PCI_ETH_ROCE;
		break;
	case FW_MB_PARAM_GET_PF_RDMA_IWARP:
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
	case FUNC_MF_CFG_PROTOCOL_FCOE:
		*p_proto = QED_PCI_FCOE;
		break;
	case FUNC_MF_CFG_PROTOCOL_ROCE:
		DP_NOTICE(p_hwfn, "RoCE personality is not a valid value!\n");
	/* Fallthrough */
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

	info->wwn_port = (u64)shmem_info.fcoe_wwn_port_name_upper |
			 (((u64)shmem_info.fcoe_wwn_port_name_lower) << 32);
	info->wwn_node = (u64)shmem_info.fcoe_wwn_node_name_upper |
			 (((u64)shmem_info.fcoe_wwn_node_name_lower) << 32);

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
		   "Read configuration from shmem: pause_on_host %02x protocol %02x BW [%02x - %02x] MAC %02x:%02x:%02x:%02x:%02x:%02x wwn port %llx node %llx ovlan %04x wol %02x\n",
		info->pause_on_host, info->protocol,
		info->bandwidth_min, info->bandwidth_max,
		info->mac[0], info->mac[1], info->mac[2],
		info->mac[3], info->mac[4], info->mac[5],
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

int qed_mcp_halt(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt)
{
	u32 resp = 0, param = 0;
	int rc;

	rc = qed_mcp_cmd(p_hwfn, p_ptt, DRV_MSG_CODE_MCP_HALT, 0, &resp,
			 &param);
	if (rc)
		DP_ERR(p_hwfn, "MCP response failure, aborting\n");

	return rc;
}

int qed_mcp_resume(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt)
{
	u32 value, cpu_mode;

	qed_wr(p_hwfn, p_ptt, MCP_REG_CPU_STATE, 0xffffffff);

	value = qed_rd(p_hwfn, p_ptt, MCP_REG_CPU_MODE);
	value &= ~MCP_REG_CPU_MODE_SOFT_HALT;
	qed_wr(p_hwfn, p_ptt, MCP_REG_CPU_MODE, value);
	cpu_mode = qed_rd(p_hwfn, p_ptt, MCP_REG_CPU_MODE);

	return (cpu_mode & MCP_REG_CPU_MODE_SOFT_HALT) ? -EAGAIN : 0;
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
					 DRV_MB_PARAM_NVM_LEN_SHIFT),
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

int qed_mcp_bist_nvm_test_get_num_images(struct qed_hwfn *p_hwfn,
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

int qed_mcp_bist_nvm_test_get_image_att(struct qed_hwfn *p_hwfn,
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
	case QED_LL2_QUEUE:
		mfw_res_id = RESOURCE_LL2_QUEUE_E;
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
		/* Fallthrough */
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

int
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
		/* Fallthrough */
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
