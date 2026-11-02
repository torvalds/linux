// SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause)
/* Copyright 2017-2019 NXP */

#include "enetc_pf_common.h"

#define ENETC_PF_MSG_SUCCESS	FIELD_PREP(ENETC_PF_MSG_CLASS_ID, \
					   ENETC_MSG_CLASS_ID_CMD_SUCCESS)
#define ENETC_PF_MSG_NOTSUPP	FIELD_PREP(ENETC_PF_MSG_CLASS_ID, \
					   ENETC_MSG_CLASS_ID_CMD_NOT_SUPPORT)
#define ENETC_PF_MSG_PERM_DENY	FIELD_PREP(ENETC_PF_MSG_CLASS_ID, \
					   ENETC_MSG_CLASS_ID_PERMISSION_DENY)
#define ENETC_PF_MSG_INV_LEN	FIELD_PREP(ENETC_PF_MSG_CLASS_ID, \
					   ENETC_MSG_CLASS_ID_INVALID_MSG_LEN)
#define ENETC_PF_MSG_MF(code)	(FIELD_PREP(ENETC_PF_MSG_CLASS_ID, \
					    ENETC_MSG_CLASS_ID_MAC_FILTER) | \
				 FIELD_PREP(ENETC_PF_MSG_CLASS_CODE, (code)))

static void enetc_disable_psiier_interrupts(struct enetc_pf *pf)
{
	struct enetc_hw *hw = &pf->si->hw;

	enetc_wr(hw, ENETC_PSIIER, 0);
}

static void enetc_enable_psiier_interrupts(struct enetc_pf *pf)
{
	u32 psiier = ENETC_PSIMR_MASK(pf->num_vfs);
	struct enetc_hw *hw = &pf->si->hw;

	if (pf->ops->vf_flr_handler)
		psiier |= ENETC_VFFLR_MASK(pf->num_vfs);

	enetc_wr(hw, ENETC_PSIIER, psiier);
}

static irqreturn_t enetc_msg_psi_msix(int irq, void *data)
{
	struct enetc_si *si = (struct enetc_si *)data;
	struct enetc_pf *pf = enetc_si_priv(si);

	enetc_disable_psiier_interrupts(pf);
	schedule_work(&si->msg_task);

	return IRQ_HANDLED;
}

/* Messaging */
static bool enetc_msg_check_crc16(void *msg_addr, u32 msg_size)
{
	u32 data_size = msg_size - 2;
	u8 *data_buf = msg_addr + 2;
	u16 verify_val;

	verify_val = crc_itu_t(ENETC_CRC_INIT, data_buf, data_size);
	verify_val = crc_itu_t(verify_val, msg_addr, 2);
	if (verify_val)
		return false;

	return true;
}

static u16 enetc_msg_set_vf_primary_mac_addr(struct enetc_pf *pf, int vf_id,
					     void *vf_msg)
{
	struct enetc_vf_state *vf_state = &pf->vf_state[vf_id];
	struct enetc_msg_mac_exact_filter *msg = vf_msg;
	struct device *dev = &pf->si->pdev->dev;
	u16 pf_msg = ENETC_PF_MSG_SUCCESS;
	char *addr = msg->mac[0].addr;

	mutex_lock(&vf_state->lock);

	/* Untrusted VFs cannot set their MAC addresses by the mailbox
	 * messages.
	 */
	if (!(vf_state->flags & ENETC_VF_FLAG_TRUSTED)) {
		pf_msg = ENETC_PF_MSG_PERM_DENY;
		goto vf_state_unlock;
	}

	if (!is_valid_ether_addr(addr)) {
		dev_err_ratelimited(dev, "VF%d attempted to set invalid MAC\n",
				    vf_id);
		pf_msg = ENETC_PF_MSG_MF(ENETC_MF_CLASS_CODE_INVALID_MAC);
		goto vf_state_unlock;
	}

	/* PF has higher privileges. If PF has already modified the MAC
	 * address for VF through .ndo_set_vf_mac() interface, VF is not
	 * allowed to set its MAC address via mailbox messages, even if
	 * it is trusted.
	 */
	if (vf_state->flags & ENETC_VF_FLAG_PF_SET_MAC) {
		dev_err_ratelimited(dev,
				    "VF%d attempted to override PF set MAC\n",
				    vf_id);
		pf_msg = FIELD_PREP(ENETC_PF_MSG_CLASS_ID,
				    ENETC_MSG_CLASS_ID_CMD_NOT_PERMITTED);
		goto vf_state_unlock;
	}

	enetc_set_si_hw_addr(pf, vf_id + 1, addr);

vf_state_unlock:
	mutex_unlock(&vf_state->lock);

	return pf_msg;
}

