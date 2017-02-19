/* QLogic qed NIC Driver
 * Copyright (c) 2015 QLogic Corporation
 *
 * This software is available under the terms of the GNU General Public License
 * (GPL) Version 2, available from the file COPYING in the main directory of
 * this source tree.
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

int qed_mcp_free(struct qed_hwfn *p_hwfn)
{
	if (p_hwfn->mcp_info) {
		kfree(p_hwfn->mcp_info->mfw_mb_cur);
		kfree(p_hwfn->mcp_info->mfw_mb_shadow);
	}
	kfree(p_hwfn->mcp_info);

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

	p_info->mcp_hist = (u16)qed_rd(p_hwfn, p_ptt, MISCS_REG_GENERIC_POR_0);

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

	/* Initialize the MFW spinlock */
	spin_lock_init(&p_info->lock);

	return 0;

err:
	qed_mcp_free(p_hwfn);
	return -ENOMEM;
}

/* Locks the MFW mailbox of a PF to ensure a single access.
 * The lock is achieved in most cases by holding a spinlock, causing other
 * threads to wait till a previous access is done.
 * In some cases (currently when a [UN]LOAD_REQ commands are sent), the single
 * access is achieved by setting a blocking flag, which will fail other
 * competing contexts to send their mailboxes.
 */
static int qed_mcp_mb_lock(struct qed_hwfn *p_hwfn, u32 cmd)
{
	spin_lock_bh(&p_hwfn->mcp_info->lock);

	/* The spinlock shouldn't be acquired when the mailbox command is
	 * [UN]LOAD_REQ, since the engine is locked by the MFW, and a parallel
	 * pending [UN]LOAD_REQ command of another PF together with a spinlock
	 * (i.e. interrupts are disabled) - can lead to a deadlock.
	 * It is assumed that for a single PF, no other mailbox commands can be
	 * sent from another context while sending LOAD_REQ, and that any
	 * parallel commands to UNLOAD_REQ can be cancelled.
	 */
	if (cmd == DRV_MSG_CODE_LOAD_DONE || cmd == DRV_MSG_CODE_UNLOAD_DONE)
		p_hwfn->mcp_info->block_mb_sending = false;

	if (p_hwfn->mcp_info->block_mb_sending) {
		DP_NOTICE(p_hwfn,
			  "Trying to send a MFW mailbox command [0x%x] in parallel to [UN]LOAD_REQ. Aborting.\n",
			  cmd);
		spin_unlock_bh(&p_hwfn->mcp_info->lock);
		return -EBUSY;
	}

	if (cmd == DRV_MSG_CODE_LOAD_REQ || cmd == DRV_MSG_CODE_UNLOAD_REQ) {
		p_hwfn->mcp_info->block_mb_sending = true;
		spin_unlock_bh(&p_hwfn->mcp_info->lock);
	}

	return 0;
}

static void qed_mcp_mb_unlock(struct qed_hwfn *p_hwfn, u32 cmd)
{
	if (cmd != DRV_MSG_CODE_LOAD_REQ && cmd != DRV_MSG_CODE_UNLOAD_REQ)
		spin_unlock_bh(&p_hwfn->mcp_info->lock);
}

int qed_mcp_reset(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt)
{
	u32 seq = ++p_hwfn->mcp_info->drv_mb_seq;
	u8 delay = CHIP_MCP_RESP_ITER_US;
	u32 org_mcp_reset_seq, cnt = 0;
	int rc = 0;

	/* Ensure that only a single thread is accessing the mailbox at a
	 * certain time.
	 */
	rc = qed_mcp_mb_lock(p_hwfn, DRV_MSG_CODE_MCP_RESET);
	if (rc != 0)
		return rc;

	/* Set drv command along with the updated sequence */
	org_mcp_reset_seq = qed_rd(p_hwfn, p_ptt, MISCS_REG_GENERIC_POR_0);
	DRV_MB_WR(p_hwfn, p_ptt, drv_mb_header,
		  (DRV_MSG_CODE_MCP_RESET | seq));

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

	qed_mcp_mb_unlock(p_hwfn, DRV_MSG_CODE_MCP_RESET);

	return rc;
}

