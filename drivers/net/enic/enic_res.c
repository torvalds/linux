/*
 * Copyright 2008 Cisco Systems, Inc.  All rights reserved.
 * Copyright 2007 Nuova Systems, Inc.  All rights reserved.
 *
 * This program is free software; you may redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
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

	err = vnic_dev_mac_addr(enic->vdev, enic->mac_addr);
	if (err) {
		printk(KERN_ERR PFX "Error getting MAC addr, %d\n", err);
		return err;
	}

#define GET_CONFIG(m) \
	do { \
		err = vnic_dev_spec(enic->vdev, \
			offsetof(struct vnic_enet_config, m), \
			sizeof(c->m), &c->m); \
		if (err) { \
			printk(KERN_ERR PFX \
				"Error getting %s, %d\n", #m, err); \
			return err; \
		} \
	} while (0)

	GET_CONFIG(flags);
	GET_CONFIG(wq_desc_count);
	GET_CONFIG(rq_desc_count);
	GET_CONFIG(mtu);
	GET_CONFIG(intr_timer);
	GET_CONFIG(intr_timer_type);
	GET_CONFIG(intr_mode);

	c->wq_desc_count =
		min_t(u32, ENIC_MAX_WQ_DESCS,
		max_t(u32, ENIC_MIN_WQ_DESCS,
		c->wq_desc_count));
	c->wq_desc_count &= 0xfffffff0; /* must be aligned to groups of 16 */

	c->rq_desc_count =
		min_t(u32, ENIC_MAX_RQ_DESCS,
		max_t(u32, ENIC_MIN_RQ_DESCS,
		c->rq_desc_count));
	c->rq_desc_count &= 0xfffffff0; /* must be aligned to groups of 16 */

	if (c->mtu == 0)
		c->mtu = 1500;
	c->mtu = min_t(u16, ENIC_MAX_MTU,
		max_t(u16, ENIC_MIN_MTU,
		c->mtu));

	c->intr_timer = min_t(u16, VNIC_INTR_TIMER_MAX, c->intr_timer);

	printk(KERN_INFO PFX "vNIC MAC addr %pM wq/rq %d/%d\n",
		enic->mac_addr, c->wq_desc_count, c->rq_desc_count);
	printk(KERN_INFO PFX "vNIC mtu %d csum tx/rx %d/%d tso/lro %d/%d "
		"intr timer %d\n",
		c->mtu, ENIC_SETTING(enic, TXCSUM),
		ENIC_SETTING(enic, RXCSUM), ENIC_SETTING(enic, TSO),
		ENIC_SETTING(enic, LRO), c->intr_timer);

	return 0;
}

void enic_add_station_addr(struct enic *enic)
{
	vnic_dev_add_addr(enic->vdev, enic->mac_addr);
}

void enic_add_multicast_addr(struct enic *enic, u8 *addr)
{
	vnic_dev_add_addr(enic->vdev, addr);
}

void enic_del_multicast_addr(struct enic *enic, u8 *addr)
{
	vnic_dev_del_addr(enic->vdev, addr);
}

void enic_add_vlan(struct enic *enic, u16 vlanid)
{
	u64 a0 = vlanid, a1 = 0;
	int wait = 1000;
	int err;

	err = vnic_dev_cmd(enic->vdev, CMD_VLAN_ADD, &a0, &a1, wait);
	if (err)
		printk(KERN_ERR PFX "Can't add vlan id, %d\n", err);
}

void enic_del_vlan(struct enic *enic, u16 vlanid)
{
	u64 a0 = vlanid, a1 = 0;
	int wait = 1000;
	int err;

	err = vnic_dev_cmd(enic->vdev, CMD_VLAN_DEL, &a0, &a1, wait);
	if (err)
		printk(KERN_ERR PFX "Can't delete vlan id, %d\n", err);
}