static u16 enetc_msg_set_vf_mac_hash_filter(struct enetc_pf *pf, int vf_id,
					    void *vf_msg)
{
	struct enetc_vf_state *vf_state = &pf->vf_state[vf_id];
	struct enetc_msg_mac_hash_filter *msg = vf_msg;
	u16 pf_msg = ENETC_PF_MSG_SUCCESS;
	struct enetc_si *si = pf->si;
	int si_id = vf_id + 1;
	u64 uc_hash, mc_hash;
	bool trusted;
	int type;

	/* Currently, hardware only supports 64 bits table size */
	if (FIELD_GET(ENETC_MSG_MAC_HASH_SIZE, msg->sz_type) !=
	    ENETC_MAC_HASH_TABLE_SIZE_64)
		return ENETC_PF_MSG_NOTSUPP;

	mutex_lock(&vf_state->lock);

	/* For an untrusted VF, unicast MAC hash filtering is not permitted.
	 * For multicast, the MAC hash filter is strictly limited to a maximum
	 * of 8 bits to satisfy its basic multicast communication requirements
	 * while preventing potential network abuse.
	 */
	trusted = !!(vf_state->flags & ENETC_VF_FLAG_TRUSTED);
	type = FIELD_GET(ENETC_MSG_MAC_TYPE, msg->sz_type);
	switch (type) {
	case ENETC_MAC_FILTER_TYPE_UC:
		if (!trusted) {
			pf_msg = ENETC_PF_MSG_PERM_DENY;
			goto vf_state_unlock;
		}

		uc_hash = (u64)msg->hash_tbl[1] << 32 | msg->hash_tbl[0];
		enetc_set_si_uc_hash_filter(si, si_id, uc_hash);
		break;
	case ENETC_MAC_FILTER_TYPE_MC:
		mc_hash = (u64)msg->hash_tbl[1] << 32 | msg->hash_tbl[0];
		if (!trusted &&
		    hweight64(mc_hash) > ENETC_VF_MC_HASH_BITS_MAX) {
			pf_msg = ENETC_PF_MSG_PERM_DENY;
			goto vf_state_unlock;
		}

		enetc_set_si_mc_hash_filter(si, si_id, mc_hash);
		break;
	case ENETC_MAC_FILTER_TYPE_ALL:
		if (!msg->hdr.len) {
			pf_msg = ENETC_PF_MSG_INV_LEN;
			goto vf_state_unlock;
		}

		uc_hash = (u64)msg->hash_tbl[1] << 32 | msg->hash_tbl[0];
		mc_hash = (u64)msg->hash_tbl[3] << 32 | msg->hash_tbl[2];

		if (!trusted &&
		    (hweight64(mc_hash) <= ENETC_VF_MC_HASH_BITS_MAX)) {
			enetc_set_si_mc_hash_filter(si, si_id, mc_hash);
			pf_msg = ENETC_PF_MSG_MF(ENETC_MF_CLASS_CODE_UCF_DENY);
			goto vf_state_unlock;
		}

		if (!trusted) {
			pf_msg = ENETC_PF_MSG_PERM_DENY;
			goto vf_state_unlock;
		}

		enetc_set_si_uc_hash_filter(si, si_id, uc_hash);
		enetc_set_si_mc_hash_filter(si, si_id, mc_hash);
		break;
	default:
		pf_msg = ENETC_PF_MSG_MF(ENETC_MF_CLASS_CODE_INVALID_TYPE);
	}

vf_state_unlock:
	mutex_unlock(&vf_state->lock);

	return pf_msg;
}

