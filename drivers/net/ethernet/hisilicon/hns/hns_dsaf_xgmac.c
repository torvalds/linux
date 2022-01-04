// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2014-2015 Hisilicon Limited.
 */

#include <linux/io-64-nonatomic-hi-lo.h>
#include <linux/of_mdio.h>
#include "hns_dsaf_main.h"
#include "hns_dsaf_mac.h"
#include "hns_dsaf_xgmac.h"
#include "hns_dsaf_reg.h"

static const struct mac_stats_string g_xgmac_stats_string[] = {
	{"xgmac_tx_bad_pkts_minto64", MAC_STATS_FIELD_OFF(tx_fragment_err)},
	{"xgmac_tx_good_pkts_minto64", MAC_STATS_FIELD_OFF(tx_undersize)},
	{"xgmac_tx_total_pkts_minto64",	MAC_STATS_FIELD_OFF(tx_under_min_pkts)},
	{"xgmac_tx_pkts_64", MAC_STATS_FIELD_OFF(tx_64bytes)},
	{"xgmac_tx_pkts_65to127", MAC_STATS_FIELD_OFF(tx_65to127)},
	{"xgmac_tx_pkts_128to255", MAC_STATS_FIELD_OFF(tx_128to255)},
	{"xgmac_tx_pkts_256to511", MAC_STATS_FIELD_OFF(tx_256to511)},
	{"xgmac_tx_pkts_512to1023", MAC_STATS_FIELD_OFF(tx_512to1023)},
	{"xgmac_tx_pkts_1024to1518", MAC_STATS_FIELD_OFF(tx_1024to1518)},
	{"xgmac_tx_pkts_1519tomax", MAC_STATS_FIELD_OFF(tx_1519tomax)},
	{"xgmac_tx_good_pkts_1519tomax",
		MAC_STATS_FIELD_OFF(tx_1519tomax_good)},
	{"xgmac_tx_good_pkts_untralmax", MAC_STATS_FIELD_OFF(tx_oversize)},
	{"xgmac_tx_bad_pkts_untralmax", MAC_STATS_FIELD_OFF(tx_jabber_err)},
	{"xgmac_tx_good_pkts_all", MAC_STATS_FIELD_OFF(tx_good_pkts)},
	{"xgmac_tx_good_byte_all", MAC_STATS_FIELD_OFF(tx_good_bytes)},
	{"xgmac_tx_total_pkt", MAC_STATS_FIELD_OFF(tx_total_pkts)},
	{"xgmac_tx_total_byt", MAC_STATS_FIELD_OFF(tx_total_bytes)},
	{"xgmac_tx_uc_pkt", MAC_STATS_FIELD_OFF(tx_uc_pkts)},
	{"xgmac_tx_mc_pkt", MAC_STATS_FIELD_OFF(tx_mc_pkts)},
	{"xgmac_tx_bc_pkt", MAC_STATS_FIELD_OFF(tx_bc_pkts)},
	{"xgmac_tx_pause_frame_num", MAC_STATS_FIELD_OFF(tx_pfc_tc0)},
	{"xgmac_tx_pfc_per_1pause_framer", MAC_STATS_FIELD_OFF(tx_pfc_tc1)},
	{"xgmac_tx_pfc_per_2pause_framer", MAC_STATS_FIELD_OFF(tx_pfc_tc2)},
	{"xgmac_tx_pfc_per_3pause_framer", MAC_STATS_FIELD_OFF(tx_pfc_tc3)},
	{"xgmac_tx_pfc_per_4pause_framer", MAC_STATS_FIELD_OFF(tx_pfc_tc4)},
	{"xgmac_tx_pfc_per_5pause_framer", MAC_STATS_FIELD_OFF(tx_pfc_tc5)},
	{"xgmac_tx_pfc_per_6pause_framer", MAC_STATS_FIELD_OFF(tx_pfc_tc6)},
	{"xgmac_tx_pfc_per_7pause_framer", MAC_STATS_FIELD_OFF(tx_pfc_tc7)},
	{"xgmac_tx_mac_ctrol_frame", MAC_STATS_FIELD_OFF(tx_ctrl)},
	{"xgmac_tx_1731_pkts", MAC_STATS_FIELD_OFF(tx_1731_pkts)},
	{"xgmac_tx_1588_pkts", MAC_STATS_FIELD_OFF(tx_1588_pkts)},
	{"xgmac_rx_good_pkt_from_dsaf", MAC_STATS_FIELD_OFF(rx_good_from_sw)},
	{"xgmac_rx_bad_pkt_from_dsaf", MAC_STATS_FIELD_OFF(rx_bad_from_sw)},
	{"xgmac_tx_bad_pkt_64tomax", MAC_STATS_FIELD_OFF(tx_bad_pkts)},

	{"xgmac_rx_bad_pkts_minto64", MAC_STATS_FIELD_OFF(rx_fragment_err)},
	{"xgmac_rx_good_pkts_minto64", MAC_STATS_FIELD_OFF(rx_undersize)},
	{"xgmac_rx_total_pkts_minto64", MAC_STATS_FIELD_OFF(rx_under_min)},
	{"xgmac_rx_pkt_64", MAC_STATS_FIELD_OFF(rx_64bytes)},
	{"xgmac_rx_pkt_65to127", MAC_STATS_FIELD_OFF(rx_65to127)},
	{"xgmac_rx_pkt_128to255", MAC_STATS_FIELD_OFF(rx_128to255)},
	{"xgmac_rx_pkt_256to511", MAC_STATS_FIELD_OFF(rx_256to511)},
	{"xgmac_rx_pkt_512to1023", MAC_STATS_FIELD_OFF(rx_512to1023)},
	{"xgmac_rx_pkt_1024to1518", MAC_STATS_FIELD_OFF(rx_1024to1518)},
	{"xgmac_rx_pkt_1519tomax", MAC_STATS_FIELD_OFF(rx_1519tomax)},
	{"xgmac_rx_good_pkt_1519tomax",	MAC_STATS_FIELD_OFF(rx_1519tomax_good)},
	{"xgmac_rx_good_pkt_untramax", MAC_STATS_FIELD_OFF(rx_oversize)},
	{"xgmac_rx_bad_pkt_untramax", MAC_STATS_FIELD_OFF(rx_jabber_err)},
	{"xgmac_rx_good_pkt", MAC_STATS_FIELD_OFF(rx_good_pkts)},
	{"xgmac_rx_good_byt", MAC_STATS_FIELD_OFF(rx_good_bytes)},
	{"xgmac_rx_pkt", MAC_STATS_FIELD_OFF(rx_total_pkts)},
	{"xgmac_rx_byt", MAC_STATS_FIELD_OFF(rx_total_bytes)},
	{"xgmac_rx_uc_pkt", MAC_STATS_FIELD_OFF(rx_uc_pkts)},
	{"xgmac_rx_mc_pkt", MAC_STATS_FIELD_OFF(rx_mc_pkts)},
	{"xgmac_rx_bc_pkt", MAC_STATS_FIELD_OFF(rx_bc_pkts)},
	{"xgmac_rx_pause_frame_num", MAC_STATS_FIELD_OFF(rx_pfc_tc0)},
	{"xgmac_rx_pfc_per_1pause_frame", MAC_STATS_FIELD_OFF(rx_pfc_tc1)},
	{"xgmac_rx_pfc_per_2pause_frame", MAC_STATS_FIELD_OFF(rx_pfc_tc2)},
	{"xgmac_rx_pfc_per_3pause_frame", MAC_STATS_FIELD_OFF(rx_pfc_tc3)},
	{"xgmac_rx_pfc_per_4pause_frame", MAC_STATS_FIELD_OFF(rx_pfc_tc4)},
	{"xgmac_rx_pfc_per_5pause_frame", MAC_STATS_FIELD_OFF(rx_pfc_tc5)},
	{"xgmac_rx_pfc_per_6pause_frame", MAC_STATS_FIELD_OFF(rx_pfc_tc6)},
	{"xgmac_rx_pfc_per_7pause_frame", MAC_STATS_FIELD_OFF(rx_pfc_tc7)},
	{"xgmac_rx_mac_control", MAC_STATS_FIELD_OFF(rx_unknown_ctrl)},
	{"xgmac_tx_good_pkt_todsaf", MAC_STATS_FIELD_OFF(tx_good_to_sw)},
	{"xgmac_tx_bad_pkt_todsaf", MAC_STATS_FIELD_OFF(tx_bad_to_sw)},
	{"xgmac_rx_1731_pkt", MAC_STATS_FIELD_OFF(rx_1731_pkts)},
	{"xgmac_rx_symbol_err_pkt", MAC_STATS_FIELD_OFF(rx_symbol_err)},
	{"xgmac_rx_fcs_pkt", MAC_STATS_FIELD_OFF(rx_fcs_err)}
};

