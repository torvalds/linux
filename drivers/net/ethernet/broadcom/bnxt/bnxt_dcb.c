/* Broadcom NetXtreme-C/E network driver.
 *
 * Copyright (c) 2014-2016 Broadcom Corporation
 * Copyright (c) 2016-2017 Broadcom Limited
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
#include <rdma/ib_verbs.h>
#include "bnxt_hsi.h"
#include "bnxt.h"
#include "bnxt_dcb.h"

#ifdef CONFIG_BNXT_DCB
static int bnxt_queue_to_tc(struct bnxt *bp, u8 queue_id)
{
	int i, j;

	for (i = 0; i < bp->max_tc; i++) {
		if (bp->q_info[i].queue_id == queue_id) {
			for (j = 0; j < bp->max_tc; j++) {
				if (bp->tc_to_qidx[j] == i)
					return j;
			}
		}
	}
	return -EINVAL;
}

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
		u8 qidx;

		req.enables |= cpu_to_le32(
			QUEUE_PRI2COS_CFG_REQ_ENABLES_PRI0_COS_QUEUE_ID << i);

		qidx = bp->tc_to_qidx[ets->prio_tc[i]];
		pri2cos[i] = bp->q_info[qidx].queue_id;
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

	mutex_lock(&bp->hwrm_cmd_lock);
	rc = _hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
	if (!rc) {
		u8 *pri2cos = &resp->pri0_cos_queue_id;
		int i;

		for (i = 0; i < IEEE_8021QAZ_MAX_TCS; i++) {
			u8 queue_id = pri2cos[i];
			int tc;

			tc = bnxt_queue_to_tc(bp, queue_id);
			if (tc >= 0)
				ets->prio_tc[i] = tc;
		}
	}
	mutex_unlock(&bp->hwrm_cmd_lock);
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
	for (i = 0; i < max_tc; i++) {
		u8 qidx = bp->tc_to_qidx[i];

		req.enables |= cpu_to_le32(
			QUEUE_COS2BW_CFG_REQ_ENABLES_COS_QUEUE_ID0_VALID <<
			qidx);

		memset(&cos2bw, 0, sizeof(cos2bw));
		cos2bw.queue_id = bp->q_info[qidx].queue_id;
		if (ets->tc_tsa[i] == IEEE_8021QAZ_TSA_STRICT) {
			cos2bw.tsa =
				QUEUE_COS2BW_QCFG_RESP_QUEUE_ID0_TSA_ASSIGN_SP;
			cos2bw.pri_lvl = i;
		} else {
			cos2bw.tsa =
				QUEUE_COS2BW_QCFG_RESP_QUEUE_ID0_TSA_ASSIGN_ETS;
			cos2bw.bw_weight = ets->tc_tx_bw[i];
			/* older firmware requires min_bw to be set to the
			 * same weight value in percent.
			 */
			cos2bw.min_bw =
				cpu_to_le32((ets->tc_tx_bw[i] * 100) |
					    BW_VALUE_UNIT_PERCENT1_100);
		}
		data = &req.unused_0 + qidx * (sizeof(cos2bw) - 4);
		memcpy(data, &cos2bw.queue_id, sizeof(cos2bw) - 4);
		if (qidx == 0) {
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

	mutex_lock(&bp->hwrm_cmd_lock);
	rc = _hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
	if (rc) {
		mutex_unlock(&bp->hwrm_cmd_lock);
		return rc;
	}

	data = &resp->queue_id0 + offsetof(struct bnxt_cos2bw_cfg, queue_id);
	for (i = 0; i < bp->max_tc; i++, data += sizeof(cos2bw) - 4) {
		int tc;

		memcpy(&cos2bw.queue_id, data, sizeof(cos2bw) - 4);
		if (i == 0)
			cos2bw.queue_id = resp->queue_id0;

		tc = bnxt_queue_to_tc(bp, cos2bw.queue_id);
		if (tc < 0)
			continue;

		if (cos2bw.tsa ==
		    QUEUE_COS2BW_QCFG_RESP_QUEUE_ID0_TSA_ASSIGN_SP) {
			ets->tc_tsa[tc] = IEEE_8021QAZ_TSA_STRICT;
		} else {
			ets->tc_tsa[tc] = IEEE_8021QAZ_TSA_ETS;
			ets->tc_tx_bw[tc] = cos2bw.bw_weight;
		}
	}
	mutex_unlock(&bp->hwrm_cmd_lock);
	return 0;
}

