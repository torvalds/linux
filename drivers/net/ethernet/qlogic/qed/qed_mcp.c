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
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/string.h>
#include "qed.h"
#include "qed_hsi.h"
#include "qed_hw.h"
#include "qed_mcp.h"
#include "qed_reg_addr.h"
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

void qed_mcp_cmd_port_init(struct qed_hwfn *p_hwfn,
			   struct qed_ptt *p_ptt)
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

void qed_mcp_read_mb(struct qed_hwfn *p_hwfn,
		     struct qed_ptt *p_ptt)
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

static int qed_load_mcp_offsets(struct qed_hwfn *p_hwfn,
				struct qed_ptt *p_ptt)
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

int qed_mcp_cmd_init(struct qed_hwfn *p_hwfn,
		     struct qed_ptt *p_ptt)
{
	struct qed_mcp_info *p_info;
	u32 size;

	/* Allocate mcp_info structure */
	p_hwfn->mcp_info = kzalloc(sizeof(*p_hwfn->mcp_info), GFP_ATOMIC);
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
	p_info->mfw_mb_cur = kzalloc(size, GFP_ATOMIC);
	p_info->mfw_mb_shadow =
		kzalloc(sizeof(u32) * MFW_DRV_MSG_MAX_DWORDS(
				p_info->mfw_mb_length), GFP_ATOMIC);
	if (!p_info->mfw_mb_shadow || !p_info->mfw_mb_addr)
		goto err;

	/* Initialize the MFW mutex */
	mutex_init(&p_info->mutex);

	return 0;

err:
	DP_NOTICE(p_hwfn, "Failed to allocate mcp memory\n");
	qed_mcp_free(p_hwfn);
	return -ENOMEM;
}

int qed_mcp_reset(struct qed_hwfn *p_hwfn,
		  struct qed_ptt *p_ptt)
{
	u32 seq = ++p_hwfn->mcp_info->drv_mb_seq;
	u8 delay = CHIP_MCP_RESP_ITER_US;
	u32 org_mcp_reset_seq, cnt = 0;
	int rc = 0;

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
		DP_ERR(p_hwfn, "MFW failed to respond!\n");
		*o_mcp_resp = 0;
		rc = -EAGAIN;
	}
	return rc;
}

int qed_mcp_cmd(struct qed_hwfn *p_hwfn,
		struct qed_ptt *p_ptt,
		u32 cmd,
		u32 param,
		u32 *o_mcp_resp,
		u32 *o_mcp_param)
{
	int rc = 0;

	/* MCP not initialized */
	if (!qed_mcp_is_init(p_hwfn)) {
		DP_NOTICE(p_hwfn, "MFW is not initialized !\n");
		return -EBUSY;
	}

	/* Lock Mutex to ensure only single thread is
	 * accessing the MCP at one time
	 */
	mutex_lock(&p_hwfn->mcp_info->mutex);
	rc = qed_do_mcp_cmd(p_hwfn, p_ptt, cmd, param,
			    o_mcp_resp, o_mcp_param);
	/* Release Mutex */
	mutex_unlock(&p_hwfn->mcp_info->mutex);

	return rc;
}

static void qed_mcp_set_drv_ver(struct qed_dev *cdev,
				struct qed_hwfn *p_hwfn,
				struct qed_ptt *p_ptt)
{
	u32 i;

	/* Copy version string to MCP */
	for (i = 0; i < MCP_DRV_VER_STR_SIZE_DWORD; i++)
		DRV_MB_WR(p_hwfn, p_ptt, union_data.ver_str[i],
			  *(u32 *)&cdev->ver_str[i * sizeof(u32)]);
}