/**
 *hns_xgmac_tx_enable - xgmac port tx enable
 *@drv: mac driver
 *@value: value of enable
 */
static void hns_xgmac_tx_enable(struct mac_driver *drv, u32 value)
{
	dsaf_set_dev_bit(drv, XGMAC_MAC_ENABLE_REG, XGMAC_ENABLE_TX_B, !!value);
}

/**
 *hns_xgmac_rx_enable - xgmac port rx enable
 *@drv: mac driver
 *@value: value of enable
 */
static void hns_xgmac_rx_enable(struct mac_driver *drv, u32 value)
{
	dsaf_set_dev_bit(drv, XGMAC_MAC_ENABLE_REG, XGMAC_ENABLE_RX_B, !!value);
}

/**
 * hns_xgmac_lf_rf_insert - insert lf rf control about xgmac
 * @mac_drv: mac driver
 * @mode: inserf rf or lf
 */
static void hns_xgmac_lf_rf_insert(struct mac_driver *mac_drv, u32 mode)
{
	dsaf_set_dev_field(mac_drv, XGMAC_MAC_TX_LF_RF_CONTROL_REG,
			   XGMAC_LF_RF_INSERT_M, XGMAC_LF_RF_INSERT_S, mode);
}

/**
 * hns_xgmac_lf_rf_control_init - initial the lf rf control register
 * @mac_drv: mac driver
 */
static void hns_xgmac_lf_rf_control_init(struct mac_driver *mac_drv)
{
	u32 val = 0;

	dsaf_set_bit(val, XGMAC_UNIDIR_EN_B, 0);
	dsaf_set_bit(val, XGMAC_RF_TX_EN_B, 1);
	dsaf_set_field(val, XGMAC_LF_RF_INSERT_M, XGMAC_LF_RF_INSERT_S, 0);
	dsaf_write_dev(mac_drv, XGMAC_MAC_TX_LF_RF_CONTROL_REG, val);
}

/**
 *hns_xgmac_enable - enable xgmac port
 *@mac_drv: mac driver
 *@mode: mode of mac port
 */
static void hns_xgmac_enable(void *mac_drv, enum mac_commom_mode mode)
{
	struct mac_driver *drv = (struct mac_driver *)mac_drv;

	hns_xgmac_lf_rf_insert(drv, HNS_XGMAC_NO_LF_RF_INSERT);

	/*enable XGE rX/tX */
	if (mode == MAC_COMM_MODE_TX) {
		hns_xgmac_tx_enable(drv, 1);
	} else if (mode == MAC_COMM_MODE_RX) {
		hns_xgmac_rx_enable(drv, 1);
	} else if (mode == MAC_COMM_MODE_RX_AND_TX) {
		hns_xgmac_tx_enable(drv, 1);
		hns_xgmac_rx_enable(drv, 1);
	} else {
		dev_err(drv->dev, "error mac mode:%d\n", mode);
	}
}

/**
 *hns_xgmac_disable - disable xgmac port
 *@mac_drv: mac driver
 *@mode: mode of mac port
 */
static void hns_xgmac_disable(void *mac_drv, enum mac_commom_mode mode)
{
	struct mac_driver *drv = (struct mac_driver *)mac_drv;

	if (mode == MAC_COMM_MODE_TX) {
		hns_xgmac_tx_enable(drv, 0);
	} else if (mode == MAC_COMM_MODE_RX) {
		hns_xgmac_rx_enable(drv, 0);
	} else if (mode == MAC_COMM_MODE_RX_AND_TX) {
		hns_xgmac_tx_enable(drv, 0);
		hns_xgmac_rx_enable(drv, 0);
	}
	hns_xgmac_lf_rf_insert(drv, HNS_XGMAC_LF_INSERT);
}

/**
 *hns_xgmac_pma_fec_enable - xgmac PMA FEC enable
 *@drv: mac driver
 *@tx_value: tx value
 *@rx_value: rx value
 *return status
 */
static void hns_xgmac_pma_fec_enable(struct mac_driver *drv, u32 tx_value,
				     u32 rx_value)
{
	u32 origin = dsaf_read_dev(drv, XGMAC_PMA_FEC_CONTROL_REG);

	dsaf_set_bit(origin, XGMAC_PMA_FEC_CTL_TX_B, !!tx_value);
	dsaf_set_bit(origin, XGMAC_PMA_FEC_CTL_RX_B, !!rx_value);
	dsaf_write_dev(drv, XGMAC_PMA_FEC_CONTROL_REG, origin);
}

/* clr exc irq for xge*/
static void hns_xgmac_exc_irq_en(struct mac_driver *drv, u32 en)
{
	u32 clr_vlue = 0xfffffffful;
	u32 msk_vlue = en ? 0xfffffffful : 0; /*1 is en, 0 is dis*/

	dsaf_write_dev(drv, XGMAC_INT_STATUS_REG, clr_vlue);
	dsaf_write_dev(drv, XGMAC_INT_ENABLE_REG, msk_vlue);
}

/**
 *hns_xgmac_init - initialize XGE
 *@mac_drv: mac driver
 */
