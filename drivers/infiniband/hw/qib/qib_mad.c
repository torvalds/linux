/*
 * Copyright (c) 2012 Intel Corporation.  All rights reserved.
 * Copyright (c) 2006 - 2012 QLogic Corporation. All rights reserved.
 * Copyright (c) 2005, 2006 PathScale, Inc. All rights reserved.
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
 *        disclaimer in the documentation and/or other materials
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

#include <rdma/ib_smi.h>

#include "qib.h"
#include "qib_mad.h"

static int reply(struct ib_smp *smp)
{
	/*
	 * The verbs framework will handle the directed/LID route
	 * packet changes.
	 */
	smp->method = IB_MGMT_METHOD_GET_RESP;
	if (smp->mgmt_class == IB_MGMT_CLASS_SUBN_DIRECTED_ROUTE)
		smp->status |= IB_SMP_DIRECTION;
	return IB_MAD_RESULT_SUCCESS | IB_MAD_RESULT_REPLY;
}

static int reply_failure(struct ib_smp *smp)
{
	/*
	 * The verbs framework will handle the directed/LID route
	 * packet changes.
	 */
	smp->method = IB_MGMT_METHOD_GET_RESP;
	if (smp->mgmt_class == IB_MGMT_CLASS_SUBN_DIRECTED_ROUTE)
		smp->status |= IB_SMP_DIRECTION;
	return IB_MAD_RESULT_FAILURE | IB_MAD_RESULT_REPLY;
}

static void qib_send_trap(struct qib_ibport *ibp, void *data, unsigned len)
{
	struct ib_mad_send_buf *send_buf;
	struct ib_mad_agent *agent;
	struct ib_smp *smp;
	int ret;
	unsigned long flags;
	unsigned long timeout;

	agent = ibp->rvp.send_agent;
	if (!agent)
		return;

	/* o14-3.2.1 */
	if (!(ppd_from_ibp(ibp)->lflags & QIBL_LINKACTIVE))
		return;

	/* o14-2 */
	if (ibp->rvp.trap_timeout &&
	    time_before(jiffies, ibp->rvp.trap_timeout))
		return;

	send_buf = ib_create_send_mad(agent, 0, 0, 0, IB_MGMT_MAD_HDR,
				      IB_MGMT_MAD_DATA, GFP_ATOMIC,
				      IB_MGMT_BASE_VERSION);
	if (IS_ERR(send_buf))
		return;

	smp = send_buf->mad;
	smp->base_version = IB_MGMT_BASE_VERSION;
	smp->mgmt_class = IB_MGMT_CLASS_SUBN_LID_ROUTED;
	smp->class_version = 1;
	smp->method = IB_MGMT_METHOD_TRAP;
	ibp->rvp.tid++;
	smp->tid = cpu_to_be64(ibp->rvp.tid);
	smp->attr_id = IB_SMP_ATTR_NOTICE;
	/* o14-1: smp->mkey = 0; */
	memcpy(smp->data, data, len);

	spin_lock_irqsave(&ibp->rvp.lock, flags);
	if (!ibp->rvp.sm_ah) {
		if (ibp->rvp.sm_lid != be16_to_cpu(IB_LID_PERMISSIVE)) {
			struct ib_ah *ah;

			ah = qib_create_qp0_ah(ibp, (u16)ibp->rvp.sm_lid);
			if (IS_ERR(ah))
				ret = PTR_ERR(ah);
			else {
				send_buf->ah = ah;
				ibp->rvp.sm_ah = ibah_to_rvtah(ah);
				ret = 0;
			}
		} else
			ret = -EINVAL;
	} else {
		send_buf->ah = &ibp->rvp.sm_ah->ibah;
		ret = 0;
	}
	spin_unlock_irqrestore(&ibp->rvp.lock, flags);

	if (!ret)
		ret = ib_post_send_mad(send_buf, NULL);
	if (!ret) {
		/* 4.096 usec. */
		timeout = (4096 * (1UL << ibp->rvp.subnet_timeout)) / 1000;
		ibp->rvp.trap_timeout = jiffies + usecs_to_jiffies(timeout);
	} else {
		ib_free_send_mad(send_buf);
		ibp->rvp.trap_timeout = 0;
	}
}

/*
 * Send a bad P_Key trap (ch. 14.3.8).
 */
void qib_bad_pkey(struct qib_ibport *ibp, u32 key, u32 sl,
		  u32 qp1, u32 qp2, __be16 lid1, __be16 lid2)
{
	struct ib_mad_notice_attr data;

	ibp->rvp.n_pkt_drops++;
	ibp->rvp.pkey_violations++;

	/* Send violation trap */
	data.generic_type = IB_NOTICE_TYPE_SECURITY;
	data.prod_type_msb = 0;
	data.prod_type_lsb = IB_NOTICE_PROD_CA;
	data.trap_num = IB_NOTICE_TRAP_BAD_PKEY;
	data.issuer_lid = cpu_to_be16(ppd_from_ibp(ibp)->lid);
	data.toggle_count = 0;
	memset(&data.details, 0, sizeof(data.details));
	data.details.ntc_257_258.lid1 = lid1;
	data.details.ntc_257_258.lid2 = lid2;
	data.details.ntc_257_258.key = cpu_to_be32(key);
	data.details.ntc_257_258.sl_qp1 = cpu_to_be32((sl << 28) | qp1);
	data.details.ntc_257_258.qp2 = cpu_to_be32(qp2);

	qib_send_trap(ibp, &data, sizeof(data));
}

/*
 * Send a bad M_Key trap (ch. 14.3.9).
 */
static void qib_bad_mkey(struct qib_ibport *ibp, struct ib_smp *smp)
{
	struct ib_mad_notice_attr data;

	/* Send violation trap */
	data.generic_type = IB_NOTICE_TYPE_SECURITY;
	data.prod_type_msb = 0;
	data.prod_type_lsb = IB_NOTICE_PROD_CA;
	data.trap_num = IB_NOTICE_TRAP_BAD_MKEY;
	data.issuer_lid = cpu_to_be16(ppd_from_ibp(ibp)->lid);
	data.toggle_count = 0;
	memset(&data.details, 0, sizeof(data.details));
	data.details.ntc_256.lid = data.issuer_lid;
	data.details.ntc_256.method = smp->method;
	data.details.ntc_256.attr_id = smp->attr_id;
	data.details.ntc_256.attr_mod = smp->attr_mod;
	data.details.ntc_256.mkey = smp->mkey;
	if (smp->mgmt_class == IB_MGMT_CLASS_SUBN_DIRECTED_ROUTE) {
		u8 hop_cnt;

		data.details.ntc_256.dr_slid = smp->dr_slid;
		data.details.ntc_256.dr_trunc_hop = IB_NOTICE_TRAP_DR_NOTICE;
		hop_cnt = smp->hop_cnt;
		if (hop_cnt > ARRAY_SIZE(data.details.ntc_256.dr_rtn_path)) {
			data.details.ntc_256.dr_trunc_hop |=
				IB_NOTICE_TRAP_DR_TRUNC;
			hop_cnt = ARRAY_SIZE(data.details.ntc_256.dr_rtn_path);
		}
		data.details.ntc_256.dr_trunc_hop |= hop_cnt;
		memcpy(data.details.ntc_256.dr_rtn_path, smp->return_path,
		       hop_cnt);
	}

	qib_send_trap(ibp, &data, sizeof(data));
}

/*
 * Send a Port Capability Mask Changed trap (ch. 14.3.11).
 */
void qib_cap_mask_chg(struct rvt_dev_info *rdi, u8 port_num)
{
	struct qib_ibdev *ibdev = container_of(rdi, struct qib_ibdev, rdi);
	struct qib_devdata *dd = dd_from_dev(ibdev);
	struct qib_ibport *ibp = &dd->pport[port_num - 1].ibport_data;
	struct ib_mad_notice_attr data;

	data.generic_type = IB_NOTICE_TYPE_INFO;
	data.prod_type_msb = 0;
	data.prod_type_lsb = IB_NOTICE_PROD_CA;
	data.trap_num = IB_NOTICE_TRAP_CAP_MASK_CHG;
	data.issuer_lid = cpu_to_be16(ppd_from_ibp(ibp)->lid);
	data.toggle_count = 0;
	memset(&data.details, 0, sizeof(data.details));
	data.details.ntc_144.lid = data.issuer_lid;
	data.details.ntc_144.new_cap_mask =
					cpu_to_be32(ibp->rvp.port_cap_flags);
	qib_send_trap(ibp, &data, sizeof(data));
}

/*
 * Send a System Image GUID Changed trap (ch. 14.3.12).
 */
void qib_sys_guid_chg(struct qib_ibport *ibp)
{
	struct ib_mad_notice_attr data;

	data.generic_type = IB_NOTICE_TYPE_INFO;
	data.prod_type_msb = 0;
	data.prod_type_lsb = IB_NOTICE_PROD_CA;
	data.trap_num = IB_NOTICE_TRAP_SYS_GUID_CHG;
	data.issuer_lid = cpu_to_be16(ppd_from_ibp(ibp)->lid);
	data.toggle_count = 0;
	memset(&data.details, 0, sizeof(data.details));
	data.details.ntc_145.lid = data.issuer_lid;
	data.details.ntc_145.new_sys_guid = ib_qib_sys_image_guid;

	qib_send_trap(ibp, &data, sizeof(data));
}

/*
 * Send a Node Description Changed trap (ch. 14.3.13).
 */
void qib_node_desc_chg(struct qib_ibport *ibp)
{
	struct ib_mad_notice_attr data;

	data.generic_type = IB_NOTICE_TYPE_INFO;
	data.prod_type_msb = 0;
	data.prod_type_lsb = IB_NOTICE_PROD_CA;
	data.trap_num = IB_NOTICE_TRAP_CAP_MASK_CHG;
	data.issuer_lid = cpu_to_be16(ppd_from_ibp(ibp)->lid);
	data.toggle_count = 0;
	memset(&data.details, 0, sizeof(data.details));
	data.details.ntc_144.lid = data.issuer_lid;
	data.details.ntc_144.local_changes = 1;
	data.details.ntc_144.change_flags = IB_NOTICE_TRAP_NODE_DESC_CHG;

	qib_send_trap(ibp, &data, sizeof(data));
}

static int subn_get_nodedescription(struct ib_smp *smp,
				    struct ib_device *ibdev)
{
	if (smp->attr_mod)
		smp->status |= IB_SMP_INVALID_FIELD;

	memcpy(smp->data, ibdev->node_desc, sizeof(smp->data));

	return reply(smp);
}

static int subn_get_nodeinfo(struct ib_smp *smp, struct ib_device *ibdev,
			     u8 port)
{
	struct ib_node_info *nip = (struct ib_node_info *)&smp->data;
	struct qib_devdata *dd = dd_from_ibdev(ibdev);
	u32 vendor, majrev, minrev;
	unsigned pidx = port - 1; /* IB number port from 1, hdw from 0 */

	/* GUID 0 is illegal */
	if (smp->attr_mod || pidx >= dd->num_pports ||
	    dd->pport[pidx].guid == 0)
		smp->status |= IB_SMP_INVALID_FIELD;
	else
		nip->port_guid = dd->pport[pidx].guid;

	nip->base_version = 1;
	nip->class_version = 1;
	nip->node_type = 1;     /* channel adapter */
	nip->num_ports = ibdev->phys_port_cnt;
	/* This is already in network order */
	nip->sys_guid = ib_qib_sys_image_guid;
	nip->node_guid = dd->pport->guid; /* Use first-port GUID as node */
	nip->partition_cap = cpu_to_be16(qib_get_npkeys(dd));
	nip->device_id = cpu_to_be16(dd->deviceid);
	majrev = dd->majrev;
	minrev = dd->minrev;
	nip->revision = cpu_to_be32((majrev << 16) | minrev);
	nip->local_port_num = port;
	vendor = dd->vendorid;
	nip->vendor_id[0] = QIB_SRC_OUI_1;
	nip->vendor_id[1] = QIB_SRC_OUI_2;
	nip->vendor_id[2] = QIB_SRC_OUI_3;

	return reply(smp);
}

static int subn_get_guidinfo(struct ib_smp *smp, struct ib_device *ibdev,
			     u8 port)
{
	struct qib_devdata *dd = dd_from_ibdev(ibdev);
	u32 startgx = 8 * be32_to_cpu(smp->attr_mod);
	__be64 *p = (__be64 *) smp->data;
	unsigned pidx = port - 1; /* IB number port from 1, hdw from 0 */

	/* 32 blocks of 8 64-bit GUIDs per block */

	memset(smp->data, 0, sizeof(smp->data));

	if (startgx == 0 && pidx < dd->num_pports) {
		struct qib_pportdata *ppd = dd->pport + pidx;
		struct qib_ibport *ibp = &ppd->ibport_data;
		__be64 g = ppd->guid;
		unsigned i;

		/* GUID 0 is illegal */
		if (g == 0)
			smp->status |= IB_SMP_INVALID_FIELD;
		else {
			/* The first is a copy of the read-only HW GUID. */
			p[0] = g;
			for (i = 1; i < QIB_GUIDS_PER_PORT; i++)
				p[i] = ibp->guids[i - 1];
		}
	} else
		smp->status |= IB_SMP_INVALID_FIELD;

	return reply(smp);
}