static int qed_do_mcp_cmd(struct qed_hwfn *p_hwfn,
			  struct qed_ptt *p_ptt,
			  u32 cmd,
			  u32 param,
			  u32 *o_mcp_resp,
			  u32 *o_mcp_param)
{
	u8 delay = CHIP_MCP_RESP_ITER_US;
	u32 seq, cnt = 1, actual_mb_seq;
	int rc = 0;

	/* Get actual driver mailbox sequence */
	actual_mb_seq = DRV_MB_RD(p_hwfn, p_ptt, drv_mb_header) &
			DRV_MSG_SEQ_NUMBER_MASK;

	/* Use MCP history register to check if MCP reset occurred between
	 * init time and now.
	 */
	if (p_hwfn->mcp_info->mcp_hist !=
	    qed_rd(p_hwfn, p_ptt, MISCS_REG_GENERIC_POR_0)) {
		DP_VERBOSE(p_hwfn, QED_MSG_SP, "Rereading MCP offsets\n");
		qed_load_mcp_offsets(p_hwfn, p_ptt);
		qed_mcp_cmd_port_init(p_hwfn, p_ptt);
	}
	seq = ++p_hwfn->mcp_info->drv_mb_seq;

	/* Set drv param */
	DRV_MB_WR(p_hwfn, p_ptt, drv_mb_param, param);

	/* Set drv command along with the updated sequence */
	DRV_MB_WR(p_hwfn, p_ptt, drv_mb_header, (cmd | seq));

	DP_VERBOSE(p_hwfn, QED_MSG_SP,
		   "wrote command (%x) to MFW MB param 0x%08x\n",
		   (cmd | seq), param);

	do {
		/* Wait for MFW response */
		udelay(delay);
		*o_mcp_resp = DRV_MB_RD(p_hwfn, p_ptt, fw_mb_header);

		/* Give the FW up to 5 second (500*10ms) */
	} while ((seq != (*o_mcp_resp & FW_MSG_SEQ_NUMBER_MASK)) &&
		 (cnt++ < QED_DRV_MB_MAX_RETRIES));

	DP_VERBOSE(p_hwfn, QED_MSG_SP,
		   "[after %d ms] read (%x) seq is (%x) from FW MB\n",
		   cnt * delay, *o_mcp_resp, seq);

	/* Is this a reply to our command? */
	if (seq == (*o_mcp_resp & FW_MSG_SEQ_NUMBER_MASK)) {
		*o_mcp_resp &= FW_MSG_CODE_MASK;
		/* Get the MCP param */
		*o_mcp_param = DRV_MB_RD(p_hwfn, p_ptt, fw_mb_param);
	} else {
		/* FW BUG! */
		DP_ERR(p_hwfn, "MFW failed to respond [cmd 0x%x param 0x%x]\n",
		       cmd, param);
		*o_mcp_resp = 0;
		rc = -EAGAIN;
	}
	return rc;
}

static int qed_mcp_cmd_and_union(struct qed_hwfn *p_hwfn,
				 struct qed_ptt *p_ptt,
				 struct qed_mcp_mb_params *p_mb_params)
{
	u32 union_data_addr;

	int rc;

	/* MCP not initialized */
	if (!qed_mcp_is_init(p_hwfn)) {
		DP_NOTICE(p_hwfn, "MFW is not initialized!\n");
		return -EBUSY;
	}

	union_data_addr = p_hwfn->mcp_info->drv_mb_addr +
			  offsetof(struct public_drv_mb, union_data);

	/* Ensure that only a single thread is accessing the mailbox at a
	 * certain time.
	 */
	rc = qed_mcp_mb_lock(p_hwfn, p_mb_params->cmd);
	if (rc)
		return rc;

	if (p_mb_params->p_data_src != NULL)
		qed_memcpy_to(p_hwfn, p_ptt, union_data_addr,
			      p_mb_params->p_data_src,
			      sizeof(*p_mb_params->p_data_src));

	rc = qed_do_mcp_cmd(p_hwfn, p_ptt, p_mb_params->cmd,
			    p_mb_params->param, &p_mb_params->mcp_resp,
			    &p_mb_params->mcp_param);

