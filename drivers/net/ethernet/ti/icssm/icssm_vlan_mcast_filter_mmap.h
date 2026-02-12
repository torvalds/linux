/* SPDX-License-Identifier: GPL-2.0 */

/* Copyright (C) 2015-2021 Texas Instruments Incorporated - https://www.ti.com
 *
 * This file contains VLAN/Multicast filtering feature memory map
 *
 */

#ifndef ICSS_VLAN_MULTICAST_FILTER_MM_H
#define ICSS_VLAN_MULTICAST_FILTER_MM_H

/* VLAN/Multicast filter defines & offsets,
 * present on both PRU0 and PRU1 DRAM
 */

/* Feature enable/disable values for multicast filtering */
#define ICSS_EMAC_FW_MULTICAST_FILTER_CTRL_DISABLED		0x00
#define ICSS_EMAC_FW_MULTICAST_FILTER_CTRL_ENABLED		0x01

/* Feature enable/disable values for VLAN filtering */
#define ICSS_EMAC_FW_VLAN_FILTER_CTRL_DISABLED			0x00
#define ICSS_EMAC_FW_VLAN_FILTER_CTRL_ENABLED			0x01

/* Add/remove multicast mac id for filtering bin */
#define ICSS_EMAC_FW_MULTICAST_FILTER_HOST_RCV_ALLOWED		0x01
#define ICSS_EMAC_FW_MULTICAST_FILTER_HOST_RCV_NOT_ALLOWED	0x00

/* Default HASH value for the multicast filtering Mask */
#define ICSS_EMAC_FW_MULTICAST_FILTER_INIT_VAL			0xFF

/* Size requirements for Multicast filtering feature */
#define ICSS_EMAC_FW_MULTICAST_TABLE_SIZE_BYTES			       256
#define ICSS_EMAC_FW_MULTICAST_FILTER_MASK_SIZE_BYTES			 6
#define ICSS_EMAC_FW_MULTICAST_FILTER_CTRL_SIZE_BYTES			 1
#define ICSS_EMAC_FW_MULTICAST_FILTER_MASK_OVERRIDE_STATUS_SIZE_BYTES	 1
#define ICSS_EMAC_FW_MULTICAST_FILTER_DROP_CNT_SIZE_BYTES		 4

/* Size requirements for VLAN filtering feature : 4096 bits = 512 bytes */
#define ICSS_EMAC_FW_VLAN_FILTER_TABLE_SIZE_BYTES		       512
#define ICSS_EMAC_FW_VLAN_FILTER_CTRL_SIZE_BYTES			 1
#define ICSS_EMAC_FW_VLAN_FILTER_DROP_CNT_SIZE_BYTES			 4

/* Mask override set status */
#define ICSS_EMAC_FW_MULTICAST_FILTER_MASK_OVERRIDE_SET			 1
/* Mask override not set status */
#define ICSS_EMAC_FW_MULTICAST_FILTER_MASK_OVERRIDE_NOT_SET		 0
/* 6 bytes HASH Mask for the MAC */
#define ICSS_EMAC_FW_MULTICAST_FILTER_MASK_OFFSET	  0xF4
/* 0 -> multicast filtering disabled | 1 -> multicast filtering enabled */
#define ICSS_EMAC_FW_MULTICAST_FILTER_CTRL_OFFSET	\
	(ICSS_EMAC_FW_MULTICAST_FILTER_MASK_OFFSET +	\
	 ICSS_EMAC_FW_MULTICAST_FILTER_MASK_SIZE_BYTES)
/* Status indicating if the HASH override is done or not: 0: no, 1: yes */
#define ICSS_EMAC_FW_MULTICAST_FILTER_OVERRIDE_STATUS	\
	(ICSS_EMAC_FW_MULTICAST_FILTER_CTRL_OFFSET +	\
	 ICSS_EMAC_FW_MULTICAST_FILTER_CTRL_SIZE_BYTES)