static void set_link_width_enabled(struct qib_pportdata *ppd, u32 w)
{
	(void) ppd->dd->f_set_ib_cfg(ppd, QIB_IB_CFG_LWID_ENB, w);
}

static void set_link_speed_enabled(struct qib_pportdata *ppd, u32 s)
{
	(void) ppd->dd->f_set_ib_cfg(ppd, QIB_IB_CFG_SPD_ENB, s);
}

static int get_overrunthreshold(struct qib_pportdata *ppd)
{
	return ppd->dd->f_get_ib_cfg(ppd, QIB_IB_CFG_OVERRUN_THRESH);
}

/**
 * set_overrunthreshold - set the overrun threshold
 * @ppd: the physical port data
 * @n: the new threshold
 *
 * Note that this will only take effect when the link state changes.
 */
static int set_overrunthreshold(struct qib_pportdata *ppd, unsigned n)
{
	(void) ppd->dd->f_set_ib_cfg(ppd, QIB_IB_CFG_OVERRUN_THRESH,
					 (u32)n);
	return 0;
}

static int get_phyerrthreshold(struct qib_pportdata *ppd)
{
	return ppd->dd->f_get_ib_cfg(ppd, QIB_IB_CFG_PHYERR_THRESH);
}

/**
 * set_phyerrthreshold - set the physical error threshold
 * @ppd: the physical port data
 * @n: the new threshold
 *
 * Note that this will only take effect when the link state changes.
 */
static int set_phyerrthreshold(struct qib_pportdata *ppd, unsigned n)
{
	(void) ppd->dd->f_set_ib_cfg(ppd, QIB_IB_CFG_PHYERR_THRESH,
					 (u32)n);
	return 0;
}

/**
 * get_linkdowndefaultstate - get the default linkdown state
 * @ppd: the physical port data
 *
 * Returns zero if the default is POLL, 1 if the default is SLEEP.
 */
static int get_linkdowndefaultstate(struct qib_pportdata *ppd)
{
	return ppd->dd->f_get_ib_cfg(ppd, QIB_IB_CFG_LINKDEFAULT) ==
		IB_LINKINITCMD_SLEEP;
}

static int check_mkey(struct qib_ibport *ibp, struct ib_smp *smp, int mad_flags)
{
	int valid_mkey = 0;
	int ret = 0;

	/* Is the mkey in the process of expiring? */
	if (ibp->rvp.mkey_lease_timeout &&
	    time_after_eq(jiffies, ibp->rvp.mkey_lease_timeout)) {
		/* Clear timeout and mkey protection field. */
		ibp->rvp.mkey_lease_timeout = 0;
		ibp->rvp.mkeyprot = 0;
	}

	if ((mad_flags & IB_MAD_IGNORE_MKEY) ||  ibp->rvp.mkey == 0 ||
	    ibp->rvp.mkey == smp->mkey)
		valid_mkey = 1;

	/* Unset lease timeout on any valid Get/Set/TrapRepress */
	if (valid_mkey && ibp->rvp.mkey_lease_timeout &&
	    (smp->method == IB_MGMT_METHOD_GET ||
	     smp->method == IB_MGMT_METHOD_SET ||
	     smp->method == IB_MGMT_METHOD_TRAP_REPRESS))
		ibp->rvp.mkey_lease_timeout = 0;

	if (!valid_mkey) {
		switch (smp->method) {
		case IB_MGMT_METHOD_GET:
			/* Bad mkey not a violation below level 2 */
			if (ibp->rvp.mkeyprot < 2)
				break;
		case IB_MGMT_METHOD_SET:
		case IB_MGMT_METHOD_TRAP_REPRESS:
			if (ibp->rvp.mkey_violations != 0xFFFF)
				++ibp->rvp.mkey_violations;
			if (!ibp->rvp.mkey_lease_timeout &&
			    ibp->rvp.mkey_lease_period)
				ibp->rvp.mkey_lease_timeout = jiffies +
					ibp->rvp.mkey_lease_period * HZ;
			/* Generate a trap notice. */
			qib_bad_mkey(ibp, smp);
			ret = 1;
		}
	}

	return ret;
}

static int subn_get_portinfo(struct ib_smp *smp, struct ib_device *ibdev,
			     u8 port)
{
	struct qib_devdata *dd;
	struct qib_pportdata *ppd;
	struct qib_ibport *ibp;
	struct ib_port_info *pip = (struct ib_port_info *)smp->data;
	u8 mtu;
	int ret;
	u32 state;
	u32 port_num = be32_to_cpu(smp->attr_mod);

	if (port_num == 0)
		port_num = port;
	else {
		if (port_num > ibdev->phys_port_cnt) {
			smp->status |= IB_SMP_INVALID_FIELD;
			ret = reply(smp);
			goto bail;
		}
		if (port_num != port) {
			ibp = to_iport(ibdev, port_num);
			ret = check_mkey(ibp, smp, 0);
			if (ret) {
				ret = IB_MAD_RESULT_FAILURE;
				goto bail;
			}
		}
	}

	dd = dd_from_ibdev(ibdev);
	/* IB numbers ports from 1, hdw from 0 */
	ppd = dd->pport + (port_num - 1);
	ibp = &ppd->ibport_data;

	/* Clear all fields.  Only set the non-zero fields. */
	memset(smp->data, 0, sizeof(smp->data));

	/* Only return the mkey if the protection field allows it. */
	if (!(smp->method == IB_MGMT_METHOD_GET &&
	      ibp->rvp.mkey != smp->mkey &&
	      ibp->rvp.mkeyprot == 1))
		pip->mkey = ibp->rvp.mkey;
	pip->gid_prefix = ibp->rvp.gid_prefix;
	pip->lid = cpu_to_be16(ppd->lid);
	pip->sm_lid = cpu_to_be16((u16)ibp->rvp.sm_lid);
	pip->cap_mask = cpu_to_be32(ibp->rvp.port_cap_flags);
	/* pip->diag_code; */
	pip->mkey_lease_period = cpu_to_be16(ibp->rvp.mkey_lease_period);
	pip->local_port_num = port;
	pip->link_width_enabled = ppd->link_width_enabled;
	pip->link_width_supported = ppd->link_width_supported;
	pip->link_width_active = ppd->link_width_active;
	state = dd->f_iblink_state(ppd->lastibcstat);
	pip->linkspeed_portstate = ppd->link_speed_supported << 4 | state;

	pip->portphysstate_linkdown =
		(dd->f_ibphys_portstate(ppd->lastibcstat) << 4) |
		(get_linkdowndefaultstate(ppd) ? 1 : 2);
	pip->mkeyprot_resv_lmc = (ibp->rvp.mkeyprot << 6) | ppd->lmc;
	pip->linkspeedactive_enabled = (ppd->link_speed_active << 4) |
		ppd->link_speed_enabled;
	switch (ppd->ibmtu) {
	default: /* something is wrong; fall through */
	case 4096:
		mtu = IB_MTU_4096;
		break;
	case 2048:
		mtu = IB_MTU_2048;
		break;
	case 1024:
		mtu = IB_MTU_1024;
		break;
	case 512:
		mtu = IB_MTU_512;
		break;
	case 256:
		mtu = IB_MTU_256;
		break;
	}
	pip->neighbormtu_mastersmsl = (mtu << 4) | ibp->rvp.sm_sl;
	pip->vlcap_inittype = ppd->vls_supported << 4;  /* InitType = 0 */
	pip->vl_high_limit = ibp->rvp.vl_high_limit;
	pip->vl_arb_high_cap =
		dd->f_get_ib_cfg(ppd, QIB_IB_CFG_VL_HIGH_CAP);
	pip->vl_arb_low_cap =
		dd->f_get_ib_cfg(ppd, QIB_IB_CFG_VL_LOW_CAP);
	/* InitTypeReply = 0 */
	pip->inittypereply_mtucap = qib_ibmtu ? qib_ibmtu : IB_MTU_4096;
	/* HCAs ignore VLStallCount and HOQLife */
	/* pip->vlstallcnt_hoqlife; */
	pip->operationalvl_pei_peo_fpi_fpo =
		dd->f_get_ib_cfg(ppd, QIB_IB_CFG_OP_VLS) << 4;
	pip->mkey_violations = cpu_to_be16(ibp->rvp.mkey_violations);
	/* P_KeyViolations are counted by hardware. */
	pip->pkey_violations = cpu_to_be16(ibp->rvp.pkey_violations);
	pip->qkey_violations = cpu_to_be16(ibp->rvp.qkey_violations);
	/* Only the hardware GUID is supported for now */
	pip->guid_cap = QIB_GUIDS_PER_PORT;
	pip->clientrereg_resv_subnetto = ibp->rvp.subnet_timeout;
	/* 32.768 usec. response time (guessing) */
	pip->resv_resptimevalue = 3;
	pip->localphyerrors_overrunerrors =
		(get_phyerrthreshold(ppd) << 4) |
		get_overrunthreshold(ppd);
	/* pip->max_credit_hint; */
	if (ibp->rvp.port_cap_flags & IB_PORT_LINK_LATENCY_SUP) {
		u32 v;

		v = dd->f_get_ib_cfg(ppd, QIB_IB_CFG_LINKLATENCY);
		pip->link_roundtrip_latency[0] = v >> 16;
		pip->link_roundtrip_latency[1] = v >> 8;
		pip->link_roundtrip_latency[2] = v;
	}

	ret = reply(smp);

bail:
	return ret;
}

/**
 * get_pkeys - return the PKEY table
 * @dd: the qlogic_ib device
 * @port: the IB port number
 * @pkeys: the pkey table is placed here
 */
static int get_pkeys(struct qib_devdata *dd, u8 port, u16 *pkeys)
{
	struct qib_pportdata *ppd = dd->pport + port - 1;
	/*
	 * always a kernel context, no locking needed.
	 * If we get here with ppd setup, no need to check
	 * that pd is valid.
	 */
	struct qib_ctxtdata *rcd = dd->rcd[ppd->hw_pidx];

	memcpy(pkeys, rcd->pkeys, sizeof(rcd->pkeys));

	return 0;
}

static int subn_get_pkeytable(struct ib_smp *smp, struct ib_device *ibdev,
			      u8 port)
{
	u32 startpx = 32 * (be32_to_cpu(smp->attr_mod) & 0xffff);
	u16 *p = (u16 *) smp->data;
	__be16 *q = (__be16 *) smp->data;

	/* 64 blocks of 32 16-bit P_Key entries */

	memset(smp->data, 0, sizeof(smp->data));
	if (startpx == 0) {
		struct qib_devdata *dd = dd_from_ibdev(ibdev);
		unsigned i, n = qib_get_npkeys(dd);

		get_pkeys(dd, port, p);

		for (i = 0; i < n; i++)
			q[i] = cpu_to_be16(p[i]);
	} else
		smp->status |= IB_SMP_INVALID_FIELD;

	return reply(smp);
}

static int subn_set_guidinfo(struct ib_smp *smp, struct ib_device *ibdev,
			     u8 port)
{
	struct qib_devdata *dd = dd_from_ibdev(ibdev);
	u32 startgx = 8 * be32_to_cpu(smp->attr_mod);
	__be64 *p = (__be64 *) smp->data;
	unsigned pidx = port - 1; /* IB number port from 1, hdw from 0 */

	/* 32 blocks of 8 64-bit GUIDs per block */

	if (startgx == 0 && pidx < dd->num_pports) {
		struct qib_pportdata *ppd = dd->pport + pidx;
		struct qib_ibport *ibp = &ppd->ibport_data;
		unsigned i;

		/* The first entry is read-only. */
		for (i = 1; i < QIB_GUIDS_PER_PORT; i++)
			ibp->guids[i - 1] = p[i];
	} else
		smp->status |= IB_SMP_INVALID_FIELD;

	/* The only GUID we support is the first read-only entry. */
	return subn_get_guidinfo(smp, ibdev, port);
}

/**
 * subn_set_portinfo - set port information
 * @smp: the incoming SM packet
 * @ibdev: the infiniband device
 * @port: the port on the device
 *
 * Set Portinfo (see ch. 14.2.5.6).
 */
