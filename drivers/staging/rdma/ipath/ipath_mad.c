/*
 * Copyright (c) 2006, 2007, 2008 QLogic Corporation. All rights reserved.
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
#include <rdma/ib_pma.h>

#include "ipath_kernel.h"
#include "ipath_verbs.h"
#include "ipath_common.h"

#define IB_SMP_UNSUP_VERSION	cpu_to_be16(0x0004)
#define IB_SMP_UNSUP_METHOD	cpu_to_be16(0x0008)
#define IB_SMP_UNSUP_METH_ATTR	cpu_to_be16(0x000C)
#define IB_SMP_INVALID_FIELD	cpu_to_be16(0x001C)

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

static int recv_subn_get_nodedescription(struct ib_smp *smp,
					 struct ib_device *ibdev)
{
	if (smp->attr_mod)
		smp->status |= IB_SMP_INVALID_FIELD;

	memcpy(smp->data, ibdev->node_desc, sizeof(smp->data));

	return reply(smp);
}

struct nodeinfo {
	u8 base_version;
	u8 class_version;
	u8 node_type;
	u8 num_ports;
	__be64 sys_guid;
	__be64 node_guid;
	__be64 port_guid;
	__be16 partition_cap;
	__be16 device_id;
	__be32 revision;
	u8 local_port_num;
	u8 vendor_id[3];
} __attribute__ ((packed));

static int recv_subn_get_nodeinfo(struct ib_smp *smp,
				  struct ib_device *ibdev, u8 port)
{
	struct nodeinfo *nip = (struct nodeinfo *)&smp->data;
	struct ipath_devdata *dd = to_idev(ibdev)->dd;
	u32 vendor, majrev, minrev;

	/* GUID 0 is illegal */
	if (smp->attr_mod || (dd->ipath_guid == 0))
		smp->status |= IB_SMP_INVALID_FIELD;

	nip->base_version = 1;
	nip->class_version = 1;
	nip->node_type = 1;	/* channel adapter */
	/*
	 * XXX The num_ports value will need a layer function to get
	 * the value if we ever have more than one IB port on a chip.
	 * We will also need to get the GUID for the port.
	 */
	nip->num_ports = ibdev->phys_port_cnt;
	/* This is already in network order */
	nip->sys_guid = to_idev(ibdev)->sys_image_guid;
	nip->node_guid = dd->ipath_guid;
	nip->port_guid = dd->ipath_guid;
	nip->partition_cap = cpu_to_be16(ipath_get_npkeys(dd));
	nip->device_id = cpu_to_be16(dd->ipath_deviceid);
	majrev = dd->ipath_majrev;
	minrev = dd->ipath_minrev;
	nip->revision = cpu_to_be32((majrev << 16) | minrev);
	nip->local_port_num = port;
	vendor = dd->ipath_vendorid;
	nip->vendor_id[0] = IPATH_SRC_OUI_1;
	nip->vendor_id[1] = IPATH_SRC_OUI_2;
	nip->vendor_id[2] = IPATH_SRC_OUI_3;

	return reply(smp);
}

static int recv_subn_get_guidinfo(struct ib_smp *smp,
				  struct ib_device *ibdev)
{
	u32 startgx = 8 * be32_to_cpu(smp->attr_mod);
	__be64 *p = (__be64 *) smp->data;

	/* 32 blocks of 8 64-bit GUIDs per block */

	memset(smp->data, 0, sizeof(smp->data));

	/*
	 * We only support one GUID for now.  If this changes, the
	 * portinfo.guid_cap field needs to be updated too.
	 */
	if (startgx == 0) {
		__be64 g = to_idev(ibdev)->dd->ipath_guid;
		if (g == 0)
			/* GUID 0 is illegal */
			smp->status |= IB_SMP_INVALID_FIELD;
		else
			/* The first is a copy of the read-only HW GUID. */
			*p = g;
	} else
		smp->status |= IB_SMP_INVALID_FIELD;

	return reply(smp);
}

static void set_link_width_enabled(struct ipath_devdata *dd, u32 w)
{
	(void) dd->ipath_f_set_ib_cfg(dd, IPATH_IB_CFG_LWID_ENB, w);
}

static void set_link_speed_enabled(struct ipath_devdata *dd, u32 s)
{
	(void) dd->ipath_f_set_ib_cfg(dd, IPATH_IB_CFG_SPD_ENB, s);
}

static int get_overrunthreshold(struct ipath_devdata *dd)
{
	return (dd->ipath_ibcctrl >>
		INFINIPATH_IBCC_OVERRUNTHRESHOLD_SHIFT) &
		INFINIPATH_IBCC_OVERRUNTHRESHOLD_MASK;
}

/**
 * set_overrunthreshold - set the overrun threshold
 * @dd: the infinipath device
 * @n: the new threshold
 *
 * Note that this will only take effect when the link state changes.
 */
static int set_overrunthreshold(struct ipath_devdata *dd, unsigned n)
{
	unsigned v;

	v = (dd->ipath_ibcctrl >> INFINIPATH_IBCC_OVERRUNTHRESHOLD_SHIFT) &
		INFINIPATH_IBCC_OVERRUNTHRESHOLD_MASK;
	if (v != n) {
		dd->ipath_ibcctrl &=
			~(INFINIPATH_IBCC_OVERRUNTHRESHOLD_MASK <<
			  INFINIPATH_IBCC_OVERRUNTHRESHOLD_SHIFT);
		dd->ipath_ibcctrl |=
			(u64) n << INFINIPATH_IBCC_OVERRUNTHRESHOLD_SHIFT;
		ipath_write_kreg(dd, dd->ipath_kregs->kr_ibcctrl,
				 dd->ipath_ibcctrl);
	}
	return 0;
}

static int get_phyerrthreshold(struct ipath_devdata *dd)
{
	return (dd->ipath_ibcctrl >>
		INFINIPATH_IBCC_PHYERRTHRESHOLD_SHIFT) &
		INFINIPATH_IBCC_PHYERRTHRESHOLD_MASK;
}

/**
 * set_phyerrthreshold - set the physical error threshold
 * @dd: the infinipath device
 * @n: the new threshold
 *
 * Note that this will only take effect when the link state changes.
 */
