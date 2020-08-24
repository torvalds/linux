/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2016 - 2019 Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 ******************************************************************************/

#include "halmac_init_8821c.h"
#include "halmac_8821c_cfg.h"
#if HALMAC_PCIE_SUPPORT
#include "halmac_pcie_8821c.h"
#endif
#if HALMAC_SDIO_SUPPORT
#include "halmac_sdio_8821c.h"
#include "../halmac_sdio_88xx.h"
#endif
#if HALMAC_USB_SUPPORT
#include "halmac_usb_8821c.h"
#endif
#include "halmac_gpio_8821c.h"
#include "halmac_common_8821c.h"
#include "halmac_cfg_wmac_8821c.h"
#include "../halmac_common_88xx.h"
#include "../halmac_init_88xx.h"

#if HALMAC_8821C_SUPPORT

#define SYS_FUNC_EN		0xD8

#define RSVD_PG_DRV_NUM			16
#define RSVD_PG_H2C_EXTRAINFO_NUM	24
#define RSVD_PG_H2C_STATICINFO_NUM	8
#define RSVD_PG_H2CQ_NUM		8
#define RSVD_PG_CPU_INSTRUCTION_NUM	0
#define RSVD_PG_FW_TXBUF_NUM		4
#define RSVD_PG_CSIBUF_NUM		0
#define RSVD_PG_DLLB_NUM		(TX_FIFO_SIZE_8821C / 3 >> \
					TX_PAGE_SIZE_SHIFT_88XX)

#define MAC_TRX_ENABLE	(BIT_HCI_TXDMA_EN | BIT_HCI_RXDMA_EN | BIT_TXDMA_EN | \
			BIT_RXDMA_EN | BIT_PROTOCOL_EN | BIT_SCHEDULE_EN | \
			BIT_MACTXEN | BIT_MACRXEN)

#define BLK_DESC_NUM   0x3

#define WLAN_SLOT_TIME		0x09
#define WLAN_PIFS_TIME		0x19
#define WLAN_SIFS_CCK_CONT_TX	0xA
#define WLAN_SIFS_OFDM_CONT_TX	0xE
#define WLAN_SIFS_CCK_TRX	0x10
#define WLAN_SIFS_OFDM_TRX	0x10
#define WLAN_VO_TXOP_LIMIT	0x186 /* unit : 32us */
#define WLAN_VI_TXOP_LIMIT	0x3BC /* unit : 32us */
#define WLAN_RDG_NAV		0x05
#define WLAN_TXOP_NAV		0x1B
#define WLAN_CCK_RX_TSF		0x30
#define WLAN_OFDM_RX_TSF	0x30
#define WLAN_TBTT_PROHIBIT	0x04 /* unit : 32us */
#define WLAN_TBTT_HOLD_TIME	0x064 /* unit : 32us */
#define WLAN_DRV_EARLY_INT	0x04
#define WLAN_BCN_DMA_TIME	0x02
#define WLAN_ACK_TO_CCK		0x40

#define WLAN_RX_FILTER0		0x0FFFFFFF
#define WLAN_RX_FILTER2		0xFFFF
#define WLAN_RCR_CFG		0xE400220E
#define WLAN_RXPKT_MAX_SZ	12288
#define WLAN_RXPKT_MAX_SZ_512	(WLAN_RXPKT_MAX_SZ >> 9)

#define WLAN_AMPDU_MAX_TIME		0x70
#define WLAN_RTS_LEN_TH			0xFF
#define WLAN_RTS_TX_TIME_TH		0x08
#define WLAN_MAX_AGG_PKT_LIMIT		0x10
#define WLAN_RTS_MAX_AGG_PKT_LIMIT	0x10
#define WLAN_MAX_AGG_PKT_LIMIT_SDIO	0x2B
#define WLAN_RTS_MAX_AGG_PKT_LIMIT_SDIO	0x2B
#define WLAN_PRE_TXCNT_TIME_TH		0x1E4
#define WALN_FAST_EDCA_VO_TH		0x06
#define WLAN_FAST_EDCA_VI_TH		0x06
#define WLAN_FAST_EDCA_BE_TH		0x06
#define WLAN_FAST_EDCA_BK_TH		0x06
#define WLAN_BAR_RETRY_LIMIT		0x01
#define WLAN_RA_TRY_RATE_AGG_LIMIT	0x08

#define WLAN_TX_FUNC_CFG1		0x30
#define WLAN_TX_FUNC_CFG2		0x30
#define WLAN_MAC_OPT_NORM_FUNC1		0x98
#define WLAN_MAC_OPT_LB_FUNC1		0x80
#define WLAN_MAC_OPT_FUNC2		0x30810041

#define WLAN_SIFS_CFG	(WLAN_SIFS_CCK_CONT_TX | \
			(WLAN_SIFS_OFDM_CONT_TX << BIT_SHIFT_SIFS_OFDM_CTX) | \
			(WLAN_SIFS_CCK_TRX << BIT_SHIFT_SIFS_CCK_TRX) | \
			(WLAN_SIFS_OFDM_TRX << BIT_SHIFT_SIFS_OFDM_TRX))

#define WLAN_TBTT_TIME	(WLAN_TBTT_PROHIBIT |\
			(WLAN_TBTT_HOLD_TIME << BIT_SHIFT_TBTT_HOLD_TIME_AP))

#define WLAN_NAV_CFG		(WLAN_RDG_NAV | (WLAN_TXOP_NAV << 16))
#define WLAN_RX_TSF_CFG		(WLAN_CCK_RX_TSF | (WLAN_OFDM_RX_TSF) << 8)