static int subn_set_portinfo(struct ib_smp *smp, struct ib_device *ibdev,
			     u8 port)
{
	struct ib_port_info *pip = (struct ib_port_info *)smp->data;
	struct ib_event event;
	struct qib_devdata *dd;
	struct qib_pportdata *ppd;
	struct qib_ibport *ibp;
	u8 clientrereg = (pip->clientrereg_resv_subnetto & 0x80);
	unsigned long flags;
	u16 lid, smlid;
	u8 lwe;
	u8 lse;
	u8 state;
	u8 vls;
	u8 msl;
	u16 lstate;
	int ret, ore, mtu;
	u32 port_num = be32_to_cpu(smp->attr_mod);

	if (port_num == 0)
		port_num = port;
	else {
		if (port_num > ibdev->phys_port_cnt)
			goto err;
		/* Port attributes can only be set on the receiving port */
		if (port_num != port)
			goto get_only;
	}

	dd = dd_from_ibdev(ibdev);
	/* IB numbers ports from 1, hdw from 0 */
	ppd = dd->pport + (port_num - 1);
	ibp = &ppd->ibport_data;
	event.device = ibdev;
	event.element.port_num = port;

	ibp->rvp.mkey = pip->mkey;
	ibp->rvp.gid_prefix = pip->gid_prefix;
	ibp->rvp.mkey_lease_period = be16_to_cpu(pip->mkey_lease_period);

	lid = be16_to_cpu(pip->lid);
	/* Must be a valid unicast LID address. */
	if (lid == 0 || lid >= be16_to_cpu(IB_MULTICAST_LID_BASE))
		smp->status |= IB_SMP_INVALID_FIELD;
	else if (ppd->lid != lid || ppd->lmc != (pip->mkeyprot_resv_lmc & 7)) {
		if (ppd->lid != lid)
			qib_set_uevent_bits(ppd, _QIB_EVENT_LID_CHANGE_BIT);
		if (ppd->lmc != (pip->mkeyprot_resv_lmc & 7))
			qib_set_uevent_bits(ppd, _QIB_EVENT_LMC_CHANGE_BIT);
		qib_set_lid(ppd, lid, pip->mkeyprot_resv_lmc & 7);
		event.event = IB_EVENT_LID_CHANGE;
		ib_dispatch_event(&event);
	}

	smlid = be16_to_cpu(pip->sm_lid);
	msl = pip->neighbormtu_mastersmsl & 0xF;
	/* Must be a valid unicast LID address. */
	if (smlid == 0 || smlid >= be16_to_cpu(IB_MULTICAST_LID_BASE))
		smp->status |= IB_SMP_INVALID_FIELD;
	else if (smlid != ibp->rvp.sm_lid || msl != ibp->rvp.sm_sl) {
		spin_lock_irqsave(&ibp->rvp.lock, flags);
		if (ibp->rvp.sm_ah) {
			if (smlid != ibp->rvp.sm_lid)
				rdma_ah_set_dlid(&ibp->rvp.sm_ah->attr,
						 smlid);
			if (msl != ibp->rvp.sm_sl)
				rdma_ah_set_sl(&ibp->rvp.sm_ah->attr, msl);
		}
		spin_unlock_irqrestore(&ibp->rvp.lock, flags);
		if (smlid != ibp->rvp.sm_lid)
			ibp->rvp.sm_lid = smlid;
		if (msl != ibp->rvp.sm_sl)
			ibp->rvp.sm_sl = msl;
		event.event = IB_EVENT_SM_CHANGE;
		ib_dispatch_event(&event);
	}

	/* Allow 1x or 4x to be set (see 14.2.6.6). */
	lwe = pip->link_width_enabled;
	if (lwe) {
		if (lwe == 0xFF)
			set_link_width_enabled(ppd, ppd->link_width_supported);
		else if (lwe >= 16 || (lwe & ~ppd->link_width_supported))
			smp->status |= IB_SMP_INVALID_FIELD;
		else if (lwe != ppd->link_width_enabled)
			set_link_width_enabled(ppd, lwe);
	}

	lse = pip->linkspeedactive_enabled & 0xF;
	if (lse) {
		/*
		 * The IB 1.2 spec. only allows link speed values
		 * 1, 3, 5, 7, 15.  1.2.1 extended to allow specific
		 * speeds.
		 */
		if (lse == 15)
			set_link_speed_enabled(ppd,
					       ppd->link_speed_supported);
		else if (lse >= 8 || (lse & ~ppd->link_speed_supported))
			smp->status |= IB_SMP_INVALID_FIELD;
		else if (lse != ppd->link_speed_enabled)
			set_link_speed_enabled(ppd, lse);
	}

	/* Set link down default state. */
	switch (pip->portphysstate_linkdown & 0xF) {
	case 0: /* NOP */
		break;
	case 1: /* SLEEP */
		(void) dd->f_set_ib_cfg(ppd, QIB_IB_CFG_LINKDEFAULT,
					IB_LINKINITCMD_SLEEP);
		break;
	case 2: /* POLL */
		(void) dd->f_set_ib_cfg(ppd, QIB_IB_CFG_LINKDEFAULT,
					IB_LINKINITCMD_POLL);
		break;
	default:
		smp->status |= IB_SMP_INVALID_FIELD;
	}

	ibp->rvp.mkeyprot = pip->mkeyprot_resv_lmc >> 6;
	ibp->rvp.vl_high_limit = pip->vl_high_limit;
	(void) dd->f_set_ib_cfg(ppd, QIB_IB_CFG_VL_HIGH_LIMIT,
				    ibp->rvp.vl_high_limit);

	mtu = ib_mtu_enum_to_int((pip->neighbormtu_mastersmsl >> 4) & 0xF);
	if (mtu == -1)
		smp->status |= IB_SMP_INVALID_FIELD;
	else
		qib_set_mtu(ppd, mtu);

	/* Set operational VLs */
	vls = (pip->operationalvl_pei_peo_fpi_fpo >> 4) & 0xF;
	if (vls) {
		if (vls > ppd->vls_supported)
			smp->status |= IB_SMP_INVALID_FIELD;
		else
			(void) dd->f_set_ib_cfg(ppd, QIB_IB_CFG_OP_VLS, vls);
	}

	if (pip->mkey_violations == 0)
		ibp->rvp.mkey_violations = 0;

	if (pip->pkey_violations == 0)
		ibp->rvp.pkey_violations = 0;

	if (pip->qkey_violations == 0)
		ibp->rvp.qkey_violations = 0;

	ore = pip->localphyerrors_overrunerrors;
	if (set_phyerrthreshold(ppd, (ore >> 4) & 0xF))
		smp->status |= IB_SMP_INVALID_FIELD;

	if (set_overrunthreshold(ppd, (ore & 0xF)))
		smp->status |= IB_SMP_INVALID_FIELD;

	ibp->rvp.subnet_timeout = pip->clientrereg_resv_subnetto & 0x1F;

	/*
	 * Do the port state change now that the other link parameters
	 * have been set.
	 * Changing the port physical state only makes sense if the link
	 * is down or is being set to down.
	 */
	state = pip->linkspeed_portstate & 0xF;
	lstate = (pip->portphysstate_linkdown >> 4) & 0xF;
	if (lstate && !(state == IB_PORT_DOWN || state == IB_PORT_NOP))
		smp->status |= IB_SMP_INVALID_FIELD;

	/*
	 * Only state changes of DOWN, ARM, and ACTIVE are valid
	 * and must be in the correct state to take effect (see 7.2.6).
	 */
	switch (state) {
	case IB_PORT_NOP:
		if (lstate == 0)
			break;
		/* FALLTHROUGH */
	case IB_PORT_DOWN:
		if (lstate == 0)
			lstate = QIB_IB_LINKDOWN_ONLY;
		else if (lstate == 1)
			lstate = QIB_IB_LINKDOWN_SLEEP;
		else if (lstate == 2)
			lstate = QIB_IB_LINKDOWN;
		else if (lstate == 3)
			lstate = QIB_IB_LINKDOWN_DISABLE;
		else {
			smp->status |= IB_SMP_INVALID_FIELD;
			break;
		}
		spin_lock_irqsave(&ppd->lflags_lock, flags);
		ppd->lflags &= ~QIBL_LINKV;
		spin_unlock_irqrestore(&ppd->lflags_lock, flags);
		qib_set_linkstate(ppd, lstate);
		/*
		 * Don't send a reply if the response would be sent
		 * through the disabled port.
		 */
		if (lstate == QIB_IB_LINKDOWN_DISABLE && smp->hop_cnt) {
			ret = IB_MAD_RESULT_SUCCESS | IB_MAD_RESULT_CONSUMED;
			goto done;
		}
		qib_wait_linkstate(ppd, QIBL_LINKV, 10);
		break;
	case IB_PORT_ARMED:
		qib_set_linkstate(ppd, QIB_IB_LINKARM);
		break;
	case IB_PORT_ACTIVE:
		qib_set_linkstate(ppd, QIB_IB_LINKACTIVE);
		break;
	default:
		smp->status |= IB_SMP_INVALID_FIELD;
	}

	if (clientrereg) {
		event.event = IB_EVENT_CLIENT_REREGISTER;
		ib_dispatch_event(&event);
	}

	/* restore re-reg bit per o14-12.2.1 */
	pip->clientrereg_resv_subnetto |= clientrereg;

	goto get_only;

err:
	smp->status |= IB_SMP_INVALID_FIELD;
get_only:
	ret = subn_get_portinfo(smp, ibdev, port);
done:
	return ret;
}

/**
 * rm_pkey - decrecment the reference count for the given PKEY
 * @dd: the qlogic_ib device
 * @key: the PKEY index
 *
 * Return true if this was the last reference and the hardware table entry
 * needs to be changed.
 */
static int rm_pkey(struct qib_pportdata *ppd, u16 key)
{
	int i;
	int ret;

	for (i = 0; i < ARRAY_SIZE(ppd->pkeys); i++) {
		if (ppd->pkeys[i] != key)
			continue;
		if (atomic_dec_and_test(&ppd->pkeyrefs[i])) {
			ppd->pkeys[i] = 0;
			ret = 1;
			goto bail;
		}
		break;
	}

	ret = 0;

bail:
	return ret;
}

/**
 * add_pkey - add the given PKEY to the hardware table
 * @dd: the qlogic_ib device
 * @key: the PKEY
 *
 * Return an error code if unable to add the entry, zero if no change,
 * or 1 if the hardware PKEY register needs to be updated.
 */
static int add_pkey(struct qib_pportdata *ppd, u16 key)
{
	int i;
	u16 lkey = key & 0x7FFF;
	int any = 0;
	int ret;

	if (lkey == 0x7FFF) {
		ret = 0;
		goto bail;
	}

	/* Look for an empty slot or a matching PKEY. */
	for (i = 0; i < ARRAY_SIZE(ppd->pkeys); i++) {
		if (!ppd->pkeys[i]) {
			any++;
			continue;
		}
		/* If it matches exactly, try to increment the ref count */
		if (ppd->pkeys[i] == key) {
			if (atomic_inc_return(&ppd->pkeyrefs[i]) > 1) {
				ret = 0;
				goto bail;
			}
			/* Lost the race. Look for an empty slot below. */
			atomic_dec(&ppd->pkeyrefs[i]);
			any++;
		}
		/*
		 * It makes no sense to have both the limited and unlimited
		 * PKEY set at the same time since the unlimited one will
		 * disable the limited one.
		 */
		if ((ppd->pkeys[i] & 0x7FFF) == lkey) {
			ret = -EEXIST;
			goto bail;
		}
	}
	if (!any) {
		ret = -EBUSY;
		goto bail;
	}
	for (i = 0; i < ARRAY_SIZE(ppd->pkeys); i++) {
		if (!ppd->pkeys[i] &&
		    atomic_inc_return(&ppd->pkeyrefs[i]) == 1) {
			/* for qibstats, etc. */
			ppd->pkeys[i] = key;
			ret = 1;
			goto bail;
		}
	}
	ret = -EBUSY;

bail:
	return ret;
}

/**
 * set_pkeys - set the PKEY table for ctxt 0
 * @dd: the qlogic_ib device
 * @port: the IB port number
 * @pkeys: the PKEY table
 */
