/*
 * QLogic qlcnic NIC Driver
 * Copyright (c)  2009-2013 QLogic Corporation
 *
 * See LICENSE.qlcnic for copyright and licensing details.
 */

#include <linux/types.h>
#include "qlcnic.h"

#define QLC_DCB_NUM_PARAM		3
#define QLC_DCB_LOCAL_IDX		0
#define QLC_DCB_OPER_IDX		1
#define QLC_DCB_PEER_IDX		2

#define QLC_DCB_GET_MAP(V)		(1 << V)

#define QLC_DCB_FW_VER			0x2
#define QLC_DCB_MAX_TC			0x8
#define QLC_DCB_MAX_APP			0x8
#define QLC_DCB_MAX_PRIO		QLC_DCB_MAX_TC
#define QLC_DCB_MAX_PG			QLC_DCB_MAX_TC

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

static const struct dcbnl_rtnl_ops qlcnic_dcbnl_ops;

static void qlcnic_dcb_aen_work(struct work_struct *);
static void qlcnic_dcb_data_cee_param_map(struct qlcnic_adapter *);

static inline void __qlcnic_init_dcbnl_ops(struct qlcnic_dcb *);
static void __qlcnic_dcb_free(struct qlcnic_dcb *);
static int __qlcnic_dcb_attach(struct qlcnic_dcb *);
static int __qlcnic_dcb_query_hw_capability(struct qlcnic_dcb *, char *);
static void __qlcnic_dcb_get_info(struct qlcnic_dcb *);

static int qlcnic_82xx_dcb_get_hw_capability(struct qlcnic_dcb *);
static int qlcnic_82xx_dcb_query_cee_param(struct qlcnic_dcb *, char *, u8);
static int qlcnic_82xx_dcb_get_cee_cfg(struct qlcnic_dcb *);
static void qlcnic_82xx_dcb_aen_handler(struct qlcnic_dcb *, void *);

static int qlcnic_83xx_dcb_get_hw_capability(struct qlcnic_dcb *);
static int qlcnic_83xx_dcb_query_cee_param(struct qlcnic_dcb *, char *, u8);
static int qlcnic_83xx_dcb_get_cee_cfg(struct qlcnic_dcb *);
static void qlcnic_83xx_dcb_aen_handler(struct qlcnic_dcb *, void *);

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

enum qlcnic_dcb_selector {
	QLC_SELECTOR_DEF = 0x0,
	QLC_SELECTOR_ETHER,
	QLC_SELECTOR_TCP,
	QLC_SELECTOR_UDP,
};

enum qlcnic_dcb_prio_type {
	QLC_PRIO_NONE = 0,
	QLC_PRIO_GROUP,
	QLC_PRIO_LINK,
};

enum qlcnic_dcb_pfc_type {
	QLC_PFC_DISABLED = 0,
	QLC_PFC_FULL,
	QLC_PFC_TX,
	QLC_PFC_RX
};

struct qlcnic_dcb_prio_cfg {
	bool valid;
	enum qlcnic_dcb_pfc_type pfc_type;
};

struct qlcnic_dcb_pg_cfg {
	bool valid;
	u8 total_bw_percent;		/* of Link/ port BW */
	u8 prio_count;
	u8 tsa_type;
};

struct qlcnic_dcb_tc_cfg {
	bool valid;
	struct qlcnic_dcb_prio_cfg prio_cfg[QLC_DCB_MAX_PRIO];
	enum qlcnic_dcb_prio_type prio_type;	/* always prio_link */
	u8 link_percent;			/* % of link bandwidth */
	u8 bwg_percent;				/* % of BWG's bandwidth */
	u8 up_tc_map;
	u8 pgid;
};

struct qlcnic_dcb_app {
	bool valid;
	enum qlcnic_dcb_selector selector;
	u16 protocol;
	u8 priority;
};

struct qlcnic_dcb_cee {
	struct qlcnic_dcb_tc_cfg tc_cfg[QLC_DCB_MAX_TC];
	struct qlcnic_dcb_pg_cfg pg_cfg[QLC_DCB_MAX_PG];
	struct qlcnic_dcb_app app[QLC_DCB_MAX_APP];
	bool tc_param_valid;
	bool pfc_mode_enable;
};