#if HALMAC_PLATFORM_WINDOWS
/*SDIO RQPN Mapping for Windows, extra queue is not implemented in Driver code*/
static struct halmac_rqpn HALMAC_RQPN_SDIO_8821C[] = {
	/* { mode, vo_map, vi_map, be_map, bk_map, mg_map, hi_map } */
	{HALMAC_TRX_MODE_NORMAL,
	 HALMAC_MAP2_NQ, HALMAC_MAP2_NQ, HALMAC_MAP2_LQ, HALMAC_MAP2_LQ,
	 HALMAC_MAP2_HQ, HALMAC_MAP2_HQ},
	{HALMAC_TRX_MODE_TRXSHARE,
	 HALMAC_MAP2_NQ, HALMAC_MAP2_NQ, HALMAC_MAP2_LQ, HALMAC_MAP2_LQ,
	 HALMAC_MAP2_HQ, HALMAC_MAP2_HQ},
	{HALMAC_TRX_MODE_WMM,
	 HALMAC_MAP2_HQ, HALMAC_MAP2_NQ, HALMAC_MAP2_LQ, HALMAC_MAP2_NQ,
	 HALMAC_MAP2_EXQ, HALMAC_MAP2_HQ},
	{HALMAC_TRX_MODE_P2P,
	 HALMAC_MAP2_NQ, HALMAC_MAP2_NQ, HALMAC_MAP2_LQ, HALMAC_MAP2_LQ,
	 HALMAC_MAP2_HQ, HALMAC_MAP2_HQ},
	{HALMAC_TRX_MODE_LOOPBACK,
	 HALMAC_MAP2_NQ, HALMAC_MAP2_NQ, HALMAC_MAP2_LQ, HALMAC_MAP2_LQ,
	 HALMAC_MAP2_HQ, HALMAC_MAP2_HQ},
	{HALMAC_TRX_MODE_DELAY_LOOPBACK,
	 HALMAC_MAP2_NQ, HALMAC_MAP2_NQ, HALMAC_MAP2_LQ, HALMAC_MAP2_LQ,
	 HALMAC_MAP2_HQ, HALMAC_MAP2_HQ},
};
#else
/*SDIO RQPN Mapping*/
static struct halmac_rqpn HALMAC_RQPN_SDIO_8821C[] = {
	/* { mode, vo_map, vi_map, be_map, bk_map, mg_map, hi_map } */
	{HALMAC_TRX_MODE_NORMAL,
	 HALMAC_MAP2_NQ, HALMAC_MAP2_NQ, HALMAC_MAP2_LQ, HALMAC_MAP2_LQ,
	 HALMAC_MAP2_EXQ, HALMAC_MAP2_HQ},
	{HALMAC_TRX_MODE_TRXSHARE,
	 HALMAC_MAP2_NQ, HALMAC_MAP2_NQ, HALMAC_MAP2_LQ, HALMAC_MAP2_LQ,
	 HALMAC_MAP2_EXQ, HALMAC_MAP2_HQ},
	{HALMAC_TRX_MODE_WMM,
	 HALMAC_MAP2_HQ, HALMAC_MAP2_NQ, HALMAC_MAP2_LQ, HALMAC_MAP2_NQ,
	 HALMAC_MAP2_EXQ, HALMAC_MAP2_HQ},
	{HALMAC_TRX_MODE_P2P,
	 HALMAC_MAP2_NQ, HALMAC_MAP2_NQ, HALMAC_MAP2_LQ, HALMAC_MAP2_LQ,
	 HALMAC_MAP2_EXQ, HALMAC_MAP2_HQ},
	{HALMAC_TRX_MODE_LOOPBACK,
	 HALMAC_MAP2_NQ, HALMAC_MAP2_NQ, HALMAC_MAP2_LQ, HALMAC_MAP2_LQ,
	 HALMAC_MAP2_EXQ, HALMAC_MAP2_HQ},
	{HALMAC_TRX_MODE_DELAY_LOOPBACK,
	 HALMAC_MAP2_NQ, HALMAC_MAP2_NQ, HALMAC_MAP2_LQ, HALMAC_MAP2_LQ,
	 HALMAC_MAP2_EXQ, HALMAC_MAP2_HQ},
};
#endif

/*PCIE RQPN Mapping*/
static struct halmac_rqpn HALMAC_RQPN_PCIE_8821C[] = {
	/* { mode, vo_map, vi_map, be_map, bk_map, mg_map, hi_map } */
	{HALMAC_TRX_MODE_NORMAL,
	 HALMAC_MAP2_NQ, HALMAC_MAP2_NQ, HALMAC_MAP2_LQ, HALMAC_MAP2_LQ,
	 HALMAC_MAP2_EXQ, HALMAC_MAP2_HQ},
	{HALMAC_TRX_MODE_TRXSHARE,
	 HALMAC_MAP2_NQ, HALMAC_MAP2_NQ, HALMAC_MAP2_LQ, HALMAC_MAP2_LQ,
	 HALMAC_MAP2_EXQ, HALMAC_MAP2_HQ},
	{HALMAC_TRX_MODE_WMM,
	 HALMAC_MAP2_HQ, HALMAC_MAP2_NQ, HALMAC_MAP2_LQ, HALMAC_MAP2_NQ,
	 HALMAC_MAP2_EXQ, HALMAC_MAP2_HQ},
	{HALMAC_TRX_MODE_P2P,
	 HALMAC_MAP2_NQ, HALMAC_MAP2_NQ, HALMAC_MAP2_LQ, HALMAC_MAP2_LQ,
	 HALMAC_MAP2_EXQ, HALMAC_MAP2_HQ},
	{HALMAC_TRX_MODE_LOOPBACK,
	 HALMAC_MAP2_NQ, HALMAC_MAP2_NQ, HALMAC_MAP2_LQ, HALMAC_MAP2_LQ,
	 HALMAC_MAP2_EXQ, HALMAC_MAP2_HQ},
	{HALMAC_TRX_MODE_DELAY_LOOPBACK,
	 HALMAC_MAP2_NQ, HALMAC_MAP2_NQ, HALMAC_MAP2_LQ, HALMAC_MAP2_LQ,
	 HALMAC_MAP2_EXQ, HALMAC_MAP2_HQ},
};

/*USB 2 Bulkout RQPN Mapping*/
static struct halmac_rqpn HALMAC_RQPN_2BULKOUT_8821C[] = {
	/* { mode, vo_map, vi_map, be_map, bk_map, mg_map, hi_map } */
	{HALMAC_TRX_MODE_NORMAL,
	 HALMAC_MAP2_NQ, HALMAC_MAP2_NQ, HALMAC_MAP2_NQ, HALMAC_MAP2_HQ,
	 HALMAC_MAP2_HQ, HALMAC_MAP2_HQ},
	{HALMAC_TRX_MODE_TRXSHARE,
	 HALMAC_MAP2_NQ, HALMAC_MAP2_NQ, HALMAC_MAP2_NQ, HALMAC_MAP2_HQ,
	 HALMAC_MAP2_HQ, HALMAC_MAP2_HQ},
	{HALMAC_TRX_MODE_WMM,
	 HALMAC_MAP2_NQ, HALMAC_MAP2_NQ, HALMAC_MAP2_NQ, HALMAC_MAP2_HQ,
	 HALMAC_MAP2_HQ, HALMAC_MAP2_HQ},
	{HALMAC_TRX_MODE_P2P,
	 HALMAC_MAP2_HQ, HALMAC_MAP2_HQ, HALMAC_MAP2_HQ, HALMAC_MAP2_NQ,
	 HALMAC_MAP2_HQ, HALMAC_MAP2_HQ},
	{HALMAC_TRX_MODE_LOOPBACK,
	 HALMAC_MAP2_HQ, HALMAC_MAP2_HQ, HALMAC_MAP2_HQ, HALMAC_MAP2_NQ,
	 HALMAC_MAP2_HQ, HALMAC_MAP2_HQ},
	{HALMAC_TRX_MODE_DELAY_LOOPBACK,
	 HALMAC_MAP2_HQ, HALMAC_MAP2_HQ, HALMAC_MAP2_HQ, HALMAC_MAP2_NQ,
	 HALMAC_MAP2_HQ, HALMAC_MAP2_HQ},
};