static int set_pkeys(struct qib_devdata *dd, u8 port, u16 *pkeys)
{
	struct qib_pportdata *ppd;
	struct qib_ctxtdata *rcd;
	int i;
	int changed = 0;

	/*
	 * IB port one/two always maps to context zero/one,
	 * always a kernel context, no locking needed
	 * If we get here with ppd setup, no need to check
	 * that rcd is valid.
	 */
	ppd = dd->pport + (port - 1);
	rcd = dd->rcd[ppd->hw_pidx];

	for (i = 0; i < ARRAY_SIZE(rcd->pkeys); i++) {
		u16 key = pkeys[i];
		u16 okey = rcd->pkeys[i];

		if (key == okey)
			continue;
		/*
		 * The value of this PKEY table entry is changing.
		 * Remove the old entry in the hardware's array of PKEYs.
		 */
		if (okey & 0x7FFF)
			changed |= rm_pkey(ppd, okey);
		if (key & 0x7FFF) {
			int ret = add_pkey(ppd, key);

			if (ret < 0)
				key = 0;
			else
				changed |= ret;
		}
		rcd->pkeys[i] = key;
	}
	if (changed) {
		struct ib_event event;

		(void) dd->f_set_ib_cfg(ppd, QIB_IB_CFG_PKEYS, 0);

		event.event = IB_EVENT_PKEY_CHANGE;
		event.device = &dd->verbs_dev.rdi.ibdev;
		event.element.port_num = port;
		ib_dispatch_event(&event);
	}
	return 0;
}

static int subn_set_pkeytable(struct ib_smp *smp, struct ib_device *ibdev,
			      u8 port)
{
	u32 startpx = 32 * (be32_to_cpu(smp->attr_mod) & 0xffff);
	__be16 *p = (__be16 *) smp->data;
	u16 *q = (u16 *) smp->data;
	struct qib_devdata *dd = dd_from_ibdev(ibdev);
	unsigned i, n = qib_get_npkeys(dd);

	for (i = 0; i < n; i++)
		q[i] = be16_to_cpu(p[i]);

	if (startpx != 0 || set_pkeys(dd, port, q) != 0)
		smp->status |= IB_SMP_INVALID_FIELD;

	return subn_get_pkeytable(smp, ibdev, port);
}

static int subn_get_sl_to_vl(struct ib_smp *smp, struct ib_device *ibdev,
			     u8 port)
{
	struct qib_ibport *ibp = to_iport(ibdev, port);
	u8 *p = (u8 *) smp->data;
	unsigned i;

	memset(smp->data, 0, sizeof(smp->data));

	if (!(ibp->rvp.port_cap_flags & IB_PORT_SL_MAP_SUP))
		smp->status |= IB_SMP_UNSUP_METHOD;
	else
		for (i = 0; i < ARRAY_SIZE(ibp->sl_to_vl); i += 2)
			*p++ = (ibp->sl_to_vl[i] << 4) | ibp->sl_to_vl[i + 1];

	return reply(smp);
}

static int subn_set_sl_to_vl(struct ib_smp *smp, struct ib_device *ibdev,
			     u8 port)
{
	struct qib_ibport *ibp = to_iport(ibdev, port);
	u8 *p = (u8 *) smp->data;
	unsigned i;

	if (!(ibp->rvp.port_cap_flags & IB_PORT_SL_MAP_SUP)) {
		smp->status |= IB_SMP_UNSUP_METHOD;
		return reply(smp);
	}

	for (i = 0; i < ARRAY_SIZE(ibp->sl_to_vl); i += 2, p++) {
		ibp->sl_to_vl[i] = *p >> 4;
		ibp->sl_to_vl[i + 1] = *p & 0xF;
	}
	qib_set_uevent_bits(ppd_from_ibp(to_iport(ibdev, port)),
			    _QIB_EVENT_SL2VL_CHANGE_BIT);

	return subn_get_sl_to_vl(smp, ibdev, port);
}

static int subn_get_vl_arb(struct ib_smp *smp, struct ib_device *ibdev,
			   u8 port)
{
	unsigned which = be32_to_cpu(smp->attr_mod) >> 16;
	struct qib_pportdata *ppd = ppd_from_ibp(to_iport(ibdev, port));

	memset(smp->data, 0, sizeof(smp->data));

	if (ppd->vls_supported == IB_VL_VL0)
		smp->status |= IB_SMP_UNSUP_METHOD;
	else if (which == IB_VLARB_LOWPRI_0_31)
		(void) ppd->dd->f_get_ib_table(ppd, QIB_IB_TBL_VL_LOW_ARB,
						   smp->data);
	else if (which == IB_VLARB_HIGHPRI_0_31)
		(void) ppd->dd->f_get_ib_table(ppd, QIB_IB_TBL_VL_HIGH_ARB,
						   smp->data);
	else
		smp->status |= IB_SMP_INVALID_FIELD;

	return reply(smp);
}

static int subn_set_vl_arb(struct ib_smp *smp, struct ib_device *ibdev,
			   u8 port)
{
	unsigned which = be32_to_cpu(smp->attr_mod) >> 16;
	struct qib_pportdata *ppd = ppd_from_ibp(to_iport(ibdev, port));

	if (ppd->vls_supported == IB_VL_VL0)
		smp->status |= IB_SMP_UNSUP_METHOD;
	else if (which == IB_VLARB_LOWPRI_0_31)
		(void) ppd->dd->f_set_ib_table(ppd, QIB_IB_TBL_VL_LOW_ARB,
						   smp->data);
	else if (which == IB_VLARB_HIGHPRI_0_31)
		(void) ppd->dd->f_set_ib_table(ppd, QIB_IB_TBL_VL_HIGH_ARB,
						   smp->data);
	else
		smp->status |= IB_SMP_INVALID_FIELD;

	return subn_get_vl_arb(smp, ibdev, port);
}

static int subn_trap_repress(struct ib_smp *smp, struct ib_device *ibdev,
			     u8 port)
{
	/*
	 * For now, we only send the trap once so no need to process this.
	 * o13-6, o13-7,
	 * o14-3.a4 The SMA shall not send any message in response to a valid
	 * SubnTrapRepress() message.
	 */
	return IB_MAD_RESULT_SUCCESS | IB_MAD_RESULT_CONSUMED;
}

static int pma_get_classportinfo(struct ib_pma_mad *pmp,
				 struct ib_device *ibdev)
{
	struct ib_class_port_info *p =
		(struct ib_class_port_info *)pmp->data;
	struct qib_devdata *dd = dd_from_ibdev(ibdev);

	memset(pmp->data, 0, sizeof(pmp->data));

	if (pmp->mad_hdr.attr_mod != 0)
		pmp->mad_hdr.status |= IB_SMP_INVALID_FIELD;

	/* Note that AllPortSelect is not valid */
	p->base_version = 1;
	p->class_version = 1;
	p->capability_mask = IB_PMA_CLASS_CAP_EXT_WIDTH;
	/*
	 * Set the most significant bit of CM2 to indicate support for
	 * congestion statistics
	 */
	ib_set_cpi_capmask2(p,
			    dd->psxmitwait_supported <<
			    (31 - IB_CLASS_PORT_INFO_RESP_TIME_FIELD_SIZE));
	/*
	 * Expected response time is 4.096 usec. * 2^18 == 1.073741824 sec.
	 */
	ib_set_cpi_resp_time(p, 18);

	return reply((struct ib_smp *) pmp);
}

static int pma_get_portsamplescontrol(struct ib_pma_mad *pmp,
				      struct ib_device *ibdev, u8 port)
{
	struct ib_pma_portsamplescontrol *p =
		(struct ib_pma_portsamplescontrol *)pmp->data;
	struct qib_ibdev *dev = to_idev(ibdev);
	struct qib_devdata *dd = dd_from_dev(dev);
	struct qib_ibport *ibp = to_iport(ibdev, port);
	struct qib_pportdata *ppd = ppd_from_ibp(ibp);
	unsigned long flags;
	u8 port_select = p->port_select;

	memset(pmp->data, 0, sizeof(pmp->data));

	p->port_select = port_select;
	if (pmp->mad_hdr.attr_mod != 0 || port_select != port) {
		pmp->mad_hdr.status |= IB_SMP_INVALID_FIELD;
		goto bail;
	}
	spin_lock_irqsave(&ibp->rvp.lock, flags);
	p->tick = dd->f_get_ib_cfg(ppd, QIB_IB_CFG_PMA_TICKS);
	p->sample_status = dd->f_portcntr(ppd, QIBPORTCNTR_PSSTAT);
	p->counter_width = 4;   /* 32 bit counters */
	p->counter_mask0_9 = COUNTER_MASK0_9;
	p->sample_start = cpu_to_be32(ibp->rvp.pma_sample_start);
	p->sample_interval = cpu_to_be32(ibp->rvp.pma_sample_interval);
	p->tag = cpu_to_be16(ibp->rvp.pma_tag);
	p->counter_select[0] = ibp->rvp.pma_counter_select[0];
	p->counter_select[1] = ibp->rvp.pma_counter_select[1];
	p->counter_select[2] = ibp->rvp.pma_counter_select[2];
	p->counter_select[3] = ibp->rvp.pma_counter_select[3];
	p->counter_select[4] = ibp->rvp.pma_counter_select[4];
	spin_unlock_irqrestore(&ibp->rvp.lock, flags);

bail:
	return reply((struct ib_smp *) pmp);
}

static int pma_set_portsamplescontrol(struct ib_pma_mad *pmp,
				      struct ib_device *ibdev, u8 port)
{
	struct ib_pma_portsamplescontrol *p =
		(struct ib_pma_portsamplescontrol *)pmp->data;
	struct qib_ibdev *dev = to_idev(ibdev);
	struct qib_devdata *dd = dd_from_dev(dev);
	struct qib_ibport *ibp = to_iport(ibdev, port);
	struct qib_pportdata *ppd = ppd_from_ibp(ibp);
	unsigned long flags;
	u8 status, xmit_flags;
	int ret;

	if (pmp->mad_hdr.attr_mod != 0 || p->port_select != port) {
		pmp->mad_hdr.status |= IB_SMP_INVALID_FIELD;
		ret = reply((struct ib_smp *) pmp);
		goto bail;
	}

	spin_lock_irqsave(&ibp->rvp.lock, flags);

	/* Port Sampling code owns the PS* HW counters */
	xmit_flags = ppd->cong_stats.flags;
	ppd->cong_stats.flags = IB_PMA_CONG_HW_CONTROL_SAMPLE;
	status = dd->f_portcntr(ppd, QIBPORTCNTR_PSSTAT);
	if (status == IB_PMA_SAMPLE_STATUS_DONE ||
	    (status == IB_PMA_SAMPLE_STATUS_RUNNING &&
	     xmit_flags == IB_PMA_CONG_HW_CONTROL_TIMER)) {
		ibp->rvp.pma_sample_start = be32_to_cpu(p->sample_start);
		ibp->rvp.pma_sample_interval = be32_to_cpu(p->sample_interval);
		ibp->rvp.pma_tag = be16_to_cpu(p->tag);
		ibp->rvp.pma_counter_select[0] = p->counter_select[0];
		ibp->rvp.pma_counter_select[1] = p->counter_select[1];
		ibp->rvp.pma_counter_select[2] = p->counter_select[2];
		ibp->rvp.pma_counter_select[3] = p->counter_select[3];
		ibp->rvp.pma_counter_select[4] = p->counter_select[4];
		dd->f_set_cntr_sample(ppd, ibp->rvp.pma_sample_interval,
				      ibp->rvp.pma_sample_start);
	}
	spin_unlock_irqrestore(&ibp->rvp.lock, flags);

	ret = pma_get_portsamplescontrol(pmp, ibdev, port);

bail:
	return ret;
}

static u64 get_counter(struct qib_ibport *ibp, struct qib_pportdata *ppd,
		       __be16 sel)
{
	u64 ret;

	switch (sel) {
	case IB_PMA_PORT_XMIT_DATA:
		ret = ppd->dd->f_portcntr(ppd, QIBPORTCNTR_PSXMITDATA);
		break;
	case IB_PMA_PORT_RCV_DATA:
		ret = ppd->dd->f_portcntr(ppd, QIBPORTCNTR_PSRCVDATA);
		break;
	case IB_PMA_PORT_XMIT_PKTS:
		ret = ppd->dd->f_portcntr(ppd, QIBPORTCNTR_PSXMITPKTS);
		break;
	case IB_PMA_PORT_RCV_PKTS:
		ret = ppd->dd->f_portcntr(ppd, QIBPORTCNTR_PSRCVPKTS);
		break;
	case IB_PMA_PORT_XMIT_WAIT:
		ret = ppd->dd->f_portcntr(ppd, QIBPORTCNTR_PSXMITWAIT);
		break;
	default:
		ret = 0;
	}

	return ret;
}

/* This function assumes that the xmit_wait lock is already held */
static u64 xmit_wait_get_value_delta(struct qib_pportdata *ppd)
{
	u32 delta;

	delta = get_counter(&ppd->ibport_data, ppd,
			    IB_PMA_PORT_XMIT_WAIT);
	return ppd->cong_stats.counter + delta;
}

