/*
 * QLogic qlcnic NIC Driver
 * Copyright (c)  2009-2013 QLogic Corporation
 *
 * See LICENSE.qlcnic for copyright and licensing details.
 */

#include "qlcnic.h"

#define QLC_DCB_MAX_TC			0x8

#define QLC_DCB_TSA_SUPPORT(V)		(V & 0x1)
#define QLC_DCB_ETS_SUPPORT(V)		((V >> 1) & 0x1)
#define QLC_DCB_VERSION_SUPPORT(V)	((V >> 2) & 0xf)
#define QLC_DCB_MAX_NUM_TC(V)		((V >> 20) & 0xf)
#define QLC_DCB_MAX_NUM_ETS_TC(V)	((V >> 24) & 0xf)
#define QLC_DCB_MAX_NUM_PFC_TC(V)	((V >> 28) & 0xf)

static void __qlcnic_dcb_free(struct qlcnic_adapter *);
static int __qlcnic_dcb_attach(struct qlcnic_adapter *);
static int __qlcnic_dcb_query_hw_capability(struct qlcnic_adapter *, char *);
static void __qlcnic_dcb_get_info(struct qlcnic_adapter *);

static int qlcnic_82xx_dcb_get_hw_capability(struct qlcnic_adapter *);

static int qlcnic_83xx_dcb_get_hw_capability(struct qlcnic_adapter *);

struct qlcnic_dcb_capability {
	bool	tsa_capability;
	bool	ets_capability;
	u8	max_num_tc;
	u8	max_ets_tc;
	u8	max_pfc_tc;
	u8	dcb_capability;
};

struct qlcnic_dcb_cfg {
	struct qlcnic_dcb_capability capability;
	u32 version;
};

static struct qlcnic_dcb_ops qlcnic_83xx_dcb_ops = {
	.free			= __qlcnic_dcb_free,
	.attach			= __qlcnic_dcb_attach,
	.query_hw_capability	= __qlcnic_dcb_query_hw_capability,
	.get_info		= __qlcnic_dcb_get_info,

	.get_hw_capability	= qlcnic_83xx_dcb_get_hw_capability,
};

static struct qlcnic_dcb_ops qlcnic_82xx_dcb_ops = {
	.free			= __qlcnic_dcb_free,
	.attach			= __qlcnic_dcb_attach,
	.query_hw_capability	= __qlcnic_dcb_query_hw_capability,
	.get_info		= __qlcnic_dcb_get_info,

	.get_hw_capability	= qlcnic_82xx_dcb_get_hw_capability,
};

void qlcnic_set_dcb_ops(struct qlcnic_adapter *adapter)
{
	if (qlcnic_82xx_check(adapter))
		adapter->dcb->ops = &qlcnic_82xx_dcb_ops;
	else if (qlcnic_83xx_check(adapter))
		adapter->dcb->ops = &qlcnic_83xx_dcb_ops;
}

int __qlcnic_register_dcb(struct qlcnic_adapter *adapter)
{
	struct qlcnic_dcb *dcb;

	dcb = kzalloc(sizeof(struct qlcnic_dcb), GFP_ATOMIC);
	if (!dcb)
		return -ENOMEM;

	adapter->dcb = dcb;
	qlcnic_set_dcb_ops(adapter);

	return 0;
}

static void __qlcnic_dcb_free(struct qlcnic_adapter *adapter)
{
	struct qlcnic_dcb *dcb = adapter->dcb;

	if (!dcb)
		return;

	kfree(dcb->cfg);
	dcb->cfg = NULL;
	kfree(dcb);
	adapter->dcb = NULL;
}

static void __qlcnic_dcb_get_info(struct qlcnic_adapter *adapter)
{
	qlcnic_dcb_get_hw_capability(adapter);
}