/*USB 3 Bulkout RQPN Mapping*/
static struct halmac_rqpn HALMAC_RQPN_3BULKOUT_8821C[] = {
	/* { mode, vo_map, vi_map, be_map, bk_map, mg_map, hi_map } */
	{HALMAC_TRX_MODE_NORMAL,
	 HALMAC_MAP2_NQ, HALMAC_MAP2_NQ, HALMAC_MAP2_LQ, HALMAC_MAP2_LQ,
	 HALMAC_MAP2_HQ, HALMAC_MAP2_HQ},
	{HALMAC_TRX_MODE_TRXSHARE,
	 HALMAC_MAP2_NQ, HALMAC_MAP2_NQ, HALMAC_MAP2_LQ, HALMAC_MAP2_LQ,
	 HALMAC_MAP2_HQ, HALMAC_MAP2_HQ},
	{HALMAC_TRX_MODE_WMM,
	 HALMAC_MAP2_HQ, HALMAC_MAP2_NQ, HALMAC_MAP2_LQ, HALMAC_MAP2_NQ,
	 HALMAC_MAP2_HQ, HALMAC_MAP2_HQ},
	{HALMAC_TRX_MODE_P2P,
	 HALMAC_MAP2_HQ, HALMAC_MAP2_HQ, HALMAC_MAP2_LQ, HALMAC_MAP2_NQ,
	 HALMAC_MAP2_HQ, HALMAC_MAP2_HQ},
	{HALMAC_TRX_MODE_LOOPBACK,
	 HALMAC_MAP2_HQ, HALMAC_MAP2_HQ, HALMAC_MAP2_LQ, HALMAC_MAP2_NQ,
	 HALMAC_MAP2_HQ, HALMAC_MAP2_HQ},
	{HALMAC_TRX_MODE_DELAY_LOOPBACK,
	 HALMAC_MAP2_HQ, HALMAC_MAP2_HQ, HALMAC_MAP2_LQ, HALMAC_MAP2_NQ,
	 HALMAC_MAP2_HQ, HALMAC_MAP2_HQ},
};

/*USB 4 Bulkout RQPN Mapping*/
static struct halmac_rqpn HALMAC_RQPN_4BULKOUT_8821C[] = {
	/* { mode, vo_map, vi_map, be_map, bk_map, mg_map, hi_map } */
	{HALMAC_TRX_MODE_NORMAL,
	 HALMAC_MAP2_NQ, HALMAC_MAP2_NQ, HALMAC_MAP2_LQ, HALMAC_MAP2_LQ,
	 HALMAC_MAP2_EXQ, HALMAC_MAP2_HQ},
	{HALMAC_TRX_MODE_TRXSHARE,
	 HALMAC_MAP2_NQ, HALMAC_MAP2_NQ, HALMAC_MAP2_LQ, HALMAC_MAP2_LQ,
	 HALMAC_MAP2_EXQ, HALMAC_MAP2_HQ},
	{HALMAC_TRX_MODE_WMM,
	 HALMAC_MAP2_HQ, HALMAC_MAP2_NQ, HALMAC_MAP2_LQ, HALMAC_MAP2_NQ,
	 HALMAC_MAP2_EXQ, HALMAC_MAP2_HQ},
	{HALMAC_TRX_MODE_P2P,
	 HALMAC_MAP2_NQ, HALMAC_MAP2_NQ, HALMAC_MAP2_LQ, HALMAC_MAP2_LQ,
	 HALMAC_MAP2_EXQ, HALMAC_MAP2_HQ},
	{HALMAC_TRX_MODE_LOOPBACK,
	 HALMAC_MAP2_NQ, HALMAC_MAP2_NQ, HALMAC_MAP2_LQ, HALMAC_MAP2_LQ,
	 HALMAC_MAP2_EXQ, HALMAC_MAP2_HQ},
	{HALMAC_TRX_MODE_DELAY_LOOPBACK,
	 HALMAC_MAP2_NQ, HALMAC_MAP2_NQ, HALMAC_MAP2_LQ, HALMAC_MAP2_LQ,
	 HALMAC_MAP2_EXQ, HALMAC_MAP2_HQ},
};

#if HALMAC_PLATFORM_WINDOWS
/*SDIO Page Number*/
static struct halmac_pg_num HALMAC_PG_NUM_SDIO_8821C[] = {
	/* { mode, hq_num, nq_num, lq_num, exq_num, gap_num} */
	{HALMAC_TRX_MODE_NORMAL, 16, 16, 16, 0, 1},
	{HALMAC_TRX_MODE_TRXSHARE, 8, 8, 8, 0, 1},
	{HALMAC_TRX_MODE_WMM, 16, 16, 16, 0, 1},
	{HALMAC_TRX_MODE_P2P, 16, 16, 16, 0, 1},
	{HALMAC_TRX_MODE_LOOPBACK, 16, 16, 16, 0, 1},
	{HALMAC_TRX_MODE_DELAY_LOOPBACK, 16, 16, 16, 0, 1},
};
#else
/*SDIO Page Number*/
static struct halmac_pg_num HALMAC_PG_NUM_SDIO_8821C[] = {
	/* { mode, hq_num, nq_num, lq_num, exq_num, gap_num} */
	{HALMAC_TRX_MODE_NORMAL, 16, 16, 16, 14, 1},
	{HALMAC_TRX_MODE_TRXSHARE, 8, 8, 8, 8, 1},
	{HALMAC_TRX_MODE_WMM, 16, 16, 16, 14, 1},
	{HALMAC_TRX_MODE_P2P, 16, 16, 16, 14, 1},
	{HALMAC_TRX_MODE_LOOPBACK, 16, 16, 16, 14, 1},
	{HALMAC_TRX_MODE_DELAY_LOOPBACK, 16, 16, 16, 14, 1},
};
#endif

/*PCIE Page Number*/
static struct halmac_pg_num HALMAC_PG_NUM_PCIE_8821C[] = {
	/* { mode, hq_num, nq_num, lq_num, exq_num, gap_num} */
	{HALMAC_TRX_MODE_NORMAL, 16, 16, 16, 14, 1},
	{HALMAC_TRX_MODE_TRXSHARE, 16, 16, 16, 14, 1},
	{HALMAC_TRX_MODE_WMM, 16, 16, 16, 14, 1},
	{HALMAC_TRX_MODE_P2P, 16, 16, 16, 14, 1},
	{HALMAC_TRX_MODE_LOOPBACK, 16, 16, 16, 14, 1},
	{HALMAC_TRX_MODE_DELAY_LOOPBACK, 16, 16, 16, 14, 1},
};

/*USB 2 Bulkout Page Number*/
static struct halmac_pg_num HALMAC_PG_NUM_2BULKOUT_8821C[] = {
	/* { mode, hq_num, nq_num, lq_num, exq_num, gap_num} */
	{HALMAC_TRX_MODE_NORMAL, 16, 16, 0, 0, 1},
	{HALMAC_TRX_MODE_TRXSHARE, 16, 16, 0, 0, 1},
	{HALMAC_TRX_MODE_WMM, 16, 16, 0, 0, 1},
	{HALMAC_TRX_MODE_P2P, 16, 16, 0, 0, 1},
	{HALMAC_TRX_MODE_LOOPBACK, 16, 16, 0, 0, 1},
	{HALMAC_TRX_MODE_DELAY_LOOPBACK, 16, 16, 0, 0, 1},
};