static void cache_hw_sample_counters(struct qib_pportdata *ppd)
{
	struct qib_ibport *ibp = &ppd->ibport_data;

	ppd->cong_stats.counter_cache.psxmitdata =
		get_counter(ibp, ppd, IB_PMA_PORT_XMIT_DATA);
	ppd->cong_stats.counter_cache.psrcvdata =
		get_counter(ibp, ppd, IB_PMA_PORT_RCV_DATA);
	ppd->cong_stats.counter_cache.psxmitpkts =
		get_counter(ibp, ppd, IB_PMA_PORT_XMIT_PKTS);
	ppd->cong_stats.counter_cache.psrcvpkts =
		get_counter(ibp, ppd, IB_PMA_PORT_RCV_PKTS);
	ppd->cong_stats.counter_cache.psxmitwait =
		get_counter(ibp, ppd, IB_PMA_PORT_XMIT_WAIT);
}

static u64 get_cache_hw_sample_counters(struct qib_pportdata *ppd,
					__be16 sel)
{
	u64 ret;

	switch (sel) {
	case IB_PMA_PORT_XMIT_DATA:
		ret = ppd->cong_stats.counter_cache.psxmitdata;
		break;
	case IB_PMA_PORT_RCV_DATA:
		ret = ppd->cong_stats.counter_cache.psrcvdata;
		break;
	case IB_PMA_PORT_XMIT_PKTS:
		ret = ppd->cong_stats.counter_cache.psxmitpkts;
		break;
	case IB_PMA_PORT_RCV_PKTS:
		ret = ppd->cong_stats.counter_cache.psrcvpkts;
		break;
	case IB_PMA_PORT_XMIT_WAIT:
		ret = ppd->cong_stats.counter_cache.psxmitwait;
		break;
	default:
		ret = 0;
	}

	return ret;
}

static int pma_get_portsamplesresult(struct ib_pma_mad *pmp,
				     struct ib_device *ibdev, u8 port)
{
	struct ib_pma_portsamplesresult *p =
		(struct ib_pma_portsamplesresult *)pmp->data;
	struct qib_ibdev *dev = to_idev(ibdev);
	struct qib_devdata *dd = dd_from_dev(dev);
	struct qib_ibport *ibp = to_iport(ibdev, port);
	struct qib_pportdata *ppd = ppd_from_ibp(ibp);
	unsigned long flags;
	u8 status;
	int i;

	memset(pmp->data, 0, sizeof(pmp->data));
	spin_lock_irqsave(&ibp->rvp.lock, flags);
	p->tag = cpu_to_be16(ibp->rvp.pma_tag);
	if (ppd->cong_stats.flags == IB_PMA_CONG_HW_CONTROL_TIMER)
		p->sample_status = IB_PMA_SAMPLE_STATUS_DONE;
	else {
		status = dd->f_portcntr(ppd, QIBPORTCNTR_PSSTAT);
		p->sample_status = cpu_to_be16(status);
		if (status == IB_PMA_SAMPLE_STATUS_DONE) {
			cache_hw_sample_counters(ppd);
			ppd->cong_stats.counter =
				xmit_wait_get_value_delta(ppd);
			dd->f_set_cntr_sample(ppd,
					      QIB_CONG_TIMER_PSINTERVAL, 0);
			ppd->cong_stats.flags = IB_PMA_CONG_HW_CONTROL_TIMER;
		}
	}
	for (i = 0; i < ARRAY_SIZE(ibp->rvp.pma_counter_select); i++)
		p->counter[i] = cpu_to_be32(
			get_cache_hw_sample_counters(
				ppd, ibp->rvp.pma_counter_select[i]));
	spin_unlock_irqrestore(&ibp->rvp.lock, flags);

	return reply((struct ib_smp *) pmp);
}

static int pma_get_portsamplesresult_ext(struct ib_pma_mad *pmp,
					 struct ib_device *ibdev, u8 port)
{
	struct ib_pma_portsamplesresult_ext *p =
		(struct ib_pma_portsamplesresult_ext *)pmp->data;
	struct qib_ibdev *dev = to_idev(ibdev);
	struct qib_devdata *dd = dd_from_dev(dev);
	struct qib_ibport *ibp = to_iport(ibdev, port);
	struct qib_pportdata *ppd = ppd_from_ibp(ibp);
	unsigned long flags;
	u8 status;
	int i;

	/* Port Sampling code owns the PS* HW counters */
	memset(pmp->data, 0, sizeof(pmp->data));
	spin_lock_irqsave(&ibp->rvp.lock, flags);
	p->tag = cpu_to_be16(ibp->rvp.pma_tag);
	if (ppd->cong_stats.flags == IB_PMA_CONG_HW_CONTROL_TIMER)
		p->sample_status = IB_PMA_SAMPLE_STATUS_DONE;
	else {
		status = dd->f_portcntr(ppd, QIBPORTCNTR_PSSTAT);
		p->sample_status = cpu_to_be16(status);
		/* 64 bits */
		p->extended_width = cpu_to_be32(0x80000000);
		if (status == IB_PMA_SAMPLE_STATUS_DONE) {
			cache_hw_sample_counters(ppd);
			ppd->cong_stats.counter =
				xmit_wait_get_value_delta(ppd);
			dd->f_set_cntr_sample(ppd,
					      QIB_CONG_TIMER_PSINTERVAL, 0);
			ppd->cong_stats.flags = IB_PMA_CONG_HW_CONTROL_TIMER;
		}
	}
	for (i = 0; i < ARRAY_SIZE(ibp->rvp.pma_counter_select); i++)
		p->counter[i] = cpu_to_be64(
			get_cache_hw_sample_counters(
				ppd, ibp->rvp.pma_counter_select[i]));
	spin_unlock_irqrestore(&ibp->rvp.lock, flags);

	return reply((struct ib_smp *) pmp);
}

static int pma_get_portcounters(struct ib_pma_mad *pmp,
				struct ib_device *ibdev, u8 port)
{
	struct ib_pma_portcounters *p = (struct ib_pma_portcounters *)
		pmp->data;
	struct qib_ibport *ibp = to_iport(ibdev, port);
	struct qib_pportdata *ppd = ppd_from_ibp(ibp);
	struct qib_verbs_counters cntrs;
	u8 port_select = p->port_select;

	qib_get_counters(ppd, &cntrs);

	/* Adjust counters for any resets done. */
	cntrs.symbol_error_counter -= ibp->z_symbol_error_counter;
	cntrs.link_error_recovery_counter -=
		ibp->z_link_error_recovery_counter;
	cntrs.link_downed_counter -= ibp->z_link_downed_counter;
	cntrs.port_rcv_errors -= ibp->z_port_rcv_errors;
	cntrs.port_rcv_remphys_errors -= ibp->z_port_rcv_remphys_errors;
	cntrs.port_xmit_discards -= ibp->z_port_xmit_discards;
	cntrs.port_xmit_data -= ibp->z_port_xmit_data;
	cntrs.port_rcv_data -= ibp->z_port_rcv_data;
	cntrs.port_xmit_packets -= ibp->z_port_xmit_packets;
	cntrs.port_rcv_packets -= ibp->z_port_rcv_packets;
	cntrs.local_link_integrity_errors -=
		ibp->z_local_link_integrity_errors;
	cntrs.excessive_buffer_overrun_errors -=
		ibp->z_excessive_buffer_overrun_errors;
	cntrs.vl15_dropped -= ibp->z_vl15_dropped;
	cntrs.vl15_dropped += ibp->rvp.n_vl15_dropped;

	memset(pmp->data, 0, sizeof(pmp->data));

	p->port_select = port_select;
	if (pmp->mad_hdr.attr_mod != 0 || port_select != port)
		pmp->mad_hdr.status |= IB_SMP_INVALID_FIELD;

	if (cntrs.symbol_error_counter > 0xFFFFUL)
		p->symbol_error_counter = cpu_to_be16(0xFFFF);
	else
		p->symbol_error_counter =
			cpu_to_be16((u16)cntrs.symbol_error_counter);
	if (cntrs.link_error_recovery_counter > 0xFFUL)
		p->link_error_recovery_counter = 0xFF;
	else
		p->link_error_recovery_counter =
			(u8)cntrs.link_error_recovery_counter;
	if (cntrs.link_downed_counter > 0xFFUL)
		p->link_downed_counter = 0xFF;
	else
		p->link_downed_counter = (u8)cntrs.link_downed_counter;
	if (cntrs.port_rcv_errors > 0xFFFFUL)
		p->port_rcv_errors = cpu_to_be16(0xFFFF);
	else
		p->port_rcv_errors =
			cpu_to_be16((u16) cntrs.port_rcv_errors);
	if (cntrs.port_rcv_remphys_errors > 0xFFFFUL)
		p->port_rcv_remphys_errors = cpu_to_be16(0xFFFF);
	else
		p->port_rcv_remphys_errors =
			cpu_to_be16((u16)cntrs.port_rcv_remphys_errors);
	if (cntrs.port_xmit_discards > 0xFFFFUL)
		p->port_xmit_discards = cpu_to_be16(0xFFFF);
	else
		p->port_xmit_discards =
			cpu_to_be16((u16)cntrs.port_xmit_discards);
	if (cntrs.local_link_integrity_errors > 0xFUL)
		cntrs.local_link_integrity_errors = 0xFUL;
	if (cntrs.excessive_buffer_overrun_errors > 0xFUL)
		cntrs.excessive_buffer_overrun_errors = 0xFUL;
	p->link_overrun_errors = (cntrs.local_link_integrity_errors << 4) |
		cntrs.excessive_buffer_overrun_errors;
	if (cntrs.vl15_dropped > 0xFFFFUL)
		p->vl15_dropped = cpu_to_be16(0xFFFF);
	else
		p->vl15_dropped = cpu_to_be16((u16)cntrs.vl15_dropped);
	if (cntrs.port_xmit_data > 0xFFFFFFFFUL)
		p->port_xmit_data = cpu_to_be32(0xFFFFFFFF);
	else
		p->port_xmit_data = cpu_to_be32((u32)cntrs.port_xmit_data);
	if (cntrs.port_rcv_data > 0xFFFFFFFFUL)
		p->port_rcv_data = cpu_to_be32(0xFFFFFFFF);
	else
		p->port_rcv_data = cpu_to_be32((u32)cntrs.port_rcv_data);
	if (cntrs.port_xmit_packets > 0xFFFFFFFFUL)
		p->port_xmit_packets = cpu_to_be32(0xFFFFFFFF);
	else
		p->port_xmit_packets =
			cpu_to_be32((u32)cntrs.port_xmit_packets);
	if (cntrs.port_rcv_packets > 0xFFFFFFFFUL)
		p->port_rcv_packets = cpu_to_be32(0xFFFFFFFF);
	else
		p->port_rcv_packets =
			cpu_to_be32((u32) cntrs.port_rcv_packets);

	return reply((struct ib_smp *) pmp);
}

static int pma_get_portcounters_cong(struct ib_pma_mad *pmp,
				     struct ib_device *ibdev, u8 port)
{
	/* Congestion PMA packets start at offset 24 not 64 */
	struct ib_pma_portcounters_cong *p =
		(struct ib_pma_portcounters_cong *)pmp->reserved;
	struct qib_verbs_counters cntrs;
	struct qib_ibport *ibp = to_iport(ibdev, port);
	struct qib_pportdata *ppd = ppd_from_ibp(ibp);
	struct qib_devdata *dd = dd_from_ppd(ppd);
	u32 port_select = be32_to_cpu(pmp->mad_hdr.attr_mod) & 0xFF;
	u64 xmit_wait_counter;
	unsigned long flags;

	/*
	 * This check is performed only in the GET method because the
	 * SET method ends up calling this anyway.
	 */
	if (!dd->psxmitwait_supported)
		pmp->mad_hdr.status |= IB_SMP_UNSUP_METH_ATTR;
	if (port_select != port)
		pmp->mad_hdr.status |= IB_SMP_INVALID_FIELD;

	qib_get_counters(ppd, &cntrs);
	spin_lock_irqsave(&ppd->ibport_data.rvp.lock, flags);
	xmit_wait_counter = xmit_wait_get_value_delta(ppd);
	spin_unlock_irqrestore(&ppd->ibport_data.rvp.lock, flags);

	/* Adjust counters for any resets done. */
	cntrs.symbol_error_counter -= ibp->z_symbol_error_counter;
	cntrs.link_error_recovery_counter -=
		ibp->z_link_error_recovery_counter;
	cntrs.link_downed_counter -= ibp->z_link_downed_counter;
	cntrs.port_rcv_errors -= ibp->z_port_rcv_errors;
	cntrs.port_rcv_remphys_errors -=
		ibp->z_port_rcv_remphys_errors;
	cntrs.port_xmit_discards -= ibp->z_port_xmit_discards;
	cntrs.local_link_integrity_errors -=
		ibp->z_local_link_integrity_errors;
	cntrs.excessive_buffer_overrun_errors -=
		ibp->z_excessive_buffer_overrun_errors;
	cntrs.vl15_dropped -= ibp->z_vl15_dropped;
	cntrs.vl15_dropped += ibp->rvp.n_vl15_dropped;
	cntrs.port_xmit_data -= ibp->z_port_xmit_data;
	cntrs.port_rcv_data -= ibp->z_port_rcv_data;
	cntrs.port_xmit_packets -= ibp->z_port_xmit_packets;
	cntrs.port_rcv_packets -= ibp->z_port_rcv_packets;

