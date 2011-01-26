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

#ifndef _WQ_ENET_DESC_H_
#define _WQ_ENET_DESC_H_

/* Ethernet work queue descriptor: 16B */
struct wq_enet_desc {
	__le64 address;
	__le16 length;
	__le16 mss_loopback;
	__le16 header_length_flags;
	__le16 vlan_tag;
};

#define WQ_ENET_ADDR_BITS		64
#define WQ_ENET_LEN_BITS		14
#define WQ_ENET_LEN_MASK		((1 << WQ_ENET_LEN_BITS) - 1)
#define WQ_ENET_MSS_BITS		14
#define WQ_ENET_MSS_MASK		((1 << WQ_ENET_MSS_BITS) - 1)
#define WQ_ENET_MSS_SHIFT		2
#define WQ_ENET_LOOPBACK_SHIFT		1
#define WQ_ENET_HDRLEN_BITS		10
#define WQ_ENET_HDRLEN_MASK		((1 << WQ_ENET_HDRLEN_BITS) - 1)
#define WQ_ENET_FLAGS_OM_BITS		2
#define WQ_ENET_FLAGS_OM_MASK		((1 << WQ_ENET_FLAGS_OM_BITS) - 1)
#define WQ_ENET_FLAGS_EOP_SHIFT		12
#define WQ_ENET_FLAGS_CQ_ENTRY_SHIFT	13
#define WQ_ENET_FLAGS_FCOE_ENCAP_SHIFT	14
#define WQ_ENET_FLAGS_VLAN_TAG_INSERT_SHIFT	15

#define WQ_ENET_OFFLOAD_MODE_CSUM	0
#define WQ_ENET_OFFLOAD_MODE_RESERVED	1
#define WQ_ENET_OFFLOAD_MODE_CSUM_L4	2
#define WQ_ENET_OFFLOAD_MODE_TSO	3

static inline void wq_enet_desc_enc(struct wq_enet_desc *desc,
	u64 address, u16 length, u16 mss, u16 header_length,
	u8 offload_mode, u8 eop, u8 cq_entry, u8 fcoe_encap,
	u8 vlan_tag_insert, u16 vlan_tag, u8 loopback)
{
	desc->address = cpu_to_le64(address);
	desc->length = cpu_to_le16(length & WQ_ENET_LEN_MASK);
	desc->mss_loopback = cpu_to_le16((mss & WQ_ENET_MSS_MASK) <<
		WQ_ENET_MSS_SHIFT | (loopback & 1) << WQ_ENET_LOOPBACK_SHIFT);
	desc->header_length_flags = cpu_to_le16(
		(header_length & WQ_ENET_HDRLEN_MASK) |
		(offload_mode & WQ_ENET_FLAGS_OM_MASK) << WQ_ENET_HDRLEN_BITS |
		(eop & 1) << WQ_ENET_FLAGS_EOP_SHIFT |
		(cq_entry & 1) << WQ_ENET_FLAGS_CQ_ENTRY_SHIFT |
		(fcoe_encap & 1) << WQ_ENET_FLAGS_FCOE_ENCAP_SHIFT |
		(vlan_tag_insert & 1) << WQ_ENET_FLAGS_VLAN_TAG_INSERT_SHIFT);
	desc->vlan_tag = cpu_to_le16(vlan_tag);
}

static inline void wq_enet_desc_dec(struct wq_enet_desc *desc,
	u64 *address, u16 *length, u16 *mss, u16 *header_length,
	u8 *offload_mode, u8 *eop, u8 *cq_entry, u8 *fcoe_encap,
	u8 *vlan_tag_insert, u16 *vlan_tag, u8 *loopback)
{
	*address = le64_to_cpu(desc->address);
	*length = le16_to_cpu(desc->length) & WQ_ENET_LEN_MASK;
	*mss = (le16_to_cpu(desc->mss_loopback) >> WQ_ENET_MSS_SHIFT) &
		WQ_ENET_MSS_MASK;
	*loopback = (u8)((le16_to_cpu(desc->mss_loopback) >>
		WQ_ENET_LOOPBACK_SHIFT) & 1);
	*header_length = le16_to_cpu(desc->header_length_flags) &
		WQ_ENET_HDRLEN_MASK;
	*offload_mode = (u8)((le16_to_cpu(desc->header_length_flags) >>
		WQ_ENET_HDRLEN_BITS) & WQ_ENET_FLAGS_OM_MASK);
	*eop = (u8)((le16_to_cpu(desc->header_length_flags) >>
		WQ_ENET_FLAGS_EOP_SHIFT) & 1);
	*cq_entry = (u8)((le16_to_cpu(desc->header_length_flags) >>
		WQ_ENET_FLAGS_CQ_ENTRY_SHIFT) & 1);
	*fcoe_encap = (u8)((le16_to_cpu(desc->header_length_flags) >>
		WQ_ENET_FLAGS_FCOE_ENCAP_SHIFT) & 1);
	*vlan_tag_insert = (u8)((le16_to_cpu(desc->header_length_flags) >>
		WQ_ENET_FLAGS_VLAN_TAG_INSERT_SHIFT) & 1);
	*vlan_tag = le16_to_cpu(desc->vlan_tag);
}

#endif /* _WQ_ENET_DESC_H_ */
