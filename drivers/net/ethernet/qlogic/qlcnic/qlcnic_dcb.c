/*
 * QLogic qlcnic NIC Driver
 * Copyright (c)  2009-2013 QLogic Corporation
 *
 * See LICENSE.qlcnic for copyright and licensing details.
 */

#include <linux/types.h>
#include "qlcnic.h"

#define QLC_DCB_NUM_PARAM		3

#define QLC_DCB_AEN_BIT			0x2
#define QLC_DCB_FW_VER			0x2
#define QLC_DCB_MAX_TC			0x8
#define QLC_DCB_MAX_APP			0x8

#define QLC_DCB_TSA_SUPPORT(V)		(V & 0x1)
#define QLC_DCB_ETS_SUPPORT(V)		((V >> 1) & 0x1)
#define QLC_DCB_VERSION_SUPPORT(V)	((V >> 2) & 0xf)
#define QLC_DCB_MAX_NUM_TC(V)		((V >> 20) & 0xf)
#define QLC_DCB_MAX_NUM_ETS_TC(V)	((V >> 24) & 0xf)
#define QLC_DCB_MAX_NUM_PFC_TC(V)	((V >> 28) & 0xf)
#define QLC_DCB_GET_TC_PRIO(X, P)	((X >> (P * 3)) & 0x7)
#define QLC_DCB_GET_PGID_PRIO(X, P)	((X >> (P * 8)) & 0xff)
#define QLC_DCB_GET_BWPER_PG(X, P)	((X >> (P * 8)) & 0xff)
#define QLC_DCB_GET_TSA_PG(X, P)	((X >> (P * 8)) & 0xff)
#define QLC_DCB_GET_PFC_PRIO(X, P)	(((X >> 24) >> P) & 0x1)
#define QLC_DCB_GET_PROTO_ID_APP(X)	((X >> 8) & 0xffff)
#define QLC_DCB_GET_SELECTOR_APP(X)	(X & 0xff)

#define QLC_DCB_LOCAL_PARAM_FWID	0x3
#define QLC_DCB_OPER_PARAM_FWID		0x1
#define QLC_DCB_PEER_PARAM_FWID		0x2

#define QLC_83XX_DCB_GET_NUMAPP(X)	((X >> 2) & 0xf)
#define QLC_83XX_DCB_TSA_VALID(X)	(X & 0x1)
#define QLC_83XX_DCB_PFC_VALID(X)	((X >> 1) & 0x1)
#define QLC_83XX_DCB_GET_PRIOMAP_APP(X)	(X >> 24)

#define QLC_82XX_DCB_GET_NUMAPP(X)	((X >> 12) & 0xf)
#define QLC_82XX_DCB_TSA_VALID(X)	((X >> 4) & 0x1)
#define QLC_82XX_DCB_PFC_VALID(X)	((X >> 5) & 0x1)
#define QLC_82XX_DCB_GET_PRIOVAL_APP(X)	((X >> 24) & 0x7)
#define QLC_82XX_DCB_GET_PRIOMAP_APP(X)	(1 << X)
#define QLC_82XX_DCB_PRIO_TC_MAP	(0x76543210)

static void qlcnic_dcb_aen_work(struct work_struct *);

static void __qlcnic_dcb_free(struct qlcnic_adapter *);
static int __qlcnic_dcb_attach(struct qlcnic_adapter *);
static int __qlcnic_dcb_query_hw_capability(struct qlcnic_adapter *, char *);
static void __qlcnic_dcb_get_info(struct qlcnic_adapter *);

static int qlcnic_82xx_dcb_get_hw_capability(struct qlcnic_adapter *);
static int qlcnic_82xx_dcb_query_cee_param(struct qlcnic_adapter *, char *, u8);
static int qlcnic_82xx_dcb_get_cee_cfg(struct qlcnic_adapter *);
static void qlcnic_82xx_dcb_handle_aen(struct qlcnic_adapter *, void *);

static int qlcnic_83xx_dcb_get_hw_capability(struct qlcnic_adapter *);
static int qlcnic_83xx_dcb_query_cee_param(struct qlcnic_adapter *, char *, u8);
static int qlcnic_83xx_dcb_get_cee_cfg(struct qlcnic_adapter *);
static int qlcnic_83xx_dcb_register_aen(struct qlcnic_adapter *, bool);
static void qlcnic_83xx_dcb_handle_aen(struct qlcnic_adapter *, void *);

