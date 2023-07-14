/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright (C) 2021 Martin Blumenstingl <martin.blumenstingl@googlemail.com>
 * Copyright (C) 2021 Jernej Skrabec <jernej.skrabec@gmail.com>
 */

#ifndef __REG_SDIO_H_
#define __REG_SDIO_H_

/* I/O bus domain address mapping */
#define SDIO_LOCAL_OFFSET			0x10250000
#define WLAN_IOREG_OFFSET			0x10260000
#define FIRMWARE_FIFO_OFFSET			0x10270000
#define TX_HIQ_OFFSET				0x10310000
#define TX_MIQ_OFFSET				0x10320000
#define TX_LOQ_OFFSET				0x10330000
#define TX_EPQ_OFFSET				0x10350000
#define RX_RX0FF_OFFSET				0x10340000

#define RTW_SDIO_BUS_MSK			0xffff0000
#define SDIO_LOCAL_REG_MSK			0x00000fff
#define WLAN_IOREG_REG_MSK			0x0000ffff

/* SDIO Tx Control */
#define REG_SDIO_TX_CTRL			(SDIO_LOCAL_OFFSET + 0x0000)

/*SDIO status timeout*/
#define REG_SDIO_TIMEOUT			(SDIO_LOCAL_OFFSET + 0x0002)

/* SDIO Host Interrupt Mask */
#define REG_SDIO_HIMR				(SDIO_LOCAL_OFFSET + 0x0014)
#define REG_SDIO_HIMR_RX_REQUEST		BIT(0)
#define REG_SDIO_HIMR_AVAL			BIT(1)
#define REG_SDIO_HIMR_TXERR			BIT(2)
#define REG_SDIO_HIMR_RXERR			BIT(3)
#define REG_SDIO_HIMR_TXFOVW			BIT(4)
#define REG_SDIO_HIMR_RXFOVW			BIT(5)
#define REG_SDIO_HIMR_TXBCNOK			BIT(6)
#define REG_SDIO_HIMR_TXBCNERR			BIT(7)
#define REG_SDIO_HIMR_BCNERLY_INT		BIT(16)
#define REG_SDIO_HIMR_C2HCMD			BIT(17)
#define REG_SDIO_HIMR_CPWM1			BIT(18)
#define REG_SDIO_HIMR_CPWM2			BIT(19)
#define REG_SDIO_HIMR_HSISR_IND			BIT(20)
#define REG_SDIO_HIMR_GTINT3_IND		BIT(21)
#define REG_SDIO_HIMR_GTINT4_IND		BIT(22)
#define REG_SDIO_HIMR_PSTIMEOUT			BIT(23)
#define REG_SDIO_HIMR_OCPINT			BIT(24)
#define REG_SDIO_HIMR_ATIMEND			BIT(25)
#define REG_SDIO_HIMR_ATIMEND_E			BIT(26)
#define REG_SDIO_HIMR_CTWEND			BIT(27)
/* the following two are RTL8188 SDIO Specific */
#define REG_SDIO_HIMR_MCU_ERR			BIT(28)
#define REG_SDIO_HIMR_TSF_BIT32_TOGGLE		BIT(29)

/* SDIO Host Interrupt Service Routine */
#define REG_SDIO_HISR				(SDIO_LOCAL_OFFSET + 0x0018)
#define REG_SDIO_HISR_RX_REQUEST		BIT(0)
#define REG_SDIO_HISR_AVAL			BIT(1)
#define REG_SDIO_HISR_TXERR			BIT(2)
#define REG_SDIO_HISR_RXERR			BIT(3)
#define REG_SDIO_HISR_TXFOVW			BIT(4)
#define REG_SDIO_HISR_RXFOVW			BIT(5)
#define REG_SDIO_HISR_TXBCNOK			BIT(6)
#define REG_SDIO_HISR_TXBCNERR			BIT(7)
#define REG_SDIO_HISR_BCNERLY_INT		BIT(16)
#define REG_SDIO_HISR_C2HCMD			BIT(17)
#define REG_SDIO_HISR_CPWM1			BIT(18)
#define REG_SDIO_HISR_CPWM2			BIT(19)
#define REG_SDIO_HISR_HSISR_IND			BIT(20)
#define REG_SDIO_HISR_GTINT3_IND		BIT(21)
#define REG_SDIO_HISR_GTINT4_IND		BIT(22)
#define REG_SDIO_HISR_PSTIMEOUT			BIT(23)
#define REG_SDIO_HISR_OCPINT			BIT(24)
#define REG_SDIO_HISR_ATIMEND			BIT(25)
#define REG_SDIO_HISR_ATIMEND_E			BIT(26)
#define REG_SDIO_HISR_CTWEND			BIT(27)
/* the following two are RTL8188 SDIO Specific */
#define REG_SDIO_HISR_MCU_ERR			BIT(28)
#define REG_SDIO_HISR_TSF_BIT32_TOGGLE		BIT(29)