static u16 enetc_msg_set_vf_mac_promisc_mode(struct enetc_pf *pf, int vf_id,
					     void *vf_msg)
{
	struct enetc_vf_state *vf_state = &pf->vf_state[vf_id];
	struct enetc_msg_mac_promisc_mode *msg = vf_msg;
	u16 pf_msg = ENETC_PF_MSG_SUCCESS;
	struct enetc_si *si = pf->si;
	bool promisc, flush_macs;
	int si_id = vf_id + 1;
	int type;

	flush_macs = !!(msg->config & ENETC_MSG_MAC_FLUSH_MACS);
	type = FIELD_GET(ENETC_MSG_MAC_TYPE, msg->config);
	if (!type)
		return ENETC_PF_MSG_MF(ENETC_MF_CLASS_CODE_INVALID_TYPE);

	mutex_lock(&vf_state->lock);

	promisc = !!(msg->config & ENETC_MSG_MAC_PROMISC_MODE);
	if (promisc && !(vf_state->flags & ENETC_VF_FLAG_TRUSTED)) {
		pf_msg = ENETC_PF_MSG_PERM_DENY;
		goto vf_state_unlock;
	}

	if (type & ENETC_MAC_FILTER_TYPE_UC) {
		if (promisc)
			vf_state->flags |= ENETC_VF_FLAG_UC_PROMISC;
		else
			vf_state->flags &= ~ENETC_VF_FLAG_UC_PROMISC;
	}

	if (type & ENETC_MAC_FILTER_TYPE_MC) {
		if (promisc)
			vf_state->flags |= ENETC_VF_FLAG_MC_PROMISC;
		else
			vf_state->flags &= ~ENETC_VF_FLAG_MC_PROMISC;
	}

	spin_lock(&si->gen_lock);
	if (type & ENETC_MAC_FILTER_TYPE_UC)
		enetc_set_si_uc_promisc(si, si_id, promisc);

	if (type & ENETC_MAC_FILTER_TYPE_MC)
		enetc_set_si_mc_promisc(si, si_id, promisc);
	spin_unlock(&si->gen_lock);

	if ((type & ENETC_MAC_FILTER_TYPE_UC) && flush_macs)
		enetc_set_si_uc_hash_filter(si, si_id, 0);

	if ((type & ENETC_MAC_FILTER_TYPE_MC) && flush_macs)
		enetc_set_si_mc_hash_filter(si, si_id, 0);

vf_state_unlock:
	mutex_unlock(&vf_state->lock);

	return pf_msg;
}

static u16 enetc_msg_handle_mac_filter(struct enetc_pf *pf, int vf_id,
				       void *vf_msg)
{
	struct enetc_msg_header *msg_hdr = vf_msg;

	switch (msg_hdr->cmd_id) {
	case ENETC_MSG_SET_PRIMARY_MAC:
		return enetc_msg_set_vf_primary_mac_addr(pf, vf_id, vf_msg);
	case ENETC_MSG_SET_MAC_HASH_TABLE:
		return enetc_msg_set_vf_mac_hash_filter(pf, vf_id, vf_msg);
	case ENETC_MSG_SET_MAC_PROMISC_MODE:
		return enetc_msg_set_vf_mac_promisc_mode(pf, vf_id, vf_msg);
	default:
		return ENETC_PF_MSG_NOTSUPP;
	}
}

static u16 enetc_msg_handle_ip_revision(struct enetc_pf *pf, void *vf_msg)
{
	struct enetc_msg_header *msg_hdr = vf_msg;

	switch (msg_hdr->cmd_id) {
	case ENETC_MSG_GET_IP_MN:
		return (FIELD_PREP(ENETC_PF_MSG_CLASS_ID,
				   ENETC_MSG_CLASS_ID_IP_REVISION) |
			FIELD_PREP(ENETC_PF_MSG_CLASS_CODE_U8,
				   pf->si->revision));
	default:
		return ENETC_PF_MSG_NOTSUPP;
	}
}

static void enetc_pf_reply_msg(struct enetc_hw *hw, int vf_id, u16 pf_msg)
{
	/* w1c to clear the corresponding VF MR bit */
	enetc_wr(hw, ENETC_PSIIDR, ENETC_PSIMR_BIT(vf_id));
	enetc_wr(hw, ENETC_PSIMSGRR, ENETC_SIMSGSR_SET_MC(pf_msg) |
		 ENETC_PSIMR_BIT(vf_id));
}

static u16 enetc_build_link_status_msg(struct enetc_ndev_priv *priv,
				       bool link_up)
{
	u8 status = 0;

	if (link_up) {
		if (test_bit(ENETC_RXBDR_CM, &priv->flags))
			status |= ENETC_CLASS_CODE_TX_PAUSE_EN;
	} else {
		status |= ENETC_CLASS_CODE_LINK_DOWN;
	}

	return FIELD_PREP(ENETC_PF_MSG_CLASS_ID,
			  ENETC_MSG_CLASS_ID_LINK_STATUS) |
	       FIELD_PREP(ENETC_PF_MSG_CLASS_CODE_U8, status);
}

static void enetc_msg_get_link_status(struct enetc_pf *pf, int vf_id)
{
	struct enetc_ndev_priv *priv = netdev_priv(pf->si->ndev);
	struct enetc_si *si = pf->si;
	u16 pf_msg;

	spin_lock(&si->gen_lock);
	pf_msg = enetc_build_link_status_msg(priv, pf->link_up);
	enetc_pf_reply_msg(&si->hw, vf_id, pf_msg);
	spin_unlock(&si->gen_lock);
}