static void hns_xgmac_init(void *mac_drv)
{
	struct mac_driver *drv = (struct mac_driver *)mac_drv;
	struct dsaf_device *dsaf_dev
		= (struct dsaf_device *)dev_get_drvdata(drv->dev);
	u32 port = drv->mac_id;

	dsaf_dev->misc_op->xge_srst(dsaf_dev, port, 0);
	msleep(100);
	dsaf_dev->misc_op->xge_srst(dsaf_dev, port, 1);

	msleep(100);
	hns_xgmac_lf_rf_control_init(drv);
	hns_xgmac_exc_irq_en(drv, 0);

	hns_xgmac_pma_fec_enable(drv, 0x0, 0x0);

	hns_xgmac_disable(mac_drv, MAC_COMM_MODE_RX_AND_TX);
}

/**
 *hns_xgmac_config_pad_and_crc - set xgmac pad and crc enable the same time
 *@mac_drv: mac driver
 *@newval:enable of pad and crc
 */
static void hns_xgmac_config_pad_and_crc(void *mac_drv, u8 newval)
{
	struct mac_driver *drv = (struct mac_driver *)mac_drv;
	u32 origin = dsaf_read_dev(drv, XGMAC_MAC_CONTROL_REG);

	dsaf_set_bit(origin, XGMAC_CTL_TX_PAD_B, !!newval);
	dsaf_set_bit(origin, XGMAC_CTL_TX_FCS_B, !!newval);
	dsaf_set_bit(origin, XGMAC_CTL_RX_FCS_B, !!newval);
	dsaf_write_dev(drv, XGMAC_MAC_CONTROL_REG, origin);
}

/**
 *hns_xgmac_pausefrm_cfg - set pause param about xgmac
 *@mac_drv: mac driver
 *@rx_en: enable receive
 *@tx_en: enable transmit
 */
static void hns_xgmac_pausefrm_cfg(void *mac_drv, u32 rx_en, u32 tx_en)
{
	struct mac_driver *drv = (struct mac_driver *)mac_drv;
	u32 origin = dsaf_read_dev(drv, XGMAC_MAC_PAUSE_CTRL_REG);

	dsaf_set_bit(origin, XGMAC_PAUSE_CTL_TX_B, !!tx_en);
	dsaf_set_bit(origin, XGMAC_PAUSE_CTL_RX_B, !!rx_en);
	dsaf_write_dev(drv, XGMAC_MAC_PAUSE_CTRL_REG, origin);
}

static void hns_xgmac_set_pausefrm_mac_addr(void *mac_drv, char *mac_addr)
{
	struct mac_driver *drv = (struct mac_driver *)mac_drv;

	u32 high_val = mac_addr[1] | (mac_addr[0] << 8);
	u32 low_val = mac_addr[5] | (mac_addr[4] << 8)
		| (mac_addr[3] << 16) | (mac_addr[2] << 24);
	dsaf_write_dev(drv, XGMAC_MAC_PAUSE_LOCAL_MAC_L_REG, low_val);
	dsaf_write_dev(drv, XGMAC_MAC_PAUSE_LOCAL_MAC_H_REG, high_val);
}

/**
 *hns_xgmac_set_tx_auto_pause_frames - set tx pause param about xgmac
 *@mac_drv: mac driver
 *@enable:enable tx pause param
 */
static void hns_xgmac_set_tx_auto_pause_frames(void *mac_drv, u16 enable)
{
	struct mac_driver *drv = (struct mac_driver *)mac_drv;

	dsaf_set_dev_bit(drv, XGMAC_MAC_PAUSE_CTRL_REG,
			 XGMAC_PAUSE_CTL_TX_B, !!enable);

	/*if enable is not zero ,set tx pause time */
	if (enable)
		dsaf_write_dev(drv, XGMAC_MAC_PAUSE_TIME_REG, enable);
}

/**
 *hns_xgmac_config_max_frame_length - set xgmac max frame length
 *@mac_drv: mac driver
 *@newval:xgmac max frame length
 */
static void hns_xgmac_config_max_frame_length(void *mac_drv, u16 newval)
{
	struct mac_driver *drv = (struct mac_driver *)mac_drv;

	dsaf_write_dev(drv, XGMAC_MAC_MAX_PKT_SIZE_REG, newval);
}