/* HCI Current Power Mode */
#define REG_SDIO_HCPWM				(SDIO_LOCAL_OFFSET + 0x0019)
/* RXDMA Request Length */
#define REG_SDIO_RX0_REQ_LEN			(SDIO_LOCAL_OFFSET + 0x001C)
/* OQT Free Page */
#define REG_SDIO_OQT_FREE_PG			(SDIO_LOCAL_OFFSET + 0x001E)
/* Free Tx Buffer Page */
#define REG_SDIO_FREE_TXPG			(SDIO_LOCAL_OFFSET + 0x0020)
/* HCI Current Power Mode 1 */
#define REG_SDIO_HCPWM1				(SDIO_LOCAL_OFFSET + 0x0024)
/* HCI Current Power Mode 2 */
#define REG_SDIO_HCPWM2				(SDIO_LOCAL_OFFSET + 0x0026)
/* Free Tx Page Sequence */
#define REG_SDIO_FREE_TXPG_SEQ			(SDIO_LOCAL_OFFSET + 0x0028)
/* HTSF Information */
#define REG_SDIO_HTSFR_INFO			(SDIO_LOCAL_OFFSET + 0x0030)
#define REG_SDIO_HCPWM1_V2			(SDIO_LOCAL_OFFSET + 0x0038)
/* H2C */
#define REG_SDIO_H2C				(SDIO_LOCAL_OFFSET + 0x0060)
/* HCI Request Power Mode 1 */
#define REG_SDIO_HRPWM1				(SDIO_LOCAL_OFFSET + 0x0080)
/* HCI Request Power Mode 2 */
#define REG_SDIO_HRPWM2				(SDIO_LOCAL_OFFSET + 0x0082)
/* HCI Power Save Clock */
#define REG_SDIO_HPS_CLKR			(SDIO_LOCAL_OFFSET + 0x0084)
/* SDIO HCI Suspend Control */
#define REG_SDIO_HSUS_CTRL			(SDIO_LOCAL_OFFSET + 0x0086)
#define BIT_HCI_SUS_REQ				BIT(0)
#define BIT_HCI_RESUME_RDY			BIT(1)
/* SDIO Host Extension Interrupt Mask Always */
#define REG_SDIO_HIMR_ON			(SDIO_LOCAL_OFFSET + 0x0090)
/* SDIO Host Extension Interrupt Status Always */
#define REG_SDIO_HISR_ON			(SDIO_LOCAL_OFFSET + 0x0091)

#define REG_SDIO_INDIRECT_REG_CFG		(SDIO_LOCAL_OFFSET + 0x0040)
#define BIT_SDIO_INDIRECT_REG_CFG_WORD		BIT(16)
#define BIT_SDIO_INDIRECT_REG_CFG_DWORD		BIT(17)
#define BIT_SDIO_INDIRECT_REG_CFG_WRITE		BIT(18)
#define BIT_SDIO_INDIRECT_REG_CFG_READ		BIT(19)
#define BIT_SDIO_INDIRECT_REG_CFG_UNK20		BIT(20)
#define REG_SDIO_INDIRECT_REG_DATA		(SDIO_LOCAL_OFFSET + 0x0044)

/* Sdio Address for SDIO Local Reg, TRX FIFO, MAC Reg */
#define REG_SDIO_CMD_ADDR_MSK			GENMASK(16, 13)
#define REG_SDIO_CMD_ADDR_SDIO_REG		0
#define REG_SDIO_CMD_ADDR_MAC_REG		8
#define REG_SDIO_CMD_ADDR_TXFF_HIGH		4
#define REG_SDIO_CMD_ADDR_TXFF_LOW		6
#define REG_SDIO_CMD_ADDR_TXFF_NORMAL		5
#define REG_SDIO_CMD_ADDR_TXFF_EXTRA		7
#define REG_SDIO_CMD_ADDR_RXFF			7

#define RTW_SDIO_BLOCK_SIZE			512
#define RTW_SDIO_ADDR_RX_RX0FF_GEN(_id)		(0x0e000 | ((_id) & 0x3))

#define RTW_SDIO_DATA_PTR_ALIGN			8

struct sdio_func;
struct sdio_device_id;

struct rtw_sdio_tx_data {
	u8 sn;
};

struct rtw_sdio_work_data {
	struct work_struct work;
	struct rtw_dev *rtwdev;
};

struct rtw_sdio {
	struct sdio_func *sdio_func;

	u32 irq_mask;
	u8 rx_addr;
	bool sdio3_bus_mode;

	void *irq_thread;

	struct workqueue_struct *txwq;
	struct rtw_sdio_work_data *tx_handler_data;
	struct sk_buff_head tx_queue[RTK_MAX_TX_QUEUE_NUM];
};

extern const struct dev_pm_ops rtw_sdio_pm_ops;

int rtw_sdio_probe(struct sdio_func *sdio_func,
		   const struct sdio_device_id *id);
void rtw_sdio_remove(struct sdio_func *sdio_func);
void rtw_sdio_shutdown(struct device *dev);

static inline bool rtw_sdio_is_sdio30_supported(struct rtw_dev *rtwdev)
{
	struct rtw_sdio *rtwsdio = (struct rtw_sdio *)rtwdev->priv;

	return rtwsdio->sdio3_bus_mode;
}

#endif