	memset(pmp->reserved, 0, sizeof(pmp->reserved));
	memset(pmp->data, 0, sizeof(pmp->data));

	/*
	 * Set top 3 bits to indicate interval in picoseconds in
	 * remaining bits.
	 */
	p->port_check_rate =
		cpu_to_be16((QIB_XMIT_RATE_PICO << 13) |
			    (dd->psxmitwait_check_rate &
			     ~(QIB_XMIT_RATE_PICO << 13)));
	p->port_adr_events = cpu_to_be64(0);
	p->port_xmit_wait = cpu_to_be64(xmit_wait_counter);
	p->port_xmit_data = cpu_to_be64(cntrs.port_xmit_data);
	p->port_rcv_data = cpu_to_be64(cntrs.port_rcv_data);
	p->port_xmit_packets =
		cpu_to_be64(cntrs.port_xmit_packets);
	p->port_rcv_packets =
		cpu_to_be64(cntrs.port_rcv_packets);
	if (cntrs.symbol_error_counter > 0xFFFFUL)
		p->symbol_error_counter = cpu_to_be16(0xFFFF);
	else
		p->symbol_error_counter =
			cpu_to_be16(
				(u16)cntrs.symbol_error_counter);
	if (cntrs.link_error_recovery_counter > 0xFFUL)
		p->link_error_recovery_counter = 0xFF;
	else
		p->link_error_recovery_counter =
			(u8)cntrs.link_error_recovery_counter;
	if (cntrs.link_downed_counter > 0xFFUL)
		p->link_downed_counter = 0xFF;
	else
		p->link_downed_counter =
			(u8)cntrs.link_downed_counter;
	if (cntrs.port_rcv_errors > 0xFFFFUL)
		p->port_rcv_errors = cpu_to_be16(0xFFFF);
	else
		p->port_rcv_errors =
			cpu_to_be16((u16) cntrs.port_rcv_errors);
	if (cntrs.port_rcv_remphys_errors > 0xFFFFUL)
		p->port_rcv_remphys_errors = cpu_to_be16(0xFFFF);
	else
		p->port_rcv_remphys_errors =
			cpu_to_be16(
				(u16)cntrs.port_rcv_remphys_errors);
	if (cntrs.port_xmit_discards > 0xFFFFUL)
		p->port_xmit_discards = cpu_to_be16(0xFFFF);
	else
		p->port_xmit_discards =
			cpu_to_be16((u16)cntrs.port_xmit_discards);
	if (cntrs.local_link_integrity_errors > 0xFUL)
		cntrs.local_link_integrity_errors = 0xFUL;
	if (cntrs.excessive_buffer_overrun_errors > 0xFUL)
		cntrs.excessive_buffer_overrun_errors = 0xFUL;
	p->link_overrun_errors = (cntrs.local_link_integrity_errors << 4) |
		cntrs.excessive_buffer_overrun_errors;
	if (cntrs.vl15_dropped > 0xFFFFUL)
		p->vl15_dropped = cpu_to_be16(0xFFFF);
	else
		p->vl15_dropped = cpu_to_be16((u16)cntrs.vl15_dropped);

	return reply((struct ib_smp *)pmp);
}

static void qib_snapshot_pmacounters(
	struct qib_ibport *ibp,
	struct qib_pma_counters *pmacounters)
{
	struct qib_pma_counters *p;
	int cpu;

	memset(pmacounters, 0, sizeof(*pmacounters));
	for_each_possible_cpu(cpu) {
		p = per_cpu_ptr(ibp->pmastats, cpu);
		pmacounters->n_unicast_xmit += p->n_unicast_xmit;
		pmacounters->n_unicast_rcv += p->n_unicast_rcv;
		pmacounters->n_multicast_xmit += p->n_multicast_xmit;
		pmacounters->n_multicast_rcv += p->n_multicast_rcv;
	}
}

static int pma_get_portcounters_ext(struct ib_pma_mad *pmp,
				    struct ib_device *ibdev, u8 port)
{
	struct ib_pma_portcounters_ext *p =
		(struct ib_pma_portcounters_ext *)pmp->data;
	struct qib_ibport *ibp = to_iport(ibdev, port);
	struct qib_pportdata *ppd = ppd_from_ibp(ibp);
	u64 swords, rwords, spkts, rpkts, xwait;
	struct qib_pma_counters pma;
	u8 port_select = p->port_select;

	memset(pmp->data, 0, sizeof(pmp->data));

	p->port_select = port_select;
	if (pmp->mad_hdr.attr_mod != 0 || port_select != port) {
		pmp->mad_hdr.status |= IB_SMP_INVALID_FIELD;
		goto bail;
	}

	qib_snapshot_counters(ppd, &swords, &rwords, &spkts, &rpkts, &xwait);

	/* Adjust counters for any resets done. */
	swords -= ibp->z_port_xmit_data;
	rwords -= ibp->z_port_rcv_data;
	spkts -= ibp->z_port_xmit_packets;
	rpkts -= ibp->z_port_rcv_packets;

	p->port_xmit_data = cpu_to_be64(swords);
	p->port_rcv_data = cpu_to_be64(rwords);
	p->port_xmit_packets = cpu_to_be64(spkts);
	p->port_rcv_packets = cpu_to_be64(rpkts);

	qib_snapshot_pmacounters(ibp, &pma);

	p->port_unicast_xmit_packets = cpu_to_be64(pma.n_unicast_xmit
		- ibp->z_unicast_xmit);
	p->port_unicast_rcv_packets = cpu_to_be64(pma.n_unicast_rcv
		- ibp->z_unicast_rcv);
	p->port_multicast_xmit_packets = cpu_to_be64(pma.n_multicast_xmit
		- ibp->z_multicast_xmit);
	p->port_multicast_rcv_packets = cpu_to_be64(pma.n_multicast_rcv
		- ibp->z_multicast_rcv);

bail:
	return reply((struct ib_smp *) pmp);
}

static int pma_set_portcounters(struct ib_pma_mad *pmp,
				struct ib_device *ibdev, u8 port)
{
	struct ib_pma_portcounters *p = (struct ib_pma_portcounters *)
		pmp->data;
	struct qib_ibport *ibp = to_iport(ibdev, port);
	struct qib_pportdata *ppd = ppd_from_ibp(ibp);
	struct qib_verbs_counters cntrs;

	/*
	 * Since the HW doesn't support clearing counters, we save the
	 * current count and subtract it from future responses.
	 */
	qib_get_counters(ppd, &cntrs);

	if (p->counter_select & IB_PMA_SEL_SYMBOL_ERROR)
		ibp->z_symbol_error_counter = cntrs.symbol_error_counter;

	if (p->counter_select & IB_PMA_SEL_LINK_ERROR_RECOVERY)
		ibp->z_link_error_recovery_counter =
			cntrs.link_error_recovery_counter;

	if (p->counter_select & IB_PMA_SEL_LINK_DOWNED)
		ibp->z_link_downed_counter = cntrs.link_downed_counter;

	if (p->counter_select & IB_PMA_SEL_PORT_RCV_ERRORS)
		ibp->z_port_rcv_errors = cntrs.port_rcv_errors;

	if (p->counter_select & IB_PMA_SEL_PORT_RCV_REMPHYS_ERRORS)
		ibp->z_port_rcv_remphys_errors =
			cntrs.port_rcv_remphys_errors;

	if (p->counter_select & IB_PMA_SEL_PORT_XMIT_DISCARDS)
		ibp->z_port_xmit_discards = cntrs.port_xmit_discards;

	if (p->counter_select & IB_PMA_SEL_LOCAL_LINK_INTEGRITY_ERRORS)
		ibp->z_local_link_integrity_errors =
			cntrs.local_link_integrity_errors;

	if (p->counter_select & IB_PMA_SEL_EXCESSIVE_BUFFER_OVERRUNS)
		ibp->z_excessive_buffer_overrun_errors =
			cntrs.excessive_buffer_overrun_errors;

	if (p->counter_select & IB_PMA_SEL_PORT_VL15_DROPPED) {
		ibp->rvp.n_vl15_dropped = 0;
		ibp->z_vl15_dropped = cntrs.vl15_dropped;
	}

	if (p->counter_select & IB_PMA_SEL_PORT_XMIT_DATA)
		ibp->z_port_xmit_data = cntrs.port_xmit_data;

	if (p->counter_select & IB_PMA_SEL_PORT_RCV_DATA)
		ibp->z_port_rcv_data = cntrs.port_rcv_data;

	if (p->counter_select & IB_PMA_SEL_PORT_XMIT_PACKETS)
		ibp->z_port_xmit_packets = cntrs.port_xmit_packets;

	if (p->counter_select & IB_PMA_SEL_PORT_RCV_PACKETS)
		ibp->z_port_rcv_packets = cntrs.port_rcv_packets;

	return pma_get_portcounters(pmp, ibdev, port);
}

static int pma_set_portcounters_cong(struct ib_pma_mad *pmp,
				     struct ib_device *ibdev, u8 port)
{
	struct qib_ibport *ibp = to_iport(ibdev, port);
	struct qib_pportdata *ppd = ppd_from_ibp(ibp);
	struct qib_devdata *dd = dd_from_ppd(ppd);
	struct qib_verbs_counters cntrs;
	u32 counter_select = (be32_to_cpu(pmp->mad_hdr.attr_mod) >> 24) & 0xFF;
	int ret = 0;
	unsigned long flags;

	qib_get_counters(ppd, &cntrs);
	/* Get counter values before we save them */
	ret = pma_get_portcounters_cong(pmp, ibdev, port);

	if (counter_select & IB_PMA_SEL_CONG_XMIT) {
		spin_lock_irqsave(&ppd->ibport_data.rvp.lock, flags);
		ppd->cong_stats.counter = 0;
		dd->f_set_cntr_sample(ppd, QIB_CONG_TIMER_PSINTERVAL,
				      0x0);
		spin_unlock_irqrestore(&ppd->ibport_data.rvp.lock, flags);
	}
	if (counter_select & IB_PMA_SEL_CONG_PORT_DATA) {
		ibp->z_port_xmit_data = cntrs.port_xmit_data;
		ibp->z_port_rcv_data = cntrs.port_rcv_data;
		ibp->z_port_xmit_packets = cntrs.port_xmit_packets;
		ibp->z_port_rcv_packets = cntrs.port_rcv_packets;
	}
	if (counter_select & IB_PMA_SEL_CONG_ALL) {
		ibp->z_symbol_error_counter =
			cntrs.symbol_error_counter;
		ibp->z_link_error_recovery_counter =
			cntrs.link_error_recovery_counter;
		ibp->z_link_downed_counter =
			cntrs.link_downed_counter;
		ibp->z_port_rcv_errors = cntrs.port_rcv_errors;
		ibp->z_port_rcv_remphys_errors =
			cntrs.port_rcv_remphys_errors;
		ibp->z_port_xmit_discards =
			cntrs.port_xmit_discards;
		ibp->z_local_link_integrity_errors =
			cntrs.local_link_integrity_errors;
		ibp->z_excessive_buffer_overrun_errors =
			cntrs.excessive_buffer_overrun_errors;
		ibp->rvp.n_vl15_dropped = 0;
		ibp->z_vl15_dropped = cntrs.vl15_dropped;
	}

	return ret;
}

static int pma_set_portcounters_ext(struct ib_pma_mad *pmp,
				    struct ib_device *ibdev, u8 port)
{
	struct ib_pma_portcounters *p = (struct ib_pma_portcounters *)
		pmp->data;
	struct qib_ibport *ibp = to_iport(ibdev, port);
	struct qib_pportdata *ppd = ppd_from_ibp(ibp);
	u64 swords, rwords, spkts, rpkts, xwait;
	struct qib_pma_counters pma;

	qib_snapshot_counters(ppd, &swords, &rwords, &spkts, &rpkts, &xwait);

	if (p->counter_select & IB_PMA_SELX_PORT_XMIT_DATA)
		ibp->z_port_xmit_data = swords;

	if (p->counter_select & IB_PMA_SELX_PORT_RCV_DATA)
		ibp->z_port_rcv_data = rwords;

	if (p->counter_select & IB_PMA_SELX_PORT_XMIT_PACKETS)
		ibp->z_port_xmit_packets = spkts;

	if (p->counter_select & IB_PMA_SELX_PORT_RCV_PACKETS)
		ibp->z_port_rcv_packets = rpkts;

	qib_snapshot_pmacounters(ibp, &pma);