	if (p_mb_params->p_data_dst != NULL)
		qed_memcpy_from(p_hwfn, p_ptt, p_mb_params->p_data_dst,
				union_data_addr,
				sizeof(*p_mb_params->p_data_dst));

	qed_mcp_mb_unlock(p_hwfn, p_mb_params->cmd);

	return rc;
}

int qed_mcp_cmd(struct qed_hwfn *p_hwfn,
		struct qed_ptt *p_ptt,
		u32 cmd,
		u32 param,
		u32 *o_mcp_resp,
		u32 *o_mcp_param)
{
	struct qed_mcp_mb_params mb_params;
	union drv_union_data data_src;
	int rc;

	memset(&mb_params, 0, sizeof(mb_params));
	memset(&data_src, 0, sizeof(data_src));
	mb_params.cmd = cmd;
	mb_params.param = param;

	/* In case of UNLOAD_DONE, set the primary MAC */
	if ((cmd == DRV_MSG_CODE_UNLOAD_DONE) &&
	    (p_hwfn->cdev->wol_config == QED_OV_WOL_ENABLED)) {
		u8 *p_mac = p_hwfn->cdev->wol_mac;

		data_src.wol_mac.mac_upper = p_mac[0] << 8 | p_mac[1];
		data_src.wol_mac.mac_lower = p_mac[2] << 24 | p_mac[3] << 16 |
					     p_mac[4] << 8 | p_mac[5];

		DP_VERBOSE(p_hwfn,
			   (QED_MSG_SP | NETIF_MSG_IFDOWN),
			   "Setting WoL MAC: %pM --> [%08x,%08x]\n",
			   p_mac, data_src.wol_mac.mac_upper,
			   data_src.wol_mac.mac_lower);

		mb_params.p_data_src = &data_src;
	}

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
	union drv_union_data union_data;
	int rc;

	memset(&mb_params, 0, sizeof(mb_params));
	mb_params.cmd = cmd;
	mb_params.param = param;
	mb_params.p_data_dst = &union_data;
	rc = qed_mcp_cmd_and_union(p_hwfn, p_ptt, &mb_params);
	if (rc)
		return rc;

	*o_mcp_resp = mb_params.mcp_resp;
	*o_mcp_param = mb_params.mcp_param;

	*o_txn_size = *o_mcp_param;
	memcpy(o_buf, &union_data.raw_data, *o_txn_size);

	return 0;
}

int qed_mcp_load_req(struct qed_hwfn *p_hwfn,
		     struct qed_ptt *p_ptt, u32 *p_load_code)
{
	struct qed_dev *cdev = p_hwfn->cdev;
	struct qed_mcp_mb_params mb_params;
	union drv_union_data union_data;
	int rc;

	memset(&mb_params, 0, sizeof(mb_params));
	/* Load Request */
	mb_params.cmd = DRV_MSG_CODE_LOAD_REQ;
	mb_params.param = PDA_COMP | DRV_ID_MCP_HSI_VER_CURRENT |
			  cdev->drv_type;
	memcpy(&union_data.ver_str, cdev->ver_str, MCP_DRV_VER_STR_SIZE);
	mb_params.p_data_src = &union_data;
	rc = qed_mcp_cmd_and_union(p_hwfn, p_ptt, &mb_params);

	/* if mcp fails to respond we must abort */
	if (rc) {
		DP_ERR(p_hwfn, "MCP response failure, aborting\n");
		return rc;
	}

	*p_load_code = mb_params.mcp_resp;

	/* If MFW refused (e.g. other port is in diagnostic mode) we
	 * must abort. This can happen in the following cases:
	 * - Other port is in diagnostic mode
	 * - Previously loaded function on the engine is not compliant with
	 *   the requester.
	 * - MFW cannot cope with the requester's DRV_MFW_HSI_VERSION.
	 *      -
	 */
	if (!(*p_load_code) ||
	    ((*p_load_code) == FW_MSG_CODE_DRV_LOAD_REFUSED_HSI) ||
	    ((*p_load_code) == FW_MSG_CODE_DRV_LOAD_REFUSED_PDA) ||
	    ((*p_load_code) == FW_MSG_CODE_DRV_LOAD_REFUSED_DIAG)) {
		DP_ERR(p_hwfn, "MCP refused load request, aborting\n");
		return -EBUSY;
	}