struct qlcnic_dcb_cfg {
	/* 0 - local, 1 - operational, 2 - remote */
	struct qlcnic_dcb_cee type[QLC_DCB_NUM_PARAM];
	struct qlcnic_dcb_capability capability;
	u32 version;
};

static struct qlcnic_dcb_ops qlcnic_83xx_dcb_ops = {
	.init_dcbnl_ops		= __qlcnic_init_dcbnl_ops,
	.free			= __qlcnic_dcb_free,
	.attach			= __qlcnic_dcb_attach,
	.query_hw_capability	= __qlcnic_dcb_query_hw_capability,
	.get_info		= __qlcnic_dcb_get_info,

	.get_hw_capability	= qlcnic_83xx_dcb_get_hw_capability,
	.query_cee_param	= qlcnic_83xx_dcb_query_cee_param,
	.get_cee_cfg		= qlcnic_83xx_dcb_get_cee_cfg,
	.aen_handler		= qlcnic_83xx_dcb_aen_handler,
};

static struct qlcnic_dcb_ops qlcnic_82xx_dcb_ops = {
	.init_dcbnl_ops		= __qlcnic_init_dcbnl_ops,
	.free			= __qlcnic_dcb_free,
	.attach			= __qlcnic_dcb_attach,
	.query_hw_capability	= __qlcnic_dcb_query_hw_capability,
	.get_info		= __qlcnic_dcb_get_info,

	.get_hw_capability	= qlcnic_82xx_dcb_get_hw_capability,
	.query_cee_param	= qlcnic_82xx_dcb_query_cee_param,
	.get_cee_cfg		= qlcnic_82xx_dcb_get_cee_cfg,
	.aen_handler		= qlcnic_82xx_dcb_aen_handler,
};

static u8 qlcnic_dcb_get_num_app(struct qlcnic_adapter *adapter, u32 val)
{
	if (qlcnic_82xx_check(adapter))
		return QLC_82XX_DCB_GET_NUMAPP(val);
	else
		return QLC_83XX_DCB_GET_NUMAPP(val);
}

static inline u8 qlcnic_dcb_pfc_hdr_valid(struct qlcnic_adapter *adapter,
					  u32 val)
{
	if (qlcnic_82xx_check(adapter))
		return QLC_82XX_DCB_PFC_VALID(val);
	else
		return QLC_83XX_DCB_PFC_VALID(val);
}

static inline u8 qlcnic_dcb_tsa_hdr_valid(struct qlcnic_adapter *adapter,
					  u32 val)
{
	if (qlcnic_82xx_check(adapter))
		return QLC_82XX_DCB_TSA_VALID(val);
	else
		return QLC_83XX_DCB_TSA_VALID(val);
}

static inline u8 qlcnic_dcb_get_prio_map_app(struct qlcnic_adapter *adapter,
					     u32 val)
{
	if (qlcnic_82xx_check(adapter))
		return QLC_82XX_DCB_GET_PRIOMAP_APP(val);
	else
		return QLC_83XX_DCB_GET_PRIOMAP_APP(val);
}

static int qlcnic_dcb_prio_count(u8 up_tc_map)
{
	int j;

	for (j = 0; j < QLC_DCB_MAX_TC; j++)
		if (up_tc_map & QLC_DCB_GET_MAP(j))
			break;

	return j;
}

static inline void __qlcnic_init_dcbnl_ops(struct qlcnic_dcb *dcb)
{
	if (test_bit(QLCNIC_DCB_STATE, &dcb->state))
		dcb->adapter->netdev->dcbnl_ops = &qlcnic_dcbnl_ops;
}

static void qlcnic_set_dcb_ops(struct qlcnic_adapter *adapter)
{
	if (qlcnic_82xx_check(adapter))
		adapter->dcb->ops = &qlcnic_82xx_dcb_ops;
	else if (qlcnic_83xx_check(adapter))
		adapter->dcb->ops = &qlcnic_83xx_dcb_ops;
}

int qlcnic_register_dcb(struct qlcnic_adapter *adapter)
{
	struct qlcnic_dcb *dcb;

	if (qlcnic_sriov_vf_check(adapter))
		return 0;

	dcb = kzalloc(sizeof(struct qlcnic_dcb), GFP_ATOMIC);
	if (!dcb)
		return -ENOMEM;

	adapter->dcb = dcb;
	dcb->adapter = adapter;
	qlcnic_set_dcb_ops(adapter);
	dcb->state = 0;

	return 0;
}

