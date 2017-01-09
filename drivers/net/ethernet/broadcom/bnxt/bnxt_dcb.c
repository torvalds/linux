/* Broadcom NetXtreme-C/E network driver.
 *
 * Copyright (c) 2014-2016 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 */

#include <linux/netdevice.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/rtnetlink.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/etherdevice.h>
#include "bnxt_hsi.h"
#include "bnxt.h"
#include "bnxt_dcb.h"

#ifdef CONFIG_BNXT_DCB
static int bnxt_hwrm_queue_pri2cos_cfg(struct bnxt *bp, struct ieee_ets *ets)
{
	struct hwrm_queue_pri2cos_cfg_input req = {0};
	int rc = 0, i;
	u8 *pri2cos;

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_QUEUE_PRI2COS_CFG, -1, -1);
	req.flags = cpu_to_le32(QUEUE_PRI2COS_CFG_REQ_FLAGS_PATH_BIDIR |
				QUEUE_PRI2COS_CFG_REQ_FLAGS_IVLAN);

	pri2cos = &req.pri0_cos_queue_id;
	for (i = 0; i < IEEE_8021QAZ_MAX_TCS; i++) {
		req.enables |= cpu_to_le32(
			QUEUE_PRI2COS_CFG_REQ_ENABLES_PRI0_COS_QUEUE_ID << i);

		pri2cos[i] = bp->q_info[ets->prio_tc[i]].queue_id;
	}
	rc = hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
	return rc;
}

static int bnxt_hwrm_queue_pri2cos_qcfg(struct bnxt *bp, struct ieee_ets *ets)
{
	struct hwrm_queue_pri2cos_qcfg_output *resp = bp->hwrm_cmd_resp_addr;
	struct hwrm_queue_pri2cos_qcfg_input req = {0};
	int rc = 0;

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_QUEUE_PRI2COS_QCFG, -1, -1);
	req.flags = cpu_to_le32(QUEUE_PRI2COS_QCFG_REQ_FLAGS_IVLAN);
	rc = hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
	if (!rc) {
		u8 *pri2cos = &resp->pri0_cos_queue_id;
		int i, j;

		for (i = 0; i < IEEE_8021QAZ_MAX_TCS; i++) {
			u8 queue_id = pri2cos[i];

			for (j = 0; j < bp->max_tc; j++) {
				if (bp->q_info[j].queue_id == queue_id) {
					ets->prio_tc[i] = j;
					break;
				}
			}
		}
	}
	return rc;
}

static int bnxt_hwrm_queue_cos2bw_cfg(struct bnxt *bp, struct ieee_ets *ets,
				      u8 max_tc)
{
	struct hwrm_queue_cos2bw_cfg_input req = {0};
	struct bnxt_cos2bw_cfg cos2bw;
	int rc = 0, i;
	void *data;

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_QUEUE_COS2BW_CFG, -1, -1);
	data = &req.unused_0;
	for (i = 0; i < max_tc; i++, data += sizeof(cos2bw) - 4) {
		req.enables |= cpu_to_le32(
			QUEUE_COS2BW_CFG_REQ_ENABLES_COS_QUEUE_ID0_VALID << i);

		memset(&cos2bw, 0, sizeof(cos2bw));
		cos2bw.queue_id = bp->q_info[i].queue_id;
		if (ets->tc_tsa[i] == IEEE_8021QAZ_TSA_STRICT) {
			cos2bw.tsa =
				QUEUE_COS2BW_QCFG_RESP_QUEUE_ID0_TSA_ASSIGN_SP;
			cos2bw.pri_lvl = i;
		} else {
			cos2bw.tsa =
				QUEUE_COS2BW_QCFG_RESP_QUEUE_ID0_TSA_ASSIGN_ETS;
			cos2bw.bw_weight = ets->tc_tx_bw[i];
		}
		memcpy(data, &cos2bw.queue_id, sizeof(cos2bw) - 4);
		if (i == 0) {
			req.queue_id0 = cos2bw.queue_id;
			req.unused_0 = 0;
		}
	}
	rc = hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
	return rc;
}

