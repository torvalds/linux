// SPDX-License-Identifier: GPL-2.0-only
/* Atlantic Network Driver
 * Copyright (C) 2020 Marvell International Ltd.
 */

#include "aq_macsec.h"
#include "aq_nic.h"
#include <linux/rtnetlink.h>

static int aq_mdo_dev_open(struct macsec_context *ctx)
{
	return 0;
}

static int aq_mdo_dev_stop(struct macsec_context *ctx)
{
	return 0;
}

static int aq_mdo_add_secy(struct macsec_context *ctx)
{
	return -EOPNOTSUPP;
}

static int aq_mdo_upd_secy(struct macsec_context *ctx)
{
	return -EOPNOTSUPP;
}

static int aq_mdo_del_secy(struct macsec_context *ctx)
{
	return -EOPNOTSUPP;
}

static int aq_mdo_add_txsa(struct macsec_context *ctx)
{
	return -EOPNOTSUPP;
}

static int aq_mdo_upd_txsa(struct macsec_context *ctx)
{
	return -EOPNOTSUPP;
}

static int aq_mdo_del_txsa(struct macsec_context *ctx)
{
	return -EOPNOTSUPP;
}

static int aq_mdo_add_rxsc(struct macsec_context *ctx)
{
	return -EOPNOTSUPP;
}

static int aq_mdo_upd_rxsc(struct macsec_context *ctx)
{
	return -EOPNOTSUPP;
}

static int aq_mdo_del_rxsc(struct macsec_context *ctx)
{
	return -EOPNOTSUPP;
}

static int aq_mdo_add_rxsa(struct macsec_context *ctx)
{
	return -EOPNOTSUPP;
}

static int aq_mdo_upd_rxsa(struct macsec_context *ctx)
{
	return -EOPNOTSUPP;
}

static int aq_mdo_del_rxsa(struct macsec_context *ctx)
{
	return -EOPNOTSUPP;
}

static void aq_check_txsa_expiration(struct aq_nic_s *nic)
{
}

const struct macsec_ops aq_macsec_ops = {
	.mdo_dev_open = aq_mdo_dev_open,
	.mdo_dev_stop = aq_mdo_dev_stop,
	.mdo_add_secy = aq_mdo_add_secy,
	.mdo_upd_secy = aq_mdo_upd_secy,
	.mdo_del_secy = aq_mdo_del_secy,
	.mdo_add_rxsc = aq_mdo_add_rxsc,
	.mdo_upd_rxsc = aq_mdo_upd_rxsc,
	.mdo_del_rxsc = aq_mdo_del_rxsc,
	.mdo_add_rxsa = aq_mdo_add_rxsa,
	.mdo_upd_rxsa = aq_mdo_upd_rxsa,
	.mdo_del_rxsa = aq_mdo_del_rxsa,
	.mdo_add_txsa = aq_mdo_add_txsa,
	.mdo_upd_txsa = aq_mdo_upd_txsa,
	.mdo_del_txsa = aq_mdo_del_txsa,
};

int aq_macsec_init(struct aq_nic_s *nic)
{
	struct aq_macsec_cfg *cfg;
	u32 caps_lo;

	if (!nic->aq_fw_ops->get_link_capabilities)
		return 0;

	caps_lo = nic->aq_fw_ops->get_link_capabilities(nic->aq_hw);

	if (!(caps_lo & BIT(CAPS_LO_MACSEC)))
		return 0;

	nic->macsec_cfg = kzalloc(sizeof(*cfg), GFP_KERNEL);
	if (!nic->macsec_cfg)
		return -ENOMEM;

	nic->ndev->features |= NETIF_F_HW_MACSEC;
	nic->ndev->macsec_ops = &aq_macsec_ops;

	return 0;
}

void aq_macsec_free(struct aq_nic_s *nic)
{
	kfree(nic->macsec_cfg);
	nic->macsec_cfg = NULL;
}

int aq_macsec_enable(struct aq_nic_s *nic)
{
	struct macsec_msg_fw_response resp = { 0 };
	struct macsec_msg_fw_request msg = { 0 };
	struct aq_hw_s *hw = nic->aq_hw;
	int ret = 0;

	if (!nic->macsec_cfg)
		return 0;

	rtnl_lock();

	if (nic->aq_fw_ops->send_macsec_req) {
		struct macsec_cfg_request cfg = { 0 };

		cfg.enabled = 1;
		cfg.egress_threshold = 0xffffffff;
		cfg.ingress_threshold = 0xffffffff;
		cfg.interrupts_enabled = 1;

		msg.msg_type = macsec_cfg_msg;
		msg.cfg = cfg;

		ret = nic->aq_fw_ops->send_macsec_req(hw, &msg, &resp);
		if (ret)
			goto unlock;
	}

unlock:
	rtnl_unlock();
	return ret;
}

void aq_macsec_work(struct aq_nic_s *nic)
{
	if (!nic->macsec_cfg)
		return;

	if (!netif_carrier_ok(nic->ndev))
		return;

	rtnl_lock();
	aq_check_txsa_expiration(nic);
	rtnl_unlock();
}