struct qlcnic_dcb_capability {
	bool	tsa_capability;
	bool	ets_capability;
	u8	max_num_tc;
	u8	max_ets_tc;
	u8	max_pfc_tc;
	u8	dcb_capability;
};

struct qlcnic_dcb_param {
	u32 hdr_prio_pfc_map[2];
	u32 prio_pg_map[2];
	u32 pg_bw_map[2];
	u32 pg_tsa_map[2];
	u32 app[QLC_DCB_MAX_APP];
};

struct qlcnic_dcb_mbx_params {
	/* 1st local, 2nd operational 3rd remote */
	struct qlcnic_dcb_param type[3];
	u32 prio_tc_map;
};

struct qlcnic_82xx_dcb_param_mbx_le {
	__le32 hdr_prio_pfc_map[2];
	__le32 prio_pg_map[2];
	__le32 pg_bw_map[2];
	__le32 pg_tsa_map[2];
	__le32 app[QLC_DCB_MAX_APP];
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
	.query_cee_param	= qlcnic_83xx_dcb_query_cee_param,
	.get_cee_cfg		= qlcnic_83xx_dcb_get_cee_cfg,
	.register_aen		= qlcnic_83xx_dcb_register_aen,
	.handle_aen		= qlcnic_83xx_dcb_handle_aen,
};

static struct qlcnic_dcb_ops qlcnic_82xx_dcb_ops = {
	.free			= __qlcnic_dcb_free,
	.attach			= __qlcnic_dcb_attach,
	.query_hw_capability	= __qlcnic_dcb_query_hw_capability,
	.get_info		= __qlcnic_dcb_get_info,

	.get_hw_capability	= qlcnic_82xx_dcb_get_hw_capability,
	.query_cee_param	= qlcnic_82xx_dcb_query_cee_param,
	.get_cee_cfg		= qlcnic_82xx_dcb_get_cee_cfg,
	.handle_aen		= qlcnic_82xx_dcb_handle_aen,
};

static u8 qlcnic_dcb_get_num_app(struct qlcnic_adapter *adapter, u32 val)
{
	if (qlcnic_82xx_check(adapter))
		return QLC_82XX_DCB_GET_NUMAPP(val);
	else
		return QLC_83XX_DCB_GET_NUMAPP(val);
}

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
	dcb->adapter = adapter;
	qlcnic_set_dcb_ops(adapter);

	return 0;
}

static void __qlcnic_dcb_free(struct qlcnic_adapter *adapter)
{
	struct qlcnic_dcb *dcb = adapter->dcb;

	if (!dcb)
		return;

	qlcnic_dcb_register_aen(adapter, 0);

	while (test_bit(__QLCNIC_DCB_IN_AEN, &adapter->state))
		usleep_range(10000, 11000);

	cancel_delayed_work_sync(&dcb->aen_work);

	if (dcb->wq) {
		destroy_workqueue(dcb->wq);
		dcb->wq = NULL;
	}

	kfree(dcb->cfg);
	dcb->cfg = NULL;
	kfree(dcb->param);
	dcb->param = NULL;
	kfree(dcb);
	adapter->dcb = NULL;
}

static void __qlcnic_dcb_get_info(struct qlcnic_adapter *adapter)
{
	qlcnic_dcb_get_hw_capability(adapter);
	qlcnic_dcb_get_cee_cfg(adapter);
	qlcnic_dcb_register_aen(adapter, 1);
}