static int set_phyerrthreshold(struct ipath_devdata *dd, unsigned n)
{
	unsigned v;

	v = (dd->ipath_ibcctrl >> INFINIPATH_IBCC_PHYERRTHRESHOLD_SHIFT) &
		INFINIPATH_IBCC_PHYERRTHRESHOLD_MASK;
	if (v != n) {
		dd->ipath_ibcctrl &=
			~(INFINIPATH_IBCC_PHYERRTHRESHOLD_MASK <<
			  INFINIPATH_IBCC_PHYERRTHRESHOLD_SHIFT);
		dd->ipath_ibcctrl |=
			(u64) n << INFINIPATH_IBCC_PHYERRTHRESHOLD_SHIFT;
		ipath_write_kreg(dd, dd->ipath_kregs->kr_ibcctrl,
				 dd->ipath_ibcctrl);
	}
	return 0;
}

/**
 * get_linkdowndefaultstate - get the default linkdown state
 * @dd: the infinipath device
 *
 * Returns zero if the default is POLL, 1 if the default is SLEEP.
 */
static int get_linkdowndefaultstate(struct ipath_devdata *dd)
{
	return !!(dd->ipath_ibcctrl & INFINIPATH_IBCC_LINKDOWNDEFAULTSTATE);
}

static int recv_subn_get_portinfo(struct ib_smp *smp,
				  struct ib_device *ibdev, u8 port)
{
	struct ipath_ibdev *dev;
	struct ipath_devdata *dd;
	struct ib_port_info *pip = (struct ib_port_info *)smp->data;
	u16 lid;
	u8 ibcstat;
	u8 mtu;
	int ret;

	if (be32_to_cpu(smp->attr_mod) > ibdev->phys_port_cnt) {
		smp->status |= IB_SMP_INVALID_FIELD;
		ret = reply(smp);
		goto bail;
	}

	dev = to_idev(ibdev);
	dd = dev->dd;

	/* Clear all fields.  Only set the non-zero fields. */
	memset(smp->data, 0, sizeof(smp->data));

	/* Only return the mkey if the protection field allows it. */
	if (smp->method == IB_MGMT_METHOD_SET || dev->mkey == smp->mkey ||
	    dev->mkeyprot == 0)
		pip->mkey = dev->mkey;
	pip->gid_prefix = dev->gid_prefix;
	lid = dd->ipath_lid;
	pip->lid = lid ? cpu_to_be16(lid) : IB_LID_PERMISSIVE;
	pip->sm_lid = cpu_to_be16(dev->sm_lid);
	pip->cap_mask = cpu_to_be32(dev->port_cap_flags);
	/* pip->diag_code; */
	pip->mkey_lease_period = cpu_to_be16(dev->mkey_lease_period);
	pip->local_port_num = port;
	pip->link_width_enabled = dd->ipath_link_width_enabled;
	pip->link_width_supported = dd->ipath_link_width_supported;
	pip->link_width_active = dd->ipath_link_width_active;
	pip->linkspeed_portstate = dd->ipath_link_speed_supported << 4;
	ibcstat = dd->ipath_lastibcstat;
	/* map LinkState to IB portinfo values.  */
	pip->linkspeed_portstate |= ipath_ib_linkstate(dd, ibcstat) + 1;

	pip->portphysstate_linkdown =
		(ipath_cvt_physportstate[ibcstat & dd->ibcs_lts_mask] << 4) |
		(get_linkdowndefaultstate(dd) ? 1 : 2);
	pip->mkeyprot_resv_lmc = (dev->mkeyprot << 6) | dd->ipath_lmc;
	pip->linkspeedactive_enabled = (dd->ipath_link_speed_active << 4) |
		dd->ipath_link_speed_enabled;
	switch (dd->ipath_ibmtu) {
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
	default:		/* oops, something is wrong */
		mtu = IB_MTU_2048;
		break;
	}
	pip->neighbormtu_mastersmsl = (mtu << 4) | dev->sm_sl;
	pip->vlcap_inittype = 0x10;	/* VLCap = VL0, InitType = 0 */
	pip->vl_high_limit = dev->vl_high_limit;
	/* pip->vl_arb_high_cap; // only one VL */
	/* pip->vl_arb_low_cap; // only one VL */
	/* InitTypeReply = 0 */
	/* our mtu cap depends on whether 4K MTU enabled or not */
	pip->inittypereply_mtucap = ipath_mtu4096 ? IB_MTU_4096 : IB_MTU_2048;
	/* HCAs ignore VLStallCount and HOQLife */
	/* pip->vlstallcnt_hoqlife; */
	pip->operationalvl_pei_peo_fpi_fpo = 0x10;	/* OVLs = 1 */
	pip->mkey_violations = cpu_to_be16(dev->mkey_violations);
	/* P_KeyViolations are counted by hardware. */
	pip->pkey_violations =
		cpu_to_be16((ipath_get_cr_errpkey(dd) -
			     dev->z_pkey_violations) & 0xFFFF);
	pip->qkey_violations = cpu_to_be16(dev->qkey_violations);
	/* Only the hardware GUID is supported for now */
	pip->guid_cap = 1;
	pip->clientrereg_resv_subnetto = dev->subnet_timeout;
	/* 32.768 usec. response time (guessing) */
	pip->resv_resptimevalue = 3;
	pip->localphyerrors_overrunerrors =
		(get_phyerrthreshold(dd) << 4) |
		get_overrunthreshold(dd);
	/* pip->max_credit_hint; */
	if (dev->port_cap_flags & IB_PORT_LINK_LATENCY_SUP) {
		u32 v;

		v = dd->ipath_f_get_ib_cfg(dd, IPATH_IB_CFG_LINKLATENCY);
		pip->link_roundtrip_latency[0] = v >> 16;
		pip->link_roundtrip_latency[1] = v >> 8;
		pip->link_roundtrip_latency[2] = v;
	}

	ret = reply(smp);

bail:
	return ret;
}

/**
 * get_pkeys - return the PKEY table for port 0
 * @dd: the infinipath device
 * @pkeys: the pkey table is placed here
 */
static int get_pkeys(struct ipath_devdata *dd, u16 * pkeys)
{
	/* always a kernel port, no locking needed */
	struct ipath_portdata *pd = dd->ipath_pd[0];

	memcpy(pkeys, pd->port_pkeys, sizeof(pd->port_pkeys));

	return 0;
}

static int recv_subn_get_pkeytable(struct ib_smp *smp,
				   struct ib_device *ibdev)
{
	u32 startpx = 32 * (be32_to_cpu(smp->attr_mod) & 0xffff);
	u16 *p = (u16 *) smp->data;
	__be16 *q = (__be16 *) smp->data;

	/* 64 blocks of 32 16-bit P_Key entries */