static int bnxt_queue_remap(struct bnxt *bp, unsigned int lltc_mask)
{
	unsigned long qmap = 0;
	int max = bp->max_tc;
	int i, j, rc;

	/* Assign lossless TCs first */
	for (i = 0, j = 0; i < max; ) {
		if (lltc_mask & (1 << i)) {
			if (BNXT_LLQ(bp->q_info[j].queue_profile)) {
				bp->tc_to_qidx[i] = j;
				__set_bit(j, &qmap);
				i++;
			}
			j++;
			continue;
		}
		i++;
	}

	for (i = 0, j = 0; i < max; i++) {
		if (lltc_mask & (1 << i))
			continue;
		j = find_next_zero_bit(&qmap, max, j);
		bp->tc_to_qidx[i] = j;
		__set_bit(j, &qmap);
		j++;
	}

	if (netif_running(bp->dev)) {
		bnxt_close_nic(bp, false, false);
		rc = bnxt_open_nic(bp, false, false);
		if (rc) {
			netdev_warn(bp->dev, "failed to open NIC, rc = %d\n", rc);
			return rc;
		}
	}
	if (bp->ieee_ets) {
		int tc = netdev_get_num_tc(bp->dev);

		if (!tc)
			tc = 1;
		rc = bnxt_hwrm_queue_cos2bw_cfg(bp, bp->ieee_ets, tc);
		if (rc) {
			netdev_warn(bp->dev, "failed to config BW, rc = %d\n", rc);
			return rc;
		}
		rc = bnxt_hwrm_queue_pri2cos_cfg(bp, bp->ieee_ets);
		if (rc) {
			netdev_warn(bp->dev, "failed to config prio, rc = %d\n", rc);
			return rc;
		}
	}
	return 0;
}

static int bnxt_hwrm_queue_pfc_cfg(struct bnxt *bp, struct ieee_pfc *pfc)
{
	struct hwrm_queue_pfcenable_cfg_input req = {0};
	struct ieee_ets *my_ets = bp->ieee_ets;
	unsigned int tc_mask = 0, pri_mask = 0;
	u8 i, pri, lltc_count = 0;
	bool need_q_remap = false;
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

	for (i = 0; i < bp->max_tc; i++) {
		if (tc_mask & (1 << i)) {
			u8 qidx = bp->tc_to_qidx[i];

			if (!BNXT_LLQ(bp->q_info[qidx].queue_profile)) {
				need_q_remap = true;
				break;
			}
		}
	}

	if (need_q_remap)
		rc = bnxt_queue_remap(bp, tc_mask);

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_QUEUE_PFCENABLE_CFG, -1, -1);
	req.flags = cpu_to_le32(pri_mask);
	rc = hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
	if (rc)
		return rc;

	return rc;
}

static int bnxt_hwrm_queue_pfc_qcfg(struct bnxt *bp, struct ieee_pfc *pfc)
{
	struct hwrm_queue_pfcenable_qcfg_output *resp = bp->hwrm_cmd_resp_addr;
	struct hwrm_queue_pfcenable_qcfg_input req = {0};
	u8 pri_mask;
	int rc;

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_QUEUE_PFCENABLE_QCFG, -1, -1);

	mutex_lock(&bp->hwrm_cmd_lock);
	rc = _hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
	if (rc) {
		mutex_unlock(&bp->hwrm_cmd_lock);
		return rc;
	}

	pri_mask = le32_to_cpu(resp->flags);
	pfc->pfc_en = pri_mask;
	mutex_unlock(&bp->hwrm_cmd_lock);
	return 0;
}

