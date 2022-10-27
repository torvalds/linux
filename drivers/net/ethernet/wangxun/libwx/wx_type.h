/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2015 - 2022 Beijing WangXun Technology Co., Ltd. */

#ifndef _WX_TYPE_H_
#define _WX_TYPE_H_

/* Vendor ID */
#ifndef PCI_VENDOR_ID_WANGXUN
#define PCI_VENDOR_ID_WANGXUN                   0x8088
#endif

/* FMGR Registers */
#define WX_SPI_CMD                   0x10104
#define WX_SPI_CMD_READ_DWORD        0x1
#define WX_SPI_CLK_DIV               0x3
#define WX_SPI_CMD_CMD(_v)           (((_v) & 0x7) << 28)
#define WX_SPI_CMD_CLK(_v)           (((_v) & 0x7) << 25)
#define WX_SPI_CMD_ADDR(_v)          (((_v) & 0xFFFFFF))
#define WX_SPI_DATA                  0x10108
#define WX_SPI_DATA_BYPASS           BIT(31)
#define WX_SPI_DATA_STATUS(_v)       (((_v) & 0xFF) << 16)
#define WX_SPI_DATA_OP_DONE          BIT(0)
#define WX_SPI_STATUS                0x1010C
#define WX_SPI_STATUS_OPDONE         BIT(0)
#define WX_SPI_STATUS_FLASH_BYPASS   BIT(31)
#define WX_SPI_ILDR_STATUS           0x10120

/* Bus parameters */
struct wx_bus_info {
	u8 func;
	u16 device;
};

struct wx_hw {
	u8 __iomem *hw_addr;
	struct pci_dev *pdev;
	struct wx_bus_info bus;
	u16 device_id;
	u16 vendor_id;
	u16 subsystem_device_id;
	u16 subsystem_vendor_id;
	u8 revision_id;
	u16 oem_ssid;
	u16 oem_svid;
};

/* register operations */
#define wr32(a, reg, value)	writel((value), ((a)->hw_addr + (reg)))
#define rd32(a, reg)		readl((a)->hw_addr + (reg))

#define wx_err(wxhw, fmt, arg...) \
	dev_err(&(wxhw)->pdev->dev, fmt, ##arg)

#endif /* _WX_TYPE_H_ */