static int bnxt_hwrm_queue_cos2bw_qcfg(struct bnxt *bp, struct ieee_ets *ets)
{
	struct hwrm_queue_cos2bw_qcfg_output *resp = bp->hwrm_cmd_resp_addr;
	struct hwrm_queue_cos2bw_qcfg_input req = {0};
	struct bnxt_cos2bw_cfg cos2bw;
	void *data;
	int rc, i;

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_QUEUE_COS2BW_QCFG, -1, -1);
	rc = hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
	if (rc)
		return rc;

	data = &resp->queue_id0 + offsetof(struct bnxt_cos2bw_cfg, queue_id);
	for (i = 0; i < bp->max_tc; i++, data += sizeof(cos2bw) - 4) {
		int j;

		memcpy(&cos2bw.queue_id, data, sizeof(cos2bw) - 4);
		if (i == 0)
			cos2bw.queue_id = resp->queue_id0;

		for (j = 0; j < bp->max_tc; j++) {
			if (bp->q_info[j].queue_id != cos2bw.queue_id)
				continue;
			if (cos2bw.tsa ==
			    QUEUE_COS2BW_QCFG_RESP_QUEUE_ID0_TSA_ASSIGN_SP) {
				ets->tc_tsa[j] = IEEE_8021QAZ_TSA_STRICT;
			} else {
				ets->tc_tsa[j] = IEEE_8021QAZ_TSA_ETS;
				ets->tc_tx_bw[j] = cos2bw.bw_weight;
			}
		}
	}
	return 0;
}

static int bnxt_hwrm_queue_cfg(struct bnxt *bp, unsigned int lltc_mask)
{
	struct hwrm_queue_cfg_input req = {0};
	int i;

	if (netif_running(bp->dev))
		bnxt_tx_disable(bp);

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_QUEUE_CFG, -1, -1);
	req.flags = cpu_to_le32(QUEUE_CFG_REQ_FLAGS_PATH_BIDIR);
	req.enables = cpu_to_le32(QUEUE_CFG_REQ_ENABLES_SERVICE_PROFILE);

	/* Configure lossless queues to lossy first */
	req.service_profile = QUEUE_CFG_REQ_SERVICE_PROFILE_LOSSY;
	for (i = 0; i < bp->max_tc; i++) {
		if (BNXT_LLQ(bp->q_info[i].queue_profile)) {
			req.queue_id = cpu_to_le32(bp->q_info[i].queue_id);
			hwrm_send_message(bp, &req, sizeof(req),
					  HWRM_CMD_TIMEOUT);
			bp->q_info[i].queue_profile =
				QUEUE_CFG_REQ_SERVICE_PROFILE_LOSSY;
		}
	}

	/* Now configure desired queues to lossless */
	req.service_profile = QUEUE_CFG_REQ_SERVICE_PROFILE_LOSSLESS;
	for (i = 0; i < bp->max_tc; i++) {
		if (lltc_mask & (1 << i)) {
			req.queue_id = cpu_to_le32(bp->q_info[i].queue_id);
			hwrm_send_message(bp, &req, sizeof(req),
					  HWRM_CMD_TIMEOUT);
			bp->q_info[i].queue_profile =
				QUEUE_CFG_REQ_SERVICE_PROFILE_LOSSLESS;
		}
	}
	if (netif_running(bp->dev))
		bnxt_tx_enable(bp);

	return 0;
}

static int bnxt_hwrm_queue_pfc_cfg(struct bnxt *bp, struct ieee_pfc *pfc)
{
	struct hwrm_queue_pfcenable_cfg_input req = {0};
	struct ieee_ets *my_ets = bp->ieee_ets;
	unsigned int tc_mask = 0, pri_mask = 0;
	u8 i, pri, lltc_count = 0;
	bool need_q_recfg = false;
	int rc;

	if (!my_ets)
		return -EINVAL;

	for (i = 0; i < bp->max_tc; i++) {
		for (pri = 0; pri < IEEE_8021QAZ_MAX_TCS; pri++) {
			if ((pfc->pfc_en & (1 << pri)) &&
			    (my_ets->prio_tc[pri] == i)) {
				pri_mask |= 1 << pri;
				tc_mask |= 1 << i;
			}
		}
		if (tc_mask & (1 << i))
			lltc_count++;
	}
	if (lltc_count > bp->max_lltc)
		return -EINVAL;

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_QUEUE_PFCENABLE_CFG, -1, -1);
	req.flags = cpu_to_le32(pri_mask);
	rc = hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
	if (rc)
		return rc;

	for (i = 0; i < bp->max_tc; i++) {
		if (tc_mask & (1 << i)) {
			if (!BNXT_LLQ(bp->q_info[i].queue_profile))
				need_q_recfg = true;
		}
	}

	if (need_q_recfg)
		rc = bnxt_hwrm_queue_cfg(bp, tc_mask);

	return rc;
}

static int bnxt_hwrm_queue_pfc_qcfg(struct bnxt *bp, struct ieee_pfc *pfc)
{
	struct hwrm_queue_pfcenable_qcfg_output *resp = bp->hwrm_cmd_resp_addr;
	struct hwrm_queue_pfcenable_qcfg_input req = {0};
	u8 pri_mask;
	int rc;

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_QUEUE_PFCENABLE_QCFG, -1, -1);
	rc = hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
	if (rc)
		return rc;

	pri_mask = le32_to_cpu(resp->flags);
	pfc->pfc_en = pri_mask;
	return 0;
}