static int __qlcnic_dcb_attach(struct qlcnic_adapter *adapter)
{
	struct qlcnic_dcb *dcb = adapter->dcb;
	int err = 0;

	INIT_DELAYED_WORK(&dcb->aen_work, qlcnic_dcb_aen_work);

	dcb->wq = create_singlethread_workqueue("qlcnic-dcb");
	if (!dcb->wq) {
		dev_err(&adapter->pdev->dev,
			"DCB workqueue allocation failed. DCB will be disabled\n");
		return -1;
	}

	dcb->cfg = kzalloc(sizeof(struct qlcnic_dcb_cfg), GFP_ATOMIC);
	if (!dcb->cfg) {
		err = -ENOMEM;
		goto out_free_wq;
	}

	dcb->param = kzalloc(sizeof(struct qlcnic_dcb_mbx_params), GFP_ATOMIC);
	if (!dcb->param) {
		err = -ENOMEM;
		goto out_free_cfg;
	}

	qlcnic_dcb_get_info(adapter);

	return 0;
out_free_cfg:
	kfree(dcb->cfg);
	dcb->cfg = NULL;

out_free_wq:
	destroy_workqueue(dcb->wq);
	dcb->wq = NULL;

	return err;
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

static int qlcnic_82xx_dcb_query_cee_param(struct qlcnic_adapter *adapter,
					   char *buf, u8 type)
{
	u16 size = sizeof(struct qlcnic_82xx_dcb_param_mbx_le);
	struct qlcnic_82xx_dcb_param_mbx_le *prsp_le;
	struct device *dev = &adapter->pdev->dev;
	dma_addr_t cardrsp_phys_addr;
	struct qlcnic_dcb_param rsp;
	struct qlcnic_cmd_args cmd;
	u64 phys_addr;
	void *addr;
	int err, i;

	switch (type) {
	case QLC_DCB_LOCAL_PARAM_FWID:
	case QLC_DCB_OPER_PARAM_FWID:
	case QLC_DCB_PEER_PARAM_FWID:
		break;
	default:
		dev_err(dev, "Invalid parameter type %d\n", type);
		return -EINVAL;
	}

	addr = dma_alloc_coherent(&adapter->pdev->dev, size, &cardrsp_phys_addr,
				  GFP_KERNEL);
	if (addr == NULL)
		return -ENOMEM;

	prsp_le = addr;

	err = qlcnic_alloc_mbx_args(&cmd, adapter, QLCNIC_CMD_DCB_QUERY_PARAM);
	if (err)
		goto out_free_rsp;

	phys_addr = cardrsp_phys_addr;
	cmd.req.arg[1] = size | (type << 16);
	cmd.req.arg[2] = MSD(phys_addr);
	cmd.req.arg[3] = LSD(phys_addr);

	err = qlcnic_issue_cmd(adapter, &cmd);
	if (err) {
		dev_err(dev, "Failed to query DCBX parameter, err %d\n", err);
		goto out;
	}

	memset(&rsp, 0, sizeof(struct qlcnic_dcb_param));
	rsp.hdr_prio_pfc_map[0] = le32_to_cpu(prsp_le->hdr_prio_pfc_map[0]);
	rsp.hdr_prio_pfc_map[1] = le32_to_cpu(prsp_le->hdr_prio_pfc_map[1]);
	rsp.prio_pg_map[0] = le32_to_cpu(prsp_le->prio_pg_map[0]);
	rsp.prio_pg_map[1] = le32_to_cpu(prsp_le->prio_pg_map[1]);
	rsp.pg_bw_map[0] = le32_to_cpu(prsp_le->pg_bw_map[0]);
	rsp.pg_bw_map[1] = le32_to_cpu(prsp_le->pg_bw_map[1]);
	rsp.pg_tsa_map[0] = le32_to_cpu(prsp_le->pg_tsa_map[0]);
	rsp.pg_tsa_map[1] = le32_to_cpu(prsp_le->pg_tsa_map[1]);

	for (i = 0; i < QLC_DCB_MAX_APP; i++)
		rsp.app[i] = le32_to_cpu(prsp_le->app[i]);

	if (buf)
		memcpy(buf, &rsp, size);
out:
	qlcnic_free_mbx_args(&cmd);

out_free_rsp:
	dma_free_coherent(&adapter->pdev->dev, size, addr, cardrsp_phys_addr);

	return err;
}

static int qlcnic_82xx_dcb_get_cee_cfg(struct qlcnic_adapter *adapter)
{
	struct qlcnic_dcb_mbx_params *mbx;
	int err;

	mbx = adapter->dcb->param;
	if (!mbx)
		return 0;

	err = qlcnic_dcb_query_cee_param(adapter, (char *)&mbx->type[0],
					 QLC_DCB_LOCAL_PARAM_FWID);
	if (err)
		return err;

	err = qlcnic_dcb_query_cee_param(adapter, (char *)&mbx->type[1],
					 QLC_DCB_OPER_PARAM_FWID);
	if (err)
		return err;

	err = qlcnic_dcb_query_cee_param(adapter, (char *)&mbx->type[2],
					 QLC_DCB_PEER_PARAM_FWID);
	if (err)
		return err;

	mbx->prio_tc_map = QLC_82XX_DCB_PRIO_TC_MAP;

	return err;
}

static void qlcnic_dcb_aen_work(struct work_struct *work)
{
	struct qlcnic_adapter *adapter;
	struct qlcnic_dcb *dcb;

	dcb = container_of(work, struct qlcnic_dcb, aen_work.work);
	adapter = dcb->adapter;

	qlcnic_dcb_get_cee_cfg(adapter);
	clear_bit(__QLCNIC_DCB_IN_AEN, &adapter->state);
}

static void qlcnic_82xx_dcb_handle_aen(struct qlcnic_adapter *adapter,
				       void *data)
{
	struct qlcnic_dcb *dcb = adapter->dcb;

	if (test_and_set_bit(__QLCNIC_DCB_IN_AEN, &adapter->state))
		return;

	queue_delayed_work(dcb->wq, &dcb->aen_work, 0);
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

static int qlcnic_83xx_dcb_query_cee_param(struct qlcnic_adapter *adapter,
					   char *buf, u8 idx)
{
	struct qlcnic_dcb_mbx_params mbx_out;
	int err, i, j, k, max_app, size;
	struct qlcnic_dcb_param *each;
	struct qlcnic_cmd_args cmd;
	u32 val;
	char *p;

	size = 0;
	memset(&mbx_out, 0, sizeof(struct qlcnic_dcb_mbx_params));
	memset(buf, 0, sizeof(struct qlcnic_dcb_mbx_params));

	err = qlcnic_alloc_mbx_args(&cmd, adapter, QLCNIC_CMD_DCB_QUERY_PARAM);
	if (err)
		return err;

	cmd.req.arg[0] |= QLC_DCB_FW_VER << 29;
	err = qlcnic_issue_cmd(adapter, &cmd);
	if (err) {
		dev_err(&adapter->pdev->dev,
			"Failed to query DCBX param, err %d\n", err);
		goto out;
	}

	mbx_out.prio_tc_map = cmd.rsp.arg[1];
	p = memcpy(buf, &mbx_out, sizeof(u32));
	k = 2;
	p += sizeof(u32);

	for (j = 0; j < QLC_DCB_NUM_PARAM; j++) {
		each = &mbx_out.type[j];

		each->hdr_prio_pfc_map[0] = cmd.rsp.arg[k++];
		each->hdr_prio_pfc_map[1] = cmd.rsp.arg[k++];
		each->prio_pg_map[0] = cmd.rsp.arg[k++];
		each->prio_pg_map[1] = cmd.rsp.arg[k++];
		each->pg_bw_map[0] = cmd.rsp.arg[k++];
		each->pg_bw_map[1] = cmd.rsp.arg[k++];
		each->pg_tsa_map[0] = cmd.rsp.arg[k++];
		each->pg_tsa_map[1] = cmd.rsp.arg[k++];
		val = each->hdr_prio_pfc_map[0];

		max_app = qlcnic_dcb_get_num_app(adapter, val);
		for (i = 0; i < max_app; i++)
			each->app[i] = cmd.rsp.arg[i + k];

		size = 16 * sizeof(u32);
		memcpy(p, &each->hdr_prio_pfc_map[0], size);
		p += size;
		if (j == 0)
			k = 18;
		else
			k = 34;
	}
out:
	qlcnic_free_mbx_args(&cmd);

	return err;
}

static int qlcnic_83xx_dcb_get_cee_cfg(struct qlcnic_adapter *adapter)
{
	struct qlcnic_dcb *dcb = adapter->dcb;

	return qlcnic_dcb_query_cee_param(adapter, (char *)dcb->param, 0);
}

static int qlcnic_83xx_dcb_register_aen(struct qlcnic_adapter *adapter,
					bool flag)
{
	u8 val = (flag ? QLCNIC_CMD_INIT_NIC_FUNC : QLCNIC_CMD_STOP_NIC_FUNC);
	struct qlcnic_cmd_args cmd;
	int err;

	err = qlcnic_alloc_mbx_args(&cmd, adapter, val);
	if (err)
		return err;

	cmd.req.arg[1] = QLC_DCB_AEN_BIT;

	err = qlcnic_issue_cmd(adapter, &cmd);
	if (err)
		dev_err(&adapter->pdev->dev, "Failed to %s DCBX AEN, err %d\n",
			(flag ? "register" : "unregister"), err);

	qlcnic_free_mbx_args(&cmd);

	return err;
}

static void qlcnic_83xx_dcb_handle_aen(struct qlcnic_adapter *adapter,
				       void *data)
{
	struct qlcnic_dcb *dcb = adapter->dcb;
	u32 *val = data;

	if (test_and_set_bit(__QLCNIC_DCB_IN_AEN, &adapter->state))
		return;

	if (*val & BIT_8)
		set_bit(__QLCNIC_DCB_STATE, &adapter->state);
	else
		clear_bit(__QLCNIC_DCB_STATE, &adapter->state);

	queue_delayed_work(dcb->wq, &dcb->aen_work, 0);
}