static void enetc_msg_register_link_status_notifier(struct enetc_pf *pf,
						    int vf_id)
{
	struct enetc_si *si = pf->si;

	spin_lock(&si->gen_lock);
	enetc_pf_reply_msg(&si->hw, vf_id, ENETC_PF_MSG_SUCCESS);

	/* SR-IOV is being disabled if pf->sriov_enabled is false, so no
	 * need to set link_status_ms_mask and notify the link status.
	 */
	if (!pf->sriov_enabled) {
		spin_unlock(&si->gen_lock);
		return;
	}

	pf->link_status_ms_mask |= PSIMSGSR_MS(vf_id);
	spin_unlock(&si->gen_lock);

	/* Notify VF the current link status */
	queue_work(si->workqueue, &pf->link_status_task);
}

static void enetc_msg_unregister_link_status_notifier(struct enetc_pf *pf,
						      int vf_id)
{
	spin_lock(&pf->si->gen_lock);
	pf->link_status_ms_mask &= ~PSIMSGSR_MS(vf_id);
	enetc_pf_reply_msg(&pf->si->hw, vf_id, ENETC_PF_MSG_SUCCESS);
	spin_unlock(&pf->si->gen_lock);
}

static u16 enetc_msg_handle_link_status(struct enetc_pf *pf, int vf_id,
					void *vf_msg)
{
	struct enetc_msg_header *msg_hdr = vf_msg;

	switch (msg_hdr->cmd_id) {
	case ENETC_MSG_GET_CURRENT_LINK_STATUS:
		/* Currently, this message is intended only for
		 * DPDK-owned VFs.
		 */
		enetc_msg_get_link_status(pf, vf_id);
		break;
	case ENETC_MSG_REGISTER_LINK_CHANGE_NOTIFIER:
		enetc_msg_register_link_status_notifier(pf, vf_id);
		break;
	case ENETC_MSG_UNREGISTER_LINK_CHANGE_NOTIFIER:
		enetc_msg_unregister_link_status_notifier(pf, vf_id);
		break;
	default:
		return ENETC_PF_MSG_NOTSUPP;
	}

	return 0;
}

static u16 enetc_build_link_speed_msg(int speed, int duplex)
{
	u32 speed_code = ENETC_MSG_SPEED_UNKNOWN;

	switch (speed) {
	case SPEED_10:
		if (duplex == DUPLEX_HALF)
			speed_code = ENETC_MSG_SPEED_10M_HD;
		else if (duplex == DUPLEX_FULL)
			speed_code = ENETC_MSG_SPEED_10M_FD;
		break;
	case SPEED_100:
		if (duplex == DUPLEX_HALF)
			speed_code = ENETC_MSG_SPEED_100M_HD;
		else if (duplex == DUPLEX_FULL)
			speed_code = ENETC_MSG_SPEED_100M_FD;
		break;
	case SPEED_1000:
		speed_code = ENETC_MSG_SPEED_1000M;
		break;
	case SPEED_2500:
		speed_code = ENETC_MSG_SPEED_2500M;
		break;
	case SPEED_5000:
		speed_code = ENETC_MSG_SPEED_5G;
		break;
	default:
		if (speed < SPEED_5000)
			break;

		speed_code = (speed - SPEED_5000) / SPEED_1000 +
			     ENETC_MSG_SPEED_5G;
		if (speed_code > ENETC_MSG_SPEED_MAX)
			speed_code = ENETC_MSG_SPEED_UNKNOWN;
	}

	return FIELD_PREP(ENETC_PF_MSG_CLASS_ID,
			  ENETC_MSG_CLASS_ID_LINK_SPEED) |
	       FIELD_PREP(ENETC_PF_MSG_CLASS_CODE_U8, speed_code);
}

static u16 enetc_msg_get_link_speed(struct enetc_pf *pf, int vf_id)
{
	struct enetc_ndev_priv *priv = netdev_priv(pf->si->ndev);
	struct enetc_vf_state *vf_state = &pf->vf_state[vf_id];
	struct ethtool_link_ksettings link_info = {};

	/* A malicious or malfunctioning VM could potentially spam these
	 * messages in a tight loop causing global rtnl_lock contention,
	 * which may severely starve other processes on the host that
	 * require rtnl_lock for routine network configuration, resulting
	 * in a system-wide control-plane denial of service. Therefore,
	 * we expect the VF query for link speed to be trusted. There's no
	 * need to consider the transition from trusted to untrusted here,
	 * as this won't cause rtnl_lock() to be called frequently.
	 */
	mutex_lock(&vf_state->lock);
	if (!(vf_state->flags & ENETC_VF_FLAG_TRUSTED)) {
		mutex_unlock(&vf_state->lock);

		return ENETC_PF_MSG_PERM_DENY;
	}
	mutex_unlock(&vf_state->lock);

	rtnl_lock();
	phylink_ethtool_ksettings_get(priv->phylink, &link_info);
	rtnl_unlock();

	return enetc_build_link_speed_msg(link_info.base.speed,
					  link_info.base.duplex);
}