static void hns_xgmac_update_stats(void *mac_drv)
{
	struct mac_driver *drv = (struct mac_driver *)mac_drv;
	struct mac_hw_stats *hw_stats = &drv->mac_cb->hw_stats;

	/* TX */
	hw_stats->tx_fragment_err
		= hns_mac_reg_read64(drv, XGMAC_TX_PKTS_FRAGMENT);
	hw_stats->tx_undersize
		= hns_mac_reg_read64(drv, XGMAC_TX_PKTS_UNDERSIZE);
	hw_stats->tx_under_min_pkts
		= hns_mac_reg_read64(drv, XGMAC_TX_PKTS_UNDERMIN);
	hw_stats->tx_64bytes = hns_mac_reg_read64(drv, XGMAC_TX_PKTS_64OCTETS);
	hw_stats->tx_65to127
		= hns_mac_reg_read64(drv, XGMAC_TX_PKTS_65TO127OCTETS);
	hw_stats->tx_128to255
		= hns_mac_reg_read64(drv, XGMAC_TX_PKTS_128TO255OCTETS);
	hw_stats->tx_256to511
		= hns_mac_reg_read64(drv, XGMAC_TX_PKTS_256TO511OCTETS);
	hw_stats->tx_512to1023
		= hns_mac_reg_read64(drv, XGMAC_TX_PKTS_512TO1023OCTETS);
	hw_stats->tx_1024to1518
		= hns_mac_reg_read64(drv, XGMAC_TX_PKTS_1024TO1518OCTETS);
	hw_stats->tx_1519tomax
		= hns_mac_reg_read64(drv, XGMAC_TX_PKTS_1519TOMAXOCTETS);
	hw_stats->tx_1519tomax_good
		= hns_mac_reg_read64(drv, XGMAC_TX_PKTS_1519TOMAXOCTETSOK);
	hw_stats->tx_oversize = hns_mac_reg_read64(drv, XGMAC_TX_PKTS_OVERSIZE);
	hw_stats->tx_jabber_err = hns_mac_reg_read64(drv, XGMAC_TX_PKTS_JABBER);
	hw_stats->tx_good_pkts = hns_mac_reg_read64(drv, XGMAC_TX_GOODPKTS);
	hw_stats->tx_good_bytes = hns_mac_reg_read64(drv, XGMAC_TX_GOODOCTETS);
	hw_stats->tx_total_pkts = hns_mac_reg_read64(drv, XGMAC_TX_TOTAL_PKTS);
	hw_stats->tx_total_bytes
		= hns_mac_reg_read64(drv, XGMAC_TX_TOTALOCTETS);
	hw_stats->tx_uc_pkts = hns_mac_reg_read64(drv, XGMAC_TX_UNICASTPKTS);
	hw_stats->tx_mc_pkts = hns_mac_reg_read64(drv, XGMAC_TX_MULTICASTPKTS);
	hw_stats->tx_bc_pkts = hns_mac_reg_read64(drv, XGMAC_TX_BROADCASTPKTS);
	hw_stats->tx_pfc_tc0 = hns_mac_reg_read64(drv, XGMAC_TX_PRI0PAUSEPKTS);
	hw_stats->tx_pfc_tc1 = hns_mac_reg_read64(drv, XGMAC_TX_PRI1PAUSEPKTS);
	hw_stats->tx_pfc_tc2 = hns_mac_reg_read64(drv, XGMAC_TX_PRI2PAUSEPKTS);
	hw_stats->tx_pfc_tc3 = hns_mac_reg_read64(drv, XGMAC_TX_PRI3PAUSEPKTS);
	hw_stats->tx_pfc_tc4 = hns_mac_reg_read64(drv, XGMAC_TX_PRI4PAUSEPKTS);
	hw_stats->tx_pfc_tc5 = hns_mac_reg_read64(drv, XGMAC_TX_PRI5PAUSEPKTS);
	hw_stats->tx_pfc_tc6 = hns_mac_reg_read64(drv, XGMAC_TX_PRI6PAUSEPKTS);
	hw_stats->tx_pfc_tc7 = hns_mac_reg_read64(drv, XGMAC_TX_PRI7PAUSEPKTS);
	hw_stats->tx_ctrl = hns_mac_reg_read64(drv, XGMAC_TX_MACCTRLPKTS);
	hw_stats->tx_1731_pkts = hns_mac_reg_read64(drv, XGMAC_TX_1731PKTS);
	hw_stats->tx_1588_pkts = hns_mac_reg_read64(drv, XGMAC_TX_1588PKTS);
	hw_stats->rx_good_from_sw
		= hns_mac_reg_read64(drv, XGMAC_RX_FROMAPPGOODPKTS);
	hw_stats->rx_bad_from_sw
		= hns_mac_reg_read64(drv, XGMAC_RX_FROMAPPBADPKTS);
	hw_stats->tx_bad_pkts = hns_mac_reg_read64(drv, XGMAC_TX_ERRALLPKTS);

	/* RX */
	hw_stats->rx_fragment_err
		= hns_mac_reg_read64(drv, XGMAC_RX_PKTS_FRAGMENT);
	hw_stats->rx_undersize
		= hns_mac_reg_read64(drv, XGMAC_RX_PKTSUNDERSIZE);
	hw_stats->rx_under_min
		= hns_mac_reg_read64(drv, XGMAC_RX_PKTS_UNDERMIN);
	hw_stats->rx_64bytes = hns_mac_reg_read64(drv, XGMAC_RX_PKTS_64OCTETS);
	hw_stats->rx_65to127
		= hns_mac_reg_read64(drv, XGMAC_RX_PKTS_65TO127OCTETS);
	hw_stats->rx_128to255
		= hns_mac_reg_read64(drv, XGMAC_RX_PKTS_128TO255OCTETS);
	hw_stats->rx_256to511
		= hns_mac_reg_read64(drv, XGMAC_RX_PKTS_256TO511OCTETS);
	hw_stats->rx_512to1023
		= hns_mac_reg_read64(drv, XGMAC_RX_PKTS_512TO1023OCTETS);
	hw_stats->rx_1024to1518
		= hns_mac_reg_read64(drv, XGMAC_RX_PKTS_1024TO1518OCTETS);
	hw_stats->rx_1519tomax
		= hns_mac_reg_read64(drv, XGMAC_RX_PKTS_1519TOMAXOCTETS);
	hw_stats->rx_1519tomax_good
		= hns_mac_reg_read64(drv, XGMAC_RX_PKTS_1519TOMAXOCTETSOK);
	hw_stats->rx_oversize = hns_mac_reg_read64(drv, XGMAC_RX_PKTS_OVERSIZE);
	hw_stats->rx_jabber_err = hns_mac_reg_read64(drv, XGMAC_RX_PKTS_JABBER);
	hw_stats->rx_good_pkts = hns_mac_reg_read64(drv, XGMAC_RX_GOODPKTS);
	hw_stats->rx_good_bytes = hns_mac_reg_read64(drv, XGMAC_RX_GOODOCTETS);
	hw_stats->rx_total_pkts = hns_mac_reg_read64(drv, XGMAC_RX_TOTAL_PKTS);
	hw_stats->rx_total_bytes
		= hns_mac_reg_read64(drv, XGMAC_RX_TOTALOCTETS);
	hw_stats->rx_uc_pkts = hns_mac_reg_read64(drv, XGMAC_RX_UNICASTPKTS);
	hw_stats->rx_mc_pkts = hns_mac_reg_read64(drv, XGMAC_RX_MULTICASTPKTS);
	hw_stats->rx_bc_pkts = hns_mac_reg_read64(drv, XGMAC_RX_BROADCASTPKTS);
	hw_stats->rx_pfc_tc0 = hns_mac_reg_read64(drv, XGMAC_RX_PRI0PAUSEPKTS);
	hw_stats->rx_pfc_tc1 = hns_mac_reg_read64(drv, XGMAC_RX_PRI1PAUSEPKTS);
	hw_stats->rx_pfc_tc2 = hns_mac_reg_read64(drv, XGMAC_RX_PRI2PAUSEPKTS);
	hw_stats->rx_pfc_tc3 = hns_mac_reg_read64(drv, XGMAC_RX_PRI3PAUSEPKTS);
	hw_stats->rx_pfc_tc4 = hns_mac_reg_read64(drv, XGMAC_RX_PRI4PAUSEPKTS);
	hw_stats->rx_pfc_tc5 = hns_mac_reg_read64(drv, XGMAC_RX_PRI5PAUSEPKTS);
	hw_stats->rx_pfc_tc6 = hns_mac_reg_read64(drv, XGMAC_RX_PRI6PAUSEPKTS);
	hw_stats->rx_pfc_tc7 = hns_mac_reg_read64(drv, XGMAC_RX_PRI7PAUSEPKTS);

	hw_stats->rx_unknown_ctrl
		= hns_mac_reg_read64(drv, XGMAC_RX_MACCTRLPKTS);
	hw_stats->tx_good_to_sw
		= hns_mac_reg_read64(drv, XGMAC_TX_SENDAPPGOODPKTS);
	hw_stats->tx_bad_to_sw
		= hns_mac_reg_read64(drv, XGMAC_TX_SENDAPPBADPKTS);
	hw_stats->rx_1731_pkts = hns_mac_reg_read64(drv, XGMAC_RX_1731PKTS);
	hw_stats->rx_symbol_err
		= hns_mac_reg_read64(drv, XGMAC_RX_SYMBOLERRPKTS);
	hw_stats->rx_fcs_err = hns_mac_reg_read64(drv, XGMAC_RX_FCSERRPKTS);
}

/**
 *hns_xgmac_free - free xgmac driver
 *@mac_drv: mac driver
 */