/* Multicast drop statistics */
#define ICSS_EMAC_FW_MULTICAST_FILTER_DROP_CNT_OFFSET	\
	(ICSS_EMAC_FW_MULTICAST_FILTER_OVERRIDE_STATUS +\
	 ICSS_EMAC_FW_MULTICAST_FILTER_MASK_OVERRIDE_STATUS_SIZE_BYTES)
/* Multicast table */
#define ICSS_EMAC_FW_MULTICAST_FILTER_TABLE		\
	(ICSS_EMAC_FW_MULTICAST_FILTER_DROP_CNT_OFFSET +\
	 ICSS_EMAC_FW_MULTICAST_FILTER_DROP_CNT_SIZE_BYTES)

/* Multicast filter defines & offsets for LRE
 */
#define ICSS_LRE_FW_MULTICAST_TABLE_SEARCH_OP_CONTROL_BIT	0xE0
/* one byte field :
 * 0 -> multicast filtering disabled
 * 1 -> multicast filtering enabled
 */
#define ICSS_LRE_FW_MULTICAST_FILTER_MASK			 0xE4
#define ICSS_LRE_FW_MULTICAST_FILTER_TABLE			 0x100

/* VLAN table Offsets */
#define ICSS_EMAC_FW_VLAN_FLTR_TBL_BASE_ADDR		 0x200
#define ICSS_EMAC_FW_VLAN_FILTER_CTRL_BITMAP_OFFSET	 0xEF
#define ICSS_EMAC_FW_VLAN_FILTER_DROP_CNT_OFFSET	\
	(ICSS_EMAC_FW_VLAN_FILTER_CTRL_BITMAP_OFFSET +	\
	 ICSS_EMAC_FW_VLAN_FILTER_CTRL_SIZE_BYTES)

/* VLAN filter Control Bit maps */
/* one bit field, bit 0: | 0 : VLAN filter disabled (default),
 * 1: VLAN filter enabled
 */
#define ICSS_EMAC_FW_VLAN_FILTER_CTRL_ENABLE_BIT		       0
/* one bit field, bit 1: | 0 : untagged host rcv allowed (default),
 * 1: untagged host rcv not allowed
 */
#define ICSS_EMAC_FW_VLAN_FILTER_UNTAG_HOST_RCV_ALLOW_CTRL_BIT	       1
/* one bit field, bit 1: | 0 : priotag host rcv allowed (default),
 * 1: priotag host rcv not allowed
 */
#define ICSS_EMAC_FW_VLAN_FILTER_PRIOTAG_HOST_RCV_ALLOW_CTRL_BIT       2
/* one bit field, bit 1: | 0 : skip sv vlan flow
 * :1 : take sv vlan flow  (not applicable for dual emac )
 */
#define ICSS_EMAC_FW_VLAN_FILTER_SV_VLAN_FLOW_HOST_RCV_ALLOW_CTRL_BIT  3

/* VLAN IDs */
#define ICSS_EMAC_FW_VLAN_FILTER_PRIOTAG_VID			       0
#define ICSS_EMAC_FW_VLAN_FILTER_VID_MIN			       0x0000
#define ICSS_EMAC_FW_VLAN_FILTER_VID_MAX			       0x0FFF

/* VLAN Filtering Commands */
#define ICSS_EMAC_FW_VLAN_FILTER_ADD_VLAN_VID_CMD		       0x00
#define ICSS_EMAC_FW_VLAN_FILTER_REMOVE_VLAN_VID_CMD		       0x01

/* Switch defines for VLAN/MC filtering */
/* SRAM
 * VLAN filter defines & offsets
 */
#define ICSS_LRE_FW_VLAN_FLTR_CTRL_BYTE				 0x1FE
/* one bit field | 0 : VLAN filter disabled
 *		 | 1 : VLAN filter enabled
 */
#define ICSS_LRE_FW_VLAN_FLTR_TBL_BASE_ADDR			 0x200

#endif /* ICSS_MULTICAST_FILTER_MM_H */