static u16 enetc_msg_handle_link_speed(struct enetc_pf *pf, int vf_id,
				       void *vf_msg)
{
	struct enetc_msg_header *msg_hdr = vf_msg;

	switch (msg_hdr->cmd_id) {
	case ENETC_MSG_GET_CURRENT_LINK_SPEED:
		return enetc_msg_get_link_speed(pf, vf_id);
	case ENETC_MSG_REGISTER_SPEED_CHANGE_NOTIFIER:
	case ENETC_MSG_UNREGISTER_SPEED_CHANGE_NOTIFIER:
	default:
		return ENETC_PF_MSG_NOTSUPP;
	}
}

/* If *pf_msg is set to 0, it means that PF has responded to VF in
 * enetc_msg_handle_rxmsg() through enetc_pf_reply_msg(), which also
 * clears the corresponding VF MR bit in PSIIDR.
 */
static void enetc_msg_handle_rxmsg(struct enetc_pf *pf, int vf_id,
				   u16 *pf_msg)
{
	struct enetc_msg_swbd *msg_swbd = &pf->rxmsg[vf_id];
	struct enetc_msg_header *msg_hdr = msg_swbd->vaddr;
	u32 msg_size = ENETC_MSG_SIZE(msg_hdr->len);
	struct device *dev = &pf->si->pdev->dev;
	u8 *msg;

	if (msg_size > ENETC_DEFAULT_MSG_SIZE) {
		dev_err_ratelimited(dev,
				    "Invalid message size: %u\n", msg_size);
		*pf_msg = ENETC_PF_MSG_INV_LEN;
		return;
	}

	/* To prevent malicious VF from tampering with the original data by
	 * sending new messages after passing the check, the DMA buffer data
	 * is copied to the msg buffer before validation.
	 */
	msg = kzalloc_objs(*msg, msg_size);
	if (!msg) {
		dev_err_ratelimited(dev,
				    "Failed to allocate message buffer\n");
		*pf_msg = FIELD_PREP(ENETC_PF_MSG_CLASS_ID,
				     ENETC_MSG_CLASS_ID_CMD_FAIL);
		return;
	}

	memcpy(msg, msg_swbd->vaddr, msg_size);
	msg_hdr = (struct enetc_msg_header *)msg;

	/* Check message length whether is changed */
	if (ENETC_MSG_SIZE(msg_hdr->len) != msg_size) {
		*pf_msg = ENETC_PF_MSG_INV_LEN;
		goto free_msg;
	}

	if (!enetc_msg_check_crc16(msg, msg_size)) {
		dev_err_ratelimited(dev, "VSI to PSI Message CRC16 error\n");
		*pf_msg = FIELD_PREP(ENETC_PF_MSG_CLASS_ID,
				     ENETC_MSG_CLASS_ID_CRC_ERROR);

		goto free_msg;
	}

	/* Default to not supported */
	*pf_msg = ENETC_PF_MSG_NOTSUPP;

	/* Currently, asynchronous actions are not supported */
	if (FIELD_GET(ENETC_VF_MSG_COOKIE, msg_hdr->cookie)) {
		dev_err_ratelimited(dev,
				    "Cookie field is not supported yet\n");
		goto free_msg;
	}

	/* Currently only support protocol version 0 */
	if (msg_hdr->proto_ver) {
		dev_err_ratelimited(dev, "Unsupported protocol version %u\n",
				    msg_hdr->proto_ver);
		goto free_msg;
	}

	/* The new messages are currently only supported on ENETC v4. If v1
	 * requires them, the current restriction can be lifted.
	 */
	if (is_enetc_rev1(pf->si) &&
	    !(msg_hdr->class_id == ENETC_MSG_CLASS_ID_MAC_FILTER &&
	      msg_hdr->cmd_id == ENETC_MSG_SET_PRIMARY_MAC)) {
		dev_err_ratelimited(dev, "Unsupported message for ENETC v1\n");

		goto free_msg;
	}

	switch (msg_hdr->class_id) {
	case ENETC_MSG_CLASS_ID_MAC_FILTER:
		*pf_msg = enetc_msg_handle_mac_filter(pf, vf_id, msg);
		break;
	case ENETC_MSG_CLASS_ID_IP_REVISION:
		*pf_msg = enetc_msg_handle_ip_revision(pf, msg);
		break;
	case ENETC_MSG_CLASS_ID_LINK_STATUS:
		*pf_msg = enetc_msg_handle_link_status(pf, vf_id, msg);
		break;
	case ENETC_MSG_CLASS_ID_LINK_SPEED:
		*pf_msg = enetc_msg_handle_link_speed(pf, vf_id, msg);
		break;
	default:
		dev_err_ratelimited(dev,
				    "Unsupported message class ID: 0x%x\n",
				    msg_hdr->class_id);
	}

free_msg:
	kfree(msg);
}

