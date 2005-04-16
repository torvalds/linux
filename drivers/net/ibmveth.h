/**************************************************************************/
/*                                                                        */
/* IBM eServer i/[Series Virtual Ethernet Device Driver                   */
/* Copyright (C) 2003 IBM Corp.                                           */
/*  Dave Larson (larson1@us.ibm.com)                                      */
/*  Santiago Leon (santil@us.ibm.com)                                     */
/*                                                                        */
/*  This program is free software; you can redistribute it and/or modify  */
/*  it under the terms of the GNU General Public License as published by  */
/*  the Free Software Foundation; either version 2 of the License, or     */
/*  (at your option) any later version.                                   */
/*                                                                        */
/*  This program is distributed in the hope that it will be useful,       */
/*  but WITHOUT ANY WARRANTY; without even the implied warranty of        */
/*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         */
/*  GNU General Public License for more details.                          */
/*                                                                        */
/*  You should have received a copy of the GNU General Public License     */
/*  along with this program; if not, write to the Free Software           */
/*  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  */
/*                                                                   USA  */
/*                                                                        */
/**************************************************************************/

#ifndef _IBMVETH_H
#define _IBMVETH_H

#define IbmVethMaxSendFrags 6

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

/* hcall numbers */
#define H_VIO_SIGNAL             0x104
#define H_REGISTER_LOGICAL_LAN   0x114
#define H_FREE_LOGICAL_LAN       0x118
#define H_ADD_LOGICAL_LAN_BUFFER 0x11C
#define H_SEND_LOGICAL_LAN       0x120
#define H_MULTICAST_CTRL         0x130
#define H_CHANGE_LOGICAL_LAN_MAC 0x14C

/* hcall macros */
#define h_register_logical_lan(ua, buflst, rxq, fltlst, mac) \
  plpar_hcall_norets(H_REGISTER_LOGICAL_LAN, ua, buflst, rxq, fltlst, mac)

#define h_free_logical_lan(ua) \
  plpar_hcall_norets(H_FREE_LOGICAL_LAN, ua)

#define h_add_logical_lan_buffer(ua, buf) \
  plpar_hcall_norets(H_ADD_LOGICAL_LAN_BUFFER, ua, buf)

#define h_send_logical_lan(ua, buf1, buf2, buf3, buf4, buf5, buf6, correlator) \
  plpar_hcall_8arg_2ret(H_SEND_LOGICAL_LAN, ua, buf1, buf2, buf3, buf4, buf5, buf6, correlator, &correlator)

#define h_multicast_ctrl(ua, cmd, mac) \
  plpar_hcall_norets(H_MULTICAST_CTRL, ua, cmd, mac)

#define h_change_logical_lan_mac(ua, mac) \
  plpar_hcall_norets(H_CHANGE_LOGICAL_LAN_MAC, ua, mac)

#define IbmVethNumBufferPools 3
#define IbmVethPool0DftSize (1024 * 2)
#define IbmVethPool1DftSize (1024 * 4)
#define IbmVethPool2DftSize (1024 * 10)
#define IbmVethPool0DftCnt  256
#define IbmVethPool1DftCnt  256
#define IbmVethPool2DftCnt  256

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
    struct net_device_stats stats;
    unsigned int mcastFilterSize;
    unsigned long mac_addr;
    unsigned long liobn;
    void * buffer_list_addr;
    void * filter_list_addr;
    dma_addr_t buffer_list_dma;
    dma_addr_t filter_list_dma;
    struct ibmveth_buff_pool rx_buff_pool[IbmVethNumBufferPools];
    struct ibmveth_rx_q rx_queue;
    atomic_t not_replenishing;

    /* helper tasks */
    struct work_struct replenish_task;

    /* adapter specific stats */
    u64 replenish_task_cycles;
    u64 replenish_no_mem;
    u64 replenish_add_buff_failure;
    u64 replenish_add_buff_success;
    u64 rx_invalid_buffer;
    u64 rx_no_buffer;
    u64 tx_multidesc_send;
    u64 tx_linearized;
    u64 tx_linearize_failed;
    u64 tx_map_failed;
    u64 tx_send_failed;
};

struct ibmveth_buf_desc_fields {	
    u32 valid : 1;
    u32 toggle : 1;
    u32 reserved : 6;
    u32 length : 24;
    u32 address;
};

union ibmveth_buf_desc {
    u64 desc;	
    struct ibmveth_buf_desc_fields fields;
};

struct ibmveth_rx_q_entry {
    u16 toggle : 1;
    u16 valid : 1;
    u16 reserved : 14;
    u16 offset;
    u32 length;
    u64 correlator;
};

#endif /* _IBMVETH_H */