	memset(smp->data, 0, sizeof(smp->data));
	if (startpx == 0) {
		struct ipath_ibdev *dev = to_idev(ibdev);
		unsigned i, n = ipath_get_npkeys(dev->dd);

		get_pkeys(dev->dd, p);

		for (i = 0; i < n; i++)
			q[i] = cpu_to_be16(p[i]);
	} else
		smp->status |= IB_SMP_INVALID_FIELD;

	return reply(smp);
}

static int recv_subn_set_guidinfo(struct ib_smp *smp,
				  struct ib_device *ibdev)
{
	/* The only GUID we support is the first read-only entry. */
	return recv_subn_get_guidinfo(smp, ibdev);
}

/**
 * set_linkdowndefaultstate - set the default linkdown state
 * @dd: the infinipath device
 * @sleep: the new state
 *
 * Note that this will only take effect when the link state changes.
 */
static int set_linkdowndefaultstate(struct ipath_devdata *dd, int sleep)
{
	if (sleep)
		dd->ipath_ibcctrl |= INFINIPATH_IBCC_LINKDOWNDEFAULTSTATE;
	else
		dd->ipath_ibcctrl &= ~INFINIPATH_IBCC_LINKDOWNDEFAULTSTATE;
	ipath_write_kreg(dd, dd->ipath_kregs->kr_ibcctrl,
			 dd->ipath_ibcctrl);
	return 0;
}

/**
 * recv_subn_set_portinfo - set port information
 * @smp: the incoming SM packet
 * @ibdev: the infiniband device
 * @port: the port on the device
 *
 * Set Portinfo (see ch. 14.2.5.6).
 */
static int recv_subn_set_portinfo(struct ib_smp *smp,
				  struct ib_device *ibdev, u8 port)
{
	struct ib_port_info *pip = (struct ib_port_info *)smp->data;
	struct ib_event event;
	struct ipath_ibdev *dev;
	struct ipath_devdata *dd;
	char clientrereg = 0;
	u16 lid, smlid;
	u8 lwe;
	u8 lse;
	u8 state;
	u16 lstate;
	u32 mtu;
	int ret, ore;

	if (be32_to_cpu(smp->attr_mod) > ibdev->phys_port_cnt)
		goto err;

	dev = to_idev(ibdev);
	dd = dev->dd;
	event.device = ibdev;
	event.element.port_num = port;

	dev->mkey = pip->mkey;
	dev->gid_prefix = pip->gid_prefix;
	dev->mkey_lease_period = be16_to_cpu(pip->mkey_lease_period);

	lid = be16_to_cpu(pip->lid);
	if (dd->ipath_lid != lid ||
	    dd->ipath_lmc != (pip->mkeyprot_resv_lmc & 7)) {
		/* Must be a valid unicast LID address. */
		if (lid == 0 || lid >= IPATH_MULTICAST_LID_BASE)
			goto err;
		ipath_set_lid(dd, lid, pip->mkeyprot_resv_lmc & 7);
		event.event = IB_EVENT_LID_CHANGE;
		ib_dispatch_event(&event);
	}

	smlid = be16_to_cpu(pip->sm_lid);
	if (smlid != dev->sm_lid) {
		/* Must be a valid unicast LID address. */
		if (smlid == 0 || smlid >= IPATH_MULTICAST_LID_BASE)
			goto err;
		dev->sm_lid = smlid;
		event.event = IB_EVENT_SM_CHANGE;
		ib_dispatch_event(&event);
	}

	/* Allow 1x or 4x to be set (see 14.2.6.6). */
	lwe = pip->link_width_enabled;
	if (lwe) {
		if (lwe == 0xFF)
			lwe = dd->ipath_link_width_supported;
		else if (lwe >= 16 || (lwe & ~dd->ipath_link_width_supported))
			goto err;
		set_link_width_enabled(dd, lwe);
	}

	/* Allow 2.5 or 5.0 Gbs. */
	lse = pip->linkspeedactive_enabled & 0xF;
	if (lse) {
		if (lse == 15)
			lse = dd->ipath_link_speed_supported;
		else if (lse >= 8 || (lse & ~dd->ipath_link_speed_supported))
			goto err;
		set_link_speed_enabled(dd, lse);
	}

	/* Set link down default state. */
	switch (pip->portphysstate_linkdown & 0xF) {
	case 0: /* NOP */
		break;
	case 1: /* SLEEP */
		if (set_linkdowndefaultstate(dd, 1))
			goto err;
		break;
	case 2: /* POLL */
		if (set_linkdowndefaultstate(dd, 0))
			goto err;
		break;
	default:
		goto err;
	}

	dev->mkeyprot = pip->mkeyprot_resv_lmc >> 6;
	dev->vl_high_limit = pip->vl_high_limit;

	switch ((pip->neighbormtu_mastersmsl >> 4) & 0xF) {
	case IB_MTU_256:
		mtu = 256;
		break;
	case IB_MTU_512:
		mtu = 512;
		break;
	case IB_MTU_1024:
		mtu = 1024;
		break;
	case IB_MTU_2048:
		mtu = 2048;
		break;
	case IB_MTU_4096:
		if (!ipath_mtu4096)
			goto err;
		mtu = 4096;
		break;
	default:
		/* XXX We have already partially updated our state! */
		goto err;
	}
	ipath_set_mtu(dd, mtu);

	dev->sm_sl = pip->neighbormtu_mastersmsl & 0xF;

	/* We only support VL0 */
	if (((pip->operationalvl_pei_peo_fpi_fpo >> 4) & 0xF) > 1)
		goto err;

	if (pip->mkey_violations == 0)
		dev->mkey_violations = 0;

	/*
	 * Hardware counter can't be reset so snapshot and subtract
	 * later.
	 */
	if (pip->pkey_violations == 0)
		dev->z_pkey_violations = ipath_get_cr_errpkey(dd);

	if (pip->qkey_violations == 0)
		dev->qkey_violations = 0;

	ore = pip->localphyerrors_overrunerrors;
	if (set_phyerrthreshold(dd, (ore >> 4) & 0xF))
		goto err;

	if (set_overrunthreshold(dd, (ore & 0xF)))
		goto err;

	dev->subnet_timeout = pip->clientrereg_resv_subnetto & 0x1F;

	if (pip->clientrereg_resv_subnetto & 0x80) {
		clientrereg = 1;
		event.event = IB_EVENT_CLIENT_REREGISTER;
		ib_dispatch_event(&event);
	}