static void enetc_vf_flr_handler(struct enetc_pf *pf)
{
	u32 flr_mask = ENETC_VFFLR_MASK(pf->num_vfs);
	struct enetc_hw *hw = &pf->si->hw;
	u32 flr_status;

	if (!pf->ops->vf_flr_handler)
		return;

	flr_status = enetc_rd(hw, ENETC_PSIIDR) & flr_mask;
	if (!flr_status)
		return;

	for (int i = 0; i < pf->num_vfs; i++) {
		if (!(ENETC_VFFLR_BIT(i) & flr_status))
			continue;

		/* Clear FLR interrupt status, W1C */
		enetc_wr(hw, ENETC_PSIIDR, ENETC_VFFLR_BIT(i));
		pf->ops->vf_flr_handler(pf, i);
	}
}

static void enetc_msg_task(struct work_struct *work)
{
	struct enetc_si *si = container_of(work, struct enetc_si, msg_task);
	struct enetc_pf *pf = enetc_si_priv(si);
	struct enetc_hw *hw = &si->hw;
	u32 mr_status, mr_mask;
	int i;

	enetc_vf_flr_handler(pf);

	mr_mask = ENETC_PSIMR_MASK(pf->num_vfs);
	mr_status = (enetc_rd(hw, ENETC_PSIMSGRR) & mr_mask) |
		    (enetc_rd(hw, ENETC_PSIIDR) & mr_mask);
	if (!mr_status)
		goto out;

	for (i = 0; i < pf->num_vfs; i++) {
		u16 msg_code;

		if (!(ENETC_PSIMR_BIT(i) & mr_status))
			continue;

		enetc_msg_handle_rxmsg(pf, i, &msg_code);

		/* If msg_code is 0, it means that PF has responded to VF
		 * in enetc_msg_handle_rxmsg() through enetc_pf_reply_msg(),
		 * which also clears the corresponding VF MR bit in PSIIDR.
		 */
		if (!msg_code)
			continue;

		enetc_pf_reply_msg(hw, i, msg_code);
	}

out:
	enetc_enable_psiier_interrupts(pf);
}

/* Init */
static int enetc_msg_alloc_mbx(struct enetc_si *si, int idx)
{
	struct enetc_pf *pf = enetc_si_priv(si);
	struct device *dev = &si->pdev->dev;
	struct enetc_hw *hw = &si->hw;
	struct enetc_msg_swbd *msg;
	u32 val;

	msg = &pf->rxmsg[idx];
	/* allocate and set receive buffer */
	msg->size = ENETC_DEFAULT_MSG_SIZE;

	msg->vaddr = dma_alloc_coherent(dev, msg->size, &msg->dma,
					GFP_KERNEL);
	if (!msg->vaddr) {
		dev_err(dev, "msg: fail to alloc dma buffer of size: %d\n",
			msg->size);
		return -ENOMEM;
	}

	/* set multiple of 32 bytes */
	val = lower_32_bits(msg->dma);
	enetc_wr(hw, ENETC_PSIVMSGRCVAR0(idx), val);
	val = upper_32_bits(msg->dma);
	enetc_wr(hw, ENETC_PSIVMSGRCVAR1(idx), val);

	return 0;
}

static void enetc_msg_free_mbx(struct enetc_si *si, int idx)
{
	struct enetc_pf *pf = enetc_si_priv(si);
	struct enetc_hw *hw = &si->hw;
	struct enetc_msg_swbd *msg;

	enetc_wr(hw, ENETC_PSIVMSGRCVAR0(idx), 0);
	enetc_wr(hw, ENETC_PSIVMSGRCVAR1(idx), 0);

	msg = &pf->rxmsg[idx];
	dma_free_coherent(&si->pdev->dev, msg->size, msg->vaddr, msg->dma);
	memset(msg, 0, sizeof(*msg));
}