static void hns_xgmac_free(void *mac_drv)
{
	struct mac_driver *drv = (struct mac_driver *)mac_drv;
	struct dsaf_device *dsaf_dev
		= (struct dsaf_device *)dev_get_drvdata(drv->dev);

	u32 mac_id = drv->mac_id;

	dsaf_dev->misc_op->xge_srst(dsaf_dev, mac_id, 0);
}

/**
 *hns_xgmac_get_info - get xgmac information
 *@mac_drv: mac driver
 *@mac_info:mac information
 */
static void hns_xgmac_get_info(void *mac_drv, struct mac_info *mac_info)
{
	struct mac_driver *drv = (struct mac_driver *)mac_drv;
	u32 pause_time, pause_ctrl, port_mode, ctrl_val;

	ctrl_val = dsaf_read_dev(drv, XGMAC_MAC_CONTROL_REG);
	mac_info->pad_and_crc_en = dsaf_get_bit(ctrl_val, XGMAC_CTL_TX_PAD_B);
	mac_info->auto_neg = 0;

	pause_time = dsaf_read_dev(drv, XGMAC_MAC_PAUSE_TIME_REG);
	mac_info->tx_pause_time = pause_time;

	port_mode = dsaf_read_dev(drv, XGMAC_PORT_MODE_REG);
	mac_info->port_en = dsaf_get_field(port_mode, XGMAC_PORT_MODE_TX_M,
					   XGMAC_PORT_MODE_TX_S) &&
				dsaf_get_field(port_mode, XGMAC_PORT_MODE_RX_M,
					       XGMAC_PORT_MODE_RX_S);
	mac_info->duplex = 1;
	mac_info->speed = MAC_SPEED_10000;

	pause_ctrl = dsaf_read_dev(drv, XGMAC_MAC_PAUSE_CTRL_REG);
	mac_info->rx_pause_en = dsaf_get_bit(pause_ctrl, XGMAC_PAUSE_CTL_RX_B);
	mac_info->tx_pause_en = dsaf_get_bit(pause_ctrl, XGMAC_PAUSE_CTL_TX_B);
}

/**
 *hns_xgmac_get_pausefrm_cfg - get xgmac pause param
 *@mac_drv: mac driver
 *@rx_en:xgmac rx pause enable
 *@tx_en:xgmac tx pause enable
 */
static void hns_xgmac_get_pausefrm_cfg(void *mac_drv, u32 *rx_en, u32 *tx_en)
{
	struct mac_driver *drv = (struct mac_driver *)mac_drv;
	u32 pause_ctrl;

	pause_ctrl = dsaf_read_dev(drv, XGMAC_MAC_PAUSE_CTRL_REG);
	*rx_en = dsaf_get_bit(pause_ctrl, XGMAC_PAUSE_CTL_RX_B);
	*tx_en = dsaf_get_bit(pause_ctrl, XGMAC_PAUSE_CTL_TX_B);
}

/**
 *hns_xgmac_get_link_status - get xgmac link status
 *@mac_drv: mac driver
 *@link_stat: xgmac link stat
 */
static void hns_xgmac_get_link_status(void *mac_drv, u32 *link_stat)
{
	struct mac_driver *drv = (struct mac_driver *)mac_drv;

	*link_stat = dsaf_read_dev(drv, XGMAC_LINK_STATUS_REG);
}

/**
 *hns_xgmac_get_regs - dump xgmac regs
 *@mac_drv: mac driver
 *@data:data for value of regs
 */
