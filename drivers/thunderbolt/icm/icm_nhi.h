/*******************************************************************************
 *
 * Thunderbolt(TM) driver
 * Copyright(c) 2014 - 2017 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 ******************************************************************************/

#ifndef ICM_NHI_H_
#define ICM_NHI_H_

#include <linux/module.h>
#include <linux/pci.h>
#include "../nhi_regs.h"

#define DRV_VERSION "17.1.63.1"

#define PCI_DEVICE_ID_INTEL_WIN_RIDGE_2C_NHI		0x157d /*Tbt 2 Low Pwr*/
#define PCI_DEVICE_ID_INTEL_WIN_RIDGE_2C_BRIDGE		0x157e
#define PCI_DEVICE_ID_INTEL_ALPINE_RIDGE_LP_NHI		0x15bf /*Tbt 3 Low Pwr*/
#define PCI_DEVICE_ID_INTEL_ALPINE_RIDGE_LP_BRIDGE	0x15c0
#define PCI_DEVICE_ID_INTEL_ALPINE_RIDGE_C_4C_NHI	0x15d2 /*Thunderbolt 3*/
#define PCI_DEVICE_ID_INTEL_ALPINE_RIDGE_C_4C_BRIDGE	0x15d3
#define PCI_DEVICE_ID_INTEL_ALPINE_RIDGE_C_2C_NHI	0x15d9
#define PCI_DEVICE_ID_INTEL_ALPINE_RIDGE_C_2C_BRIDGE	0x15da
#define PCI_DEVICE_ID_INTEL_ALPINE_RIDGE_LP_USBONLY_NHI	0x15dc
#define PCI_DEVICE_ID_INTEL_ALPINE_RIDGE_USBONLY_NHI	0x15dd
#define PCI_DEVICE_ID_INTEL_ALPINE_RIDGE_C_USBONLY_NHI	0x15de

#define TBT_ICM_RING_MAX_FRAME_SIZE	256
#define TBT_ICM_RING_NUM		0
#define TBT_RING_MAX_FRM_DATA_SZ	(TBT_RING_MAX_FRAME_SIZE - \
					 sizeof(struct tbt_frame_header))

enum icm_operation_mode {
	SAFE_MODE,
	AUTHENTICATION_MODE_FUNCTIONALITY,
	ENDPOINT_OPERATION_MODE,
	FULL_FUNCTIONALITY,
};

#define TBT_ICM_RING_NUM_TX_BUFS TBT_RING_MIN_NUM_BUFFERS
#define TBT_ICM_RING_NUM_RX_BUFS ((PAGE_SIZE - (TBT_ICM_RING_NUM_TX_BUFS * \
	(sizeof(struct tbt_buf_desc) + TBT_ICM_RING_MAX_FRAME_SIZE))) / \
	(sizeof(struct tbt_buf_desc) + TBT_ICM_RING_MAX_FRAME_SIZE))

/* struct tbt_icm_ring_shared_memory - memory area for DMA */
struct tbt_icm_ring_shared_memory {
	u8 tx_buf[TBT_ICM_RING_NUM_TX_BUFS][TBT_ICM_RING_MAX_FRAME_SIZE];
	u8 rx_buf[TBT_ICM_RING_NUM_RX_BUFS][TBT_ICM_RING_MAX_FRAME_SIZE];
	struct tbt_buf_desc tx_buf_desc[TBT_ICM_RING_NUM_TX_BUFS];
	struct tbt_buf_desc rx_buf_desc[TBT_ICM_RING_NUM_RX_BUFS];
} __aligned(TBT_ICM_RING_MAX_FRAME_SIZE);

/* mailbox data from SW */
#define REG_INMAIL_DATA		0x39900

/* mailbox command from SW */
#define REG_INMAIL_CMD		0x39904
#define REG_INMAIL_CMD_CMD_SHIFT	0
#define REG_INMAIL_CMD_CMD_MASK		GENMASK(7, REG_INMAIL_CMD_CMD_SHIFT)
#define REG_INMAIL_CMD_ERROR		BIT(30)
#define REG_INMAIL_CMD_REQUEST		BIT(31)

/* mailbox command from FW */
#define REG_OUTMAIL_CMD		0x3990C
#define REG_OUTMAIL_CMD_STS_SHIFT	0
#define REG_OUTMAIL_CMD_STS_MASK	GENMASK(7, REG_OUTMAIL_CMD_STS_SHIFT)
#define REG_OUTMAIL_CMD_OP_MODE_SHIFT	8
#define REG_OUTMAIL_CMD_OP_MODE_MASK	\
				GENMASK(11, REG_OUTMAIL_CMD_OP_MODE_SHIFT)
#define REG_OUTMAIL_CMD_REQUEST		BIT(31)

#define REG_FW_STS		0x39944
#define REG_FW_STS_ICM_EN		GENMASK(1, 0)
#define REG_FW_STS_NVM_AUTH_DONE	BIT(31)

#endif