int qed_mcp_load_req(struct qed_hwfn *p_hwfn,
		     struct qed_ptt *p_ptt,
		     u32 *p_load_code)
{
	struct qed_dev *cdev = p_hwfn->cdev;
	u32 param;
	int rc;

	if (!qed_mcp_is_init(p_hwfn)) {
		DP_NOTICE(p_hwfn, "MFW is not initialized !\n");
		return -EBUSY;
	}

	/* Save driver's version to shmem */
	qed_mcp_set_drv_ver(cdev, p_hwfn, p_ptt);

	DP_VERBOSE(p_hwfn, QED_MSG_SP, "fw_seq 0x%08x, drv_pulse 0x%x\n",
		   p_hwfn->mcp_info->drv_mb_seq,
		   p_hwfn->mcp_info->drv_pulse_seq);

	/* Load Request */
	rc = qed_mcp_cmd(p_hwfn, p_ptt, DRV_MSG_CODE_LOAD_REQ,
			 (PDA_COMP | DRV_ID_MCP_HSI_VER_CURRENT |
			  cdev->drv_type),
			 p_load_code, &param);

	/* if mcp fails to respond we must abort */
	if (rc) {
		DP_ERR(p_hwfn, "MCP response failure, aborting\n");
		return rc;
	}

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

static void qed_mcp_handle_link_change(struct qed_hwfn *p_hwfn,
				       struct qed_ptt *p_ptt,
				       bool b_reset)
{
	struct qed_mcp_link_state *p_link;
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
				 offsetof(struct public_port,
					  link_status)));
	} else {
		DP_VERBOSE(p_hwfn, NETIF_MSG_LINK,
			   "Resetting link indications\n");
		return;
	}

	p_link->link_up = !!(status & LINK_STATUS_LINK_UP);

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

	/* Correct speed according to bandwidth allocation */
	if (p_hwfn->mcp_info->func_info.bandwidth_max && p_link->speed) {
		p_link->speed = p_link->speed *
				p_hwfn->mcp_info->func_info.bandwidth_max /
				100;
		qed_init_pf_rl(p_hwfn, p_ptt, p_hwfn->rel_pf_id,
			       p_link->speed);
		DP_VERBOSE(p_hwfn, NETIF_MSG_LINK,
			   "Configured MAX bandwidth to be %08x Mb/sec\n",
			   p_link->speed);
	}

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