static void hns_xgmac_get_regs(void *mac_drv, void *data)
{
	u32 i;
	struct mac_driver *drv = (struct mac_driver *)mac_drv;
	u32 *regs = data;
	u64 qtmp;

	/* base config registers */
	regs[0] = dsaf_read_dev(drv, XGMAC_INT_STATUS_REG);
	regs[1] = dsaf_read_dev(drv, XGMAC_INT_ENABLE_REG);
	regs[2] = dsaf_read_dev(drv, XGMAC_INT_SET_REG);
	regs[3] = dsaf_read_dev(drv, XGMAC_IERR_U_INFO_REG);
	regs[4] = dsaf_read_dev(drv, XGMAC_OVF_INFO_REG);
	regs[5] = dsaf_read_dev(drv, XGMAC_OVF_CNT_REG);
	regs[6] = dsaf_read_dev(drv, XGMAC_PORT_MODE_REG);
	regs[7] = dsaf_read_dev(drv, XGMAC_CLK_ENABLE_REG);
	regs[8] = dsaf_read_dev(drv, XGMAC_RESET_REG);
	regs[9] = dsaf_read_dev(drv, XGMAC_LINK_CONTROL_REG);
	regs[10] = dsaf_read_dev(drv, XGMAC_LINK_STATUS_REG);

	regs[11] = dsaf_read_dev(drv, XGMAC_SPARE_REG);
	regs[12] = dsaf_read_dev(drv, XGMAC_SPARE_CNT_REG);
	regs[13] = dsaf_read_dev(drv, XGMAC_MAC_ENABLE_REG);
	regs[14] = dsaf_read_dev(drv, XGMAC_MAC_CONTROL_REG);
	regs[15] = dsaf_read_dev(drv, XGMAC_MAC_IPG_REG);
	regs[16] = dsaf_read_dev(drv, XGMAC_MAC_MSG_CRC_EN_REG);
	regs[17] = dsaf_read_dev(drv, XGMAC_MAC_MSG_IMG_REG);
	regs[18] = dsaf_read_dev(drv, XGMAC_MAC_MSG_FC_CFG_REG);
	regs[19] = dsaf_read_dev(drv, XGMAC_MAC_MSG_TC_CFG_REG);
	regs[20] = dsaf_read_dev(drv, XGMAC_MAC_PAD_SIZE_REG);
	regs[21] = dsaf_read_dev(drv, XGMAC_MAC_MIN_PKT_SIZE_REG);
	regs[22] = dsaf_read_dev(drv, XGMAC_MAC_MAX_PKT_SIZE_REG);
	regs[23] = dsaf_read_dev(drv, XGMAC_MAC_PAUSE_CTRL_REG);
	regs[24] = dsaf_read_dev(drv, XGMAC_MAC_PAUSE_TIME_REG);
	regs[25] = dsaf_read_dev(drv, XGMAC_MAC_PAUSE_GAP_REG);
	regs[26] = dsaf_read_dev(drv, XGMAC_MAC_PAUSE_LOCAL_MAC_H_REG);
	regs[27] = dsaf_read_dev(drv, XGMAC_MAC_PAUSE_LOCAL_MAC_L_REG);
	regs[28] = dsaf_read_dev(drv, XGMAC_MAC_PAUSE_PEER_MAC_H_REG);
	regs[29] = dsaf_read_dev(drv, XGMAC_MAC_PAUSE_PEER_MAC_L_REG);
	regs[30] = dsaf_read_dev(drv, XGMAC_MAC_PFC_PRI_EN_REG);
	regs[31] = dsaf_read_dev(drv, XGMAC_MAC_1588_CTRL_REG);
	regs[32] = dsaf_read_dev(drv, XGMAC_MAC_1588_TX_PORT_DLY_REG);
	regs[33] = dsaf_read_dev(drv, XGMAC_MAC_1588_RX_PORT_DLY_REG);
	regs[34] = dsaf_read_dev(drv, XGMAC_MAC_1588_ASYM_DLY_REG);
	regs[35] = dsaf_read_dev(drv, XGMAC_MAC_1588_ADJUST_CFG_REG);

	regs[36] = dsaf_read_dev(drv, XGMAC_MAC_Y1731_ETH_TYPE_REG);
	regs[37] = dsaf_read_dev(drv, XGMAC_MAC_MIB_CONTROL_REG);
	regs[38] = dsaf_read_dev(drv, XGMAC_MAC_WAN_RATE_ADJUST_REG);
	regs[39] = dsaf_read_dev(drv, XGMAC_MAC_TX_ERR_MARK_REG);
	regs[40] = dsaf_read_dev(drv, XGMAC_MAC_TX_LF_RF_CONTROL_REG);
	regs[41] = dsaf_read_dev(drv, XGMAC_MAC_RX_LF_RF_STATUS_REG);
	regs[42] = dsaf_read_dev(drv, XGMAC_MAC_TX_RUNT_PKT_CNT_REG);
	regs[43] = dsaf_read_dev(drv, XGMAC_MAC_RX_RUNT_PKT_CNT_REG);
	regs[44] = dsaf_read_dev(drv, XGMAC_MAC_RX_PREAM_ERR_PKT_CNT_REG);
	regs[45] = dsaf_read_dev(drv, XGMAC_MAC_TX_LF_RF_TERM_PKT_CNT_REG);
	regs[46] = dsaf_read_dev(drv, XGMAC_MAC_TX_SN_MISMATCH_PKT_CNT_REG);
	regs[47] = dsaf_read_dev(drv, XGMAC_MAC_RX_ERR_MSG_CNT_REG);
	regs[48] = dsaf_read_dev(drv, XGMAC_MAC_RX_ERR_EFD_CNT_REG);
	regs[49] = dsaf_read_dev(drv, XGMAC_MAC_ERR_INFO_REG);
	regs[50] = dsaf_read_dev(drv, XGMAC_MAC_DBG_INFO_REG);

	regs[51] = dsaf_read_dev(drv, XGMAC_PCS_BASER_SYNC_THD_REG);
	regs[52] = dsaf_read_dev(drv, XGMAC_PCS_STATUS1_REG);
	regs[53] = dsaf_read_dev(drv, XGMAC_PCS_BASER_STATUS1_REG);
	regs[54] = dsaf_read_dev(drv, XGMAC_PCS_BASER_STATUS2_REG);
	regs[55] = dsaf_read_dev(drv, XGMAC_PCS_BASER_SEEDA_0_REG);
	regs[56] = dsaf_read_dev(drv, XGMAC_PCS_BASER_SEEDA_1_REG);
	regs[57] = dsaf_read_dev(drv, XGMAC_PCS_BASER_SEEDB_0_REG);
	regs[58] = dsaf_read_dev(drv, XGMAC_PCS_BASER_SEEDB_1_REG);
	regs[59] = dsaf_read_dev(drv, XGMAC_PCS_BASER_TEST_CONTROL_REG);
	regs[60] = dsaf_read_dev(drv, XGMAC_PCS_BASER_TEST_ERR_CNT_REG);
	regs[61] = dsaf_read_dev(drv, XGMAC_PCS_DBG_INFO_REG);
	regs[62] = dsaf_read_dev(drv, XGMAC_PCS_DBG_INFO1_REG);
	regs[63] = dsaf_read_dev(drv, XGMAC_PCS_DBG_INFO2_REG);
	regs[64] = dsaf_read_dev(drv, XGMAC_PCS_DBG_INFO3_REG);

	regs[65] = dsaf_read_dev(drv, XGMAC_PMA_ENABLE_REG);
	regs[66] = dsaf_read_dev(drv, XGMAC_PMA_CONTROL_REG);
	regs[67] = dsaf_read_dev(drv, XGMAC_PMA_SIGNAL_STATUS_REG);
	regs[68] = dsaf_read_dev(drv, XGMAC_PMA_DBG_INFO_REG);
	regs[69] = dsaf_read_dev(drv, XGMAC_PMA_FEC_ABILITY_REG);
	regs[70] = dsaf_read_dev(drv, XGMAC_PMA_FEC_CONTROL_REG);
	regs[71] = dsaf_read_dev(drv, XGMAC_PMA_FEC_CORR_BLOCK_CNT__REG);
	regs[72] = dsaf_read_dev(drv, XGMAC_PMA_FEC_UNCORR_BLOCK_CNT__REG);

	/* status registers */
#define hns_xgmac_cpy_q(p, q) \
	do {\
		*(p) = (u32)(q);\
		*((p) + 1) = (u32)((q) >> 32);\
	} while (0)

	qtmp = hns_mac_reg_read64(drv, XGMAC_TX_PKTS_FRAGMENT);
	hns_xgmac_cpy_q(&regs[73], qtmp);
	qtmp = hns_mac_reg_read64(drv, XGMAC_TX_PKTS_UNDERSIZE);
	hns_xgmac_cpy_q(&regs[75], qtmp);
	qtmp = hns_mac_reg_read64(drv, XGMAC_TX_PKTS_UNDERMIN);
	hns_xgmac_cpy_q(&regs[77], qtmp);
	qtmp = hns_mac_reg_read64(drv, XGMAC_TX_PKTS_64OCTETS);
	hns_xgmac_cpy_q(&regs[79], qtmp);
	qtmp = hns_mac_reg_read64(drv, XGMAC_TX_PKTS_65TO127OCTETS);
	hns_xgmac_cpy_q(&regs[81], qtmp);
	qtmp = hns_mac_reg_read64(drv, XGMAC_TX_PKTS_128TO255OCTETS);
	hns_xgmac_cpy_q(&regs[83], qtmp);
	qtmp = hns_mac_reg_read64(drv, XGMAC_TX_PKTS_256TO511OCTETS);
	hns_xgmac_cpy_q(&regs[85], qtmp);
	qtmp = hns_mac_reg_read64(drv, XGMAC_TX_PKTS_512TO1023OCTETS);
	hns_xgmac_cpy_q(&regs[87], qtmp);
	qtmp = hns_mac_reg_read64(drv, XGMAC_TX_PKTS_1024TO1518OCTETS);
	hns_xgmac_cpy_q(&regs[89], qtmp);
	qtmp = hns_mac_reg_read64(drv, XGMAC_TX_PKTS_1519TOMAXOCTETS);
	hns_xgmac_cpy_q(&regs[91], qtmp);
	qtmp = hns_mac_reg_read64(drv, XGMAC_TX_PKTS_1519TOMAXOCTETSOK);
	hns_xgmac_cpy_q(&regs[93], qtmp);
	qtmp = hns_mac_reg_read64(drv, XGMAC_TX_PKTS_OVERSIZE);
	hns_xgmac_cpy_q(&regs[95], qtmp);
	qtmp = hns_mac_reg_read64(drv, XGMAC_TX_PKTS_JABBER);
	hns_xgmac_cpy_q(&regs[97], qtmp);
	qtmp = hns_mac_reg_read64(drv, XGMAC_TX_GOODPKTS);
	hns_xgmac_cpy_q(&regs[99], qtmp);
	qtmp = hns_mac_reg_read64(drv, XGMAC_TX_GOODOCTETS);
	hns_xgmac_cpy_q(&regs[101], qtmp);
	qtmp = hns_mac_reg_read64(drv, XGMAC_TX_TOTAL_PKTS);
	hns_xgmac_cpy_q(&regs[103], qtmp);
	qtmp = hns_mac_reg_read64(drv, XGMAC_TX_TOTALOCTETS);
	hns_xgmac_cpy_q(&regs[105], qtmp);
	qtmp = hns_mac_reg_read64(drv, XGMAC_TX_UNICASTPKTS);
	hns_xgmac_cpy_q(&regs[107], qtmp);
	qtmp = hns_mac_reg_read64(drv, XGMAC_TX_MULTICASTPKTS);
	hns_xgmac_cpy_q(&regs[109], qtmp);
	qtmp = hns_mac_reg_read64(drv, XGMAC_TX_BROADCASTPKTS);
	hns_xgmac_cpy_q(&regs[111], qtmp);
	qtmp = hns_mac_reg_read64(drv, XGMAC_TX_PRI0PAUSEPKTS);
	hns_xgmac_cpy_q(&regs[113], qtmp);
	qtmp = hns_mac_reg_read64(drv, XGMAC_TX_PRI1PAUSEPKTS);
	hns_xgmac_cpy_q(&regs[115], qtmp);
	qtmp = hns_mac_reg_read64(drv, XGMAC_TX_PRI2PAUSEPKTS);
	hns_xgmac_cpy_q(&regs[117], qtmp);
	qtmp = hns_mac_reg_read64(drv, XGMAC_TX_PRI3PAUSEPKTS);
	hns_xgmac_cpy_q(&regs[119], qtmp);
	qtmp = hns_mac_reg_read64(drv, XGMAC_TX_PRI4PAUSEPKTS);
	hns_xgmac_cpy_q(&regs[121], qtmp);
	qtmp = hns_mac_reg_read64(drv, XGMAC_TX_PRI5PAUSEPKTS);
	hns_xgmac_cpy_q(&regs[123], qtmp);
	qtmp = hns_mac_reg_read64(drv, XGMAC_TX_PRI6PAUSEPKTS);
	hns_xgmac_cpy_q(&regs[125], qtmp);
	qtmp = hns_mac_reg_read64(drv, XGMAC_TX_PRI7PAUSEPKTS);
	hns_xgmac_cpy_q(&regs[127], qtmp);
	qtmp = hns_mac_reg_read64(drv, XGMAC_TX_MACCTRLPKTS);
	hns_xgmac_cpy_q(&regs[129], qtmp);
	qtmp = hns_mac_reg_read64(drv, XGMAC_TX_1731PKTS);
	hns_xgmac_cpy_q(&regs[131], qtmp);
	qtmp = hns_mac_reg_read64(drv, XGMAC_TX_1588PKTS);
	hns_xgmac_cpy_q(&regs[133], qtmp);
	qtmp = hns_mac_reg_read64(drv, XGMAC_RX_FROMAPPGOODPKTS);
	hns_xgmac_cpy_q(&regs[135], qtmp);
	qtmp = hns_mac_reg_read64(drv, XGMAC_RX_FROMAPPBADPKTS);
	hns_xgmac_cpy_q(&regs[137], qtmp);
	qtmp = hns_mac_reg_read64(drv, XGMAC_TX_ERRALLPKTS);
	hns_xgmac_cpy_q(&regs[139], qtmp);

	/* RX */
	qtmp = hns_mac_reg_read64(drv, XGMAC_RX_PKTS_FRAGMENT);
	hns_xgmac_cpy_q(&regs[141], qtmp);
	qtmp = hns_mac_reg_read64(drv, XGMAC_RX_PKTSUNDERSIZE);
	hns_xgmac_cpy_q(&regs[143], qtmp);
	qtmp = hns_mac_reg_read64(drv, XGMAC_RX_PKTS_UNDERMIN);
	hns_xgmac_cpy_q(&regs[145], qtmp);
	qtmp = hns_mac_reg_read64(drv, XGMAC_RX_PKTS_64OCTETS);
	hns_xgmac_cpy_q(&regs[147], qtmp);
	qtmp = hns_mac_reg_read64(drv, XGMAC_RX_PKTS_65TO127OCTETS);
	hns_xgmac_cpy_q(&regs[149], qtmp);
	qtmp = hns_mac_reg_read64(drv, XGMAC_RX_PKTS_128TO255OCTETS);
	hns_xgmac_cpy_q(&regs[151], qtmp);
	qtmp = hns_mac_reg_read64(drv, XGMAC_RX_PKTS_256TO511OCTETS);
	hns_xgmac_cpy_q(&regs[153], qtmp);
	qtmp = hns_mac_reg_read64(drv, XGMAC_RX_PKTS_512TO1023OCTETS);
	hns_xgmac_cpy_q(&regs[155], qtmp);
	qtmp = hns_mac_reg_read64(drv, XGMAC_RX_PKTS_1024TO1518OCTETS);
	hns_xgmac_cpy_q(&regs[157], qtmp);
	qtmp = hns_mac_reg_read64(drv, XGMAC_RX_PKTS_1519TOMAXOCTETS);
	hns_xgmac_cpy_q(&regs[159], qtmp);
	qtmp = hns_mac_reg_read64(drv, XGMAC_RX_PKTS_1519TOMAXOCTETSOK);
	hns_xgmac_cpy_q(&regs[161], qtmp);
	qtmp = hns_mac_reg_read64(drv, XGMAC_RX_PKTS_OVERSIZE);
	hns_xgmac_cpy_q(&regs[163], qtmp);
	qtmp = hns_mac_reg_read64(drv, XGMAC_RX_PKTS_JABBER);
	hns_xgmac_cpy_q(&regs[165], qtmp);
	qtmp = hns_mac_reg_read64(drv, XGMAC_RX_GOODPKTS);
	hns_xgmac_cpy_q(&regs[167], qtmp);
	qtmp = hns_mac_reg_read64(drv, XGMAC_RX_GOODOCTETS);
	hns_xgmac_cpy_q(&regs[169], qtmp);
	qtmp = hns_mac_reg_read64(drv, XGMAC_RX_TOTAL_PKTS);
	hns_xgmac_cpy_q(&regs[171], qtmp);
	qtmp = hns_mac_reg_read64(drv, XGMAC_RX_TOTALOCTETS);
	hns_xgmac_cpy_q(&regs[173], qtmp);
	qtmp = hns_mac_reg_read64(drv, XGMAC_RX_UNICASTPKTS);
	hns_xgmac_cpy_q(&regs[175], qtmp);
	qtmp = hns_mac_reg_read64(drv, XGMAC_RX_MULTICASTPKTS);
	hns_xgmac_cpy_q(&regs[177], qtmp);
	qtmp = hns_mac_reg_read64(drv, XGMAC_RX_BROADCASTPKTS);
	hns_xgmac_cpy_q(&regs[179], qtmp);
	qtmp = hns_mac_reg_read64(drv, XGMAC_RX_PRI0PAUSEPKTS);
	hns_xgmac_cpy_q(&regs[181], qtmp);
	qtmp = hns_mac_reg_read64(drv, XGMAC_RX_PRI1PAUSEPKTS);
	hns_xgmac_cpy_q(&regs[183], qtmp);
	qtmp = hns_mac_reg_read64(drv, XGMAC_RX_PRI2PAUSEPKTS);
	hns_xgmac_cpy_q(&regs[185], qtmp);
	qtmp = hns_mac_reg_read64(drv, XGMAC_RX_PRI3PAUSEPKTS);
	hns_xgmac_cpy_q(&regs[187], qtmp);
	qtmp = hns_mac_reg_read64(drv, XGMAC_RX_PRI4PAUSEPKTS);
	hns_xgmac_cpy_q(&regs[189], qtmp);
	qtmp = hns_mac_reg_read64(drv, XGMAC_RX_PRI5PAUSEPKTS);
	hns_xgmac_cpy_q(&regs[191], qtmp);
	qtmp = hns_mac_reg_read64(drv, XGMAC_RX_PRI6PAUSEPKTS);
	hns_xgmac_cpy_q(&regs[193], qtmp);
	qtmp = hns_mac_reg_read64(drv, XGMAC_RX_PRI7PAUSEPKTS);
	hns_xgmac_cpy_q(&regs[195], qtmp);

	qtmp = hns_mac_reg_read64(drv, XGMAC_RX_MACCTRLPKTS);
	hns_xgmac_cpy_q(&regs[197], qtmp);
	qtmp = hns_mac_reg_read64(drv, XGMAC_TX_SENDAPPGOODPKTS);
	hns_xgmac_cpy_q(&regs[199], qtmp);
	qtmp = hns_mac_reg_read64(drv, XGMAC_TX_SENDAPPBADPKTS);
	hns_xgmac_cpy_q(&regs[201], qtmp);
	qtmp = hns_mac_reg_read64(drv, XGMAC_RX_1731PKTS);
	hns_xgmac_cpy_q(&regs[203], qtmp);
	qtmp = hns_mac_reg_read64(drv, XGMAC_RX_SYMBOLERRPKTS);
	hns_xgmac_cpy_q(&regs[205], qtmp);
	qtmp = hns_mac_reg_read64(drv, XGMAC_RX_FCSERRPKTS);
	hns_xgmac_cpy_q(&regs[207], qtmp);

	/* mark end of mac regs */
	for (i = 208; i < 214; i++)
		regs[i] = 0xaaaaaaaa;
}