static int enetc_msg_psi_init(struct enetc_pf *pf)
{
	struct enetc_si *si = pf->si;
	int vector, i, err;

	for (i = 0; i < pf->num_vfs; i++) {
		err = enetc_msg_alloc_mbx(si, i);
		if (err)
			goto free_mbx;
	}

	/* initialize PSI mailbox */
	INIT_WORK(&si->msg_task, enetc_msg_task);

	/* register message passing interrupt handler */
	snprintf(si->msg_int_name, sizeof(si->msg_int_name), "%s-vfmsg",
		 si->ndev->name);
	vector = pci_irq_vector(si->pdev, ENETC_SI_INT_IDX);
	err = request_irq(vector, enetc_msg_psi_msix, 0, si->msg_int_name, si);
	if (err) {
		dev_err(&si->pdev->dev,
			"PSI messaging: request_irq() failed!\n");
		goto free_mbx;
	}

	/* set one IRQ entry for PSI message receive notification (SI int) */
	enetc_wr(&si->hw, ENETC_SIMSIVR, ENETC_SI_INT_IDX);

	/* enable PSIIER interrupts */
	enetc_enable_psiier_interrupts(pf);

	return 0;

free_mbx:
	for (i--; i >= 0; i--)
		enetc_msg_free_mbx(si, i);

	return err;
}

static void enetc_msg_clear_vf_config(struct enetc_pf *pf, int vf_id)
{
	struct enetc_vf_state *vf_state = &pf->vf_state[vf_id];
	struct enetc_si *si = pf->si;
	int si_id = vf_id + 1;

	/* For ENETC v1, we only support setting the VF's MAC address via
	 * VSI-to-PSI messages, so there is no configuration to clear.
	 */
	if (is_enetc_rev1(si))
		return;

	mutex_lock(&vf_state->lock);

	/* VF may set these flags by mailbox messages, so need to clear these
	 * flags when enetc_msg_psi_free() is called. PF-set flags (TRUSTED,
	 * PF_SET_MAC) are not cleared, because these flags are unrelated to
	 * whether SR-IOV is enabled or disabled.
	 */
	vf_state->flags &= ~(ENETC_VF_FLAG_UC_PROMISC |
			     ENETC_VF_FLAG_MC_PROMISC);

	spin_lock(&si->gen_lock);
	vf_state->msg_fail_cnt = 0;
	enetc_set_si_uc_promisc(si, si_id, false);
	enetc_set_si_mc_promisc(si, si_id, false);
	spin_unlock(&si->gen_lock);

	enetc_set_si_uc_hash_filter(si, si_id, 0);
	enetc_set_si_mc_hash_filter(si, si_id, 0);

	mutex_unlock(&vf_state->lock);
}

static void enetc_msg_psi_free(struct enetc_pf *pf)
{
	struct enetc_si *si = pf->si;
	int i;

	/* disable PSIIER interrupts */
	enetc_disable_psiier_interrupts(pf);

	/* de-register message passing interrupt handler */
	free_irq(pci_irq_vector(si->pdev, ENETC_SI_INT_IDX), si);

	cancel_work_sync(&si->msg_task);

	/* PSIIER interrupts may be re-enabled by workqueue */
	enetc_disable_psiier_interrupts(pf);

	for (i = 0; i < pf->num_vfs; i++) {
		enetc_msg_free_mbx(si, i);
		enetc_msg_clear_vf_config(pf, i);
	}
}

int enetc_sriov_configure(struct pci_dev *pdev, int num_vfs)
{
	struct enetc_si *si = pci_get_drvdata(pdev);
	struct enetc_pf *pf = enetc_si_priv(si);
	int err;

	if (!num_vfs) {
		spin_lock(&si->gen_lock);
		pf->sriov_enabled = false;
		pf->link_status_ms_mask = 0;
		spin_unlock(&si->gen_lock);

		pci_disable_sriov(pdev);
		enetc_msg_psi_free(pf);
		pf->num_vfs = 0;
	} else {
		pf->num_vfs = num_vfs;

		err = enetc_msg_psi_init(pf);
		if (err) {
			dev_err(&pdev->dev, "enetc_msg_psi_init (%d)\n", err);
			goto err_msg_psi;
		}

		/* As PCI SR-IOV is not enabled at the moment, there is no
		 * concurrent access to sriov_enabled. So no need to use
		 * gen_lock to protect sriov_enabled.
		 */
		pf->sriov_enabled = true;
		err = pci_enable_sriov(pdev, num_vfs);
		if (err) {
			dev_err(&pdev->dev, "pci_enable_sriov err %d\n", err);
			goto err_en_sriov;
		}
	}

	return num_vfs;

err_en_sriov:
	/* If pci_enable_sriov() fails after partially creating VFs, a VF
	 * driver that successfully bound to one of the created VFs could
	 * have sent a registration message, setting its bit in
	 * link_status_ms_mask.
	 */
	spin_lock(&si->gen_lock);
	pf->sriov_enabled = false;
	pf->link_status_ms_mask = 0;
	spin_unlock(&si->gen_lock);
	enetc_msg_psi_free(pf);
err_msg_psi:
	pf->num_vfs = 0;

	return err;
}
EXPORT_SYMBOL_GPL(enetc_sriov_configure);