	if (p->counter_select & IB_PMA_SELX_PORT_UNI_XMIT_PACKETS)
		ibp->z_unicast_xmit = pma.n_unicast_xmit;

	if (p->counter_select & IB_PMA_SELX_PORT_UNI_RCV_PACKETS)
		ibp->z_unicast_rcv = pma.n_unicast_rcv;

	if (p->counter_select & IB_PMA_SELX_PORT_MULTI_XMIT_PACKETS)
		ibp->z_multicast_xmit = pma.n_multicast_xmit;

	if (p->counter_select & IB_PMA_SELX_PORT_MULTI_RCV_PACKETS)
		ibp->z_multicast_rcv = pma.n_multicast_rcv;

	return pma_get_portcounters_ext(pmp, ibdev, port);
}

static int process_subn(struct ib_device *ibdev, int mad_flags,
			u8 port, const struct ib_mad *in_mad,
			struct ib_mad *out_mad)
{
	struct ib_smp *smp = (struct ib_smp *)out_mad;
	struct qib_ibport *ibp = to_iport(ibdev, port);
	struct qib_pportdata *ppd = ppd_from_ibp(ibp);
	int ret;

	*out_mad = *in_mad;
	if (smp->class_version != 1) {
		smp->status |= IB_SMP_UNSUP_VERSION;
		ret = reply(smp);
		goto bail;
	}

	ret = check_mkey(ibp, smp, mad_flags);
	if (ret) {
		u32 port_num = be32_to_cpu(smp->attr_mod);

		/*
		 * If this is a get/set portinfo, we already check the
		 * M_Key if the MAD is for another port and the M_Key
		 * is OK on the receiving port. This check is needed
		 * to increment the error counters when the M_Key
		 * fails to match on *both* ports.
		 */
		if (in_mad->mad_hdr.attr_id == IB_SMP_ATTR_PORT_INFO &&
		    (smp->method == IB_MGMT_METHOD_GET ||
		     smp->method == IB_MGMT_METHOD_SET) &&
		    port_num && port_num <= ibdev->phys_port_cnt &&
		    port != port_num)
			(void) check_mkey(to_iport(ibdev, port_num), smp, 0);
		ret = IB_MAD_RESULT_FAILURE;
		goto bail;
	}

	switch (smp->method) {
	case IB_MGMT_METHOD_GET:
		switch (smp->attr_id) {
		case IB_SMP_ATTR_NODE_DESC:
			ret = subn_get_nodedescription(smp, ibdev);
			goto bail;
		case IB_SMP_ATTR_NODE_INFO:
			ret = subn_get_nodeinfo(smp, ibdev, port);
			goto bail;
		case IB_SMP_ATTR_GUID_INFO:
			ret = subn_get_guidinfo(smp, ibdev, port);
			goto bail;
		case IB_SMP_ATTR_PORT_INFO:
			ret = subn_get_portinfo(smp, ibdev, port);
			goto bail;
		case IB_SMP_ATTR_PKEY_TABLE:
			ret = subn_get_pkeytable(smp, ibdev, port);
			goto bail;
		case IB_SMP_ATTR_SL_TO_VL_TABLE:
			ret = subn_get_sl_to_vl(smp, ibdev, port);
			goto bail;
		case IB_SMP_ATTR_VL_ARB_TABLE:
			ret = subn_get_vl_arb(smp, ibdev, port);
			goto bail;
		case IB_SMP_ATTR_SM_INFO:
			if (ibp->rvp.port_cap_flags & IB_PORT_SM_DISABLED) {
				ret = IB_MAD_RESULT_SUCCESS |
					IB_MAD_RESULT_CONSUMED;
				goto bail;
			}
			if (ibp->rvp.port_cap_flags & IB_PORT_SM) {
				ret = IB_MAD_RESULT_SUCCESS;
				goto bail;
			}
			/* FALLTHROUGH */
		default:
			smp->status |= IB_SMP_UNSUP_METH_ATTR;
			ret = reply(smp);
			goto bail;
		}

	case IB_MGMT_METHOD_SET:
		switch (smp->attr_id) {
		case IB_SMP_ATTR_GUID_INFO:
			ret = subn_set_guidinfo(smp, ibdev, port);
			goto bail;
		case IB_SMP_ATTR_PORT_INFO:
			ret = subn_set_portinfo(smp, ibdev, port);
			goto bail;
		case IB_SMP_ATTR_PKEY_TABLE:
			ret = subn_set_pkeytable(smp, ibdev, port);
			goto bail;
		case IB_SMP_ATTR_SL_TO_VL_TABLE:
			ret = subn_set_sl_to_vl(smp, ibdev, port);
			goto bail;
		case IB_SMP_ATTR_VL_ARB_TABLE:
			ret = subn_set_vl_arb(smp, ibdev, port);
			goto bail;
		case IB_SMP_ATTR_SM_INFO:
			if (ibp->rvp.port_cap_flags & IB_PORT_SM_DISABLED) {
				ret = IB_MAD_RESULT_SUCCESS |
					IB_MAD_RESULT_CONSUMED;
				goto bail;
			}
			if (ibp->rvp.port_cap_flags & IB_PORT_SM) {
				ret = IB_MAD_RESULT_SUCCESS;
				goto bail;
			}
			/* FALLTHROUGH */
		default:
			smp->status |= IB_SMP_UNSUP_METH_ATTR;
			ret = reply(smp);
			goto bail;
		}

	case IB_MGMT_METHOD_TRAP_REPRESS:
		if (smp->attr_id == IB_SMP_ATTR_NOTICE)
			ret = subn_trap_repress(smp, ibdev, port);
		else {
			smp->status |= IB_SMP_UNSUP_METH_ATTR;
			ret = reply(smp);
		}
		goto bail;

	case IB_MGMT_METHOD_TRAP:
	case IB_MGMT_METHOD_REPORT:
	case IB_MGMT_METHOD_REPORT_RESP:
	case IB_MGMT_METHOD_GET_RESP:
		/*
		 * The ib_mad module will call us to process responses
		 * before checking for other consumers.
		 * Just tell the caller to process it normally.
		 */
		ret = IB_MAD_RESULT_SUCCESS;
		goto bail;

	case IB_MGMT_METHOD_SEND:
		if (ib_get_smp_direction(smp) &&
		    smp->attr_id == QIB_VENDOR_IPG) {
			ppd->dd->f_set_ib_cfg(ppd, QIB_IB_CFG_PORT,
					      smp->data[0]);
			ret = IB_MAD_RESULT_SUCCESS | IB_MAD_RESULT_CONSUMED;
		} else
			ret = IB_MAD_RESULT_SUCCESS;
		goto bail;

	default:
		smp->status |= IB_SMP_UNSUP_METHOD;
		ret = reply(smp);
	}

bail:
	return ret;
}

static int process_perf(struct ib_device *ibdev, u8 port,
			const struct ib_mad *in_mad,
			struct ib_mad *out_mad)
{
	struct ib_pma_mad *pmp = (struct ib_pma_mad *)out_mad;
	int ret;

	*out_mad = *in_mad;
	if (pmp->mad_hdr.class_version != 1) {
		pmp->mad_hdr.status |= IB_SMP_UNSUP_VERSION;
		ret = reply((struct ib_smp *) pmp);
		goto bail;
	}

	switch (pmp->mad_hdr.method) {
	case IB_MGMT_METHOD_GET:
		switch (pmp->mad_hdr.attr_id) {
		case IB_PMA_CLASS_PORT_INFO:
			ret = pma_get_classportinfo(pmp, ibdev);
			goto bail;
		case IB_PMA_PORT_SAMPLES_CONTROL:
			ret = pma_get_portsamplescontrol(pmp, ibdev, port);
			goto bail;
		case IB_PMA_PORT_SAMPLES_RESULT:
			ret = pma_get_portsamplesresult(pmp, ibdev, port);
			goto bail;
		case IB_PMA_PORT_SAMPLES_RESULT_EXT:
			ret = pma_get_portsamplesresult_ext(pmp, ibdev, port);
			goto bail;
		case IB_PMA_PORT_COUNTERS:
			ret = pma_get_portcounters(pmp, ibdev, port);
			goto bail;
		case IB_PMA_PORT_COUNTERS_EXT:
			ret = pma_get_portcounters_ext(pmp, ibdev, port);
			goto bail;
		case IB_PMA_PORT_COUNTERS_CONG:
			ret = pma_get_portcounters_cong(pmp, ibdev, port);
			goto bail;
		default:
			pmp->mad_hdr.status |= IB_SMP_UNSUP_METH_ATTR;
			ret = reply((struct ib_smp *) pmp);
			goto bail;
		}

	case IB_MGMT_METHOD_SET:
		switch (pmp->mad_hdr.attr_id) {
		case IB_PMA_PORT_SAMPLES_CONTROL:
			ret = pma_set_portsamplescontrol(pmp, ibdev, port);
			goto bail;
		case IB_PMA_PORT_COUNTERS:
			ret = pma_set_portcounters(pmp, ibdev, port);
			goto bail;
		case IB_PMA_PORT_COUNTERS_EXT:
			ret = pma_set_portcounters_ext(pmp, ibdev, port);
			goto bail;
		case IB_PMA_PORT_COUNTERS_CONG:
			ret = pma_set_portcounters_cong(pmp, ibdev, port);
			goto bail;
		default:
			pmp->mad_hdr.status |= IB_SMP_UNSUP_METH_ATTR;
			ret = reply((struct ib_smp *) pmp);
			goto bail;
		}

	case IB_MGMT_METHOD_TRAP:
	case IB_MGMT_METHOD_GET_RESP:
		/*
		 * The ib_mad module will call us to process responses
		 * before checking for other consumers.
		 * Just tell the caller to process it normally.
		 */
		ret = IB_MAD_RESULT_SUCCESS;
		goto bail;

	default:
		pmp->mad_hdr.status |= IB_SMP_UNSUP_METHOD;
		ret = reply((struct ib_smp *) pmp);
	}

bail:
	return ret;
}

static int cc_get_classportinfo(struct ib_cc_mad *ccp,
				struct ib_device *ibdev)
{
	struct ib_cc_classportinfo_attr *p =
		(struct ib_cc_classportinfo_attr *)ccp->mgmt_data;

	memset(ccp->mgmt_data, 0, sizeof(ccp->mgmt_data));

	p->base_version = 1;
	p->class_version = 1;
	p->cap_mask = 0;

	/*
	 * Expected response time is 4.096 usec. * 2^18 == 1.073741824 sec.
	 */
	p->resp_time_value = 18;

	return reply((struct ib_smp *) ccp);
}

static int cc_get_congestion_info(struct ib_cc_mad *ccp,
				struct ib_device *ibdev, u8 port)
{
	struct ib_cc_info_attr *p =
		(struct ib_cc_info_attr *)ccp->mgmt_data;
	struct qib_ibport *ibp = to_iport(ibdev, port);
	struct qib_pportdata *ppd = ppd_from_ibp(ibp);

	memset(ccp->mgmt_data, 0, sizeof(ccp->mgmt_data));

	p->congestion_info = 0;
	p->control_table_cap = ppd->cc_max_table_entries;

	return reply((struct ib_smp *) ccp);
}

static int cc_get_congestion_setting(struct ib_cc_mad *ccp,
				struct ib_device *ibdev, u8 port)
{
	int i;
	struct ib_cc_congestion_setting_attr *p =
		(struct ib_cc_congestion_setting_attr *)ccp->mgmt_data;
	struct qib_ibport *ibp = to_iport(ibdev, port);
	struct qib_pportdata *ppd = ppd_from_ibp(ibp);
	struct ib_cc_congestion_entry_shadow *entries;

	memset(ccp->mgmt_data, 0, sizeof(ccp->mgmt_data));

	spin_lock(&ppd->cc_shadow_lock);

	entries = ppd->congestion_entries_shadow->entries;
	p->port_control = cpu_to_be16(
		ppd->congestion_entries_shadow->port_control);
	p->control_map = cpu_to_be16(
		ppd->congestion_entries_shadow->control_map);
	for (i = 0; i < IB_CC_CCS_ENTRIES; i++) {
		p->entries[i].ccti_increase = entries[i].ccti_increase;
		p->entries[i].ccti_timer = cpu_to_be16(entries[i].ccti_timer);
		p->entries[i].trigger_threshold = entries[i].trigger_threshold;
		p->entries[i].ccti_min = entries[i].ccti_min;
	}

	spin_unlock(&ppd->cc_shadow_lock);

	return reply((struct ib_smp *) ccp);
}