/**
 *hns_xgmac_get_stats - get xgmac statistic
 *@mac_drv: mac driver
 *@data:data for value of stats regs
 */
static void hns_xgmac_get_stats(void *mac_drv, u64 *data)
{
	u32 i;
	u64 *buf = data;
	struct mac_driver *drv = (struct mac_driver *)mac_drv;
	struct mac_hw_stats *hw_stats = NULL;

	hw_stats = &drv->mac_cb->hw_stats;

	for (i = 0; i < ARRAY_SIZE(g_xgmac_stats_string); i++) {
		buf[i] = DSAF_STATS_READ(hw_stats,
			g_xgmac_stats_string[i].offset);
	}
}

/**
 *hns_xgmac_get_strings - get xgmac strings name
 *@stringset: type of values in data
 *@data:data for value of string name
 */
static void hns_xgmac_get_strings(u32 stringset, u8 *data)
{
	u8 *buff = data;
	u32 i;

	if (stringset != ETH_SS_STATS)
		return;

	for (i = 0; i < ARRAY_SIZE(g_xgmac_stats_string); i++)
		ethtool_sprintf(&buff, g_xgmac_stats_string[i].desc);
}

/**
 *hns_xgmac_get_sset_count - get xgmac string set count
 *@stringset: type of values in data
 *return xgmac string set count
 */