void enetc_pf_send_link_status_msg(struct enetc_pf *pf)
{
	struct enetc_ndev_priv *priv = netdev_priv(pf->si->ndev);
	u16 pf_msg, ms_mask, new_ms_msk, ms_status;
	struct enetc_si *si = pf->si;
	int retry_num = 0;

retry:
	spin_lock(&si->gen_lock);
	ms_mask = pf->link_status_ms_mask;
	/* VFs have unregistered link status notification, return directly  */
	if (!ms_mask)
		goto unlock;

	/* The MS bit is set, indicating that the corresponding VF has not
	 * read the last message, PF cannot send new message to the VF. To
	 * avoid sending messages to such a VF, the bit corresponding to VF
	 * is cleared from ms_mask. Because the MS bit can only be written
	 * as 1, writing a 0 has no effect. Writing a 1 when the bit is
	 * already set is undefined.
	 */
	ms_status = enetc_rd(&si->hw, ENETC_PSIMSGSR) & 0xfffe;
	if ((ms_mask & ms_status) && retry_num++ < 200) {
		spin_unlock(&si->gen_lock);
		/* Wait VFs to handle the last message */
		usleep_range(1000, 1020);
		goto retry;
	}

	/* None of the relevant VFs have processed the previous message, and
	 * the PF has tried 200 times. This situation indicates that VF has
	 * malfunctioned.
	 */
	new_ms_msk = ms_mask & (~ms_status);
	if (!new_ms_msk) {
		dev_err_ratelimited(&si->pdev->dev,
				    "All registered VFs (MS: 0x%x) are busy\n",
				    ms_mask);
		goto ms_status_check;
	}

	if (new_ms_msk != ms_mask)
		dev_warn_ratelimited(&si->pdev->dev,
				     "Failed to notify link status to VFs (MS: 0x%x)\n",
				     ms_mask ^ new_ms_msk);

	pf_msg = enetc_build_link_status_msg(priv, pf->link_up);
	enetc_wr(&si->hw, ENETC_PSIMSGSR,
		 FIELD_PREP(PSIMSGSR_MC, pf_msg) | new_ms_msk);

ms_status_check:
	/* If the PF fails to send messages to the corresponding VF for 10
	 * consecutive times, clear that VF's bit in link_status_ms_mask.
	 */
	for (int i = 0; i < pf->num_vfs; i++) {
		struct enetc_vf_state *vf_state = &pf->vf_state[i];

		if (!(PSIMSGSR_MS(i) & ms_mask))
			continue;

		if (!(PSIMSGSR_MS(i) & ms_status)) {
			vf_state->msg_fail_cnt = 0;
			continue;
		}

		if (vf_state->msg_fail_cnt++ < 10)
			continue;

		vf_state->msg_fail_cnt = 0;
		pf->link_status_ms_mask &= ~PSIMSGSR_MS(i);
		dev_warn_ratelimited(&si->pdev->dev,
				     "Clear VF%d's link status MS bit\n", i);
	}

unlock:
	spin_unlock(&si->gen_lock);
}
EXPORT_SYMBOL_GPL(enetc_pf_send_link_status_msg);

static void enetc_pf_notify_vf_link_status(struct enetc_pf *pf,
					   bool link_up)
{
	struct enetc_si *si = pf->si;

	/* Currently we do not add link status message support for ENETC v1 */
	if (!pf->total_vfs || is_enetc_rev1(si))
		return;

	spin_lock(&si->gen_lock);
	pf->link_up = link_up;
	if (!pf->link_status_ms_mask) {
		spin_unlock(&si->gen_lock);
		return;
	}
	spin_unlock(&si->gen_lock);

	queue_work(si->workqueue, &pf->link_status_task);
}

void enetc_pf_notify_vf_link_up(struct enetc_pf *pf)
{
	enetc_pf_notify_vf_link_status(pf, true);
}
EXPORT_SYMBOL_GPL(enetc_pf_notify_vf_link_up);

void enetc_pf_notify_vf_link_down(struct enetc_pf *pf)
{
	enetc_pf_notify_vf_link_status(pf, false);
}
EXPORT_SYMBOL_GPL(enetc_pf_notify_vf_link_down);
