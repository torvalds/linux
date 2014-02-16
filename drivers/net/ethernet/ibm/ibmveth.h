/*
 * IBM Power Virtual Ethernet Device Driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * Copyright (C) IBM Corporation, 2003, 2010
 *
 * Authors: Dave Larson <larson1@us.ibm.com>
 *	    Santiago Leon <santil@linux.vnet.ibm.com>
 *	    Brian King <brking@linux.vnet.ibm.com>
 *	    Robert Jennings <rcj@linux.vnet.ibm.com>
 *	    Anton Blanchard <anton@au.ibm.com>
 */

#ifndef _IBMVETH_H
#define _IBMVETH_H

/* constants for H_MULTICAST_CTRL */
#define IbmVethMcastReceptionModifyBit     0x80000UL
#define IbmVethMcastReceptionEnableBit     0x20000UL
#define IbmVethMcastFilterModifyBit        0x40000UL
#define IbmVethMcastFilterEnableBit        0x10000UL

#define IbmVethMcastEnableRecv       (IbmVethMcastReceptionModifyBit | IbmVethMcastReceptionEnableBit)
#define IbmVethMcastDisableRecv      (IbmVethMcastReceptionModifyBit)
#define IbmVethMcastEnableFiltering  (IbmVethMcastFilterModifyBit | IbmVethMcastFilterEnableBit)
#define IbmVethMcastDisableFiltering (IbmVethMcastFilterModifyBit)
#define IbmVethMcastAddFilter        0x1UL
#define IbmVethMcastRemoveFilter     0x2UL
#define IbmVethMcastClearFilterTable 0x3UL

#define IBMVETH_ILLAN_PADDED_PKT_CSUM	0x0000000000002000UL
#define IBMVETH_ILLAN_TRUNK_PRI_MASK	0x0000000000000F00UL
#define IBMVETH_ILLAN_IPV6_TCP_CSUM		0x0000000000000004UL
#define IBMVETH_ILLAN_IPV4_TCP_CSUM		0x0000000000000002UL
#define IBMVETH_ILLAN_ACTIVE_TRUNK		0x0000000000000001UL

/* hcall macros */
#define h_register_logical_lan(ua, buflst, rxq, fltlst, mac) \
  plpar_hcall_norets(H_REGISTER_LOGICAL_LAN, ua, buflst, rxq, fltlst, mac)

#define h_free_logical_lan(ua) \
  plpar_hcall_norets(H_FREE_LOGICAL_LAN, ua)

#define h_add_logical_lan_buffer(ua, buf) \
  plpar_hcall_norets(H_ADD_LOGICAL_LAN_BUFFER, ua, buf)

static inline long h_send_logical_lan(unsigned long unit_address,
		unsigned long desc1, unsigned long desc2, unsigned long desc3,
		unsigned long desc4, unsigned long desc5, unsigned long desc6,
		unsigned long corellator_in, unsigned long *corellator_out)
{
	long rc;
	unsigned long retbuf[PLPAR_HCALL9_BUFSIZE];

	rc = plpar_hcall9(H_SEND_LOGICAL_LAN, retbuf, unit_address, desc1,
			desc2, desc3, desc4, desc5, desc6, corellator_in);

	*corellator_out = retbuf[0];

	return rc;
}

static inline long h_illan_attributes(unsigned long unit_address,
				      unsigned long reset_mask, unsigned long set_mask,
				      unsigned long *ret_attributes)
{
	long rc;
	unsigned long retbuf[PLPAR_HCALL_BUFSIZE];

	rc = plpar_hcall(H_ILLAN_ATTRIBUTES, retbuf, unit_address,
			 reset_mask, set_mask);

	*ret_attributes = retbuf[0];

	return rc;
}

#define h_multicast_ctrl(ua, cmd, mac) \
  plpar_hcall_norets(H_MULTICAST_CTRL, ua, cmd, mac)

#define h_change_logical_lan_mac(ua, mac) \
  plpar_hcall_norets(H_CHANGE_LOGICAL_LAN_MAC, ua, mac)

