/*
 * Copyright 2008-2010 Cisco Systems, Inc.  All rights reserved.
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

#ifndef _ENIC_RES_H_
#define _ENIC_RES_H_

#include "wq_enet_desc.h"
#include "rq_enet_desc.h"
#include "vnic_wq.h"
#include "vnic_rq.h"

#define ENIC_MIN_WQ_DESCS		64
#define ENIC_MAX_WQ_DESCS		4096
#define ENIC_MIN_RQ_DESCS		64
#define ENIC_MAX_RQ_DESCS		4096

#define ENIC_MIN_MTU			68
#define ENIC_MAX_MTU			9000

#define ENIC_MULTICAST_PERFECT_FILTERS	32

#define ENIC_NON_TSO_MAX_DESC		16

#define ENIC_SETTING(enic, f) ((enic->config.flags & VENETF_##f) ? 1 : 0)

static inline void enic_queue_wq_desc_ex(struct vnic_wq *wq,
	void *os_buf, dma_addr_t dma_addr, unsigned int len,
	unsigned int mss_or_csum_offset, unsigned int hdr_len,
	int vlan_tag_insert, unsigned int vlan_tag,
	int offload_mode, int cq_entry, int sop, int eop, int loopback)
{
	struct wq_enet_desc *desc = vnic_wq_next_desc(wq);

	wq_enet_desc_enc(desc,
		(u64)dma_addr | VNIC_PADDR_TARGET,
		(u16)len,
		(u16)mss_or_csum_offset,
		(u16)hdr_len, (u8)offload_mode,
		(u8)eop, (u8)cq_entry,
		0, /* fcoe_encap */
		(u8)vlan_tag_insert,
		(u16)vlan_tag,
		(u8)loopback);

	vnic_wq_post(wq, os_buf, dma_addr, len, sop, eop);
}

static inline void enic_queue_wq_desc_cont(struct vnic_wq *wq,
	void *os_buf, dma_addr_t dma_addr, unsigned int len,
	int eop, int loopback)
{
	enic_queue_wq_desc_ex(wq, os_buf, dma_addr, len,
		0, 0, 0, 0, 0,
		eop, 0 /* !SOP */, eop, loopback);
}

static inline void enic_queue_wq_desc(struct vnic_wq *wq, void *os_buf,
	dma_addr_t dma_addr, unsigned int len, int vlan_tag_insert,
	unsigned int vlan_tag, int eop, int loopback)
{
	enic_queue_wq_desc_ex(wq, os_buf, dma_addr, len,
		0, 0, vlan_tag_insert, vlan_tag,
		WQ_ENET_OFFLOAD_MODE_CSUM,
		eop, 1 /* SOP */, eop, loopback);
}

static inline void enic_queue_wq_desc_csum(struct vnic_wq *wq,
	void *os_buf, dma_addr_t dma_addr, unsigned int len,
	int ip_csum, int tcpudp_csum, int vlan_tag_insert,
	unsigned int vlan_tag, int eop, int loopback)
{
	enic_queue_wq_desc_ex(wq, os_buf, dma_addr, len,
		(ip_csum ? 1 : 0) + (tcpudp_csum ? 2 : 0),
		0, vlan_tag_insert, vlan_tag,
		WQ_ENET_OFFLOAD_MODE_CSUM,
		eop, 1 /* SOP */, eop, loopback);
}

static inline void enic_queue_wq_desc_csum_l4(struct vnic_wq *wq,
	void *os_buf, dma_addr_t dma_addr, unsigned int len,
	unsigned int csum_offset, unsigned int hdr_len,
	int vlan_tag_insert, unsigned int vlan_tag, int eop, int loopback)
{
	enic_queue_wq_desc_ex(wq, os_buf, dma_addr, len,
		csum_offset, hdr_len, vlan_tag_insert, vlan_tag,
		WQ_ENET_OFFLOAD_MODE_CSUM_L4,
		eop, 1 /* SOP */, eop, loopback);
}

static inline void enic_queue_wq_desc_tso(struct vnic_wq *wq,
	void *os_buf, dma_addr_t dma_addr, unsigned int len,
	unsigned int mss, unsigned int hdr_len, int vlan_tag_insert,
	unsigned int vlan_tag, int eop, int loopback)
{
	enic_queue_wq_desc_ex(wq, os_buf, dma_addr, len,
		mss, hdr_len, vlan_tag_insert, vlan_tag,
		WQ_ENET_OFFLOAD_MODE_TSO,
		eop, 1 /* SOP */, eop, loopback);
}

static inline void enic_queue_rq_desc(struct vnic_rq *rq,
	void *os_buf, unsigned int os_buf_index,
	dma_addr_t dma_addr, unsigned int len)
{
	struct rq_enet_desc *desc = vnic_rq_next_desc(rq);
	u8 type = os_buf_index ?
		RQ_ENET_TYPE_NOT_SOP : RQ_ENET_TYPE_ONLY_SOP;

	rq_enet_desc_enc(desc,
		(u64)dma_addr | VNIC_PADDR_TARGET,
		type, (u16)len);

	vnic_rq_post(rq, os_buf, os_buf_index, dma_addr, len);
}

struct enic;

int enic_get_vnic_config(struct enic *);
int enic_add_vlan(struct enic *enic, u16 vlanid);
int enic_del_vlan(struct enic *enic, u16 vlanid);
int enic_set_nic_cfg(struct enic *enic, u8 rss_default_cpu, u8 rss_hash_type,
	u8 rss_hash_bits, u8 rss_base_cpu, u8 rss_enable, u8 tso_ipid_split_en,
	u8 ig_vlan_strip_en);
int enic_set_rss_key(struct enic *enic, dma_addr_t key_pa, u64 len);
int enic_set_rss_cpu(struct enic *enic, dma_addr_t cpu_pa, u64 len);
void enic_get_res_counts(struct enic *enic);
void enic_init_vnic_resources(struct enic *enic);
int enic_alloc_vnic_resources(struct enic *);
void enic_free_vnic_resources(struct enic *);

#endif /* _ENIC_RES_H_ */
