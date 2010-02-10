#ifndef P54PCI_H
#define P54PCI_H

/*
 * Defines for PCI based mac80211 Prism54 driver
 *
 * Copyright (c) 2006, Michael Wu <flamingice@sourmilk.net>
 *
 * Based on the islsm (softmac prism54) driver, which is:
 * Copyright 2004-2006 Jean-Baptiste Note <jbnote@gmail.com>, et al.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/* Device Interrupt register bits */
#define ISL38XX_DEV_INT_RESET                   0x0001
#define ISL38XX_DEV_INT_UPDATE                  0x0002
#define ISL38XX_DEV_INT_WAKEUP                  0x0008
#define ISL38XX_DEV_INT_SLEEP                   0x0010
#define ISL38XX_DEV_INT_ABORT                   0x0020
/* these two only used in USB */
#define ISL38XX_DEV_INT_DATA                    0x0040
#define ISL38XX_DEV_INT_MGMT                    0x0080

#define ISL38XX_DEV_INT_PCIUART_CTS             0x4000
#define ISL38XX_DEV_INT_PCIUART_DR              0x8000

/* Interrupt Identification/Acknowledge/Enable register bits */
#define ISL38XX_INT_IDENT_UPDATE		0x0002
#define ISL38XX_INT_IDENT_INIT			0x0004
#define ISL38XX_INT_IDENT_WAKEUP		0x0008
#define ISL38XX_INT_IDENT_SLEEP			0x0010
#define ISL38XX_INT_IDENT_PCIUART_CTS		0x4000
#define ISL38XX_INT_IDENT_PCIUART_DR		0x8000

/* Control/Status register bits */
#define ISL38XX_CTRL_STAT_SLEEPMODE		0x00000200
#define ISL38XX_CTRL_STAT_CLKRUN		0x00800000
#define ISL38XX_CTRL_STAT_RESET			0x10000000
#define ISL38XX_CTRL_STAT_RAMBOOT		0x20000000
#define ISL38XX_CTRL_STAT_STARTHALTED		0x40000000
#define ISL38XX_CTRL_STAT_HOST_OVERRIDE		0x80000000

struct p54p_csr {
	__le32 dev_int;
	u8 unused_1[12];
	__le32 int_ident;
	__le32 int_ack;
	__le32 int_enable;
	u8 unused_2[4];
	union {
		__le32 ring_control_base;
		__le32 gen_purp_com[2];
	};
	u8 unused_3[8];
	__le32 direct_mem_base;
	u8 unused_4[44];
	__le32 dma_addr;
	__le32 dma_len;
	__le32 dma_ctrl;
	u8 unused_5[12];
	__le32 ctrl_stat;
	u8 unused_6[1924];
	u8 cardbus_cis[0x800];
	u8 direct_mem_win[0x1000];
} __attribute__ ((packed));

/* usb backend only needs the register defines above */
#ifndef P54USB_H
struct p54p_desc {
	__le32 host_addr;
	__le32 device_addr;
	__le16 len;
	__le16 flags;
} __attribute__ ((packed));

struct p54p_ring_control {
	__le32 host_idx[4];
	__le32 device_idx[4];
	struct p54p_desc rx_data[8];
	struct p54p_desc tx_data[32];
	struct p54p_desc rx_mgmt[4];
	struct p54p_desc tx_mgmt[4];
} __attribute__ ((packed));

#define P54P_READ(r) (__force __le32)__raw_readl(&priv->map->r)
#define P54P_WRITE(r, val) __raw_writel((__force u32)(__le32)(val), &priv->map->r)

struct p54p_priv {
	struct p54_common common;
	struct pci_dev *pdev;
	struct p54p_csr __iomem *map;
	struct tasklet_struct tasklet;
	const struct firmware *firmware;
	spinlock_t lock;
	struct p54p_ring_control *ring_control;
	dma_addr_t ring_control_dma;
	u32 rx_idx_data, tx_idx_data;
	u32 rx_idx_mgmt, tx_idx_mgmt;
	struct sk_buff *rx_buf_data[8];
	struct sk_buff *rx_buf_mgmt[4];
	struct sk_buff *tx_buf_data[32];
	struct sk_buff *tx_buf_mgmt[4];
	struct completion boot_comp;
};

#endif /* P54USB_H */
#endif /* P54PCI_H */
