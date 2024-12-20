// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2008-2010 Cisco Systems, Inc.  All rights reserved.
 * Copyright 2007 Nuova Systems, Inc.  All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/netdevice.h>

#include "wq_enet_desc.h"
#include "rq_enet_desc.h"
#include "cq_enet_desc.h"
#include "vnic_resource.h"
#include "vnic_enet.h"
#include "vnic_dev.h"
#include "vnic_wq.h"
#include "vnic_rq.h"
#include "vnic_cq.h"
#include "vnic_intr.h"
#include "vnic_stats.h"
#include "vnic_nic.h"
#include "vnic_rss.h"
#include "enic_res.h"
#include "enic.h"

int enic_get_vnic_config(struct enic *enic)
{
	struct vnic_enet_config *c = &enic->config;
	int err;

	err = vnic_dev_get_mac_addr(enic->vdev, enic->mac_addr);
	if (err) {
		dev_err(enic_get_dev(enic),
			"Error getting MAC addr, %d\n", err);
		return err;
	}

#define GET_CONFIG(m) \
	do { \
		err = vnic_dev_spec(enic->vdev, \
			offsetof(struct vnic_enet_config, m), \
			sizeof(c->m), &c->m); \
		if (err) { \
			dev_err(enic_get_dev(enic), \
				"Error getting %s, %d\n", #m, err); \
			return err; \
		} \
	} while (0)

	GET_CONFIG(flags);
	GET_CONFIG(wq_desc_count);
	GET_CONFIG(rq_desc_count);
	GET_CONFIG(mtu);
	GET_CONFIG(intr_timer_type);
	GET_CONFIG(intr_mode);
	GET_CONFIG(intr_timer_usec);
	GET_CONFIG(loop_tag);
	GET_CONFIG(num_arfs);

	c->wq_desc_count =
		min_t(u32, ENIC_MAX_WQ_DESCS,
		max_t(u32, ENIC_MIN_WQ_DESCS,
		c->wq_desc_count));
	c->wq_desc_count &= 0xffffffe0; /* must be aligned to groups of 32 */

	c->rq_desc_count =
		min_t(u32, ENIC_MAX_RQ_DESCS,
		max_t(u32, ENIC_MIN_RQ_DESCS,
		c->rq_desc_count));
	c->rq_desc_count &= 0xffffffe0; /* must be aligned to groups of 32 */

	if (c->mtu == 0)
		c->mtu = 1500;
	c->mtu = min_t(u16, ENIC_MAX_MTU,
		max_t(u16, ENIC_MIN_MTU,
		c->mtu));

	c->intr_timer_usec = min_t(u32, c->intr_timer_usec,
		vnic_dev_get_intr_coal_timer_max(enic->vdev));

	dev_info(enic_get_dev(enic),
		"vNIC MAC addr %pM wq/rq %d/%d mtu %d\n",
		enic->mac_addr, c->wq_desc_count, c->rq_desc_count, c->mtu);

	dev_info(enic_get_dev(enic), "vNIC csum tx/rx %s/%s "
		"tso/lro %s/%s rss %s intr mode %s type %s timer %d usec "
		"loopback tag 0x%04x\n",
		ENIC_SETTING(enic, TXCSUM) ? "yes" : "no",
		ENIC_SETTING(enic, RXCSUM) ? "yes" : "no",
		ENIC_SETTING(enic, TSO) ? "yes" : "no",
		ENIC_SETTING(enic, LRO) ? "yes" : "no",
		ENIC_SETTING(enic, RSS) ? "yes" : "no",
		c->intr_mode == VENET_INTR_MODE_INTX ? "INTx" :
		c->intr_mode == VENET_INTR_MODE_MSI ? "MSI" :
		c->intr_mode == VENET_INTR_MODE_ANY ? "any" :
		"unknown",
		c->intr_timer_type == VENET_INTR_TYPE_MIN ? "min" :
		c->intr_timer_type == VENET_INTR_TYPE_IDLE ? "idle" :
		"unknown",
		c->intr_timer_usec,
		c->loop_tag);

	return 0;
}

int enic_add_vlan(struct enic *enic, u16 vlanid)
{
	u64 a0 = vlanid, a1 = 0;
	int wait = 1000;
	int err;

	err = vnic_dev_cmd(enic->vdev, CMD_VLAN_ADD, &a0, &a1, wait);
	if (err)
		dev_err(enic_get_dev(enic), "Can't add vlan id, %d\n", err);

	return err;
}

int enic_del_vlan(struct enic *enic, u16 vlanid)
{
	u64 a0 = vlanid, a1 = 0;
	int wait = 1000;
	int err;

	err = vnic_dev_cmd(enic->vdev, CMD_VLAN_DEL, &a0, &a1, wait);
	if (err)
		dev_err(enic_get_dev(enic), "Can't delete vlan id, %d\n", err);

	return err;
}

int enic_set_nic_cfg(struct enic *enic, u8 rss_default_cpu, u8 rss_hash_type,
	u8 rss_hash_bits, u8 rss_base_cpu, u8 rss_enable, u8 tso_ipid_split_en,
	u8 ig_vlan_strip_en)
{
	enum vnic_devcmd_cmd cmd = CMD_NIC_CFG;
	u64 a0, a1;
	u32 nic_cfg;
	int wait = 1000;

	vnic_set_nic_cfg(&nic_cfg, rss_default_cpu,
		rss_hash_type, rss_hash_bits, rss_base_cpu,
		rss_enable, tso_ipid_split_en, ig_vlan_strip_en);

	a0 = nic_cfg;
	a1 = 0;

	if (rss_hash_type & (NIC_CFG_RSS_HASH_TYPE_UDP_IPV4 |
			     NIC_CFG_RSS_HASH_TYPE_UDP_IPV6))
		cmd = CMD_NIC_CFG_CHK;

	return vnic_dev_cmd(enic->vdev, cmd, &a0, &a1, wait);
}

int enic_set_rss_key(struct enic *enic, dma_addr_t key_pa, u64 len)
{
	u64 a0 = (u64)key_pa, a1 = len;
	int wait = 1000;

	return vnic_dev_cmd(enic->vdev, CMD_RSS_KEY, &a0, &a1, wait);
}

int enic_set_rss_cpu(struct enic *enic, dma_addr_t cpu_pa, u64 len)
{
	u64 a0 = (u64)cpu_pa, a1 = len;
	int wait = 1000;

	return vnic_dev_cmd(enic->vdev, CMD_RSS_CPU, &a0, &a1, wait);
}

void enic_free_vnic_resources(struct enic *enic)
{
	unsigned int i;

	for (i = 0; i < enic->wq_count; i++)
		vnic_wq_free(&enic->wq[i].vwq);
	for (i = 0; i < enic->rq_count; i++)
		vnic_rq_free(&enic->rq[i].vrq);
	for (i = 0; i < enic->cq_count; i++)
		vnic_cq_free(&enic->cq[i]);
	for (i = 0; i < enic->intr_count; i++)
		vnic_intr_free(&enic->intr[i]);
}

void enic_get_res_counts(struct enic *enic)
{
	enic->wq_avail = vnic_dev_get_res_count(enic->vdev, RES_TYPE_WQ);
	enic->rq_avail = vnic_dev_get_res_count(enic->vdev, RES_TYPE_RQ);
	enic->cq_avail = vnic_dev_get_res_count(enic->vdev, RES_TYPE_CQ);
	enic->intr_avail = vnic_dev_get_res_count(enic->vdev,
						  RES_TYPE_INTR_CTRL);

	enic->wq_count = enic->wq_avail;
	enic->rq_count = enic->rq_avail;
	enic->cq_count = enic->cq_avail;
	enic->intr_count = enic->intr_avail;

	dev_info(enic_get_dev(enic),
		"vNIC resources avail: wq %d rq %d cq %d intr %d\n",
		enic->wq_avail, enic->rq_avail,
		enic->cq_avail, enic->intr_avail);
}

void enic_init_vnic_resources(struct enic *enic)
{
	enum vnic_dev_intr_mode intr_mode;
	unsigned int mask_on_assertion;
	unsigned int interrupt_offset;
	unsigned int error_interrupt_enable;
	unsigned int error_interrupt_offset;
	unsigned int cq_index;
	unsigned int i;

	intr_mode = vnic_dev_get_intr_mode(enic->vdev);

	/* Init RQ/WQ resources.
	 *
	 * RQ[0 - n-1] point to CQ[0 - n-1]
	 * WQ[0 - m-1] point to CQ[n - n+m-1]
	 *
	 * Error interrupt is not enabled for MSI.
	 */

	switch (intr_mode) {
	case VNIC_DEV_INTR_MODE_INTX:
		error_interrupt_enable = 1;
		error_interrupt_offset = ENIC_LEGACY_ERR_INTR;
		break;
	case VNIC_DEV_INTR_MODE_MSIX:
		error_interrupt_enable = 1;
		error_interrupt_offset = enic_msix_err_intr(enic);
		break;
	default:
		error_interrupt_enable = 0;
		error_interrupt_offset = 0;
		break;
	}

	for (i = 0; i < enic->rq_count; i++) {
		cq_index = i;
		vnic_rq_init(&enic->rq[i].vrq,
			cq_index,
			error_interrupt_enable,
			error_interrupt_offset);
	}

	for (i = 0; i < enic->wq_count; i++) {
		cq_index = enic->rq_count + i;
		vnic_wq_init(&enic->wq[i].vwq,
			cq_index,
			error_interrupt_enable,
			error_interrupt_offset);
	}

	/* Init CQ resources
	 *
	 * All CQs point to INTR[0] for INTx, MSI
	 * CQ[i] point to INTR[ENIC_MSIX_IO_INTR_BASE + i] for MSI-X
	 */

	for (i = 0; i < enic->cq_count; i++) {

		switch (intr_mode) {
		case VNIC_DEV_INTR_MODE_MSIX:
			interrupt_offset = ENIC_MSIX_IO_INTR_BASE + i;
			break;
		default:
			interrupt_offset = 0;
			break;
		}

		vnic_cq_init(&enic->cq[i],
			0 /* flow_control_enable */,
			1 /* color_enable */,
			0 /* cq_head */,
			0 /* cq_tail */,
			1 /* cq_tail_color */,
			1 /* interrupt_enable */,
			1 /* cq_entry_enable */,
			0 /* cq_message_enable */,
			interrupt_offset,
			0 /* cq_message_addr */);
	}

	/* Init INTR resources
	 *
	 * mask_on_assertion is not used for INTx due to the level-
	 * triggered nature of INTx
	 */

	switch (intr_mode) {
	case VNIC_DEV_INTR_MODE_MSI:
	case VNIC_DEV_INTR_MODE_MSIX:
		mask_on_assertion = 1;
		break;
	default:
		mask_on_assertion = 0;
		break;
	}

	for (i = 0; i < enic->intr_count; i++) {
		vnic_intr_init(&enic->intr[i],
			enic->config.intr_timer_usec,
			enic->config.intr_timer_type,
			mask_on_assertion);
	}
}

int enic_alloc_vnic_resources(struct enic *enic)
{
	enum vnic_dev_intr_mode intr_mode;
	unsigned int i;
	int err;

	intr_mode = vnic_dev_get_intr_mode(enic->vdev);

	dev_info(enic_get_dev(enic), "vNIC resources used:  "
		"wq %d rq %d cq %d intr %d intr mode %s\n",
		enic->wq_count, enic->rq_count,
		enic->cq_count, enic->intr_count,
		intr_mode == VNIC_DEV_INTR_MODE_INTX ? "legacy PCI INTx" :
		intr_mode == VNIC_DEV_INTR_MODE_MSI ? "MSI" :
		intr_mode == VNIC_DEV_INTR_MODE_MSIX ? "MSI-X" :
		"unknown");

	/* Allocate queue resources
	 */

	for (i = 0; i < enic->wq_count; i++) {
		err = vnic_wq_alloc(enic->vdev, &enic->wq[i].vwq, i,
			enic->config.wq_desc_count,
			sizeof(struct wq_enet_desc));
		if (err)
			goto err_out_cleanup;
	}

	for (i = 0; i < enic->rq_count; i++) {
		err = vnic_rq_alloc(enic->vdev, &enic->rq[i].vrq, i,
			enic->config.rq_desc_count,
			sizeof(struct rq_enet_desc));
		if (err)
			goto err_out_cleanup;
	}

	for (i = 0; i < enic->cq_count; i++) {
		if (i < enic->rq_count)
			err = vnic_cq_alloc(enic->vdev, &enic->cq[i], i,
				enic->config.rq_desc_count,
				sizeof(struct cq_enet_rq_desc));
		else
			err = vnic_cq_alloc(enic->vdev, &enic->cq[i], i,
				enic->config.wq_desc_count,
				sizeof(struct cq_enet_wq_desc));
		if (err)
			goto err_out_cleanup;
	}

	for (i = 0; i < enic->intr_count; i++) {
		err = vnic_intr_alloc(enic->vdev, &enic->intr[i], i);
		if (err)
			goto err_out_cleanup;
	}

	/* Hook remaining resource
	 */

	enic->legacy_pba = vnic_dev_get_res(enic->vdev,
		RES_TYPE_INTR_PBA_LEGACY, 0);
	if (!enic->legacy_pba && intr_mode == VNIC_DEV_INTR_MODE_INTX) {
		dev_err(enic_get_dev(enic),
			"Failed to hook legacy pba resource\n");
		err = -ENODEV;
		goto err_out_cleanup;
	}

	return 0;

err_out_cleanup:
	enic_free_vnic_resources(enic);

	return err;
}