	/*
	 * Do the port state change now that the other link parameters
	 * have been set.
	 * Changing the port physical state only makes sense if the link
	 * is down or is being set to down.
	 */
	state = pip->linkspeed_portstate & 0xF;
	lstate = (pip->portphysstate_linkdown >> 4) & 0xF;
	if (lstate && !(state == IB_PORT_DOWN || state == IB_PORT_NOP))
		goto err;

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
			lstate = IPATH_IB_LINKDOWN_ONLY;
		else if (lstate == 1)
			lstate = IPATH_IB_LINKDOWN_SLEEP;
		else if (lstate == 2)
			lstate = IPATH_IB_LINKDOWN;
		else if (lstate == 3)
			lstate = IPATH_IB_LINKDOWN_DISABLE;
		else
			goto err;
		ipath_set_linkstate(dd, lstate);
		if (lstate == IPATH_IB_LINKDOWN_DISABLE) {
			ret = IB_MAD_RESULT_SUCCESS | IB_MAD_RESULT_CONSUMED;
			goto done;
		}
		ipath_wait_linkstate(dd, IPATH_LINKINIT | IPATH_LINKARMED |
				IPATH_LINKACTIVE, 1000);
		break;
	case IB_PORT_ARMED:
		ipath_set_linkstate(dd, IPATH_IB_LINKARM);
		break;
	case IB_PORT_ACTIVE:
		ipath_set_linkstate(dd, IPATH_IB_LINKACTIVE);
		break;
	default:
		/* XXX We have already partially updated our state! */
		goto err;
	}

	ret = recv_subn_get_portinfo(smp, ibdev, port);

	if (clientrereg)
		pip->clientrereg_resv_subnetto |= 0x80;

	goto done;

err:
	smp->status |= IB_SMP_INVALID_FIELD;
	ret = recv_subn_get_portinfo(smp, ibdev, port);

done:
	return ret;
}

/**
 * rm_pkey - decrecment the reference count for the given PKEY
 * @dd: the infinipath device
 * @key: the PKEY index
 *
 * Return true if this was the last reference and the hardware table entry
 * needs to be changed.
 */