static int bnxt_ets_validate(struct bnxt *bp, struct ieee_ets *ets, u8 *tc)
{
	int total_ets_bw = 0;
	u8 max_tc = 0;
	int i;

	for (i = 0; i < IEEE_8021QAZ_MAX_TCS; i++) {
		if (ets->prio_tc[i] > bp->max_tc) {
			netdev_err(bp->dev, "priority to TC mapping exceeds TC count %d\n",
				   ets->prio_tc[i]);
			return -EINVAL;
		}
		if (ets->prio_tc[i] > max_tc)
			max_tc = ets->prio_tc[i];

		if ((ets->tc_tx_bw[i] || ets->tc_tsa[i]) && i > bp->max_tc)
			return -EINVAL;

		switch (ets->tc_tsa[i]) {
		case IEEE_8021QAZ_TSA_STRICT:
			break;
		case IEEE_8021QAZ_TSA_ETS:
			total_ets_bw += ets->tc_tx_bw[i];
			break;
		default:
			return -ENOTSUPP;
		}
	}
	if (total_ets_bw > 100)
		return -EINVAL;

	*tc = max_tc + 1;
	return 0;
}

static int bnxt_dcbnl_ieee_getets(struct net_device *dev, struct ieee_ets *ets)
{
	struct bnxt *bp = netdev_priv(dev);
	struct ieee_ets *my_ets = bp->ieee_ets;

	ets->ets_cap = bp->max_tc;

	if (!my_ets) {
		int rc;

		if (bp->dcbx_cap & DCB_CAP_DCBX_HOST)
			return 0;

		my_ets = kzalloc(sizeof(*my_ets), GFP_KERNEL);
		if (!my_ets)
			return 0;
		rc = bnxt_hwrm_queue_cos2bw_qcfg(bp, my_ets);
		if (rc)
			return 0;
		rc = bnxt_hwrm_queue_pri2cos_qcfg(bp, my_ets);
		if (rc)
			return 0;
	}

	ets->cbs = my_ets->cbs;
	memcpy(ets->tc_tx_bw, my_ets->tc_tx_bw, sizeof(ets->tc_tx_bw));
	memcpy(ets->tc_rx_bw, my_ets->tc_rx_bw, sizeof(ets->tc_rx_bw));
	memcpy(ets->tc_tsa, my_ets->tc_tsa, sizeof(ets->tc_tsa));
	memcpy(ets->prio_tc, my_ets->prio_tc, sizeof(ets->prio_tc));
	return 0;
}

static int bnxt_dcbnl_ieee_setets(struct net_device *dev, struct ieee_ets *ets)
{
	struct bnxt *bp = netdev_priv(dev);
	struct ieee_ets *my_ets = bp->ieee_ets;
	u8 max_tc = 0;
	int rc, i;

	if (!(bp->dcbx_cap & DCB_CAP_DCBX_VER_IEEE) ||
	    !(bp->dcbx_cap & DCB_CAP_DCBX_HOST))
		return -EINVAL;

	rc = bnxt_ets_validate(bp, ets, &max_tc);
	if (!rc) {
		if (!my_ets) {
			my_ets = kzalloc(sizeof(*my_ets), GFP_KERNEL);
			if (!my_ets)
				return -ENOMEM;
			/* initialize PRI2TC mappings to invalid value */
			for (i = 0; i < IEEE_8021QAZ_MAX_TCS; i++)
				my_ets->prio_tc[i] = IEEE_8021QAZ_MAX_TCS;
			bp->ieee_ets = my_ets;
		}
		rc = bnxt_setup_mq_tc(dev, max_tc);
		if (rc)
			return rc;
		rc = bnxt_hwrm_queue_cos2bw_cfg(bp, ets, max_tc);
		if (rc)
			return rc;
		rc = bnxt_hwrm_queue_pri2cos_cfg(bp, ets);
		if (rc)
			return rc;
		memcpy(my_ets, ets, sizeof(*my_ets));
	}
	return rc;
}

static int bnxt_dcbnl_ieee_getpfc(struct net_device *dev, struct ieee_pfc *pfc)
{
	struct bnxt *bp = netdev_priv(dev);
	__le64 *stats = (__le64 *)bp->hw_rx_port_stats;
	struct ieee_pfc *my_pfc = bp->ieee_pfc;
	long rx_off, tx_off;
	int i, rc;

	pfc->pfc_cap = bp->max_lltc;

	if (!my_pfc) {
		if (bp->dcbx_cap & DCB_CAP_DCBX_HOST)
			return 0;

		my_pfc = kzalloc(sizeof(*my_pfc), GFP_KERNEL);
		if (!my_pfc)
			return 0;
		bp->ieee_pfc = my_pfc;
		rc = bnxt_hwrm_queue_pfc_qcfg(bp, my_pfc);
		if (rc)
			return 0;
	}

	pfc->pfc_en = my_pfc->pfc_en;
	pfc->mbc = my_pfc->mbc;
	pfc->delay = my_pfc->delay;

	if (!stats)
		return 0;

	rx_off = BNXT_RX_STATS_OFFSET(rx_pfc_ena_frames_pri0);
	tx_off = BNXT_TX_STATS_OFFSET(tx_pfc_ena_frames_pri0);
	for (i = 0; i < IEEE_8021QAZ_MAX_TCS; i++, rx_off++, tx_off++) {
		pfc->requests[i] = le64_to_cpu(*(stats + tx_off));
		pfc->indications[i] = le64_to_cpu(*(stats + rx_off));
	}

	return 0;
}