static int bnxt_hwrm_set_dcbx_app(struct bnxt *bp, struct dcb_app *app,
				  bool add)
{
	struct hwrm_fw_set_structured_data_input set = {0};
	struct hwrm_fw_get_structured_data_input get = {0};
	struct hwrm_struct_data_dcbx_app *fw_app;
	struct hwrm_struct_hdr *data;
	dma_addr_t mapping;
	size_t data_len;
	int rc, n, i;

	if (bp->hwrm_spec_code < 0x10601)
		return 0;

	n = IEEE_8021QAZ_MAX_TCS;
	data_len = sizeof(*data) + sizeof(*fw_app) * n;
	data = dma_zalloc_coherent(&bp->pdev->dev, data_len, &mapping,
				   GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	bnxt_hwrm_cmd_hdr_init(bp, &get, HWRM_FW_GET_STRUCTURED_DATA, -1, -1);
	get.dest_data_addr = cpu_to_le64(mapping);
	get.structure_id = cpu_to_le16(STRUCT_HDR_STRUCT_ID_DCBX_APP);
	get.subtype = cpu_to_le16(HWRM_STRUCT_DATA_SUBTYPE_HOST_OPERATIONAL);
	get.count = 0;
	rc = hwrm_send_message(bp, &get, sizeof(get), HWRM_CMD_TIMEOUT);
	if (rc)
		goto set_app_exit;

	fw_app = (struct hwrm_struct_data_dcbx_app *)(data + 1);

	if (data->struct_id != cpu_to_le16(STRUCT_HDR_STRUCT_ID_DCBX_APP)) {
		rc = -ENODEV;
		goto set_app_exit;
	}

	n = data->count;
	for (i = 0; i < n; i++, fw_app++) {
		if (fw_app->protocol_id == cpu_to_be16(app->protocol) &&
		    fw_app->protocol_selector == app->selector &&
		    fw_app->priority == app->priority) {
			if (add)
				goto set_app_exit;
			else
				break;
		}
	}
	if (add) {
		/* append */
		n++;
		fw_app->protocol_id = cpu_to_be16(app->protocol);
		fw_app->protocol_selector = app->selector;
		fw_app->priority = app->priority;
		fw_app->valid = 1;
	} else {
		size_t len = 0;

		/* not found, nothing to delete */
		if (n == i)
			goto set_app_exit;

		len = (n - 1 - i) * sizeof(*fw_app);
		if (len)
			memmove(fw_app, fw_app + 1, len);
		n--;
		memset(fw_app + n, 0, sizeof(*fw_app));
	}
	data->count = n;
	data->len = cpu_to_le16(sizeof(*fw_app) * n);
	data->subtype = cpu_to_le16(HWRM_STRUCT_DATA_SUBTYPE_HOST_OPERATIONAL);

	bnxt_hwrm_cmd_hdr_init(bp, &set, HWRM_FW_SET_STRUCTURED_DATA, -1, -1);
	set.src_data_addr = cpu_to_le64(mapping);
	set.data_len = cpu_to_le16(sizeof(*data) + sizeof(*fw_app) * n);
	set.hdr_cnt = 1;
	rc = hwrm_send_message(bp, &set, sizeof(set), HWRM_CMD_TIMEOUT);
	if (rc)
		rc = -EIO;

set_app_exit:
	dma_free_coherent(&bp->pdev->dev, data_len, data, mapping);
	return rc;
}

static int bnxt_hwrm_queue_dscp_qcaps(struct bnxt *bp)
{
	struct hwrm_queue_dscp_qcaps_output *resp = bp->hwrm_cmd_resp_addr;
	struct hwrm_queue_dscp_qcaps_input req = {0};
	int rc;

	if (bp->hwrm_spec_code < 0x10800 || BNXT_VF(bp))
		return 0;

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_QUEUE_DSCP_QCAPS, -1, -1);
	mutex_lock(&bp->hwrm_cmd_lock);
	rc = _hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
	if (!rc) {
		bp->max_dscp_value = (1 << resp->num_dscp_bits) - 1;
		if (bp->max_dscp_value < 0x3f)
			bp->max_dscp_value = 0;
	}

	mutex_unlock(&bp->hwrm_cmd_lock);
	return rc;
}