static int rm_pkey(struct ipath_devdata *dd, u16 key)
{
	int i;
	int ret;

	for (i = 0; i < ARRAY_SIZE(dd->ipath_pkeys); i++) {
		if (dd->ipath_pkeys[i] != key)
			continue;
		if (atomic_dec_and_test(&dd->ipath_pkeyrefs[i])) {
			dd->ipath_pkeys[i] = 0;
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
 * @dd: the infinipath device
 * @key: the PKEY
 *
 * Return an error code if unable to add the entry, zero if no change,
 * or 1 if the hardware PKEY register needs to be updated.
 */
static int add_pkey(struct ipath_devdata *dd, u16 key)
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
	for (i = 0; i < ARRAY_SIZE(dd->ipath_pkeys); i++) {
		if (!dd->ipath_pkeys[i]) {
			any++;
			continue;
		}
		/* If it matches exactly, try to increment the ref count */
		if (dd->ipath_pkeys[i] == key) {
			if (atomic_inc_return(&dd->ipath_pkeyrefs[i]) > 1) {
				ret = 0;
				goto bail;
			}
			/* Lost the race. Look for an empty slot below. */
			atomic_dec(&dd->ipath_pkeyrefs[i]);
			any++;
		}
		/*
		 * It makes no sense to have both the limited and unlimited
		 * PKEY set at the same time since the unlimited one will
		 * disable the limited one.
		 */
		if ((dd->ipath_pkeys[i] & 0x7FFF) == lkey) {
			ret = -EEXIST;
			goto bail;
		}
	}
	if (!any) {
		ret = -EBUSY;
		goto bail;
	}
	for (i = 0; i < ARRAY_SIZE(dd->ipath_pkeys); i++) {
		if (!dd->ipath_pkeys[i] &&
		    atomic_inc_return(&dd->ipath_pkeyrefs[i]) == 1) {
			/* for ipathstats, etc. */
			ipath_stats.sps_pkeys[i] = lkey;
			dd->ipath_pkeys[i] = key;
			ret = 1;
			goto bail;
		}
	}
	ret = -EBUSY;

bail:
	return ret;
}

/**
 * set_pkeys - set the PKEY table for port 0
 * @dd: the infinipath device
 * @pkeys: the PKEY table
 */
static int set_pkeys(struct ipath_devdata *dd, u16 *pkeys, u8 port)
{
	struct ipath_portdata *pd;
	int i;
	int changed = 0;

	/* always a kernel port, no locking needed */
	pd = dd->ipath_pd[0];

	for (i = 0; i < ARRAY_SIZE(pd->port_pkeys); i++) {
		u16 key = pkeys[i];
		u16 okey = pd->port_pkeys[i];

		if (key == okey)
			continue;
		/*
		 * The value of this PKEY table entry is changing.
		 * Remove the old entry in the hardware's array of PKEYs.
		 */
		if (okey & 0x7FFF)
			changed |= rm_pkey(dd, okey);
		if (key & 0x7FFF) {
			int ret = add_pkey(dd, key);

			if (ret < 0)
				key = 0;
			else
				changed |= ret;
		}
		pd->port_pkeys[i] = key;
	}
	if (changed) {
		u64 pkey;
		struct ib_event event;

		pkey = (u64) dd->ipath_pkeys[0] |
			((u64) dd->ipath_pkeys[1] << 16) |
			((u64) dd->ipath_pkeys[2] << 32) |
			((u64) dd->ipath_pkeys[3] << 48);
		ipath_cdbg(VERBOSE, "p0 new pkey reg %llx\n",
			   (unsigned long long) pkey);
		ipath_write_kreg(dd, dd->ipath_kregs->kr_partitionkey,
				 pkey);

		event.event = IB_EVENT_PKEY_CHANGE;
		event.device = &dd->verbs_dev->ibdev;
		event.element.port_num = port;
		ib_dispatch_event(&event);
	}
	return 0;
}

static int recv_subn_set_pkeytable(struct ib_smp *smp,
				   struct ib_device *ibdev, u8 port)
{
	u32 startpx = 32 * (be32_to_cpu(smp->attr_mod) & 0xffff);
	__be16 *p = (__be16 *) smp->data;
	u16 *q = (u16 *) smp->data;
	struct ipath_ibdev *dev = to_idev(ibdev);
	unsigned i, n = ipath_get_npkeys(dev->dd);

	for (i = 0; i < n; i++)
		q[i] = be16_to_cpu(p[i]);

	if (startpx != 0 || set_pkeys(dev->dd, q, port) != 0)
		smp->status |= IB_SMP_INVALID_FIELD;

	return recv_subn_get_pkeytable(smp, ibdev);
}

static int recv_pma_get_classportinfo(struct ib_pma_mad *pmp)
{
	struct ib_class_port_info *p =
		(struct ib_class_port_info *)pmp->data;

	memset(pmp->data, 0, sizeof(pmp->data));

	if (pmp->mad_hdr.attr_mod != 0)
		pmp->mad_hdr.status |= IB_SMP_INVALID_FIELD;

	/* Indicate AllPortSelect is valid (only one port anyway) */
	p->capability_mask = cpu_to_be16(1 << 8);
	p->base_version = 1;
	p->class_version = 1;
	/*
	 * Expected response time is 4.096 usec. * 2^18 == 1.073741824
	 * sec.
	 */
	p->resp_time_value = 18;

	return reply((struct ib_smp *) pmp);
}

/*
 * The PortSamplesControl.CounterMasks field is an array of 3 bit fields
 * which specify the N'th counter's capabilities. See ch. 16.1.3.2.
 * We support 5 counters which only count the mandatory quantities.
 */
#define COUNTER_MASK(q, n) (q << ((9 - n) * 3))
#define COUNTER_MASK0_9 cpu_to_be32(COUNTER_MASK(1, 0) | \
				    COUNTER_MASK(1, 1) | \
				    COUNTER_MASK(1, 2) | \
				    COUNTER_MASK(1, 3) | \
				    COUNTER_MASK(1, 4))

static int recv_pma_get_portsamplescontrol(struct ib_pma_mad *pmp,
					   struct ib_device *ibdev, u8 port)
{
	struct ib_pma_portsamplescontrol *p =
		(struct ib_pma_portsamplescontrol *)pmp->data;
	struct ipath_ibdev *dev = to_idev(ibdev);
	struct ipath_cregs const *crp = dev->dd->ipath_cregs;
	unsigned long flags;
	u8 port_select = p->port_select;

	memset(pmp->data, 0, sizeof(pmp->data));

	p->port_select = port_select;
	if (pmp->mad_hdr.attr_mod != 0 ||
	    (port_select != port && port_select != 0xFF))
		pmp->mad_hdr.status |= IB_SMP_INVALID_FIELD;
	/*
	 * Ticks are 10x the link transfer period which for 2.5Gbs is 4
	 * nsec.  0 == 4 nsec., 1 == 8 nsec., ..., 255 == 1020 nsec.  Sample
	 * intervals are counted in ticks.  Since we use Linux timers, that
	 * count in jiffies, we can't sample for less than 1000 ticks if HZ
	 * == 1000 (4000 ticks if HZ is 250).  link_speed_active returns 2 for
	 * DDR, 1 for SDR, set the tick to 1 for DDR, 0 for SDR on chips that
	 * have hardware support for delaying packets.
	 */
	if (crp->cr_psstat)
		p->tick = dev->dd->ipath_link_speed_active - 1;
	else
		p->tick = 250;		/* 1 usec. */
	p->counter_width = 4;	/* 32 bit counters */
	p->counter_mask0_9 = COUNTER_MASK0_9;
	spin_lock_irqsave(&dev->pending_lock, flags);
	if (crp->cr_psstat)
		p->sample_status = ipath_read_creg32(dev->dd, crp->cr_psstat);
	else
		p->sample_status = dev->pma_sample_status;
	p->sample_start = cpu_to_be32(dev->pma_sample_start);
	p->sample_interval = cpu_to_be32(dev->pma_sample_interval);
	p->tag = cpu_to_be16(dev->pma_tag);
	p->counter_select[0] = dev->pma_counter_select[0];
	p->counter_select[1] = dev->pma_counter_select[1];
	p->counter_select[2] = dev->pma_counter_select[2];
	p->counter_select[3] = dev->pma_counter_select[3];
	p->counter_select[4] = dev->pma_counter_select[4];
	spin_unlock_irqrestore(&dev->pending_lock, flags);

	return reply((struct ib_smp *) pmp);
}

static int recv_pma_set_portsamplescontrol(struct ib_pma_mad *pmp,
					   struct ib_device *ibdev, u8 port)
{
	struct ib_pma_portsamplescontrol *p =
		(struct ib_pma_portsamplescontrol *)pmp->data;
	struct ipath_ibdev *dev = to_idev(ibdev);
	struct ipath_cregs const *crp = dev->dd->ipath_cregs;
	unsigned long flags;
	u8 status;
	int ret;

	if (pmp->mad_hdr.attr_mod != 0 ||
	    (p->port_select != port && p->port_select != 0xFF)) {
		pmp->mad_hdr.status |= IB_SMP_INVALID_FIELD;
		ret = reply((struct ib_smp *) pmp);
		goto bail;
	}

	spin_lock_irqsave(&dev->pending_lock, flags);
	if (crp->cr_psstat)
		status = ipath_read_creg32(dev->dd, crp->cr_psstat);
	else
		status = dev->pma_sample_status;
	if (status == IB_PMA_SAMPLE_STATUS_DONE) {
		dev->pma_sample_start = be32_to_cpu(p->sample_start);
		dev->pma_sample_interval = be32_to_cpu(p->sample_interval);
		dev->pma_tag = be16_to_cpu(p->tag);
		dev->pma_counter_select[0] = p->counter_select[0];
		dev->pma_counter_select[1] = p->counter_select[1];
		dev->pma_counter_select[2] = p->counter_select[2];
		dev->pma_counter_select[3] = p->counter_select[3];
		dev->pma_counter_select[4] = p->counter_select[4];
		if (crp->cr_psstat) {
			ipath_write_creg(dev->dd, crp->cr_psinterval,
					 dev->pma_sample_interval);
			ipath_write_creg(dev->dd, crp->cr_psstart,
					 dev->pma_sample_start);
		} else
			dev->pma_sample_status = IB_PMA_SAMPLE_STATUS_STARTED;
	}
	spin_unlock_irqrestore(&dev->pending_lock, flags);

	ret = recv_pma_get_portsamplescontrol(pmp, ibdev, port);

bail:
	return ret;
}

static u64 get_counter(struct ipath_ibdev *dev,
		       struct ipath_cregs const *crp,
		       __be16 sel)
{
	u64 ret;

	switch (sel) {
	case IB_PMA_PORT_XMIT_DATA:
		ret = (crp->cr_psxmitdatacount) ?
			ipath_read_creg32(dev->dd, crp->cr_psxmitdatacount) :
			dev->ipath_sword;
		break;
	case IB_PMA_PORT_RCV_DATA:
		ret = (crp->cr_psrcvdatacount) ?
			ipath_read_creg32(dev->dd, crp->cr_psrcvdatacount) :
			dev->ipath_rword;
		break;
	case IB_PMA_PORT_XMIT_PKTS:
		ret = (crp->cr_psxmitpktscount) ?
			ipath_read_creg32(dev->dd, crp->cr_psxmitpktscount) :
			dev->ipath_spkts;
		break;
	case IB_PMA_PORT_RCV_PKTS:
		ret = (crp->cr_psrcvpktscount) ?
			ipath_read_creg32(dev->dd, crp->cr_psrcvpktscount) :
			dev->ipath_rpkts;
		break;
	case IB_PMA_PORT_XMIT_WAIT:
		ret = (crp->cr_psxmitwaitcount) ?
			ipath_read_creg32(dev->dd, crp->cr_psxmitwaitcount) :
			dev->ipath_xmit_wait;
		break;
	default:
		ret = 0;
	}

	return ret;
}

static int recv_pma_get_portsamplesresult(struct ib_pma_mad *pmp,
					  struct ib_device *ibdev)
{
	struct ib_pma_portsamplesresult *p =
		(struct ib_pma_portsamplesresult *)pmp->data;
	struct ipath_ibdev *dev = to_idev(ibdev);
	struct ipath_cregs const *crp = dev->dd->ipath_cregs;
	u8 status;
	int i;

	memset(pmp->data, 0, sizeof(pmp->data));
	p->tag = cpu_to_be16(dev->pma_tag);
	if (crp->cr_psstat)
		status = ipath_read_creg32(dev->dd, crp->cr_psstat);
	else
		status = dev->pma_sample_status;
	p->sample_status = cpu_to_be16(status);
	for (i = 0; i < ARRAY_SIZE(dev->pma_counter_select); i++)
		p->counter[i] = (status != IB_PMA_SAMPLE_STATUS_DONE) ? 0 :
		    cpu_to_be32(
			get_counter(dev, crp, dev->pma_counter_select[i]));

	return reply((struct ib_smp *) pmp);
}

static int recv_pma_get_portsamplesresult_ext(struct ib_pma_mad *pmp,
					      struct ib_device *ibdev)
{
	struct ib_pma_portsamplesresult_ext *p =
		(struct ib_pma_portsamplesresult_ext *)pmp->data;
	struct ipath_ibdev *dev = to_idev(ibdev);
	struct ipath_cregs const *crp = dev->dd->ipath_cregs;
	u8 status;
	int i;

	memset(pmp->data, 0, sizeof(pmp->data));
	p->tag = cpu_to_be16(dev->pma_tag);
	if (crp->cr_psstat)
		status = ipath_read_creg32(dev->dd, crp->cr_psstat);
	else
		status = dev->pma_sample_status;
	p->sample_status = cpu_to_be16(status);
	/* 64 bits */
	p->extended_width = cpu_to_be32(0x80000000);
	for (i = 0; i < ARRAY_SIZE(dev->pma_counter_select); i++)
		p->counter[i] = (status != IB_PMA_SAMPLE_STATUS_DONE) ? 0 :
		    cpu_to_be64(
			get_counter(dev, crp, dev->pma_counter_select[i]));

	return reply((struct ib_smp *) pmp);
}

static int recv_pma_get_portcounters(struct ib_pma_mad *pmp,
				     struct ib_device *ibdev, u8 port)
{
	struct ib_pma_portcounters *p = (struct ib_pma_portcounters *)
		pmp->data;
	struct ipath_ibdev *dev = to_idev(ibdev);
	struct ipath_verbs_counters cntrs;
	u8 port_select = p->port_select;

	ipath_get_counters(dev->dd, &cntrs);

	/* Adjust counters for any resets done. */
	cntrs.symbol_error_counter -= dev->z_symbol_error_counter;
	cntrs.link_error_recovery_counter -=
		dev->z_link_error_recovery_counter;
	cntrs.link_downed_counter -= dev->z_link_downed_counter;
	cntrs.port_rcv_errors += dev->rcv_errors;
	cntrs.port_rcv_errors -= dev->z_port_rcv_errors;
	cntrs.port_rcv_remphys_errors -= dev->z_port_rcv_remphys_errors;
	cntrs.port_xmit_discards -= dev->z_port_xmit_discards;
	cntrs.port_xmit_data -= dev->z_port_xmit_data;
	cntrs.port_rcv_data -= dev->z_port_rcv_data;
	cntrs.port_xmit_packets -= dev->z_port_xmit_packets;
	cntrs.port_rcv_packets -= dev->z_port_rcv_packets;
	cntrs.local_link_integrity_errors -=
		dev->z_local_link_integrity_errors;
	cntrs.excessive_buffer_overrun_errors -=
		dev->z_excessive_buffer_overrun_errors;
	cntrs.vl15_dropped -= dev->z_vl15_dropped;
	cntrs.vl15_dropped += dev->n_vl15_dropped;

	memset(pmp->data, 0, sizeof(pmp->data));

	p->port_select = port_select;
	if (pmp->mad_hdr.attr_mod != 0 ||
	    (port_select != port && port_select != 0xFF))
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

static int recv_pma_get_portcounters_ext(struct ib_pma_mad *pmp,
					 struct ib_device *ibdev, u8 port)
{
	struct ib_pma_portcounters_ext *p =
		(struct ib_pma_portcounters_ext *)pmp->data;
	struct ipath_ibdev *dev = to_idev(ibdev);
	u64 swords, rwords, spkts, rpkts, xwait;
	u8 port_select = p->port_select;

	ipath_snapshot_counters(dev->dd, &swords, &rwords, &spkts,
				&rpkts, &xwait);

	/* Adjust counters for any resets done. */
	swords -= dev->z_port_xmit_data;
	rwords -= dev->z_port_rcv_data;
	spkts -= dev->z_port_xmit_packets;
	rpkts -= dev->z_port_rcv_packets;

	memset(pmp->data, 0, sizeof(pmp->data));

	p->port_select = port_select;
	if (pmp->mad_hdr.attr_mod != 0 ||
	    (port_select != port && port_select != 0xFF))
		pmp->mad_hdr.status |= IB_SMP_INVALID_FIELD;

	p->port_xmit_data = cpu_to_be64(swords);
	p->port_rcv_data = cpu_to_be64(rwords);
	p->port_xmit_packets = cpu_to_be64(spkts);
	p->port_rcv_packets = cpu_to_be64(rpkts);
	p->port_unicast_xmit_packets = cpu_to_be64(dev->n_unicast_xmit);
	p->port_unicast_rcv_packets = cpu_to_be64(dev->n_unicast_rcv);
	p->port_multicast_xmit_packets = cpu_to_be64(dev->n_multicast_xmit);
	p->port_multicast_rcv_packets = cpu_to_be64(dev->n_multicast_rcv);

	return reply((struct ib_smp *) pmp);
}

static int recv_pma_set_portcounters(struct ib_pma_mad *pmp,
				     struct ib_device *ibdev, u8 port)
{
	struct ib_pma_portcounters *p = (struct ib_pma_portcounters *)
		pmp->data;
	struct ipath_ibdev *dev = to_idev(ibdev);
	struct ipath_verbs_counters cntrs;

	/*
	 * Since the HW doesn't support clearing counters, we save the
	 * current count and subtract it from future responses.
	 */
	ipath_get_counters(dev->dd, &cntrs);

	if (p->counter_select & IB_PMA_SEL_SYMBOL_ERROR)
		dev->z_symbol_error_counter = cntrs.symbol_error_counter;

	if (p->counter_select & IB_PMA_SEL_LINK_ERROR_RECOVERY)
		dev->z_link_error_recovery_counter =
			cntrs.link_error_recovery_counter;

	if (p->counter_select & IB_PMA_SEL_LINK_DOWNED)
		dev->z_link_downed_counter = cntrs.link_downed_counter;

	if (p->counter_select & IB_PMA_SEL_PORT_RCV_ERRORS)
		dev->z_port_rcv_errors =
			cntrs.port_rcv_errors + dev->rcv_errors;

	if (p->counter_select & IB_PMA_SEL_PORT_RCV_REMPHYS_ERRORS)
		dev->z_port_rcv_remphys_errors =
			cntrs.port_rcv_remphys_errors;

	if (p->counter_select & IB_PMA_SEL_PORT_XMIT_DISCARDS)
		dev->z_port_xmit_discards = cntrs.port_xmit_discards;

	if (p->counter_select & IB_PMA_SEL_LOCAL_LINK_INTEGRITY_ERRORS)
		dev->z_local_link_integrity_errors =
			cntrs.local_link_integrity_errors;

	if (p->counter_select & IB_PMA_SEL_EXCESSIVE_BUFFER_OVERRUNS)
		dev->z_excessive_buffer_overrun_errors =
			cntrs.excessive_buffer_overrun_errors;

	if (p->counter_select & IB_PMA_SEL_PORT_VL15_DROPPED) {
		dev->n_vl15_dropped = 0;
		dev->z_vl15_dropped = cntrs.vl15_dropped;
	}

	if (p->counter_select & IB_PMA_SEL_PORT_XMIT_DATA)
		dev->z_port_xmit_data = cntrs.port_xmit_data;

	if (p->counter_select & IB_PMA_SEL_PORT_RCV_DATA)
		dev->z_port_rcv_data = cntrs.port_rcv_data;

	if (p->counter_select & IB_PMA_SEL_PORT_XMIT_PACKETS)
		dev->z_port_xmit_packets = cntrs.port_xmit_packets;

	if (p->counter_select & IB_PMA_SEL_PORT_RCV_PACKETS)
		dev->z_port_rcv_packets = cntrs.port_rcv_packets;

	return recv_pma_get_portcounters(pmp, ibdev, port);
}

static int recv_pma_set_portcounters_ext(struct ib_pma_mad *pmp,
					 struct ib_device *ibdev, u8 port)
{
	struct ib_pma_portcounters *p = (struct ib_pma_portcounters *)
		pmp->data;
	struct ipath_ibdev *dev = to_idev(ibdev);
	u64 swords, rwords, spkts, rpkts, xwait;

	ipath_snapshot_counters(dev->dd, &swords, &rwords, &spkts,
				&rpkts, &xwait);

	if (p->counter_select & IB_PMA_SELX_PORT_XMIT_DATA)
		dev->z_port_xmit_data = swords;

	if (p->counter_select & IB_PMA_SELX_PORT_RCV_DATA)
		dev->z_port_rcv_data = rwords;

	if (p->counter_select & IB_PMA_SELX_PORT_XMIT_PACKETS)
		dev->z_port_xmit_packets = spkts;

	if (p->counter_select & IB_PMA_SELX_PORT_RCV_PACKETS)
		dev->z_port_rcv_packets = rpkts;

	if (p->counter_select & IB_PMA_SELX_PORT_UNI_XMIT_PACKETS)
		dev->n_unicast_xmit = 0;

	if (p->counter_select & IB_PMA_SELX_PORT_UNI_RCV_PACKETS)
		dev->n_unicast_rcv = 0;

	if (p->counter_select & IB_PMA_SELX_PORT_MULTI_XMIT_PACKETS)
		dev->n_multicast_xmit = 0;

	if (p->counter_select & IB_PMA_SELX_PORT_MULTI_RCV_PACKETS)
		dev->n_multicast_rcv = 0;

	return recv_pma_get_portcounters_ext(pmp, ibdev, port);
}

static int process_subn(struct ib_device *ibdev, int mad_flags,
			u8 port_num, const struct ib_mad *in_mad,
			struct ib_mad *out_mad)
{
	struct ib_smp *smp = (struct ib_smp *)out_mad;
	struct ipath_ibdev *dev = to_idev(ibdev);
	int ret;

	*out_mad = *in_mad;
	if (smp->class_version != 1) {
		smp->status |= IB_SMP_UNSUP_VERSION;
		ret = reply(smp);
		goto bail;
	}

	/* Is the mkey in the process of expiring? */
	if (dev->mkey_lease_timeout &&
	    time_after_eq(jiffies, dev->mkey_lease_timeout)) {
		/* Clear timeout and mkey protection field. */
		dev->mkey_lease_timeout = 0;
		dev->mkeyprot = 0;
	}

	/*
	 * M_Key checking depends on
	 * Portinfo:M_Key_protect_bits
	 */
	if ((mad_flags & IB_MAD_IGNORE_MKEY) == 0 && dev->mkey != 0 &&
	    dev->mkey != smp->mkey &&
	    (smp->method == IB_MGMT_METHOD_SET ||
	     (smp->method == IB_MGMT_METHOD_GET &&
	      dev->mkeyprot >= 2))) {
		if (dev->mkey_violations != 0xFFFF)
			++dev->mkey_violations;
		if (dev->mkey_lease_timeout ||
		    dev->mkey_lease_period == 0) {
			ret = IB_MAD_RESULT_SUCCESS |
				IB_MAD_RESULT_CONSUMED;
			goto bail;
		}
		dev->mkey_lease_timeout = jiffies +
			dev->mkey_lease_period * HZ;
		/* Future: Generate a trap notice. */
		ret = IB_MAD_RESULT_SUCCESS | IB_MAD_RESULT_CONSUMED;
		goto bail;
	} else if (dev->mkey_lease_timeout)
		dev->mkey_lease_timeout = 0;

	switch (smp->method) {
	case IB_MGMT_METHOD_GET:
		switch (smp->attr_id) {
		case IB_SMP_ATTR_NODE_DESC:
			ret = recv_subn_get_nodedescription(smp, ibdev);
			goto bail;
		case IB_SMP_ATTR_NODE_INFO:
			ret = recv_subn_get_nodeinfo(smp, ibdev, port_num);
			goto bail;
		case IB_SMP_ATTR_GUID_INFO:
			ret = recv_subn_get_guidinfo(smp, ibdev);
			goto bail;
		case IB_SMP_ATTR_PORT_INFO:
			ret = recv_subn_get_portinfo(smp, ibdev, port_num);
			goto bail;
		case IB_SMP_ATTR_PKEY_TABLE:
			ret = recv_subn_get_pkeytable(smp, ibdev);
			goto bail;
		case IB_SMP_ATTR_SM_INFO:
			if (dev->port_cap_flags & IB_PORT_SM_DISABLED) {
				ret = IB_MAD_RESULT_SUCCESS |
					IB_MAD_RESULT_CONSUMED;
				goto bail;
			}
			if (dev->port_cap_flags & IB_PORT_SM) {
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
			ret = recv_subn_set_guidinfo(smp, ibdev);
			goto bail;
		case IB_SMP_ATTR_PORT_INFO:
			ret = recv_subn_set_portinfo(smp, ibdev, port_num);
			goto bail;
		case IB_SMP_ATTR_PKEY_TABLE:
			ret = recv_subn_set_pkeytable(smp, ibdev, port_num);
			goto bail;
		case IB_SMP_ATTR_SM_INFO:
			if (dev->port_cap_flags & IB_PORT_SM_DISABLED) {
				ret = IB_MAD_RESULT_SUCCESS |
					IB_MAD_RESULT_CONSUMED;
				goto bail;
			}
			if (dev->port_cap_flags & IB_PORT_SM) {
				ret = IB_MAD_RESULT_SUCCESS;
				goto bail;
			}
			/* FALLTHROUGH */
		default:
			smp->status |= IB_SMP_UNSUP_METH_ATTR;
			ret = reply(smp);
			goto bail;
		}

	case IB_MGMT_METHOD_TRAP:
	case IB_MGMT_METHOD_REPORT:
	case IB_MGMT_METHOD_REPORT_RESP:
	case IB_MGMT_METHOD_TRAP_REPRESS:
	case IB_MGMT_METHOD_GET_RESP:
		/*
		 * The ib_mad module will call us to process responses
		 * before checking for other consumers.
		 * Just tell the caller to process it normally.
		 */
		ret = IB_MAD_RESULT_SUCCESS;
		goto bail;
	default:
		smp->status |= IB_SMP_UNSUP_METHOD;
		ret = reply(smp);
	}

bail:
	return ret;
}

static int process_perf(struct ib_device *ibdev, u8 port_num,
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
			ret = recv_pma_get_classportinfo(pmp);
			goto bail;
		case IB_PMA_PORT_SAMPLES_CONTROL:
			ret = recv_pma_get_portsamplescontrol(pmp, ibdev,
							      port_num);
			goto bail;
		case IB_PMA_PORT_SAMPLES_RESULT:
			ret = recv_pma_get_portsamplesresult(pmp, ibdev);
			goto bail;
		case IB_PMA_PORT_SAMPLES_RESULT_EXT:
			ret = recv_pma_get_portsamplesresult_ext(pmp,
								 ibdev);
			goto bail;
		case IB_PMA_PORT_COUNTERS:
			ret = recv_pma_get_portcounters(pmp, ibdev,
							port_num);
			goto bail;
		case IB_PMA_PORT_COUNTERS_EXT:
			ret = recv_pma_get_portcounters_ext(pmp, ibdev,
							    port_num);
			goto bail;
		default:
			pmp->mad_hdr.status |= IB_SMP_UNSUP_METH_ATTR;
			ret = reply((struct ib_smp *) pmp);
			goto bail;
		}

	case IB_MGMT_METHOD_SET:
		switch (pmp->mad_hdr.attr_id) {
		case IB_PMA_PORT_SAMPLES_CONTROL:
			ret = recv_pma_set_portsamplescontrol(pmp, ibdev,
							      port_num);
			goto bail;
		case IB_PMA_PORT_COUNTERS:
			ret = recv_pma_set_portcounters(pmp, ibdev,
							port_num);
			goto bail;
		case IB_PMA_PORT_COUNTERS_EXT:
			ret = recv_pma_set_portcounters_ext(pmp, ibdev,
							    port_num);
			goto bail;
		default:
			pmp->mad_hdr.status |= IB_SMP_UNSUP_METH_ATTR;
			ret = reply((struct ib_smp *) pmp);
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
	default:
		pmp->mad_hdr.status |= IB_SMP_UNSUP_METHOD;
		ret = reply((struct ib_smp *) pmp);
	}

bail:
	return ret;
}

/**
 * ipath_process_mad - process an incoming MAD packet
 * @ibdev: the infiniband device this packet came in on
 * @mad_flags: MAD flags
 * @port_num: the port number this packet came in on
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
int ipath_process_mad(struct ib_device *ibdev, int mad_flags, u8 port_num,
		      const struct ib_wc *in_wc, const struct ib_grh *in_grh,
		      const struct ib_mad_hdr *in, size_t in_mad_size,
		      struct ib_mad_hdr *out, size_t *out_mad_size,
		      u16 *out_mad_pkey_index)
{
	int ret;
	const struct ib_mad *in_mad = (const struct ib_mad *)in;
	struct ib_mad *out_mad = (struct ib_mad *)out;

	if (WARN_ON_ONCE(in_mad_size != sizeof(*in_mad) ||
			 *out_mad_size != sizeof(*out_mad)))
		return IB_MAD_RESULT_FAILURE;

	switch (in_mad->mad_hdr.mgmt_class) {
	case IB_MGMT_CLASS_SUBN_DIRECTED_ROUTE:
	case IB_MGMT_CLASS_SUBN_LID_ROUTED:
		ret = process_subn(ibdev, mad_flags, port_num,
				   in_mad, out_mad);
		goto bail;
	case IB_MGMT_CLASS_PERF_MGMT:
		ret = process_perf(ibdev, port_num, in_mad, out_mad);
		goto bail;
	default:
		ret = IB_MAD_RESULT_SUCCESS;
	}

bail:
	return ret;
}