static int bnxt_dcbnl_ieee_setpfc(struct net_device *dev, struct ieee_pfc *pfc)
{
	struct bnxt *bp = netdev_priv(dev);
	struct ieee_pfc *my_pfc = bp->ieee_pfc;
	int rc;

	if (!(bp->dcbx_cap & DCB_CAP_DCBX_VER_IEEE) ||
	    !(bp->dcbx_cap & DCB_CAP_DCBX_HOST))
		return -EINVAL;

	if (!my_pfc) {
		my_pfc = kzalloc(sizeof(*my_pfc), GFP_KERNEL);
		if (!my_pfc)
			return -ENOMEM;
		bp->ieee_pfc = my_pfc;
	}
	rc = bnxt_hwrm_queue_pfc_cfg(bp, pfc);
	if (!rc)
		memcpy(my_pfc, pfc, sizeof(*my_pfc));

	return rc;
}

static int bnxt_dcbnl_ieee_setapp(struct net_device *dev, struct dcb_app *app)
{
	struct bnxt *bp = netdev_priv(dev);
	int rc = -EINVAL;

	if (!(bp->dcbx_cap & DCB_CAP_DCBX_VER_IEEE) ||
	    !(bp->dcbx_cap & DCB_CAP_DCBX_HOST))
		return -EINVAL;

	rc = dcb_ieee_setapp(dev, app);
	return rc;
}

static int bnxt_dcbnl_ieee_delapp(struct net_device *dev, struct dcb_app *app)
{
	struct bnxt *bp = netdev_priv(dev);
	int rc;

	if (!(bp->dcbx_cap & DCB_CAP_DCBX_VER_IEEE))
		return -EINVAL;

	rc = dcb_ieee_delapp(dev, app);
	return rc;
}

static u8 bnxt_dcbnl_getdcbx(struct net_device *dev)
{
	struct bnxt *bp = netdev_priv(dev);

	return bp->dcbx_cap;
}

static u8 bnxt_dcbnl_setdcbx(struct net_device *dev, u8 mode)
{
	struct bnxt *bp = netdev_priv(dev);

	/* only support IEEE */
	if ((mode & DCB_CAP_DCBX_VER_CEE) || !(mode & DCB_CAP_DCBX_VER_IEEE))
		return 1;

	if ((mode & DCB_CAP_DCBX_HOST) && BNXT_VF(bp))
		return 1;

	if (mode == bp->dcbx_cap)
		return 0;

	bp->dcbx_cap = mode;
	return 0;
}

static const struct dcbnl_rtnl_ops dcbnl_ops = {
	.ieee_getets	= bnxt_dcbnl_ieee_getets,
	.ieee_setets	= bnxt_dcbnl_ieee_setets,
	.ieee_getpfc	= bnxt_dcbnl_ieee_getpfc,
	.ieee_setpfc	= bnxt_dcbnl_ieee_setpfc,
	.ieee_setapp	= bnxt_dcbnl_ieee_setapp,
	.ieee_delapp	= bnxt_dcbnl_ieee_delapp,
	.getdcbx	= bnxt_dcbnl_getdcbx,
	.setdcbx	= bnxt_dcbnl_setdcbx,
};

void bnxt_dcb_init(struct bnxt *bp)
{
	if (bp->hwrm_spec_code < 0x10501)
		return;

	bp->dcbx_cap = DCB_CAP_DCBX_VER_IEEE;
	if (BNXT_PF(bp))
		bp->dcbx_cap |= DCB_CAP_DCBX_HOST;
	else
		bp->dcbx_cap |= DCB_CAP_DCBX_LLD_MANAGED;
	bp->dev->dcbnl_ops = &dcbnl_ops;
}

void bnxt_dcb_free(struct bnxt *bp)
{
	kfree(bp->ieee_pfc);
	kfree(bp->ieee_ets);
	bp->ieee_pfc = NULL;
	bp->ieee_ets = NULL;
}

#else

void bnxt_dcb_init(struct bnxt *bp)
{
}

void bnxt_dcb_free(struct bnxt *bp)
{
}

#endif