static int __qlcnic_dcb_attach(struct qlcnic_adapter *adapter)
{
	struct qlcnic_dcb *dcb = adapter->dcb;

	dcb->cfg = kzalloc(sizeof(struct qlcnic_dcb_cfg), GFP_ATOMIC);
	if (!dcb->cfg)
		return -ENOMEM;

	qlcnic_dcb_get_info(adapter);

	return 0;
}

static int __qlcnic_dcb_query_hw_capability(struct qlcnic_adapter *adapter,
					    char *buf)
{
	struct qlcnic_cmd_args cmd;
	u32 mbx_out;
	int err;

	err = qlcnic_alloc_mbx_args(&cmd, adapter, QLCNIC_CMD_DCB_QUERY_CAP);
	if (err)
		return err;

	err = qlcnic_issue_cmd(adapter, &cmd);
	if (err) {
		dev_err(&adapter->pdev->dev,
			"Failed to query DCBX capability, err %d\n", err);
	} else {
		mbx_out = cmd.rsp.arg[1];
		if (buf)
			memcpy(buf, &mbx_out, sizeof(u32));
	}

	qlcnic_free_mbx_args(&cmd);

	return err;
}

static int __qlcnic_dcb_get_capability(struct qlcnic_adapter *adapter, u32 *val)
{
	struct qlcnic_dcb_capability *cap = &adapter->dcb->cfg->capability;
	u32 mbx_out;
	int err;

	memset(cap, 0, sizeof(struct qlcnic_dcb_capability));

	err = qlcnic_dcb_query_hw_capability(adapter, (char *)val);
	if (err)
		return err;

	mbx_out = *val;
	if (QLC_DCB_TSA_SUPPORT(mbx_out))
		cap->tsa_capability = true;

	if (QLC_DCB_ETS_SUPPORT(mbx_out))
		cap->ets_capability = true;

	cap->max_num_tc = QLC_DCB_MAX_NUM_TC(mbx_out);
	cap->max_ets_tc = QLC_DCB_MAX_NUM_ETS_TC(mbx_out);
	cap->max_pfc_tc = QLC_DCB_MAX_NUM_PFC_TC(mbx_out);

	if (cap->max_num_tc > QLC_DCB_MAX_TC ||
	    cap->max_ets_tc > cap->max_num_tc ||
	    cap->max_pfc_tc > cap->max_num_tc) {
		dev_err(&adapter->pdev->dev, "Invalid DCB configuration\n");
		return -EINVAL;
	}

	return err;
}

static int qlcnic_82xx_dcb_get_hw_capability(struct qlcnic_adapter *adapter)
{
	struct qlcnic_dcb_cfg *cfg = adapter->dcb->cfg;
	struct qlcnic_dcb_capability *cap;
	u32 mbx_out;
	int err;

	err = __qlcnic_dcb_get_capability(adapter, &mbx_out);
	if (err)
		return err;

	cap = &cfg->capability;
	cap->dcb_capability = DCB_CAP_DCBX_VER_CEE | DCB_CAP_DCBX_LLD_MANAGED;

	if (cap->dcb_capability && cap->tsa_capability && cap->ets_capability)
		set_bit(__QLCNIC_DCB_STATE, &adapter->state);

	return err;
}

static int qlcnic_83xx_dcb_get_hw_capability(struct qlcnic_adapter *adapter)
{
	struct qlcnic_dcb_capability *cap = &adapter->dcb->cfg->capability;
	u32 mbx_out;
	int err;

	err = __qlcnic_dcb_get_capability(adapter, &mbx_out);
	if (err)
		return err;

	if (mbx_out & BIT_2)
		cap->dcb_capability = DCB_CAP_DCBX_VER_CEE;
	if (mbx_out & BIT_3)
		cap->dcb_capability |= DCB_CAP_DCBX_VER_IEEE;
	if (cap->dcb_capability)
		cap->dcb_capability |= DCB_CAP_DCBX_LLD_MANAGED;

	if (cap->dcb_capability && cap->tsa_capability && cap->ets_capability)
		set_bit(__QLCNIC_DCB_STATE, &adapter->state);

	return err;
}