int qed_mcp_set_link(struct qed_hwfn *p_hwfn,
		     struct qed_ptt *p_ptt,
		     bool b_up)
{
	struct qed_mcp_link_params *params = &p_hwfn->mcp_info->link_input;
	u32 param = 0, reply = 0, cmd;
	struct pmm_phy_cfg phy_cfg;
	int rc = 0;
	u32 i;

	if (!qed_mcp_is_init(p_hwfn)) {
		DP_NOTICE(p_hwfn, "MFW is not initialized !\n");
		return -EBUSY;
	}

	/* Set the shmem configuration according to params */
	memset(&phy_cfg, 0, sizeof(phy_cfg));
	cmd = b_up ? DRV_MSG_CODE_INIT_PHY : DRV_MSG_CODE_LINK_RESET;
	if (!params->speed.autoneg)
		phy_cfg.speed = params->speed.forced_speed;
	phy_cfg.pause |= (params->pause.autoneg) ? PMM_PAUSE_AUTONEG : 0;
	phy_cfg.pause |= (params->pause.forced_rx) ? PMM_PAUSE_RX : 0;
	phy_cfg.pause |= (params->pause.forced_tx) ? PMM_PAUSE_TX : 0;
	phy_cfg.adv_speed = params->speed.advertised_speeds;
	phy_cfg.loopback_mode = params->loopback_mode;

	/* Write the requested configuration to shmem */
	for (i = 0; i < sizeof(phy_cfg); i += 4)
		qed_wr(p_hwfn, p_ptt,
		       p_hwfn->mcp_info->drv_mb_addr +
		       offsetof(struct public_drv_mb, union_data) + i,
		       ((u32 *)&phy_cfg)[i >> 2]);

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

	DP_VERBOSE(p_hwfn, QED_MSG_SP, "fw_seq 0x%08x, drv_pulse 0x%x\n",
		   p_hwfn->mcp_info->drv_mb_seq,
		   p_hwfn->mcp_info->drv_pulse_seq);

	/* Load Request */
	rc = qed_mcp_cmd(p_hwfn, p_ptt, cmd, 0, &reply, &param);

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

int qed_mcp_get_mfw_ver(struct qed_dev *cdev,
			u32 *p_mfw_ver)
{
	struct qed_hwfn *p_hwfn = &cdev->hwfns[0];
	struct qed_ptt *p_ptt;
	u32 global_offsize;

	p_ptt = qed_ptt_acquire(p_hwfn);
	if (!p_ptt)
		return -EBUSY;

	global_offsize = qed_rd(p_hwfn, p_ptt,
				SECTION_OFFSIZE_ADDR(p_hwfn->mcp_info->
						     public_base,
						     PUBLIC_GLOBAL));
	*p_mfw_ver = qed_rd(p_hwfn, p_ptt,
			    SECTION_ADDR(global_offsize, 0) +
			    offsetof(struct public_global, mfw_ver));

	qed_ptt_release(p_hwfn, p_ptt);

	return 0;
}

int qed_mcp_get_media_type(struct qed_dev *cdev,
			   u32 *p_media_type)
{
	struct qed_hwfn *p_hwfn = &cdev->hwfns[0];
	struct qed_ptt  *p_ptt;

	if (!qed_mcp_is_init(p_hwfn)) {
		DP_NOTICE(p_hwfn, "MFW is not initialized !\n");
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

static u32 qed_mcp_get_shmem_func(struct qed_hwfn *p_hwfn,
				  struct qed_ptt *p_ptt,
				  struct public_func *p_data,
				  int pfid)
{
	u32 addr = SECTION_OFFSIZE_ADDR(p_hwfn->mcp_info->public_base,
					PUBLIC_FUNC);
	u32 mfw_path_offsize = qed_rd(p_hwfn, p_ptt, addr);
	u32 func_addr = SECTION_ADDR(mfw_path_offsize, pfid);
	u32 i, size;

	memset(p_data, 0, sizeof(*p_data));

	size = min_t(u32, sizeof(*p_data),
		     QED_SECTION_SIZE(mfw_path_offsize));
	for (i = 0; i < size / sizeof(u32); i++)
		((u32 *)p_data)[i] = qed_rd(p_hwfn, p_ptt,
					    func_addr + (i << 2));

	return size;
}

static int
qed_mcp_get_shmem_proto(struct qed_hwfn *p_hwfn,
			struct public_func *p_info,
			enum qed_pci_personality *p_proto)
{
	int rc = 0;

	switch (p_info->config & FUNC_MF_CFG_PROTOCOL_MASK) {
	case FUNC_MF_CFG_PROTOCOL_ETHERNET:
		*p_proto = QED_PCI_ETH;
		break;
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

	qed_mcp_get_shmem_func(p_hwfn, p_ptt, &shmem_info,
			       MCP_PF_ID(p_hwfn));
	info = &p_hwfn->mcp_info->func_info;

	info->pause_on_host = (shmem_info.config &
			       FUNC_MF_CFG_PAUSE_ON_HOST_RING) ? 1 : 0;

	if (qed_mcp_get_shmem_proto(p_hwfn, &shmem_info,
				    &info->protocol)) {
		DP_ERR(p_hwfn, "Unknown personality %08x\n",
		       (u32)(shmem_info.config & FUNC_MF_CFG_PROTOCOL_MASK));
		return -EINVAL;
	}

	if (p_hwfn->cdev->mf_mode != SF) {
		info->bandwidth_min = (shmem_info.config &
				       FUNC_MF_CFG_MIN_BW_MASK) >>
				      FUNC_MF_CFG_MIN_BW_SHIFT;
		if (info->bandwidth_min < 1 || info->bandwidth_min > 100) {
			DP_INFO(p_hwfn,
				"bandwidth minimum out of bounds [%02x]. Set to 1\n",
				info->bandwidth_min);
			info->bandwidth_min = 1;
		}

		info->bandwidth_max = (shmem_info.config &
				       FUNC_MF_CFG_MAX_BW_MASK) >>
				      FUNC_MF_CFG_MAX_BW_SHIFT;
		if (info->bandwidth_max < 1 || info->bandwidth_max > 100) {
			DP_INFO(p_hwfn,
				"bandwidth maximum out of bounds [%02x]. Set to 100\n",
				info->bandwidth_max);
			info->bandwidth_max = 100;
		}
	}

	if (shmem_info.mac_upper || shmem_info.mac_lower) {
		info->mac[0] = (u8)(shmem_info.mac_upper >> 8);
		info->mac[1] = (u8)(shmem_info.mac_upper);
		info->mac[2] = (u8)(shmem_info.mac_lower >> 24);
		info->mac[3] = (u8)(shmem_info.mac_lower >> 16);
		info->mac[4] = (u8)(shmem_info.mac_lower >> 8);
		info->mac[5] = (u8)(shmem_info.mac_lower);
	} else {
		DP_NOTICE(p_hwfn, "MAC is 0 in shmem\n");
	}

	info->wwn_port = (u64)shmem_info.fcoe_wwn_port_name_upper |
			 (((u64)shmem_info.fcoe_wwn_port_name_lower) << 32);
	info->wwn_node = (u64)shmem_info.fcoe_wwn_node_name_upper |
			 (((u64)shmem_info.fcoe_wwn_node_name_lower) << 32);

	info->ovlan = (u16)(shmem_info.ovlan_stag & FUNC_MF_CFG_OV_STAG_MASK);

	DP_VERBOSE(p_hwfn, (QED_MSG_SP | NETIF_MSG_IFUP),
		   "Read configuration from shmem: pause_on_host %02x protocol %02x BW [%02x - %02x] MAC %02x:%02x:%02x:%02x:%02x:%02x wwn port %llx node %llx ovlan %04x\n",
		info->pause_on_host, info->protocol,
		info->bandwidth_min, info->bandwidth_max,
		info->mac[0], info->mac[1], info->mac[2],
		info->mac[3], info->mac[4], info->mac[5],
		info->wwn_port, info->wwn_node, info->ovlan);

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

int qed_mcp_drain(struct qed_hwfn *p_hwfn,
		  struct qed_ptt *p_ptt)
{
	u32 resp = 0, param = 0;
	int rc;

	rc = qed_mcp_cmd(p_hwfn, p_ptt,
			 DRV_MSG_CODE_NIG_DRAIN, 100,
			 &resp, &param);

	/* Wait for the drain to complete before returning */
	msleep(120);

	return rc;
}

int qed_mcp_get_flash_size(struct qed_hwfn *p_hwfn,
			   struct qed_ptt *p_ptt,
			   u32 *p_flash_size)
{
	u32 flash_size;

	flash_size = qed_rd(p_hwfn, p_ptt, MCP_REG_NVM_CFG4);
	flash_size = (flash_size & MCP_REG_NVM_CFG4_FLASH_SIZE) >>
		      MCP_REG_NVM_CFG4_FLASH_SIZE_SHIFT;
	flash_size = (1 << (flash_size + MCP_BYTES_PER_MBIT_SHIFT));

	*p_flash_size = flash_size;

	return 0;
}

int
qed_mcp_send_drv_version(struct qed_hwfn *p_hwfn,
			 struct qed_ptt *p_ptt,
			 struct qed_mcp_drv_version *p_ver)
{
	int rc = 0;
	u32 param = 0, reply = 0, i;

	if (!qed_mcp_is_init(p_hwfn)) {
		DP_NOTICE(p_hwfn, "MFW is not initialized !\n");
		return -EBUSY;
	}

	DRV_MB_WR(p_hwfn, p_ptt, union_data.drv_version.version,
		  p_ver->version);
	/* Copy version string to shmem */
	for (i = 0; i < (MCP_DRV_VER_STR_SIZE - 4) / 4; i++) {
		DRV_MB_WR(p_hwfn, p_ptt,
			  union_data.drv_version.name[i * sizeof(u32)],
			  *(u32 *)&p_ver->name[i * sizeof(u32)]);
	}

	rc = qed_mcp_cmd(p_hwfn, p_ptt, DRV_MSG_CODE_SET_VERSION, 0, &reply,
			 &param);
	if (rc) {
		DP_ERR(p_hwfn, "MCP response failure, aborting\n");
		return rc;
	}

	return 0;
}
