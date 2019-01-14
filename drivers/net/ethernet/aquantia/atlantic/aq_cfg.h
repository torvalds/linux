/*
 * aQuantia Corporation Network Driver
 * Copyright (C) 2014-2017 aQuantia Corporation. All rights reserved
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 */

/* File aq_cfg.h: Definition of configuration parameters and constants. */

#ifndef AQ_CFG_H
#define AQ_CFG_H

#define AQ_CFG_VECS_DEF   4U
#define AQ_CFG_TCS_DEF    1U

#define AQ_CFG_TXDS_DEF    4096U
#define AQ_CFG_RXDS_DEF    1024U

#define AQ_CFG_IS_POLLING_DEF 0U

#define AQ_CFG_FORCE_LEGACY_INT 0U

#define AQ_CFG_INTERRUPT_MODERATION_OFF		0
#define AQ_CFG_INTERRUPT_MODERATION_ON		1
#define AQ_CFG_INTERRUPT_MODERATION_AUTO	0xFFFFU

#define AQ_CFG_INTERRUPT_MODERATION_USEC_MAX (0x1FF * 2)

#define AQ_CFG_IRQ_MASK                      0x1FFU

#define AQ_CFG_VECS_MAX   8U
#define AQ_CFG_TCS_MAX    8U

#define AQ_CFG_TX_FRAME_MAX  (16U * 1024U)
#define AQ_CFG_RX_FRAME_MAX  (4U * 1024U)

#define AQ_CFG_TX_CLEAN_BUDGET 256U

/* LRO */
#define AQ_CFG_IS_LRO_DEF           1U

/* RSS */
#define AQ_CFG_RSS_INDIRECTION_TABLE_MAX  128U
#define AQ_CFG_RSS_HASHKEY_SIZE           320U

#define AQ_CFG_IS_RSS_DEF           1U
#define AQ_CFG_NUM_RSS_QUEUES_DEF   AQ_CFG_VECS_DEF
#define AQ_CFG_RSS_BASE_CPU_NUM_DEF 0U

#define AQ_CFG_PCI_FUNC_MSIX_IRQS   9U
#define AQ_CFG_PCI_FUNC_PORTS       2U

#define AQ_CFG_SERVICE_TIMER_INTERVAL    (1 * HZ)
#define AQ_CFG_POLLING_TIMER_INTERVAL   ((unsigned int)(2 * HZ))

#define AQ_CFG_SKB_FRAGS_MAX   32U

/* Number of descriptors available in one ring to resume this ring queue
 */
#define AQ_CFG_RESTART_DESC_THRES   (AQ_CFG_SKB_FRAGS_MAX * 2)

#define AQ_CFG_NAPI_WEIGHT     64U

/*#define AQ_CFG_MAC_ADDR_PERMANENT {0x30, 0x0E, 0xE3, 0x12, 0x34, 0x56}*/

#define AQ_NIC_FC_OFF    0U
#define AQ_NIC_FC_TX     1U
#define AQ_NIC_FC_RX     2U
#define AQ_NIC_FC_FULL   3U
#define AQ_NIC_FC_AUTO   4U

#define AQ_CFG_FC_MODE AQ_NIC_FC_FULL

#define AQ_CFG_SPEED_MSK  0xFFFFU	/* 0xFFFFU==auto_neg */

#define AQ_CFG_IS_AUTONEG_DEF       1U
#define AQ_CFG_MTU_DEF              1514U

#define AQ_CFG_LOCK_TRYS   100U

#define AQ_CFG_DRV_AUTHOR      "aQuantia"
#define AQ_CFG_DRV_DESC        "aQuantia Corporation(R) Network Driver"
#define AQ_CFG_DRV_NAME        "atlantic"
#define AQ_CFG_DRV_VERSION	__stringify(NIC_MAJOR_DRIVER_VERSION)"."\
				__stringify(NIC_MINOR_DRIVER_VERSION)"."\
				__stringify(NIC_BUILD_DRIVER_VERSION)"."\
				__stringify(NIC_REVISION_DRIVER_VERSION) \
				AQ_CFG_DRV_VERSION_SUFFIX

#endif /* AQ_CFG_H */