int enic_set_nic_cfg(struct enic *enic, u8 rss_default_cpu, u8 rss_hash_type,
	u8 rss_hash_bits, u8 rss_base_cpu, u8 rss_enable, u8 tso_ipid_split_en,
	u8 ig_vlan_strip_en)
{
	u64 a0, a1;
	u32 nic_cfg;
	int wait = 1000;

	vnic_set_nic_cfg(&nic_cfg, rss_default_cpu,
		rss_hash_type, rss_hash_bits, rss_base_cpu,
		rss_enable, tso_ipid_split_en, ig_vlan_strip_en);

	a0 = nic_cfg;
	a1 = 0;

	return vnic_dev_cmd(enic->vdev, CMD_NIC_CFG, &a0, &a1, wait);
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
		vnic_wq_free(&enic->wq[i]);
	for (i = 0; i < enic->rq_count; i++)
		vnic_rq_free(&enic->rq[i]);
	for (i = 0; i < enic->cq_count; i++)
		vnic_cq_free(&enic->cq[i]);
	for (i = 0; i < enic->intr_count; i++)
		vnic_intr_free(&enic->intr[i]);
}

void enic_get_res_counts(struct enic *enic)
{
	enic->wq_count = min_t(int,
		vnic_dev_get_res_count(enic->vdev, RES_TYPE_WQ),
		ENIC_WQ_MAX);
	enic->rq_count = min_t(int,
		vnic_dev_get_res_count(enic->vdev, RES_TYPE_RQ),
		ENIC_RQ_MAX);
	enic->cq_count = min_t(int,
		vnic_dev_get_res_count(enic->vdev, RES_TYPE_CQ),
		ENIC_CQ_MAX);
	enic->intr_count = min_t(int,
		vnic_dev_get_res_count(enic->vdev, RES_TYPE_INTR_CTRL),
		ENIC_INTR_MAX);

	printk(KERN_INFO PFX "vNIC resources avail: "
		"wq %d rq %d cq %d intr %d\n",
		enic->wq_count, enic->rq_count,
		enic->cq_count, enic->intr_count);
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
	case VNIC_DEV_INTR_MODE_MSIX:
		error_interrupt_enable = 1;
		error_interrupt_offset = enic->intr_count - 2;
		break;
	default:
		error_interrupt_enable = 0;
		error_interrupt_offset = 0;
		break;
	}

	for (i = 0; i < enic->rq_count; i++) {
		cq_index = i;
		vnic_rq_init(&enic->rq[i],
			cq_index,
			error_interrupt_enable,
			error_interrupt_offset);
	}

	for (i = 0; i < enic->wq_count; i++) {
		cq_index = enic->rq_count + i;
		vnic_wq_init(&enic->wq[i],
			cq_index,
			error_interrupt_enable,
			error_interrupt_offset);
	}

	/* Init CQ resources
	 *
	 * CQ[0 - n+m-1] point to INTR[0] for INTx, MSI
	 * CQ[0 - n+m-1] point to INTR[0 - n+m-1] for MSI-X
	 */

	for (i = 0; i < enic->cq_count; i++) {

		switch (intr_mode) {
		case VNIC_DEV_INTR_MODE_MSIX:
			interrupt_offset = i;
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
			enic->config.intr_timer,
			enic->config.intr_timer_type,
			mask_on_assertion);
	}

	/* Clear LIF stats
	 */

	vnic_dev_stats_clear(enic->vdev);
}

int enic_alloc_vnic_resources(struct enic *enic)
{
	enum vnic_dev_intr_mode intr_mode;
	unsigned int i;
	int err;

	intr_mode = vnic_dev_get_intr_mode(enic->vdev);

	printk(KERN_INFO PFX "vNIC resources used:  "
		"wq %d rq %d cq %d intr %d intr mode %s\n",
		enic->wq_count, enic->rq_count,
		enic->cq_count, enic->intr_count,
		intr_mode == VNIC_DEV_INTR_MODE_INTX ? "legacy PCI INTx" :
		intr_mode == VNIC_DEV_INTR_MODE_MSI ? "MSI" :
		intr_mode == VNIC_DEV_INTR_MODE_MSIX ? "MSI-X" :
		"unknown"
		);

	/* Allocate queue resources
	 */

	for (i = 0; i < enic->wq_count; i++) {
		err = vnic_wq_alloc(enic->vdev, &enic->wq[i], i,
			enic->config.wq_desc_count,
			sizeof(struct wq_enet_desc));
		if (err)
			goto err_out_cleanup;
	}

	for (i = 0; i < enic->rq_count; i++) {
		err = vnic_rq_alloc(enic->vdev, &enic->rq[i], i,
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
		printk(KERN_ERR PFX "Failed to hook legacy pba resource\n");
		err = -ENODEV;
		goto err_out_cleanup;
	}

	return 0;

err_out_cleanup:
	enic_free_vnic_resources(enic);

	return err;
}