/*USB 3 Bulkout Page Number*/
static struct halmac_pg_num HALMAC_PG_NUM_3BULKOUT_8821C[] = {
	/* { mode, hq_num, nq_num, lq_num, exq_num, gap_num} */
	{HALMAC_TRX_MODE_NORMAL, 16, 16, 16, 0, 1},
	{HALMAC_TRX_MODE_TRXSHARE, 16, 16, 16, 0, 1},
	{HALMAC_TRX_MODE_WMM, 16, 16, 16, 0, 1},
	{HALMAC_TRX_MODE_P2P, 16, 16, 16, 0, 1},
	{HALMAC_TRX_MODE_LOOPBACK, 16, 16, 16, 0, 1},
	{HALMAC_TRX_MODE_DELAY_LOOPBACK, 16, 16, 16, 0, 1},
};

/*USB 4 Bulkout Page Number*/
static struct halmac_pg_num HALMAC_PG_NUM_4BULKOUT_8821C[] = {
	/* { mode, hq_num, nq_num, lq_num, exq_num, gap_num} */
	{HALMAC_TRX_MODE_NORMAL, 16, 16, 16, 14, 1},
	{HALMAC_TRX_MODE_TRXSHARE, 16, 16, 16, 14, 1},
	{HALMAC_TRX_MODE_WMM, 16, 16, 16, 14, 1},
	{HALMAC_TRX_MODE_P2P, 16, 16, 16, 14, 1},
	{HALMAC_TRX_MODE_LOOPBACK, 16, 16, 16, 14, 1},
	{HALMAC_TRX_MODE_DELAY_LOOPBACK, 16, 16, 16, 14, 1},
};

static enum halmac_ret_status
txdma_queue_mapping_8821c(struct halmac_adapter *adapter,
			  enum halmac_trx_mode mode);

static enum halmac_ret_status
priority_queue_cfg_8821c(struct halmac_adapter *adapter,
			 enum halmac_trx_mode mode);

static enum halmac_ret_status
set_trx_fifo_info_8821c(struct halmac_adapter *adapter,
			enum halmac_trx_mode mode);

enum halmac_ret_status
mount_api_8821c(struct halmac_adapter *adapter)
{
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;

	adapter->chip_id = HALMAC_CHIP_ID_8821C;
	adapter->hw_cfg_info.efuse_size = EFUSE_SIZE_8821C;
	adapter->hw_cfg_info.eeprom_size = EEPROM_SIZE_8821C;
	adapter->hw_cfg_info.bt_efuse_size = BT_EFUSE_SIZE_8821C;
	adapter->hw_cfg_info.prtct_efuse_size = PRTCT_EFUSE_SIZE_8821C;
	adapter->hw_cfg_info.cam_entry_num = SEC_CAM_NUM_8821C;
	adapter->hw_cfg_info.tx_fifo_size = TX_FIFO_SIZE_8821C;
	adapter->hw_cfg_info.rx_fifo_size = RX_FIFO_SIZE_8821C;
	adapter->hw_cfg_info.ac_oqt_size = OQT_ENTRY_AC_8821C;
	adapter->hw_cfg_info.non_ac_oqt_size = OQT_ENTRY_NOAC_8821C;
	adapter->hw_cfg_info.usb_txagg_num = BLK_DESC_NUM;
	adapter->txff_alloc.rsvd_drv_pg_num = RSVD_PG_DRV_NUM;

	api->halmac_init_trx_cfg = init_trx_cfg_8821c;
	api->halmac_init_system_cfg = init_system_cfg_8821c;
	api->halmac_init_protocol_cfg = init_protocol_cfg_8821c;
	api->halmac_init_h2c = init_h2c_8821c;
	api->halmac_pinmux_get_func = pinmux_get_func_8821c;
	api->halmac_pinmux_set_func = pinmux_set_func_8821c;
	api->halmac_pinmux_free_func = pinmux_free_func_8821c;
	api->halmac_get_hw_value = get_hw_value_8821c;
	api->halmac_set_hw_value = set_hw_value_8821c;
	api->halmac_cfg_drv_info = cfg_drv_info_8821c;
	api->halmac_fill_txdesc_checksum = fill_txdesc_check_sum_8821c;
	api->halmac_init_low_pwr = init_low_pwr_8821c;
	api->halmac_pre_init_system_cfg = pre_init_system_cfg_8821c;

	api->halmac_init_wmac_cfg = init_wmac_cfg_8821c;
	api->halmac_init_edca_cfg = init_edca_cfg_8821c;

	if (adapter->intf == HALMAC_INTERFACE_SDIO) {
#if HALMAC_SDIO_SUPPORT
		api->halmac_init_interface_cfg = init_sdio_cfg_8821c;
		api->halmac_init_sdio_cfg = init_sdio_cfg_8821c;
		api->halmac_mac_power_switch = mac_pwr_switch_sdio_8821c;
		api->halmac_phy_cfg = phy_cfg_sdio_8821c;
		api->halmac_pcie_switch = pcie_switch_sdio_8821c;
		api->halmac_interface_integration_tuning = intf_tun_sdio_8821c;
		api->halmac_tx_allowed_sdio = tx_allowed_sdio_8821c;
		api->halmac_get_sdio_tx_addr = get_sdio_tx_addr_8821c;
		api->halmac_reg_read_8 = reg_r8_sdio_8821c;
		api->halmac_reg_write_8 = reg_w8_sdio_8821c;
		api->halmac_reg_read_16 = reg_r16_sdio_8821c;
		api->halmac_reg_write_16 = reg_w16_sdio_8821c;
		api->halmac_reg_read_32 = reg_r32_sdio_8821c;
		api->halmac_reg_write_32 = reg_w32_sdio_8821c;

		adapter->sdio_fs.macid_map_size = MACID_MAX_8821C * 2;
		if (!adapter->sdio_fs.macid_map) {
			adapter->sdio_fs.macid_map =
			(u8 *)PLTFM_MALLOC(adapter->sdio_fs.macid_map_size);
			if (!adapter->sdio_fs.macid_map)
				PLTFM_MSG_ERR("[ERR]mac id map malloc!!\n");
		}
#endif
	} else if (adapter->intf == HALMAC_INTERFACE_USB) {
#if HALMAC_USB_SUPPORT
		api->halmac_mac_power_switch = mac_pwr_switch_usb_8821c;
		api->halmac_phy_cfg = phy_cfg_usb_8821c;
		api->halmac_pcie_switch = pcie_switch_usb_8821c;
		api->halmac_interface_integration_tuning = intf_tun_usb_8821c;
#endif
	} else if (adapter->intf == HALMAC_INTERFACE_PCIE) {
#if HALMAC_PCIE_SUPPORT
		api->halmac_mac_power_switch = mac_pwr_switch_pcie_8821c;
		api->halmac_phy_cfg = phy_cfg_pcie_8821c;
		api->halmac_pcie_switch = pcie_switch_8821c;
		api->halmac_interface_integration_tuning = intf_tun_pcie_8821c;
		api->halmac_cfgspc_set_pcie = cfgspc_set_pcie_8821c;
#endif
	} else {
		PLTFM_MSG_ERR("[ERR]Undefined IC\n");
		return HALMAC_RET_CHIP_NOT_SUPPORT;
	}

	return HALMAC_RET_SUCCESS;
}