static void __qlcnic_dcb_free(struct qlcnic_dcb *dcb)
{
	struct qlcnic_adapter *adapter;

	if (!dcb)
		return;

	adapter = dcb->adapter;

	while (test_bit(QLCNIC_DCB_AEN_MODE, &dcb->state))
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

static void __qlcnic_dcb_get_info(struct qlcnic_dcb *dcb)
{
	qlcnic_dcb_get_hw_capability(dcb);
	qlcnic_dcb_get_cee_cfg(dcb);
}

static int __qlcnic_dcb_attach(struct qlcnic_dcb *dcb)
{
	int err = 0;

	INIT_DELAYED_WORK(&dcb->aen_work, qlcnic_dcb_aen_work);

	dcb->wq = create_singlethread_workqueue("qlcnic-dcb");
	if (!dcb->wq) {
		dev_err(&dcb->adapter->pdev->dev,
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

	qlcnic_dcb_get_info(dcb);

	return 0;
out_free_cfg:
	kfree(dcb->cfg);
	dcb->cfg = NULL;

out_free_wq:
	destroy_workqueue(dcb->wq);
	dcb->wq = NULL;

	return err;
}

static int __qlcnic_dcb_query_hw_capability(struct qlcnic_dcb *dcb, char *buf)
{
	struct qlcnic_adapter *adapter = dcb->adapter;
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

static int __qlcnic_dcb_get_capability(struct qlcnic_dcb *dcb, u32 *val)
{
	struct qlcnic_dcb_capability *cap = &dcb->cfg->capability;
	u32 mbx_out;
	int err;

	memset(cap, 0, sizeof(struct qlcnic_dcb_capability));

	err = qlcnic_dcb_query_hw_capability(dcb, (char *)val);
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
		dev_err(&dcb->adapter->pdev->dev, "Invalid DCB configuration\n");
		return -EINVAL;
	}

	return err;
}

static int qlcnic_82xx_dcb_get_hw_capability(struct qlcnic_dcb *dcb)
{
	struct qlcnic_dcb_cfg *cfg = dcb->cfg;
	struct qlcnic_dcb_capability *cap;
	u32 mbx_out;
	int err;

	err = __qlcnic_dcb_get_capability(dcb, &mbx_out);
	if (err)
		return err;

	cap = &cfg->capability;
	cap->dcb_capability = DCB_CAP_DCBX_VER_CEE | DCB_CAP_DCBX_LLD_MANAGED;

	if (cap->dcb_capability && cap->tsa_capability && cap->ets_capability)
		set_bit(QLCNIC_DCB_STATE, &dcb->state);

	return err;
}

static int qlcnic_82xx_dcb_query_cee_param(struct qlcnic_dcb *dcb,
					   char *buf, u8 type)
{
	u16 size = sizeof(struct qlcnic_82xx_dcb_param_mbx_le);
	struct qlcnic_adapter *adapter = dcb->adapter;
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

	addr = dma_alloc_coherent(dev, size, &cardrsp_phys_addr, GFP_KERNEL);
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
	dma_free_coherent(dev, size, addr, cardrsp_phys_addr);

	return err;
}

static int qlcnic_82xx_dcb_get_cee_cfg(struct qlcnic_dcb *dcb)
{
	struct qlcnic_dcb_mbx_params *mbx;
	int err;

	mbx = dcb->param;
	if (!mbx)
		return 0;

	err = qlcnic_dcb_query_cee_param(dcb, (char *)&mbx->type[0],
					 QLC_DCB_LOCAL_PARAM_FWID);
	if (err)
		return err;

	err = qlcnic_dcb_query_cee_param(dcb, (char *)&mbx->type[1],
					 QLC_DCB_OPER_PARAM_FWID);
	if (err)
		return err;

	err = qlcnic_dcb_query_cee_param(dcb, (char *)&mbx->type[2],
					 QLC_DCB_PEER_PARAM_FWID);
	if (err)
		return err;

	mbx->prio_tc_map = QLC_82XX_DCB_PRIO_TC_MAP;

	qlcnic_dcb_data_cee_param_map(dcb->adapter);

	return err;
}

static void qlcnic_dcb_aen_work(struct work_struct *work)
{
	struct qlcnic_dcb *dcb;

	dcb = container_of(work, struct qlcnic_dcb, aen_work.work);

	qlcnic_dcb_get_cee_cfg(dcb);
	clear_bit(QLCNIC_DCB_AEN_MODE, &dcb->state);
}

static void qlcnic_82xx_dcb_aen_handler(struct qlcnic_dcb *dcb, void *data)
{
	if (test_and_set_bit(QLCNIC_DCB_AEN_MODE, &dcb->state))
		return;

	queue_delayed_work(dcb->wq, &dcb->aen_work, 0);
}

static int qlcnic_83xx_dcb_get_hw_capability(struct qlcnic_dcb *dcb)
{
	struct qlcnic_dcb_capability *cap = &dcb->cfg->capability;
	u32 mbx_out;
	int err;

	err = __qlcnic_dcb_get_capability(dcb, &mbx_out);
	if (err)
		return err;

	if (mbx_out & BIT_2)
		cap->dcb_capability = DCB_CAP_DCBX_VER_CEE;
	if (mbx_out & BIT_3)
		cap->dcb_capability |= DCB_CAP_DCBX_VER_IEEE;
	if (cap->dcb_capability)
		cap->dcb_capability |= DCB_CAP_DCBX_LLD_MANAGED;

	if (cap->dcb_capability && cap->tsa_capability && cap->ets_capability)
		set_bit(QLCNIC_DCB_STATE, &dcb->state);

	return err;
}

static int qlcnic_83xx_dcb_query_cee_param(struct qlcnic_dcb *dcb,
					   char *buf, u8 idx)
{
	struct qlcnic_adapter *adapter = dcb->adapter;
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

static int qlcnic_83xx_dcb_get_cee_cfg(struct qlcnic_dcb *dcb)
{
	int err;

	err = qlcnic_dcb_query_cee_param(dcb, (char *)dcb->param, 0);
	if (err)
		return err;

	qlcnic_dcb_data_cee_param_map(dcb->adapter);

	return err;
}

static void qlcnic_83xx_dcb_aen_handler(struct qlcnic_dcb *dcb, void *data)
{
	u32 *val = data;

	if (test_and_set_bit(QLCNIC_DCB_AEN_MODE, &dcb->state))
		return;

	if (*val & BIT_8)
		set_bit(QLCNIC_DCB_STATE, &dcb->state);
	else
		clear_bit(QLCNIC_DCB_STATE, &dcb->state);

	queue_delayed_work(dcb->wq, &dcb->aen_work, 0);
}

static void qlcnic_dcb_fill_cee_tc_params(struct qlcnic_dcb_mbx_params *mbx,
					  struct qlcnic_dcb_param *each,
					  struct qlcnic_dcb_cee *type)
{
	struct qlcnic_dcb_tc_cfg *tc_cfg;
	u8 i, tc, pgid;

	for (i = 0; i < QLC_DCB_MAX_PRIO; i++) {
		tc = QLC_DCB_GET_TC_PRIO(mbx->prio_tc_map, i);
		tc_cfg = &type->tc_cfg[tc];
		tc_cfg->valid = true;
		tc_cfg->up_tc_map |= QLC_DCB_GET_MAP(i);

		if (QLC_DCB_GET_PFC_PRIO(each->hdr_prio_pfc_map[1], i) &&
		    type->pfc_mode_enable) {
			tc_cfg->prio_cfg[i].valid = true;
			tc_cfg->prio_cfg[i].pfc_type = QLC_PFC_FULL;
		}

		if (i < 4)
			pgid = QLC_DCB_GET_PGID_PRIO(each->prio_pg_map[0], i);
		else
			pgid = QLC_DCB_GET_PGID_PRIO(each->prio_pg_map[1], i);

		tc_cfg->pgid = pgid;

		tc_cfg->prio_type = QLC_PRIO_LINK;
		type->pg_cfg[tc_cfg->pgid].prio_count++;
	}
}

static void qlcnic_dcb_fill_cee_pg_params(struct qlcnic_dcb_param *each,
					  struct qlcnic_dcb_cee *type)
{
	struct qlcnic_dcb_pg_cfg *pg_cfg;
	u8 i, tsa, bw_per;

	for (i = 0; i < QLC_DCB_MAX_PG; i++) {
		pg_cfg = &type->pg_cfg[i];
		pg_cfg->valid = true;

		if (i < 4) {
			bw_per = QLC_DCB_GET_BWPER_PG(each->pg_bw_map[0], i);
			tsa = QLC_DCB_GET_TSA_PG(each->pg_tsa_map[0], i);
		} else {
			bw_per = QLC_DCB_GET_BWPER_PG(each->pg_bw_map[1], i);
			tsa = QLC_DCB_GET_TSA_PG(each->pg_tsa_map[1], i);
		}

		pg_cfg->total_bw_percent = bw_per;
		pg_cfg->tsa_type = tsa;
	}
}

static void
qlcnic_dcb_fill_cee_app_params(struct qlcnic_adapter *adapter, u8 idx,
			       struct qlcnic_dcb_param *each,
			       struct qlcnic_dcb_cee *type)
{
	struct qlcnic_dcb_app *app;
	u8 i, num_app, map, cnt;
	struct dcb_app new_app;

	num_app = qlcnic_dcb_get_num_app(adapter, each->hdr_prio_pfc_map[0]);
	for (i = 0; i < num_app; i++) {
		app = &type->app[i];
		app->valid = true;

		/* Only for CEE (-1) */
		app->selector = QLC_DCB_GET_SELECTOR_APP(each->app[i]) - 1;
		new_app.selector = app->selector;
		app->protocol = QLC_DCB_GET_PROTO_ID_APP(each->app[i]);
		new_app.protocol = app->protocol;
		map = qlcnic_dcb_get_prio_map_app(adapter, each->app[i]);
		cnt = qlcnic_dcb_prio_count(map);

		if (cnt >= QLC_DCB_MAX_TC)
			cnt = 0;

		app->priority = cnt;
		new_app.priority = cnt;

		if (idx == QLC_DCB_OPER_IDX && adapter->netdev->dcbnl_ops)
			dcb_setapp(adapter->netdev, &new_app);
	}
}

static void qlcnic_dcb_map_cee_params(struct qlcnic_adapter *adapter, u8 idx)
{
	struct qlcnic_dcb_mbx_params *mbx = adapter->dcb->param;
	struct qlcnic_dcb_param *each = &mbx->type[idx];
	struct qlcnic_dcb_cfg *cfg = adapter->dcb->cfg;
	struct qlcnic_dcb_cee *type = &cfg->type[idx];

	type->tc_param_valid = false;
	type->pfc_mode_enable = false;
	memset(type->tc_cfg, 0,
	       sizeof(struct qlcnic_dcb_tc_cfg) * QLC_DCB_MAX_TC);
	memset(type->pg_cfg, 0,
	       sizeof(struct qlcnic_dcb_pg_cfg) * QLC_DCB_MAX_TC);

	if (qlcnic_dcb_pfc_hdr_valid(adapter, each->hdr_prio_pfc_map[0]) &&
	    cfg->capability.max_pfc_tc)
		type->pfc_mode_enable = true;

	if (qlcnic_dcb_tsa_hdr_valid(adapter, each->hdr_prio_pfc_map[0]) &&
	    cfg->capability.max_ets_tc)
		type->tc_param_valid = true;

	qlcnic_dcb_fill_cee_tc_params(mbx, each, type);
	qlcnic_dcb_fill_cee_pg_params(each, type);
	qlcnic_dcb_fill_cee_app_params(adapter, idx, each, type);
}

static void qlcnic_dcb_data_cee_param_map(struct qlcnic_adapter *adapter)
{
	int i;

	for (i = 0; i < QLC_DCB_NUM_PARAM; i++)
		qlcnic_dcb_map_cee_params(adapter, i);

	dcbnl_cee_notify(adapter->netdev, RTM_GETDCB, DCB_CMD_CEE_GET, 0, 0);
}

static u8 qlcnic_dcb_get_state(struct net_device *netdev)
{
	struct qlcnic_adapter *adapter = netdev_priv(netdev);

	return test_bit(QLCNIC_DCB_STATE, &adapter->dcb->state);
}

static void qlcnic_dcb_get_perm_hw_addr(struct net_device *netdev, u8 *addr)
{
	memcpy(addr, netdev->perm_addr, netdev->addr_len);
}

static void
qlcnic_dcb_get_pg_tc_cfg_tx(struct net_device *netdev, int tc, u8 *prio,
			    u8 *pgid, u8 *bw_per, u8 *up_tc_map)
{
	struct qlcnic_adapter *adapter = netdev_priv(netdev);
	struct qlcnic_dcb_tc_cfg *tc_cfg, *temp;
	struct qlcnic_dcb_cee *type;
	u8 i, cnt, pg;

	type = &adapter->dcb->cfg->type[QLC_DCB_OPER_IDX];
	*prio = *pgid = *bw_per = *up_tc_map = 0;

	if (!test_bit(QLCNIC_DCB_STATE, &adapter->dcb->state) ||
	    !type->tc_param_valid)
		return;

	if (tc < 0 || (tc >= QLC_DCB_MAX_TC))
		return;

	tc_cfg = &type->tc_cfg[tc];
	if (!tc_cfg->valid)
		return;

	*pgid = tc_cfg->pgid;
	*prio = tc_cfg->prio_type;
	*up_tc_map = tc_cfg->up_tc_map;
	pg = *pgid;

	for (i = 0, cnt = 0; i < QLC_DCB_MAX_TC; i++) {
		temp = &type->tc_cfg[i];
		if (temp->valid && (pg == temp->pgid))
			cnt++;
	}

	tc_cfg->bwg_percent = (100 / cnt);
	*bw_per = tc_cfg->bwg_percent;
}

static void qlcnic_dcb_get_pg_bwg_cfg_tx(struct net_device *netdev, int pgid,
					 u8 *bw_pct)
{
	struct qlcnic_adapter *adapter = netdev_priv(netdev);
	struct qlcnic_dcb_pg_cfg *pgcfg;
	struct qlcnic_dcb_cee *type;

	*bw_pct = 0;
	type = &adapter->dcb->cfg->type[QLC_DCB_OPER_IDX];

	if (!test_bit(QLCNIC_DCB_STATE, &adapter->dcb->state) ||
	    !type->tc_param_valid)
		return;

	if (pgid < 0 || pgid >= QLC_DCB_MAX_PG)
		return;

	pgcfg = &type->pg_cfg[pgid];
	if (!pgcfg->valid)
		return;

	*bw_pct = pgcfg->total_bw_percent;
}

static void qlcnic_dcb_get_pfc_cfg(struct net_device *netdev, int prio,
				   u8 *setting)
{
	struct qlcnic_adapter *adapter = netdev_priv(netdev);
	struct qlcnic_dcb_tc_cfg *tc_cfg;
	u8 val = QLC_DCB_GET_MAP(prio);
	struct qlcnic_dcb_cee *type;
	u8 i;

	*setting = 0;
	type = &adapter->dcb->cfg->type[QLC_DCB_OPER_IDX];

	if (!test_bit(QLCNIC_DCB_STATE, &adapter->dcb->state) ||
	    !type->pfc_mode_enable)
		return;

	for (i = 0; i < QLC_DCB_MAX_TC; i++) {
		tc_cfg = &type->tc_cfg[i];
		if (!tc_cfg->valid)
			continue;

		if ((val & tc_cfg->up_tc_map) && (tc_cfg->prio_cfg[prio].valid))
			*setting = tc_cfg->prio_cfg[prio].pfc_type;
	}
}

static u8 qlcnic_dcb_get_capability(struct net_device *netdev, int capid,
				    u8 *cap)
{
	struct qlcnic_adapter *adapter = netdev_priv(netdev);

	if (!test_bit(QLCNIC_DCB_STATE, &adapter->dcb->state))
		return 0;

	switch (capid) {
	case DCB_CAP_ATTR_PG:
	case DCB_CAP_ATTR_UP2TC:
	case DCB_CAP_ATTR_PFC:
	case DCB_CAP_ATTR_GSP:
		*cap = true;
		break;
	case DCB_CAP_ATTR_PG_TCS:
	case DCB_CAP_ATTR_PFC_TCS:
		*cap = 0x80;	/* 8 priorities for PGs */
		break;
	case DCB_CAP_ATTR_DCBX:
		*cap = adapter->dcb->cfg->capability.dcb_capability;
		break;
	default:
		*cap = false;
	}

	return 0;
}

static int qlcnic_dcb_get_num_tcs(struct net_device *netdev, int attr, u8 *num)
{
	struct qlcnic_adapter *adapter = netdev_priv(netdev);
	struct qlcnic_dcb_cfg *cfg = adapter->dcb->cfg;

	if (!test_bit(QLCNIC_DCB_STATE, &adapter->dcb->state))
		return -EINVAL;

	switch (attr) {
	case DCB_NUMTCS_ATTR_PG:
		*num = cfg->capability.max_ets_tc;
		return 0;
	case DCB_NUMTCS_ATTR_PFC:
		*num = cfg->capability.max_pfc_tc;
		return 0;
	default:
		return -EINVAL;
	}
}

static u8 qlcnic_dcb_get_app(struct net_device *netdev, u8 idtype, u16 id)
{
	struct qlcnic_adapter *adapter = netdev_priv(netdev);
	struct dcb_app app = {
				.selector = idtype,
				.protocol = id,
			     };

	if (!test_bit(QLCNIC_DCB_STATE, &adapter->dcb->state))
		return 0;

	return dcb_getapp(netdev, &app);
}

static u8 qlcnic_dcb_get_pfc_state(struct net_device *netdev)
{
	struct qlcnic_adapter *adapter = netdev_priv(netdev);
	struct qlcnic_dcb *dcb = adapter->dcb;

	if (!test_bit(QLCNIC_DCB_STATE, &dcb->state))
		return 0;

	return dcb->cfg->type[QLC_DCB_OPER_IDX].pfc_mode_enable;
}

static u8 qlcnic_dcb_get_dcbx(struct net_device *netdev)
{
	struct qlcnic_adapter *adapter = netdev_priv(netdev);
	struct qlcnic_dcb_cfg *cfg = adapter->dcb->cfg;

	if (!test_bit(QLCNIC_DCB_STATE, &adapter->dcb->state))
		return 0;

	return cfg->capability.dcb_capability;
}

static u8 qlcnic_dcb_get_feat_cfg(struct net_device *netdev, int fid, u8 *flag)
{
	struct qlcnic_adapter *adapter = netdev_priv(netdev);
	struct qlcnic_dcb_cee *type;

	if (!test_bit(QLCNIC_DCB_STATE, &adapter->dcb->state))
		return 1;

	type = &adapter->dcb->cfg->type[QLC_DCB_OPER_IDX];
	*flag = 0;

	switch (fid) {
	case DCB_FEATCFG_ATTR_PG:
		if (type->tc_param_valid)
			*flag |= DCB_FEATCFG_ENABLE;
		else
			*flag |= DCB_FEATCFG_ERROR;
		break;
	case DCB_FEATCFG_ATTR_PFC:
		if (type->pfc_mode_enable) {
			if (type->tc_cfg[0].prio_cfg[0].pfc_type)
				*flag |= DCB_FEATCFG_ENABLE;
		} else {
			*flag |= DCB_FEATCFG_ERROR;
		}
		break;
	case DCB_FEATCFG_ATTR_APP:
		*flag |= DCB_FEATCFG_ENABLE;
		break;
	default:
		netdev_err(netdev, "Invalid Feature ID %d\n", fid);
		return 1;
	}

	return 0;
}

static inline void
qlcnic_dcb_get_pg_tc_cfg_rx(struct net_device *netdev, int prio, u8 *prio_type,
			    u8 *pgid, u8 *bw_pct, u8 *up_map)
{
	*prio_type = *pgid = *bw_pct = *up_map = 0;
}

static inline void
qlcnic_dcb_get_pg_bwg_cfg_rx(struct net_device *netdev, int pgid, u8 *bw_pct)
{
	*bw_pct = 0;
}

static int qlcnic_dcb_peer_app_info(struct net_device *netdev,
				    struct dcb_peer_app_info *info,
				    u16 *app_count)
{
	struct qlcnic_adapter *adapter = netdev_priv(netdev);
	struct qlcnic_dcb_cee *peer;
	int i;

	*app_count = 0;

	if (!test_bit(QLCNIC_DCB_STATE, &adapter->dcb->state))
		return 0;

	peer = &adapter->dcb->cfg->type[QLC_DCB_PEER_IDX];

	for (i = 0; i < QLC_DCB_MAX_APP; i++) {
		if (peer->app[i].valid)
			(*app_count)++;
	}

	return 0;
}

static int qlcnic_dcb_peer_app_table(struct net_device *netdev,
				     struct dcb_app *table)
{
	struct qlcnic_adapter *adapter = netdev_priv(netdev);
	struct qlcnic_dcb_cee *peer;
	struct qlcnic_dcb_app *app;
	int i, j;

	if (!test_bit(QLCNIC_DCB_STATE, &adapter->dcb->state))
		return 0;

	peer = &adapter->dcb->cfg->type[QLC_DCB_PEER_IDX];

	for (i = 0, j = 0; i < QLC_DCB_MAX_APP; i++) {
		app = &peer->app[i];
		if (!app->valid)
			continue;

		table[j].selector = app->selector;
		table[j].priority = app->priority;
		table[j++].protocol = app->protocol;
	}

	return 0;
}

static int qlcnic_dcb_cee_peer_get_pg(struct net_device *netdev,
				      struct cee_pg *pg)
{
	struct qlcnic_adapter *adapter = netdev_priv(netdev);
	struct qlcnic_dcb_cee *peer;
	u8 i, j, k, map;

	if (!test_bit(QLCNIC_DCB_STATE, &adapter->dcb->state))
		return 0;

	peer = &adapter->dcb->cfg->type[QLC_DCB_PEER_IDX];

	for (i = 0, j = 0; i < QLC_DCB_MAX_PG; i++) {
		if (!peer->pg_cfg[i].valid)
			continue;

		pg->pg_bw[j] = peer->pg_cfg[i].total_bw_percent;

		for (k = 0; k < QLC_DCB_MAX_TC; k++) {
			if (peer->tc_cfg[i].valid &&
			    (peer->tc_cfg[i].pgid == i)) {
				map = peer->tc_cfg[i].up_tc_map;
				pg->prio_pg[j++] = map;
				break;
			}
		}
	}

	return 0;
}

static int qlcnic_dcb_cee_peer_get_pfc(struct net_device *netdev,
				       struct cee_pfc *pfc)
{
	struct qlcnic_adapter *adapter = netdev_priv(netdev);
	struct qlcnic_dcb_cfg *cfg = adapter->dcb->cfg;
	struct qlcnic_dcb_tc_cfg *tc;
	struct qlcnic_dcb_cee *peer;
	u8 i, setting, prio;

	pfc->pfc_en = 0;

	if (!test_bit(QLCNIC_DCB_STATE, &adapter->dcb->state))
		return 0;

	peer = &cfg->type[QLC_DCB_PEER_IDX];

	for (i = 0; i < QLC_DCB_MAX_TC; i++) {
		tc = &peer->tc_cfg[i];
		prio = qlcnic_dcb_prio_count(tc->up_tc_map);

		setting = 0;
		qlcnic_dcb_get_pfc_cfg(netdev, prio, &setting);
		if (setting)
			pfc->pfc_en |= QLC_DCB_GET_MAP(i);
	}

	pfc->tcs_supported = cfg->capability.max_pfc_tc;

	return 0;
}

static const struct dcbnl_rtnl_ops qlcnic_dcbnl_ops = {
	.getstate		= qlcnic_dcb_get_state,
	.getpermhwaddr		= qlcnic_dcb_get_perm_hw_addr,
	.getpgtccfgtx		= qlcnic_dcb_get_pg_tc_cfg_tx,
	.getpgbwgcfgtx		= qlcnic_dcb_get_pg_bwg_cfg_tx,
	.getpfccfg		= qlcnic_dcb_get_pfc_cfg,
	.getcap			= qlcnic_dcb_get_capability,
	.getnumtcs		= qlcnic_dcb_get_num_tcs,
	.getapp			= qlcnic_dcb_get_app,
	.getpfcstate		= qlcnic_dcb_get_pfc_state,
	.getdcbx		= qlcnic_dcb_get_dcbx,
	.getfeatcfg		= qlcnic_dcb_get_feat_cfg,

	.getpgtccfgrx		= qlcnic_dcb_get_pg_tc_cfg_rx,
	.getpgbwgcfgrx		= qlcnic_dcb_get_pg_bwg_cfg_rx,

	.peer_getappinfo	= qlcnic_dcb_peer_app_info,
	.peer_getapptable	= qlcnic_dcb_peer_app_table,
	.cee_peer_getpg		= qlcnic_dcb_cee_peer_get_pg,
	.cee_peer_getpfc	= qlcnic_dcb_cee_peer_get_pfc,
};