	return 0;
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
	union drv_union_data union_data;
	int rc;
	int i;

	for (i = 0; i < (VF_MAX_STATIC / 32); i++)
		DP_VERBOSE(p_hwfn, (QED_MSG_SP | QED_MSG_IOV),
			   "Acking VFs [%08x,...,%08x] - %08x\n",
			   i * 32, (i + 1) * 32 - 1, vfs_to_ack[i]);

	memset(&mb_params, 0, sizeof(mb_params));
	mb_params.cmd = DRV_MSG_CODE_VF_DISABLED_DONE;
	memcpy(&union_data.ack_vf_disabled, vfs_to_ack, VF_MAX_STATIC / 8);
	mb_params.p_data_src = &union_data;
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
		return;
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
	qed_configure_vp_wfq_on_link_change(p_hwfn->cdev, p_link->min_pf_rate);

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
}

int qed_mcp_set_link(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt, bool b_up)
{
	struct qed_mcp_link_params *params = &p_hwfn->mcp_info->link_input;
	struct qed_mcp_mb_params mb_params;
	union drv_union_data union_data;
	struct eth_phy_cfg *phy_cfg;
	int rc = 0;
	u32 cmd;

	/* Set the shmem configuration according to params */
	phy_cfg = &union_data.drv_phy_cfg;
	memset(phy_cfg, 0, sizeof(*phy_cfg));
	cmd = b_up ? DRV_MSG_CODE_INIT_PHY : DRV_MSG_CODE_LINK_RESET;
	if (!params->speed.autoneg)
		phy_cfg->speed = params->speed.forced_speed;
	phy_cfg->pause |= (params->pause.autoneg) ? ETH_PAUSE_AUTONEG : 0;
	phy_cfg->pause |= (params->pause.forced_rx) ? ETH_PAUSE_RX : 0;
	phy_cfg->pause |= (params->pause.forced_tx) ? ETH_PAUSE_TX : 0;
	phy_cfg->adv_speed = params->speed.advertised_speeds;
	phy_cfg->loopback_mode = params->loopback_mode;

	p_hwfn->b_drv_link_init = b_up;

	if (b_up) {
		DP_VERBOSE(p_hwfn, NETIF_MSG_LINK,
			   "Configuring Link: Speed 0x%08x, Pause 0x%08x, adv_speed 0x%08x, loopback 0x%08x, features 0x%08x\n",
			   phy_cfg->speed,
			   phy_cfg->pause,
			   phy_cfg->adv_speed,
			   phy_cfg->loopback_mode,
			   phy_cfg->feature_config_flags);
	} else {
		DP_VERBOSE(p_hwfn, NETIF_MSG_LINK,
			   "Resetting link\n");
	}

	memset(&mb_params, 0, sizeof(mb_params));
	mb_params.cmd = cmd;
	mb_params.p_data_src = &union_data;
	rc = qed_mcp_cmd_and_union(p_hwfn, p_ptt, &mb_params);

	/* if mcp fails to respond we must abort */
	if (rc) {
		DP_ERR(p_hwfn, "MCP response failure, aborting\n");
		return rc;
	}

	/* Reset the link status if needed */
	if (!b_up)
		qed_mcp_handle_link_change(p_hwfn, p_ptt, true);

	return 0;
}

