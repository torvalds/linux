/* SPDX-License-Identifier: GPL-2.0
 * Marvell OcteonTx2 CGX driver
 *
 * Copyright (C) 2018 Marvell International Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef CGX_H
#define CGX_H

#include "cgx_fw_if.h"

 /* PCI device IDs */
#define	PCI_DEVID_OCTEONTX2_CGX		0xA059

/* PCI BAR nos */
#define PCI_CFG_REG_BAR_NUM		0

#define MAX_CGX				3
#define MAX_LMAC_PER_CGX		4
#define CGX_OFFSET(x)			((x) * MAX_LMAC_PER_CGX)

/* Registers */
#define CGXX_CMRX_CFG			0x00
#define  CMR_EN					BIT_ULL(55)
#define  DATA_PKT_TX_EN				BIT_ULL(53)
#define  DATA_PKT_RX_EN				BIT_ULL(54)
#define CGXX_CMRX_INT			0x040
#define  FW_CGX_INT				BIT_ULL(1)
#define CGXX_CMRX_INT_ENA_W1S		0x058
#define CGXX_CMRX_RX_ID_MAP		0x060
#define CGXX_CMRX_RX_STAT0		0x070
#define CGXX_CMRX_RX_LMACS		0x128
#define CGXX_CMRX_RX_DMAC_CTL0		0x1F8
#define  CGX_DMAC_CTL0_CAM_ENABLE		BIT_ULL(3)
#define  CGX_DMAC_CAM_ACCEPT			BIT_ULL(3)
#define  CGX_DMAC_MCAST_MODE			BIT_ULL(1)
#define  CGX_DMAC_BCAST_MODE			BIT_ULL(0)
#define CGXX_CMRX_RX_DMAC_CAM0		0x200
#define  CGX_DMAC_CAM_ADDR_ENABLE		BIT_ULL(48)
#define CGXX_CMRX_RX_DMAC_CAM1		0x400
#define CGX_RX_DMAC_ADR_MASK			GENMASK_ULL(47, 0)
#define CGXX_CMRX_TX_STAT0		0x700
#define CGXX_SCRATCH0_REG		0x1050
#define CGXX_SCRATCH1_REG		0x1058
#define CGX_CONST			0x2000

#define CGX_COMMAND_REG			CGXX_SCRATCH1_REG
#define CGX_EVENT_REG			CGXX_SCRATCH0_REG
#define CGX_CMD_TIMEOUT			2200 /* msecs */

#define CGX_NVEC			37
#define CGX_LMAC_FWI			0

struct cgx_link_event {
	struct cgx_lnk_sts lstat;
	u8 cgx_id;
	u8 lmac_id;
};

/**
 * struct cgx_event_cb
 * @notify_link_chg:	callback for link change notification
 * @data:	data passed to callback function
 */
struct cgx_event_cb {
	int (*notify_link_chg)(struct cgx_link_event *event, void *data);
	void *data;
};

extern struct pci_driver cgx_driver;

int cgx_get_cgx_cnt(void);
int cgx_get_lmac_cnt(void *cgxd);
void *cgx_get_pdata(int cgx_id);
int cgx_lmac_evh_register(struct cgx_event_cb *cb, void *cgxd, int lmac_id);
int cgx_get_tx_stats(void *cgxd, int lmac_id, int idx, u64 *tx_stat);
int cgx_get_rx_stats(void *cgxd, int lmac_id, int idx, u64 *rx_stat);
int cgx_lmac_rx_tx_enable(void *cgxd, int lmac_id, bool enable);
int cgx_lmac_addr_set(u8 cgx_id, u8 lmac_id, u8 *mac_addr);
u64 cgx_lmac_addr_get(u8 cgx_id, u8 lmac_id);
void cgx_lmac_promisc_config(int cgx_id, int lmac_id, bool enable);
#endif /* CGX_H */