/**
 * init_trx_cfg_8821c() - config trx dma register
 * @adapter : the adapter of halmac
 * @mode : trx mode selection
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
init_trx_cfg_8821c(struct halmac_adapter *adapter, enum halmac_trx_mode mode)
{
	u8 value8;
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;
	u8 en_fwff;
	u16 value16;

	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	adapter->trx_mode = mode;

	status = txdma_queue_mapping_8821c(adapter, mode);
	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]queue mapping\n");
		return status;
	}

	en_fwff = HALMAC_REG_R8(REG_WMAC_FWPKT_CR) & BIT_FWEN;
	if (en_fwff) {
		HALMAC_REG_W8_CLR(REG_WMAC_FWPKT_CR, BIT_FWEN);
		if (fwff_is_empty_88xx(adapter) != HALMAC_RET_SUCCESS)
			PLTFM_MSG_ERR("[ERR]fwff is not empty\n");
	}
	value8 = 0;
	HALMAC_REG_W8(REG_CR, value8);
	value16 = HALMAC_REG_R16(REG_FWFF_PKT_INFO);
	HALMAC_REG_W16(REG_FWFF_CTRL, value16);

	value8 = MAC_TRX_ENABLE;
	HALMAC_REG_W8(REG_CR, value8);
	if (en_fwff)
		HALMAC_REG_W8_SET(REG_WMAC_FWPKT_CR, BIT_FWEN);

	HALMAC_REG_W32(REG_H2CQ_CSR, BIT(31));

	status = priority_queue_cfg_8821c(adapter, mode);
	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]priority queue cfg\n");
		return status;
	}

	if (adapter->txff_alloc.rx_fifo_exp_mode !=
	    HALMAC_RX_FIFO_EXPANDING_MODE_DISABLE)
		HALMAC_REG_W8(REG_RX_DRVINFO_SZ, RX_DESC_DUMMY_SIZE_8821C >> 3);

	status = init_h2c_8821c(adapter);
	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]init h2cq!\n");
		return status;
	}

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

static enum halmac_ret_status
txdma_queue_mapping_8821c(struct halmac_adapter *adapter,
			  enum halmac_trx_mode mode)
{
	u16 value16;
	struct halmac_rqpn *cur_rqpn_sel = NULL;
	enum halmac_ret_status status;
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;

	if (adapter->intf == HALMAC_INTERFACE_SDIO) {
		cur_rqpn_sel = HALMAC_RQPN_SDIO_8821C;
	} else if (adapter->intf == HALMAC_INTERFACE_PCIE) {
		cur_rqpn_sel = HALMAC_RQPN_PCIE_8821C;
	} else if (adapter->intf == HALMAC_INTERFACE_USB) {
		if (adapter->bulkout_num == 2) {
			cur_rqpn_sel = HALMAC_RQPN_2BULKOUT_8821C;
		} else if (adapter->bulkout_num == 3) {
			cur_rqpn_sel = HALMAC_RQPN_3BULKOUT_8821C;
		} else if (adapter->bulkout_num == 4) {
			cur_rqpn_sel = HALMAC_RQPN_4BULKOUT_8821C;
		} else {
			PLTFM_MSG_ERR("[ERR]invalid intf\n");
			return HALMAC_RET_NOT_SUPPORT;
		}
	} else {
		return HALMAC_RET_NOT_SUPPORT;
	}

	status = rqpn_parser_88xx(adapter, mode, cur_rqpn_sel);
	if (status != HALMAC_RET_SUCCESS)
		return status;

	value16 = 0;
	value16 |= BIT_TXDMA_HIQ_MAP(adapter->pq_map[HALMAC_PQ_MAP_HI]);
	value16 |= BIT_TXDMA_MGQ_MAP(adapter->pq_map[HALMAC_PQ_MAP_MG]);
	value16 |= BIT_TXDMA_BKQ_MAP(adapter->pq_map[HALMAC_PQ_MAP_BK]);
	value16 |= BIT_TXDMA_BEQ_MAP(adapter->pq_map[HALMAC_PQ_MAP_BE]);
	value16 |= BIT_TXDMA_VIQ_MAP(adapter->pq_map[HALMAC_PQ_MAP_VI]);
	value16 |= BIT_TXDMA_VOQ_MAP(adapter->pq_map[HALMAC_PQ_MAP_VO]);
	HALMAC_REG_W16(REG_TXDMA_PQ_MAP, value16);

	return HALMAC_RET_SUCCESS;
}

static enum halmac_ret_status
priority_queue_cfg_8821c(struct halmac_adapter *adapter,
			 enum halmac_trx_mode mode)
{
	u8 transfer_mode = 0;
	u8 value8;
	u32 cnt;
	struct halmac_txff_allocation *txff_info = &adapter->txff_alloc;
	enum halmac_ret_status status;
	struct halmac_pg_num *cur_pg_num = NULL;
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;

	status = set_trx_fifo_info_8821c(adapter, mode);
	if (status != HALMAC_RET_SUCCESS) {
		PLTFM_MSG_ERR("[ERR]set trx fifo!!\n");
		return status;
	}

	if (adapter->intf == HALMAC_INTERFACE_SDIO) {
		cur_pg_num = HALMAC_PG_NUM_SDIO_8821C;
	} else if (adapter->intf == HALMAC_INTERFACE_PCIE) {
		cur_pg_num = HALMAC_PG_NUM_PCIE_8821C;
	} else if (adapter->intf == HALMAC_INTERFACE_USB) {
		if (adapter->bulkout_num == 2) {
			cur_pg_num = HALMAC_PG_NUM_2BULKOUT_8821C;
		} else if (adapter->bulkout_num == 3) {
			cur_pg_num = HALMAC_PG_NUM_3BULKOUT_8821C;
		} else if (adapter->bulkout_num == 4) {
			cur_pg_num = HALMAC_PG_NUM_4BULKOUT_8821C;
		} else {
			PLTFM_MSG_ERR("[ERR]invalid intf\n");
			return HALMAC_RET_NOT_SUPPORT;
		}
	} else {
		return HALMAC_RET_NOT_SUPPORT;
	}

	status = pg_num_parser_88xx(adapter, mode, cur_pg_num);
	if (status != HALMAC_RET_SUCCESS)
		return status;

	PLTFM_MSG_TRACE("[TRACE]Set FIFO page\n");

	HALMAC_REG_W16(REG_FIFOPAGE_INFO_1, txff_info->high_queue_pg_num);
	HALMAC_REG_W16(REG_FIFOPAGE_INFO_2, txff_info->low_queue_pg_num);
	HALMAC_REG_W16(REG_FIFOPAGE_INFO_3, txff_info->normal_queue_pg_num);
	HALMAC_REG_W16(REG_FIFOPAGE_INFO_4, txff_info->extra_queue_pg_num);
	HALMAC_REG_W16(REG_FIFOPAGE_INFO_5, txff_info->pub_queue_pg_num);
	HALMAC_REG_W32_SET(REG_RQPN_CTRL_2, BIT(31));

	adapter->sdio_fs.hiq_pg_num = txff_info->high_queue_pg_num;
	adapter->sdio_fs.miq_pg_num = txff_info->normal_queue_pg_num;
	adapter->sdio_fs.lowq_pg_num = txff_info->low_queue_pg_num;
	adapter->sdio_fs.pubq_pg_num = txff_info->pub_queue_pg_num;
	adapter->sdio_fs.exq_pg_num = txff_info->extra_queue_pg_num;

	HALMAC_REG_W16(REG_FIFOPAGE_CTRL_2, txff_info->rsvd_boundary);
	HALMAC_REG_W8_SET(REG_FWHW_TXQ_CTRL + 2, BIT(4));

	/*20170411 Soar*/
	/* SDIO sometimes use two CMD52 to do HALMAC_REG_W16 */
	/* and may cause a mismatch between HW status and Reg value. */
	/* A patch is to write high byte first, suggested by Argis */
	if (adapter->intf == HALMAC_INTERFACE_SDIO) {
		value8 = (u8)(txff_info->rsvd_boundary >> 8 & 0xFF);
		HALMAC_REG_W8(REG_BCNQ_BDNY_V1 + 1, value8);
		value8 = (u8)(txff_info->rsvd_boundary & 0xFF);
		HALMAC_REG_W8(REG_BCNQ_BDNY_V1, value8);
	} else {
		HALMAC_REG_W16(REG_BCNQ_BDNY_V1, txff_info->rsvd_boundary);
	}

	HALMAC_REG_W16(REG_FIFOPAGE_CTRL_2 + 2, txff_info->rsvd_boundary);

	/*20170411 Soar*/
	/* SDIO sometimes use two CMD52 to do HALMAC_REG_W16 */
	/* and may cause a mismatch between HW status and Reg value. */
	/* A patch is to write high byte first, suggested by Argis */
	if (adapter->intf == HALMAC_INTERFACE_SDIO) {
		value8 = (u8)(txff_info->rsvd_boundary >> 8 & 0xFF);
		HALMAC_REG_W8(REG_BCNQ1_BDNY_V1 + 1, value8);
		value8 = (u8)(txff_info->rsvd_boundary & 0xFF);
		HALMAC_REG_W8(REG_BCNQ1_BDNY_V1, value8);
	} else {
		HALMAC_REG_W16(REG_BCNQ1_BDNY_V1, txff_info->rsvd_boundary);
	}

	HALMAC_REG_W32(REG_RXFF_BNDY,
		       adapter->hw_cfg_info.rx_fifo_size -
		       C2H_PKT_BUF_88XX - 1);

	if (adapter->intf == HALMAC_INTERFACE_USB) {
		value8 = HALMAC_REG_R8(REG_AUTO_LLT_V1);
		value8 &= ~(BIT_MASK_BLK_DESC_NUM << BIT_SHIFT_BLK_DESC_NUM);
		value8 |= (BLK_DESC_NUM << BIT_SHIFT_BLK_DESC_NUM);
		HALMAC_REG_W8(REG_AUTO_LLT_V1, value8);

		HALMAC_REG_W8(REG_AUTO_LLT_V1 + 3, BLK_DESC_NUM);
		HALMAC_REG_W8_SET(REG_TXDMA_OFFSET_CHK + 1, BIT(1));
	}

	HALMAC_REG_W8_SET(REG_AUTO_LLT_V1, BIT_AUTO_INIT_LLT_V1);
	cnt = 1000;
	while (HALMAC_REG_R8(REG_AUTO_LLT_V1) & BIT_AUTO_INIT_LLT_V1) {
		cnt--;
		if (cnt == 0)
			return HALMAC_RET_INIT_LLT_FAIL;
	}

	if (mode == HALMAC_TRX_MODE_DELAY_LOOPBACK) {
		transfer_mode = HALMAC_TRNSFER_LOOPBACK_DELAY;
		HALMAC_REG_W16(REG_WMAC_LBK_BUF_HD_V1,
			       adapter->txff_alloc.rsvd_boundary);
	} else if (mode == HALMAC_TRX_MODE_LOOPBACK) {
		transfer_mode = HALMAC_TRNSFER_LOOPBACK_DIRECT;
	} else {
		transfer_mode = HALMAC_TRNSFER_NORMAL;
	}

	adapter->hw_cfg_info.trx_mode = transfer_mode;
	HALMAC_REG_W8(REG_CR + 3, (u8)transfer_mode);

	return HALMAC_RET_SUCCESS;
}