static int bnxt_hwrm_queue_dscp2pri_cfg(struct bnxt *bp, struct dcb_app *app,
					bool add)
{
	struct hwrm_queue_dscp2pri_cfg_input req = {0};
	struct bnxt_dscp2pri_entry *dscp2pri;
	dma_addr_t mapping;
	int rc;

	if (bp->hwrm_spec_code < 0x10800)
		return 0;

	bnxt_hwrm_cmd_hdr_init(bp, &req, HWRM_QUEUE_DSCP2PRI_CFG, -1, -1);
	dscp2pri = dma_alloc_coherent(&bp->pdev->dev, sizeof(*dscp2pri),
				      &mapping, GFP_KERNEL);
	if (!dscp2pri)
		return -ENOMEM;

	req.src_data_addr = cpu_to_le64(mapping);
	dscp2pri->dscp = app->protocol;
	if (add)
		dscp2pri->mask = 0x3f;
	else
		dscp2pri->mask = 0;
	dscp2pri->pri = app->priority;
	req.entry_cnt = cpu_to_le16(1);
	rc = hwrm_send_message(bp, &req, sizeof(req), HWRM_CMD_TIMEOUT);
	if (rc)
		rc = -EIO;
	dma_free_coherent(&bp->pdev->dev, sizeof(*dscp2pri), dscp2pri,
			  mapping);
	return rc;
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

	if (max_tc >= bp->max_tc)
		*tc = bp->max_tc;
	else
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

static int bnxt_dcbnl_ieee_dscp_app_prep(struct bnxt *bp, struct dcb_app *app)
{
	if (app->selector == IEEE_8021QAZ_APP_SEL_DSCP) {
		if (!bp->max_dscp_value)
			return -ENOTSUPP;
		if (app->protocol > bp->max_dscp_value)
			return -EINVAL;
	}
	return 0;
}

static int bnxt_dcbnl_ieee_setapp(struct net_device *dev, struct dcb_app *app)
{
	struct bnxt *bp = netdev_priv(dev);
	int rc;

	if (!(bp->dcbx_cap & DCB_CAP_DCBX_VER_IEEE) ||
	    !(bp->dcbx_cap & DCB_CAP_DCBX_HOST))
		return -EINVAL;

	rc = bnxt_dcbnl_ieee_dscp_app_prep(bp, app);
	if (rc)
		return rc;

	rc = dcb_ieee_setapp(dev, app);
	if (rc)
		return rc;

	if ((app->selector == IEEE_8021QAZ_APP_SEL_ETHERTYPE &&
	     app->protocol == ETH_P_IBOE) ||
	    (app->selector == IEEE_8021QAZ_APP_SEL_DGRAM &&
	     app->protocol == ROCE_V2_UDP_DPORT))
		rc = bnxt_hwrm_set_dcbx_app(bp, app, true);

	if (app->selector == IEEE_8021QAZ_APP_SEL_DSCP)
		rc = bnxt_hwrm_queue_dscp2pri_cfg(bp, app, true);

	return rc;
}

static int bnxt_dcbnl_ieee_delapp(struct net_device *dev, struct dcb_app *app)
{
	struct bnxt *bp = netdev_priv(dev);
	int rc;

	if (!(bp->dcbx_cap & DCB_CAP_DCBX_VER_IEEE) ||
	    !(bp->dcbx_cap & DCB_CAP_DCBX_HOST))
		return -EINVAL;

	rc = bnxt_dcbnl_ieee_dscp_app_prep(bp, app);
	if (rc)
		return rc;

	rc = dcb_ieee_delapp(dev, app);
	if (rc)
		return rc;
	if ((app->selector == IEEE_8021QAZ_APP_SEL_ETHERTYPE &&
	     app->protocol == ETH_P_IBOE) ||
	    (app->selector == IEEE_8021QAZ_APP_SEL_DGRAM &&
	     app->protocol == ROCE_V2_UDP_DPORT))
		rc = bnxt_hwrm_set_dcbx_app(bp, app, false);

	if (app->selector == IEEE_8021QAZ_APP_SEL_DSCP)
		rc = bnxt_hwrm_queue_dscp2pri_cfg(bp, app, false);

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

	/* All firmware DCBX settings are set in NVRAM */
	if (bp->dcbx_cap & DCB_CAP_DCBX_LLD_MANAGED)
		return 1;

	if (mode & DCB_CAP_DCBX_HOST) {
		if (BNXT_VF(bp) || (bp->fw_cap & BNXT_FW_CAP_LLDP_AGENT))
			return 1;

		/* only support IEEE */
		if ((mode & DCB_CAP_DCBX_VER_CEE) ||
		    !(mode & DCB_CAP_DCBX_VER_IEEE))
			return 1;
	}

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

	bnxt_hwrm_queue_dscp_qcaps(bp);
	bp->dcbx_cap = DCB_CAP_DCBX_VER_IEEE;
	if (BNXT_PF(bp) && !(bp->fw_cap & BNXT_FW_CAP_LLDP_AGENT))
		bp->dcbx_cap |= DCB_CAP_DCBX_HOST;
	else if (bp->fw_cap & BNXT_FW_CAP_DCBX_AGENT)
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