static int hns_xgmac_get_sset_count(int stringset)
{
	if (stringset == ETH_SS_STATS || stringset == ETH_SS_PRIV_FLAGS)
		return ARRAY_SIZE(g_xgmac_stats_string);

	return 0;
}

/**
 *hns_xgmac_get_regs_count - get xgmac regs count
 *return xgmac regs count
 */
static int hns_xgmac_get_regs_count(void)
{
	return HNS_XGMAC_DUMP_NUM;
}

void *hns_xgmac_config(struct hns_mac_cb *mac_cb, struct mac_params *mac_param)
{
	struct mac_driver *mac_drv;

	mac_drv = devm_kzalloc(mac_cb->dev, sizeof(*mac_drv), GFP_KERNEL);
	if (!mac_drv)
		return NULL;

	mac_drv->mac_init = hns_xgmac_init;
	mac_drv->mac_enable = hns_xgmac_enable;
	mac_drv->mac_disable = hns_xgmac_disable;

	mac_drv->mac_id = mac_param->mac_id;
	mac_drv->mac_mode = mac_param->mac_mode;
	mac_drv->io_base = mac_param->vaddr;
	mac_drv->dev = mac_param->dev;
	mac_drv->mac_cb = mac_cb;

	mac_drv->set_mac_addr = hns_xgmac_set_pausefrm_mac_addr;
	mac_drv->set_an_mode = NULL;
	mac_drv->config_loopback = NULL;
	mac_drv->config_pad_and_crc = hns_xgmac_config_pad_and_crc;
	mac_drv->mac_free = hns_xgmac_free;
	mac_drv->adjust_link = NULL;
	mac_drv->set_tx_auto_pause_frames = hns_xgmac_set_tx_auto_pause_frames;
	mac_drv->config_max_frame_length = hns_xgmac_config_max_frame_length;
	mac_drv->mac_pausefrm_cfg = hns_xgmac_pausefrm_cfg;
	mac_drv->autoneg_stat = NULL;
	mac_drv->get_info = hns_xgmac_get_info;
	mac_drv->get_pause_enable = hns_xgmac_get_pausefrm_cfg;
	mac_drv->get_link_status = hns_xgmac_get_link_status;
	mac_drv->get_regs = hns_xgmac_get_regs;
	mac_drv->get_ethtool_stats = hns_xgmac_get_stats;
	mac_drv->get_sset_count = hns_xgmac_get_sset_count;
	mac_drv->get_regs_count = hns_xgmac_get_regs_count;
	mac_drv->get_strings = hns_xgmac_get_strings;
	mac_drv->update_stats = hns_xgmac_update_stats;

	return (void *)mac_drv;
}