static enum halmac_ret_status
set_trx_fifo_info_8821c(struct halmac_adapter *adapter,
			enum halmac_trx_mode mode)
{
	u16 cur_pg_addr;
	u32 txff_size = TX_FIFO_SIZE_8821C;
	u32 rxff_size = RX_FIFO_SIZE_8821C;
	struct halmac_txff_allocation *info = &adapter->txff_alloc;

	if (info->rx_fifo_exp_mode == HALMAC_RX_FIFO_EXPANDING_MODE_1_BLOCK) {
		txff_size = TX_FIFO_SIZE_RX_EXPAND_1BLK_8821C;
		rxff_size = RX_FIFO_SIZE_RX_EXPAND_1BLK_8821C;
	}

	if (info->la_mode != HALMAC_LA_MODE_DISABLE) {
		txff_size = TX_FIFO_SIZE_LA_8821C;
		rxff_size = RX_FIFO_SIZE_8821C;
	}

	adapter->hw_cfg_info.tx_fifo_size = txff_size;
	adapter->hw_cfg_info.rx_fifo_size = rxff_size;
	info->tx_fifo_pg_num = (u16)(txff_size >> TX_PAGE_SIZE_SHIFT_88XX);

	info->rsvd_pg_num = info->rsvd_drv_pg_num +
					RSVD_PG_H2C_EXTRAINFO_NUM +
					RSVD_PG_H2C_STATICINFO_NUM +
					RSVD_PG_H2CQ_NUM +
					RSVD_PG_CPU_INSTRUCTION_NUM +
					RSVD_PG_FW_TXBUF_NUM +
					RSVD_PG_CSIBUF_NUM;

	if (mode == HALMAC_TRX_MODE_DELAY_LOOPBACK)
		info->rsvd_pg_num += RSVD_PG_DLLB_NUM;

	if (info->rsvd_pg_num > info->tx_fifo_pg_num)
		return HALMAC_RET_CFG_TXFIFO_PAGE_FAIL;

	info->acq_pg_num = info->tx_fifo_pg_num - info->rsvd_pg_num;
	info->rsvd_boundary = info->tx_fifo_pg_num - info->rsvd_pg_num;

	cur_pg_addr = info->tx_fifo_pg_num;
	cur_pg_addr -= RSVD_PG_CSIBUF_NUM;
	info->rsvd_csibuf_addr = cur_pg_addr;
	cur_pg_addr -= RSVD_PG_FW_TXBUF_NUM;
	info->rsvd_fw_txbuf_addr = cur_pg_addr;
	cur_pg_addr -= RSVD_PG_CPU_INSTRUCTION_NUM;
	info->rsvd_cpu_instr_addr = cur_pg_addr;
	cur_pg_addr -= RSVD_PG_H2CQ_NUM;
	info->rsvd_h2cq_addr = cur_pg_addr;
	cur_pg_addr -= RSVD_PG_H2C_STATICINFO_NUM;
	info->rsvd_h2c_sta_info_addr = cur_pg_addr;
	cur_pg_addr -= RSVD_PG_H2C_EXTRAINFO_NUM;
	info->rsvd_h2c_info_addr = cur_pg_addr;
	cur_pg_addr -= info->rsvd_drv_pg_num;
	info->rsvd_drv_addr = cur_pg_addr;