#define IBMVETH_NUM_BUFF_POOLS 5
#define IBMVETH_IO_ENTITLEMENT_DEFAULT 4243456 /* MTU of 1500 needs 4.2Mb */
#define IBMVETH_BUFF_OH 22 /* Overhead: 14 ethernet header + 8 opaque handle */
#define IBMVETH_MIN_MTU 68
#define IBMVETH_MAX_POOL_COUNT 4096
#define IBMVETH_BUFF_LIST_SIZE 4096
#define IBMVETH_FILT_LIST_SIZE 4096
#define IBMVETH_MAX_BUF_SIZE (1024 * 128)

static int pool_size[] = { 512, 1024 * 2, 1024 * 16, 1024 * 32, 1024 * 64 };
static int pool_count[] = { 256, 512, 256, 256, 256 };
static int pool_active[] = { 1, 1, 0, 0, 0};

#define IBM_VETH_INVALID_MAP ((u16)0xffff)

struct ibmveth_buff_pool {
    u32 size;
    u32 index;
    u32 buff_size;
    u32 threshold;
    atomic_t available;
    u32 consumer_index;
    u32 producer_index;
    u16 *free_map;
    dma_addr_t *dma_addr;
    struct sk_buff **skbuff;
    int active;
    struct kobject kobj;
};

struct ibmveth_rx_q {
    u64        index;
    u64        num_slots;
    u64        toggle;
    dma_addr_t queue_dma;
    u32        queue_len;
    struct ibmveth_rx_q_entry *queue_addr;
};

struct ibmveth_adapter {
    struct vio_dev *vdev;
    struct net_device *netdev;
    struct napi_struct napi;
    struct net_device_stats stats;
    unsigned int mcastFilterSize;
    unsigned long mac_addr;
    void * buffer_list_addr;
    void * filter_list_addr;
    dma_addr_t buffer_list_dma;
    dma_addr_t filter_list_dma;
    struct ibmveth_buff_pool rx_buff_pool[IBMVETH_NUM_BUFF_POOLS];
    struct ibmveth_rx_q rx_queue;
    int pool_config;
    int rx_csum;
    void *bounce_buffer;
    dma_addr_t bounce_buffer_dma;

    u64 fw_ipv6_csum_support;
    u64 fw_ipv4_csum_support;
    /* adapter specific stats */
    u64 replenish_task_cycles;
    u64 replenish_no_mem;
    u64 replenish_add_buff_failure;
    u64 replenish_add_buff_success;
    u64 rx_invalid_buffer;
    u64 rx_no_buffer;
    u64 tx_map_failed;
    u64 tx_send_failed;
};

/*
 * We pass struct ibmveth_buf_desc_fields to the hypervisor in registers,
 * so we don't need to byteswap the two elements. However since we use
 * a union (ibmveth_buf_desc) to convert from the struct to a u64 we
 * do end up with endian specific ordering of the elements and that
 * needs correcting.
 */
struct ibmveth_buf_desc_fields {
#ifdef __BIG_ENDIAN
	u32 flags_len;
	u32 address;
#else
	u32 address;
	u32 flags_len;
#endif
#define IBMVETH_BUF_VALID	0x80000000
#define IBMVETH_BUF_TOGGLE	0x40000000
#define IBMVETH_BUF_NO_CSUM	0x02000000
#define IBMVETH_BUF_CSUM_GOOD	0x01000000
#define IBMVETH_BUF_LEN_MASK	0x00FFFFFF
};

union ibmveth_buf_desc {
    u64 desc;
    struct ibmveth_buf_desc_fields fields;
};

struct ibmveth_rx_q_entry {
	__be32 flags_off;
#define IBMVETH_RXQ_TOGGLE		0x80000000
#define IBMVETH_RXQ_TOGGLE_SHIFT	31
#define IBMVETH_RXQ_VALID		0x40000000
#define IBMVETH_RXQ_NO_CSUM		0x02000000
#define IBMVETH_RXQ_CSUM_GOOD		0x01000000
#define IBMVETH_RXQ_OFF_MASK		0x0000FFFF

	__be32 length;
	/* correlator is only used by the OS, no need to byte swap */
	u64 correlator;
};

#endif /* _IBMVETH_H */