static int cc_get_congestion_control_table(struct ib_cc_mad *ccp,
				struct ib_device *ibdev, u8 port)
{
	struct ib_cc_table_attr *p =
		(struct ib_cc_table_attr *)ccp->mgmt_data;
	struct qib_ibport *ibp = to_iport(ibdev, port);
	struct qib_pportdata *ppd = ppd_from_ibp(ibp);
	u32 cct_block_index = be32_to_cpu(ccp->attr_mod);
	u32 max_cct_block;
	u32 cct_entry;
	struct ib_cc_table_entry_shadow *entries;
	int i;

	/* Is the table index more than what is supported? */
	if (cct_block_index > IB_CC_TABLE_CAP_DEFAULT - 1)
		goto bail;

	memset(ccp->mgmt_data, 0, sizeof(ccp->mgmt_data));

	spin_lock(&ppd->cc_shadow_lock);

	max_cct_block =
		(ppd->ccti_entries_shadow->ccti_last_entry + 1)/IB_CCT_ENTRIES;
	max_cct_block = max_cct_block ? max_cct_block - 1 : 0;

	if (cct_block_index > max_cct_block) {
		spin_unlock(&ppd->cc_shadow_lock);
		goto bail;
	}

	ccp->attr_mod = cpu_to_be32(cct_block_index);

	cct_entry = IB_CCT_ENTRIES * (cct_block_index + 1);

	cct_entry--;

	p->ccti_limit = cpu_to_be16(cct_entry);

	entries = &ppd->ccti_entries_shadow->
			entries[IB_CCT_ENTRIES * cct_block_index];
	cct_entry %= IB_CCT_ENTRIES;

	for (i = 0; i <= cct_entry; i++)
		p->ccti_entries[i].entry = cpu_to_be16(entries[i].entry);

	spin_unlock(&ppd->cc_shadow_lock);

	return reply((struct ib_smp *) ccp);

bail:
	return reply_failure((struct ib_smp *) ccp);
}

static int cc_set_congestion_setting(struct ib_cc_mad *ccp,
				struct ib_device *ibdev, u8 port)
{
	struct ib_cc_congestion_setting_attr *p =
		(struct ib_cc_congestion_setting_attr *)ccp->mgmt_data;
	struct qib_ibport *ibp = to_iport(ibdev, port);
	struct qib_pportdata *ppd = ppd_from_ibp(ibp);
	int i;

	ppd->cc_sl_control_map = be16_to_cpu(p->control_map);

	for (i = 0; i < IB_CC_CCS_ENTRIES; i++) {
		ppd->congestion_entries[i].ccti_increase =
			p->entries[i].ccti_increase;

		ppd->congestion_entries[i].ccti_timer =
			be16_to_cpu(p->entries[i].ccti_timer);

		ppd->congestion_entries[i].trigger_threshold =
			p->entries[i].trigger_threshold;

		ppd->congestion_entries[i].ccti_min =
			p->entries[i].ccti_min;
	}

	return reply((struct ib_smp *) ccp);
}

static int cc_set_congestion_control_table(struct ib_cc_mad *ccp,
				struct ib_device *ibdev, u8 port)
{
	struct ib_cc_table_attr *p =
		(struct ib_cc_table_attr *)ccp->mgmt_data;
	struct qib_ibport *ibp = to_iport(ibdev, port);
	struct qib_pportdata *ppd = ppd_from_ibp(ibp);
	u32 cct_block_index = be32_to_cpu(ccp->attr_mod);
	u32 cct_entry;
	struct ib_cc_table_entry_shadow *entries;
	int i;

	/* Is the table index more than what is supported? */
	if (cct_block_index > IB_CC_TABLE_CAP_DEFAULT - 1)
		goto bail;

	/* If this packet is the first in the sequence then
	 * zero the total table entry count.
	 */
	if (be16_to_cpu(p->ccti_limit) < IB_CCT_ENTRIES)
		ppd->total_cct_entry = 0;

	cct_entry = (be16_to_cpu(p->ccti_limit))%IB_CCT_ENTRIES;

	/* ccti_limit is 0 to 63 */
	ppd->total_cct_entry += (cct_entry + 1);

	if (ppd->total_cct_entry > ppd->cc_supported_table_entries)
		goto bail;

	ppd->ccti_limit = be16_to_cpu(p->ccti_limit);

	entries = ppd->ccti_entries + (IB_CCT_ENTRIES * cct_block_index);

	for (i = 0; i <= cct_entry; i++)
		entries[i].entry = be16_to_cpu(p->ccti_entries[i].entry);

	spin_lock(&ppd->cc_shadow_lock);

	ppd->ccti_entries_shadow->ccti_last_entry = ppd->total_cct_entry - 1;
	memcpy(ppd->ccti_entries_shadow->entries, ppd->ccti_entries,
		(ppd->total_cct_entry * sizeof(struct ib_cc_table_entry)));

	ppd->congestion_entries_shadow->port_control = IB_CC_CCS_PC_SL_BASED;
	ppd->congestion_entries_shadow->control_map = ppd->cc_sl_control_map;
	memcpy(ppd->congestion_entries_shadow->entries, ppd->congestion_entries,
		IB_CC_CCS_ENTRIES * sizeof(struct ib_cc_congestion_entry));

	spin_unlock(&ppd->cc_shadow_lock);

	return reply((struct ib_smp *) ccp);

bail:
	return reply_failure((struct ib_smp *) ccp);
}

static int check_cc_key(struct qib_ibport *ibp,
			struct ib_cc_mad *ccp, int mad_flags)
{
	return 0;
}

static int process_cc(struct ib_device *ibdev, int mad_flags,
			u8 port, const struct ib_mad *in_mad,
			struct ib_mad *out_mad)
{
	struct ib_cc_mad *ccp = (struct ib_cc_mad *)out_mad;
	struct qib_ibport *ibp = to_iport(ibdev, port);
	int ret;

	*out_mad = *in_mad;

	if (ccp->class_version != 2) {
		ccp->status |= IB_SMP_UNSUP_VERSION;
		ret = reply((struct ib_smp *)ccp);
		goto bail;
	}

	ret = check_cc_key(ibp, ccp, mad_flags);
	if (ret)
		goto bail;

	switch (ccp->method) {
	case IB_MGMT_METHOD_GET:
		switch (ccp->attr_id) {
		case IB_CC_ATTR_CLASSPORTINFO:
			ret = cc_get_classportinfo(ccp, ibdev);
			goto bail;

		case IB_CC_ATTR_CONGESTION_INFO:
			ret = cc_get_congestion_info(ccp, ibdev, port);
			goto bail;

		case IB_CC_ATTR_CA_CONGESTION_SETTING:
			ret = cc_get_congestion_setting(ccp, ibdev, port);
			goto bail;

		case IB_CC_ATTR_CONGESTION_CONTROL_TABLE:
			ret = cc_get_congestion_control_table(ccp, ibdev, port);
			goto bail;

			/* FALLTHROUGH */
		default:
			ccp->status |= IB_SMP_UNSUP_METH_ATTR;
			ret = reply((struct ib_smp *) ccp);
			goto bail;
		}

	case IB_MGMT_METHOD_SET:
		switch (ccp->attr_id) {
		case IB_CC_ATTR_CA_CONGESTION_SETTING:
			ret = cc_set_congestion_setting(ccp, ibdev, port);
			goto bail;

		case IB_CC_ATTR_CONGESTION_CONTROL_TABLE:
			ret = cc_set_congestion_control_table(ccp, ibdev, port);
			goto bail;

			/* FALLTHROUGH */
		default:
			ccp->status |= IB_SMP_UNSUP_METH_ATTR;
			ret = reply((struct ib_smp *) ccp);
			goto bail;
		}

	case IB_MGMT_METHOD_GET_RESP:
		/*
		 * The ib_mad module will call us to process responses
		 * before checking for other consumers.
		 * Just tell the caller to process it normally.
		 */
		ret = IB_MAD_RESULT_SUCCESS;
		goto bail;

	case IB_MGMT_METHOD_TRAP:
	default:
		ccp->status |= IB_SMP_UNSUP_METHOD;
		ret = reply((struct ib_smp *) ccp);
	}

bail:
	return ret;
}

/**
 * qib_process_mad - process an incoming MAD packet
 * @ibdev: the infiniband device this packet came in on
 * @mad_flags: MAD flags
 * @port: the port number this packet came in on
 * @in_wc: the work completion entry for this packet
 * @in_grh: the global route header for this packet
 * @in_mad: the incoming MAD
 * @out_mad: any outgoing MAD reply
 *
 * Returns IB_MAD_RESULT_SUCCESS if this is a MAD that we are not
 * interested in processing.
 *
 * Note that the verbs framework has already done the MAD sanity checks,
 * and hop count/pointer updating for IB_MGMT_CLASS_SUBN_DIRECTED_ROUTE
 * MADs.
 *
 * This is called by the ib_mad module.
 */
int qib_process_mad(struct ib_device *ibdev, int mad_flags, u8 port,
		    const struct ib_wc *in_wc, const struct ib_grh *in_grh,
		    const struct ib_mad_hdr *in, size_t in_mad_size,
		    struct ib_mad_hdr *out, size_t *out_mad_size,
		    u16 *out_mad_pkey_index)
{
	int ret;
	struct qib_ibport *ibp = to_iport(ibdev, port);
	struct qib_pportdata *ppd = ppd_from_ibp(ibp);
	const struct ib_mad *in_mad = (const struct ib_mad *)in;
	struct ib_mad *out_mad = (struct ib_mad *)out;

	if (WARN_ON_ONCE(in_mad_size != sizeof(*in_mad) ||
			 *out_mad_size != sizeof(*out_mad)))
		return IB_MAD_RESULT_FAILURE;

	switch (in_mad->mad_hdr.mgmt_class) {
	case IB_MGMT_CLASS_SUBN_DIRECTED_ROUTE:
	case IB_MGMT_CLASS_SUBN_LID_ROUTED:
		ret = process_subn(ibdev, mad_flags, port, in_mad, out_mad);
		goto bail;

	case IB_MGMT_CLASS_PERF_MGMT:
		ret = process_perf(ibdev, port, in_mad, out_mad);
		goto bail;

	case IB_MGMT_CLASS_CONG_MGMT:
		if (!ppd->congestion_entries_shadow ||
			 !qib_cc_table_size) {
			ret = IB_MAD_RESULT_SUCCESS;
			goto bail;
		}
		ret = process_cc(ibdev, mad_flags, port, in_mad, out_mad);
		goto bail;

	default:
		ret = IB_MAD_RESULT_SUCCESS;
	}

bail:
	return ret;
}

static void xmit_wait_timer_func(struct timer_list *t)
{
	struct qib_pportdata *ppd = from_timer(ppd, t, cong_stats.timer);
	struct qib_devdata *dd = dd_from_ppd(ppd);
	unsigned long flags;
	u8 status;

	spin_lock_irqsave(&ppd->ibport_data.rvp.lock, flags);
	if (ppd->cong_stats.flags == IB_PMA_CONG_HW_CONTROL_SAMPLE) {
		status = dd->f_portcntr(ppd, QIBPORTCNTR_PSSTAT);
		if (status == IB_PMA_SAMPLE_STATUS_DONE) {
			/* save counter cache */
			cache_hw_sample_counters(ppd);
			ppd->cong_stats.flags = IB_PMA_CONG_HW_CONTROL_TIMER;
		} else
			goto done;
	}
	ppd->cong_stats.counter = xmit_wait_get_value_delta(ppd);
	dd->f_set_cntr_sample(ppd, QIB_CONG_TIMER_PSINTERVAL, 0x0);
done:
	spin_unlock_irqrestore(&ppd->ibport_data.rvp.lock, flags);
	mod_timer(&ppd->cong_stats.timer, jiffies + HZ);
}

void qib_notify_create_mad_agent(struct rvt_dev_info *rdi, int port_idx)
{
	struct qib_ibdev *ibdev = container_of(rdi, struct qib_ibdev, rdi);
	struct qib_devdata *dd = container_of(ibdev,
					      struct qib_devdata, verbs_dev);

	/* Initialize xmit_wait structure */
	dd->pport[port_idx].cong_stats.counter = 0;
	timer_setup(&dd->pport[port_idx].cong_stats.timer,
		    xmit_wait_timer_func, 0);
	dd->pport[port_idx].cong_stats.timer.expires = 0;
	add_timer(&dd->pport[port_idx].cong_stats.timer);
}

void qib_notify_free_mad_agent(struct rvt_dev_info *rdi, int port_idx)
{
	struct qib_ibdev *ibdev = container_of(rdi, struct qib_ibdev, rdi);
	struct qib_devdata *dd = container_of(ibdev,
					      struct qib_devdata, verbs_dev);

	if (dd->pport[port_idx].cong_stats.timer.function)
		del_timer_sync(&dd->pport[port_idx].cong_stats.timer);

	if (dd->pport[port_idx].ibport_data.smi_ah)
		rdma_destroy_ah(&dd->pport[port_idx].ibport_data.smi_ah->ibah);
}