static void qed_mcp_send_protocol_stats(struct qed_hwfn *p_hwfn,
					struct qed_ptt *p_ptt,
					enum MFW_DRV_MSG_TYPE type)
{
	enum qed_mcp_protocol_type stats_type;
	union qed_mcp_protocol_stats stats;
	struct qed_mcp_mb_params mb_params;
	union drv_union_data union_data;
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
	memcpy(&union_data, &stats, sizeof(stats));
	mb_params.p_data_src = &union_data;
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
			DP_NOTICE(p_hwfn, "Unimplemented MFW message %d\n", i);
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
		if (qed_mcp_get_shmem_proto_mfw(p_hwfn, p_ptt, p_proto))
			qed_mcp_get_shmem_proto_legacy(p_hwfn, p_proto);
		break;
	case FUNC_MF_CFG_PROTOCOL_ISCSI:
		*p_proto = QED_PCI_ISCSI;
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

int qed_mcp_config_vf_msix(struct qed_hwfn *p_hwfn,
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

int
qed_mcp_send_drv_version(struct qed_hwfn *p_hwfn,
			 struct qed_ptt *p_ptt,
			 struct qed_mcp_drv_version *p_ver)
{
	struct drv_version_stc *p_drv_version;
	struct qed_mcp_mb_params mb_params;
	union drv_union_data union_data;
	__be32 val;
	u32 i;
	int rc;

	p_drv_version = &union_data.drv_version;
	p_drv_version->version = p_ver->version;

	for (i = 0; i < (MCP_DRV_VER_STR_SIZE - 4) / sizeof(u32); i++) {
		val = cpu_to_be32(*((u32 *)&p_ver->name[i * sizeof(u32)]));
		*(__be32 *)&p_drv_version->name[i * sizeof(u32)] = val;
	}

	memset(&mb_params, 0, sizeof(mb_params));
	mb_params.cmd = DRV_MSG_CODE_SET_VERSION;
	mb_params.p_data_src = &union_data;
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
	union drv_union_data union_data;
	int rc;

	memset(&mb_params, 0, sizeof(mb_params));
	mb_params.cmd = DRV_MSG_CODE_SET_VMAC;
	mb_params.param = DRV_MSG_CODE_VMAC_TYPE_MAC <<
			  DRV_MSG_CODE_VMAC_TYPE_SHIFT;
	mb_params.param |= MCP_PF_ID(p_hwfn);
	ether_addr_copy(&union_data.raw_data[0], mac);
	mb_params.p_data_src = &union_data;
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

#define QED_RESC_ALLOC_VERSION_MAJOR    1
#define QED_RESC_ALLOC_VERSION_MINOR    0
#define QED_RESC_ALLOC_VERSION				     \
	((QED_RESC_ALLOC_VERSION_MAJOR <<		     \
	  DRV_MB_PARAM_RESOURCE_ALLOC_VERSION_MAJOR_SHIFT) | \
	 (QED_RESC_ALLOC_VERSION_MINOR <<		     \
	  DRV_MB_PARAM_RESOURCE_ALLOC_VERSION_MINOR_SHIFT))
int qed_mcp_get_resc_info(struct qed_hwfn *p_hwfn,
			  struct qed_ptt *p_ptt,
			  struct resource_info *p_resc_info,
			  u32 *p_mcp_resp, u32 *p_mcp_param)
{
	struct qed_mcp_mb_params mb_params;
	union drv_union_data union_data;
	int rc;

	memset(&mb_params, 0, sizeof(mb_params));
	memset(&union_data, 0, sizeof(union_data));
	mb_params.cmd = DRV_MSG_GET_RESOURCE_ALLOC_MSG;
	mb_params.param = QED_RESC_ALLOC_VERSION;

	/* Need to have a sufficient large struct, as the cmd_and_union
	 * is going to do memcpy from and to it.
	 */
	memcpy(&union_data.resource, p_resc_info, sizeof(*p_resc_info));

	mb_params.p_data_src = &union_data;
	mb_params.p_data_dst = &union_data;
	rc = qed_mcp_cmd_and_union(p_hwfn, p_ptt, &mb_params);
	if (rc)
		return rc;

	/* Copy the data back */
	memcpy(p_resc_info, &union_data.resource, sizeof(*p_resc_info));
	*p_mcp_resp = mb_params.mcp_resp;
	*p_mcp_param = mb_params.mcp_param;

	DP_VERBOSE(p_hwfn,
		   QED_MSG_SP,
		   "MFW resource_info: version 0x%x, res_id 0x%x, size 0x%x, offset 0x%x, vf_size 0x%x, vf_offset 0x%x, flags 0x%x\n",
		   *p_mcp_param,
		   p_resc_info->res_id,
		   p_resc_info->size,
		   p_resc_info->offset,
		   p_resc_info->vf_size,
		   p_resc_info->vf_offset, p_resc_info->flags);

	return 0;
}