	if (mode == HALMAC_TRX_MODE_DELAY_LOOPBACK)
		info->rsvd_drv_addr -= RSVD_PG_DLLB_NUM;

	if (info->rsvd_boundary != info->rsvd_drv_addr)
		return HALMAC_RET_CFG_TXFIFO_PAGE_FAIL;

	return HALMAC_RET_SUCCESS;
}

/**
 * init_system_cfg_8821c() -  init system config
 * @adapter : the adapter of halmac
 * Author : Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
init_system_cfg_8821c(struct halmac_adapter *adapter)
{
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;
	u32 tmp = 0;
	u32 value32;
	enum halmac_ret_status status;
	u8 hwval;
	u8 value8;

	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	if (adapter->intf == HALMAC_INTERFACE_PCIE) {
		hwval = 1;
		status = set_hw_value_8821c(adapter, HALMAC_HW_PCIE_REF_AUTOK,
					    &hwval);
		if (status != HALMAC_RET_SUCCESS)
			return status;
	}

	value32 = HALMAC_REG_R32(REG_CPU_DMEM_CON) | BIT_WL_PLATFORM_RST;
	HALMAC_REG_W32(REG_CPU_DMEM_CON, value32);

	value8 = HALMAC_REG_R8(REG_SYS_FUNC_EN + 1) | SYS_FUNC_EN;
	HALMAC_REG_W8(REG_SYS_FUNC_EN + 1, value8);

	/*disable boot-from-flash for driver's DL FW*/
	tmp = HALMAC_REG_R32(REG_MCUFW_CTRL);
	if (tmp & BIT_BOOT_FSPI_EN) {
		HALMAC_REG_W32(REG_MCUFW_CTRL, tmp & (~BIT_BOOT_FSPI_EN));
		value32 = HALMAC_REG_R32(REG_GPIO_MUXCFG) & (~BIT_FSPI_EN);
		HALMAC_REG_W32(REG_GPIO_MUXCFG, value32);
	}

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * init_protocol_cfg_8821c() - config protocol register
 * @adapter : the adapter of halmac
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
init_protocol_cfg_8821c(struct halmac_adapter *adapter)
{
	u16 pre_txcnt;
	u32 max_agg_num;
	u32 max_rts_agg_num;
	u32 value32;
	u8 value8;
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;

	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	HALMAC_REG_W8(REG_AMPDU_MAX_TIME_V1, WLAN_AMPDU_MAX_TIME);
	HALMAC_REG_W8_SET(REG_TX_HANG_CTRL, BIT_EN_EOF_V1);

	pre_txcnt = WLAN_PRE_TXCNT_TIME_TH | BIT_EN_PRECNT;
	HALMAC_REG_W8(REG_PRECNT_CTRL, (u8)(pre_txcnt & 0xFF));
	HALMAC_REG_W8(REG_PRECNT_CTRL + 1, (u8)(pre_txcnt >> 8));

	max_agg_num = WLAN_MAX_AGG_PKT_LIMIT;
	max_rts_agg_num = WLAN_RTS_MAX_AGG_PKT_LIMIT;

	if (adapter->intf == HALMAC_INTERFACE_SDIO) {
		max_agg_num = WLAN_MAX_AGG_PKT_LIMIT_SDIO;
		max_rts_agg_num = WLAN_RTS_MAX_AGG_PKT_LIMIT_SDIO;
	}

	value32 = WLAN_RTS_LEN_TH | (WLAN_RTS_TX_TIME_TH << 8) |
				(max_agg_num << 16) | (max_rts_agg_num << 24);
	HALMAC_REG_W32(REG_PROT_MODE_CTRL, value32);

	HALMAC_REG_W16(REG_BAR_MODE_CTRL + 2,
		       WLAN_BAR_RETRY_LIMIT | WLAN_RA_TRY_RATE_AGG_LIMIT << 8);

	HALMAC_REG_W8(REG_FAST_EDCA_VOVI_SETTING, WALN_FAST_EDCA_VO_TH);
	HALMAC_REG_W8(REG_FAST_EDCA_VOVI_SETTING + 2, WLAN_FAST_EDCA_VI_TH);
	HALMAC_REG_W8(REG_FAST_EDCA_BEBK_SETTING, WLAN_FAST_EDCA_BE_TH);
	HALMAC_REG_W8(REG_FAST_EDCA_BEBK_SETTING + 2, WLAN_FAST_EDCA_BK_TH);

	value8 = HALMAC_REG_R8(REG_INIRTS_RATE_SEL);
	HALMAC_REG_W8(REG_INIRTS_RATE_SEL, value8 | BIT(5));

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * init_h2c_8821c() - config h2c packet buffer
 * @adapter : the adapter of halmac
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
init_h2c_8821c(struct halmac_adapter *adapter)
{
	u8 value8;
	u32 value32;
	u32 h2cq_addr;
	u32 h2cq_size;
	struct halmac_txff_allocation *txff_info = &adapter->txff_alloc;
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;

	h2cq_addr = txff_info->rsvd_h2cq_addr << TX_PAGE_SIZE_SHIFT_88XX;
	h2cq_size = RSVD_PG_H2CQ_NUM << TX_PAGE_SIZE_SHIFT_88XX;

	value32 = HALMAC_REG_R32(REG_H2C_HEAD);
	value32 = (value32 & 0xFFFC0000) | h2cq_addr;
	HALMAC_REG_W32(REG_H2C_HEAD, value32);

	value32 = HALMAC_REG_R32(REG_H2C_READ_ADDR);
	value32 = (value32 & 0xFFFC0000) | h2cq_addr;
	HALMAC_REG_W32(REG_H2C_READ_ADDR, value32);

	value32 = HALMAC_REG_R32(REG_H2C_TAIL);
	value32 &= 0xFFFC0000;
	value32 |= (h2cq_addr + h2cq_size);
	HALMAC_REG_W32(REG_H2C_TAIL, value32);

	value8 = HALMAC_REG_R8(REG_H2C_INFO);
	value8 = (u8)((value8 & 0xFC) | 0x01);
	HALMAC_REG_W8(REG_H2C_INFO, value8);

	value8 = HALMAC_REG_R8(REG_H2C_INFO);
	value8 = (u8)((value8 & 0xFB) | 0x04);
	HALMAC_REG_W8(REG_H2C_INFO, value8);

	value8 = HALMAC_REG_R8(REG_TXDMA_OFFSET_CHK + 1);
	value8 = (u8)((value8 & 0x7f) | 0x80);
	HALMAC_REG_W8(REG_TXDMA_OFFSET_CHK + 1, value8);

	adapter->h2c_info.buf_size = h2cq_size;
	get_h2c_buf_free_space_88xx(adapter);

	if (adapter->h2c_info.buf_size != adapter->h2c_info.buf_fs) {
		PLTFM_MSG_ERR("[ERR]get h2c free space error!\n");
		return HALMAC_RET_GET_H2C_SPACE_ERR;
	}

	PLTFM_MSG_TRACE("[TRACE]h2c fs : %d\n", adapter->h2c_info.buf_fs);

	return HALMAC_RET_SUCCESS;
}

/**
 * init_edca_cfg_8821c() - init EDCA config
 * @adapter : the adapter of halmac
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
init_edca_cfg_8821c(struct halmac_adapter *adapter)
{
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;

	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	/* Init SYNC_CLI_SEL : reg 0x5B4[6:4] = 0 */
	HALMAC_REG_W8_CLR(REG_TIMER0_SRC_SEL, BIT(4) | BIT(5) | BIT(6));

	/* Clear TX pause */
	HALMAC_REG_W16(REG_TXPAUSE, 0x0000);

	HALMAC_REG_W8(REG_SLOT, WLAN_SLOT_TIME);
	HALMAC_REG_W8(REG_PIFS, WLAN_PIFS_TIME);
	HALMAC_REG_W32(REG_SIFS, WLAN_SIFS_CFG);

	HALMAC_REG_W16(REG_EDCA_VO_PARAM + 2, WLAN_VO_TXOP_LIMIT);
	HALMAC_REG_W16(REG_EDCA_VI_PARAM + 2, WLAN_VI_TXOP_LIMIT);

	HALMAC_REG_W32(REG_RD_NAV_NXT, WLAN_NAV_CFG);
	HALMAC_REG_W16(REG_RXTSF_OFFSET_CCK, WLAN_RX_TSF_CFG);

	/* Set beacon cotnrol - enable TSF and other related functions */
	HALMAC_REG_W8(REG_BCN_CTRL, (u8)(HALMAC_REG_R8(REG_BCN_CTRL) |
					  BIT_EN_BCN_FUNCTION));

	/* Set send beacon related registers */
	HALMAC_REG_W32(REG_TBTT_PROHIBIT, WLAN_TBTT_TIME);
	HALMAC_REG_W8(REG_DRVERLYINT, WLAN_DRV_EARLY_INT);
	HALMAC_REG_W8(REG_BCNDMATIM, WLAN_BCN_DMA_TIME);

	HALMAC_REG_W8_CLR(REG_TX_PTCL_CTRL + 1, BIT(4));

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * init_wmac_cfg_8821c() - init wmac config
 * @adapter : the adapter of halmac
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
init_wmac_cfg_8821c(struct halmac_adapter *adapter)
{
	u8 value8;
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;
	enum halmac_ret_status status = HALMAC_RET_SUCCESS;

	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	HALMAC_REG_W32(REG_RXFLTMAP0, WLAN_RX_FILTER0);
	HALMAC_REG_W16(REG_RXFLTMAP2, WLAN_RX_FILTER2);

	HALMAC_REG_W32(REG_RCR, WLAN_RCR_CFG);

	HALMAC_REG_W8(REG_RX_PKT_LIMIT, WLAN_RXPKT_MAX_SZ_512);

	HALMAC_REG_W8(REG_TCR + 2, WLAN_TX_FUNC_CFG2);
	HALMAC_REG_W8(REG_TCR + 1, WLAN_TX_FUNC_CFG1);

	HALMAC_REG_W8(REG_ACKTO_CCK, WLAN_ACK_TO_CCK);

	HALMAC_REG_W8_SET(REG_WMAC_TRXPTCL_CTL_H, BIT_EN_TXCTS_IN_RXNAV_V1);

	HALMAC_REG_W8_SET(REG_SND_PTCL_CTRL, BIT_R_DISABLE_CHECK_VHTSIGB_CRC);

	HALMAC_REG_W32(REG_WMAC_OPTION_FUNCTION_2, WLAN_MAC_OPT_FUNC2);

	if (adapter->hw_cfg_info.trx_mode == HALMAC_TRNSFER_NORMAL)
		value8 = WLAN_MAC_OPT_NORM_FUNC1;
	else
		value8 = WLAN_MAC_OPT_LB_FUNC1;

	HALMAC_REG_W8(REG_WMAC_OPTION_FUNCTION + 4, value8);

	status = api->halmac_init_low_pwr(adapter);
	if (status != HALMAC_RET_SUCCESS)
		return status;

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

/**
 * pre_init_system_cfg_8821c() - pre-init system config
 * @adapter : the adapter of halmac
 * Author : KaiYuan Chang/Ivan Lin
 * Return : enum halmac_ret_status
 * More details of status code can be found in prototype document
 */
enum halmac_ret_status
pre_init_system_cfg_8821c(struct halmac_adapter *adapter)
{
	u32 value32;
	struct halmac_api *api = (struct halmac_api *)adapter->halmac_api;
	u8 enable_bb;

	PLTFM_MSG_TRACE("[TRACE]%s ===>\n", __func__);

	HALMAC_REG_W8(REG_RSV_CTRL, 0);

	if (adapter->intf == HALMAC_INTERFACE_SDIO) {
#if HALMAC_SDIO_SUPPORT
		if (leave_sdio_suspend_88xx(adapter) != HALMAC_RET_SUCCESS)
			return HALMAC_RET_SDIO_LEAVE_SUSPEND_FAIL;
#endif
	} else if (adapter->intf == HALMAC_INTERFACE_USB) {
#if HALMAC_USB_SUPPORT
		if (HALMAC_REG_R8(REG_SYS_CFG2 + 3) == 0x20)
			HALMAC_REG_W8(0xFE5B, HALMAC_REG_R8(0xFE5B) | BIT(4));
#endif
	} else if (adapter->intf == HALMAC_INTERFACE_PCIE) {
#if HALMAC_PCIE_SUPPORT
		/* For PCIE power on fail issue */
		HALMAC_REG_W8(REG_HCI_OPT_CTRL + 1,
			      HALMAC_REG_R8(REG_HCI_OPT_CTRL + 1) | BIT(0));
#endif
	}

	/* Config PIN Mux */
	value32 = HALMAC_REG_R32(REG_PAD_CTRL1);
	value32 = value32 & (~(BIT(28) | BIT(29)));
	value32 = value32 | BIT(28) | BIT(29);
	HALMAC_REG_W32(REG_PAD_CTRL1, value32);

	value32 = HALMAC_REG_R32(REG_LED_CFG);
	value32 = value32 & (~(BIT(25) | BIT(26)));
	HALMAC_REG_W32(REG_LED_CFG, value32);

	value32 = HALMAC_REG_R32(REG_GPIO_MUXCFG);
	value32 = value32 & (~(BIT(2)));
	value32 = value32 | BIT(2);
	HALMAC_REG_W32(REG_GPIO_MUXCFG, value32);

	enable_bb = 0;
	set_hw_value_88xx(adapter, HALMAC_HW_EN_BB_RF, &enable_bb);

	if (HALMAC_REG_R8(REG_SYS_CFG1 + 2) & BIT(4)) {
		PLTFM_MSG_ERR("[ERR]test mode!!\n");
		return HALMAC_RET_WLAN_MODE_FAIL;
	}

	PLTFM_MSG_TRACE("[TRACE]%s <===\n", __func__);

	return HALMAC_RET_SUCCESS;
}

#endif /* HALMAC_8821C_SUPPORT */
