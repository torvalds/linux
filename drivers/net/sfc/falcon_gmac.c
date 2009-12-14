/****************************************************************************
 * Driver for Solarflare Solarstorm network controllers and boards
 * Copyright 2005-2006 Fen Systems Ltd.
 * Copyright 2006-2009 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#include <linux/delay.h>
#include "net_driver.h"
#include "efx.h"
#include "nic.h"
#include "mac.h"
#include "regs.h"
#include "io.h"

/**************************************************************************
 *
 * MAC operations
 *
 *************************************************************************/

static int falcon_reconfigure_gmac(struct efx_nic *efx)
{
	struct efx_link_state *link_state = &efx->link_state;
	bool loopback, tx_fc, rx_fc, bytemode;
	int if_mode;
	unsigned int max_frame_len;
	efx_oword_t reg;

	/* Configuration register 1 */
	tx_fc = (link_state->fc & EFX_FC_TX) || !link_state->fd;
	rx_fc = !!(link_state->fc & EFX_FC_RX);
	loopback = (efx->loopback_mode == LOOPBACK_GMAC);
	bytemode = (link_state->speed == 1000);

	EFX_POPULATE_OWORD_5(reg,
			     FRF_AB_GM_LOOP, loopback,
			     FRF_AB_GM_TX_EN, 1,
			     FRF_AB_GM_TX_FC_EN, tx_fc,
			     FRF_AB_GM_RX_EN, 1,
			     FRF_AB_GM_RX_FC_EN, rx_fc);
	efx_writeo(efx, &reg, FR_AB_GM_CFG1);
	udelay(10);

	/* Configuration register 2 */
	if_mode = (bytemode) ? 2 : 1;
	EFX_POPULATE_OWORD_5(reg,
			     FRF_AB_GM_IF_MODE, if_mode,
			     FRF_AB_GM_PAD_CRC_EN, 1,
			     FRF_AB_GM_LEN_CHK, 1,
			     FRF_AB_GM_FD, link_state->fd,
			     FRF_AB_GM_PAMBL_LEN, 0x7/*datasheet recommended */);

	efx_writeo(efx, &reg, FR_AB_GM_CFG2);
	udelay(10);

	/* Max frame len register */
	max_frame_len = EFX_MAX_FRAME_LEN(efx->net_dev->mtu);
	EFX_POPULATE_OWORD_1(reg, FRF_AB_GM_MAX_FLEN, max_frame_len);
	efx_writeo(efx, &reg, FR_AB_GM_MAX_FLEN);
	udelay(10);

	/* FIFO configuration register 0 */
	EFX_POPULATE_OWORD_5(reg,
			     FRF_AB_GMF_FTFENREQ, 1,
			     FRF_AB_GMF_STFENREQ, 1,
			     FRF_AB_GMF_FRFENREQ, 1,
			     FRF_AB_GMF_SRFENREQ, 1,
			     FRF_AB_GMF_WTMENREQ, 1);
	efx_writeo(efx, &reg, FR_AB_GMF_CFG0);
	udelay(10);

	/* FIFO configuration register 1 */
	EFX_POPULATE_OWORD_2(reg,
			     FRF_AB_GMF_CFGFRTH, 0x12,
			     FRF_AB_GMF_CFGXOFFRTX, 0xffff);
	efx_writeo(efx, &reg, FR_AB_GMF_CFG1);
	udelay(10);

	/* FIFO configuration register 2 */
	EFX_POPULATE_OWORD_2(reg,
			     FRF_AB_GMF_CFGHWM, 0x3f,
			     FRF_AB_GMF_CFGLWM, 0xa);
	efx_writeo(efx, &reg, FR_AB_GMF_CFG2);
	udelay(10);

	/* FIFO configuration register 3 */
	EFX_POPULATE_OWORD_2(reg,
			     FRF_AB_GMF_CFGHWMFT, 0x1c,
			     FRF_AB_GMF_CFGFTTH, 0x08);
	efx_writeo(efx, &reg, FR_AB_GMF_CFG3);
	udelay(10);

	/* FIFO configuration register 4 */
	EFX_POPULATE_OWORD_1(reg, FRF_AB_GMF_HSTFLTRFRM_PAUSE, 1);
	efx_writeo(efx, &reg, FR_AB_GMF_CFG4);
	udelay(10);

	/* FIFO configuration register 5 */
	efx_reado(efx, &reg, FR_AB_GMF_CFG5);
	EFX_SET_OWORD_FIELD(reg, FRF_AB_GMF_CFGBYTMODE, bytemode);
	EFX_SET_OWORD_FIELD(reg, FRF_AB_GMF_CFGHDPLX, !link_state->fd);
	EFX_SET_OWORD_FIELD(reg, FRF_AB_GMF_HSTDRPLT64, !link_state->fd);
	EFX_SET_OWORD_FIELD(reg, FRF_AB_GMF_HSTFLTRFRMDC_PAUSE, 0);
	efx_writeo(efx, &reg, FR_AB_GMF_CFG5);
	udelay(10);

	/* MAC address */
	EFX_POPULATE_OWORD_4(reg,
			     FRF_AB_GM_ADR_B0, efx->net_dev->dev_addr[5],
			     FRF_AB_GM_ADR_B1, efx->net_dev->dev_addr[4],
			     FRF_AB_GM_ADR_B2, efx->net_dev->dev_addr[3],
			     FRF_AB_GM_ADR_B3, efx->net_dev->dev_addr[2]);
	efx_writeo(efx, &reg, FR_AB_GM_ADR1);
	udelay(10);
	EFX_POPULATE_OWORD_2(reg,
			     FRF_AB_GM_ADR_B4, efx->net_dev->dev_addr[1],
			     FRF_AB_GM_ADR_B5, efx->net_dev->dev_addr[0]);
	efx_writeo(efx, &reg, FR_AB_GM_ADR2);
	udelay(10);

	falcon_reconfigure_mac_wrapper(efx);

	return 0;
}

static void falcon_update_stats_gmac(struct efx_nic *efx)
{
	struct efx_mac_stats *mac_stats = &efx->mac_stats;
	unsigned long old_rx_pause, old_tx_pause;
	unsigned long new_rx_pause, new_tx_pause;

	/* Pause frames are erroneously counted as errors (SFC bug 3269) */
	old_rx_pause = mac_stats->rx_pause;
	old_tx_pause = mac_stats->tx_pause;

	/* Update MAC stats from DMAed values */
	FALCON_STAT(efx, GRxGoodOct, rx_good_bytes);
	FALCON_STAT(efx, GRxBadOct, rx_bad_bytes);
	FALCON_STAT(efx, GRxMissPkt, rx_missed);
	FALCON_STAT(efx, GRxFalseCRS, rx_false_carrier);
	FALCON_STAT(efx, GRxPausePkt, rx_pause);
	FALCON_STAT(efx, GRxBadPkt, rx_bad);
	FALCON_STAT(efx, GRxUcastPkt, rx_unicast);
	FALCON_STAT(efx, GRxMcastPkt, rx_multicast);
	FALCON_STAT(efx, GRxBcastPkt, rx_broadcast);
	FALCON_STAT(efx, GRxGoodLt64Pkt, rx_good_lt64);
	FALCON_STAT(efx, GRxBadLt64Pkt, rx_bad_lt64);
	FALCON_STAT(efx, GRx64Pkt, rx_64);
	FALCON_STAT(efx, GRx65to127Pkt, rx_65_to_127);
	FALCON_STAT(efx, GRx128to255Pkt, rx_128_to_255);
	FALCON_STAT(efx, GRx256to511Pkt, rx_256_to_511);
	FALCON_STAT(efx, GRx512to1023Pkt, rx_512_to_1023);
	FALCON_STAT(efx, GRx1024to15xxPkt, rx_1024_to_15xx);
	FALCON_STAT(efx, GRx15xxtoJumboPkt, rx_15xx_to_jumbo);
	FALCON_STAT(efx, GRxGtJumboPkt, rx_gtjumbo);
	FALCON_STAT(efx, GRxFcsErr64to15xxPkt, rx_bad_64_to_15xx);
	FALCON_STAT(efx, GRxFcsErr15xxtoJumboPkt, rx_bad_15xx_to_jumbo);
	FALCON_STAT(efx, GRxFcsErrGtJumboPkt, rx_bad_gtjumbo);
	FALCON_STAT(efx, GTxGoodBadOct, tx_bytes);
	FALCON_STAT(efx, GTxGoodOct, tx_good_bytes);
	FALCON_STAT(efx, GTxSglColPkt, tx_single_collision);
	FALCON_STAT(efx, GTxMultColPkt, tx_multiple_collision);
	FALCON_STAT(efx, GTxExColPkt, tx_excessive_collision);
	FALCON_STAT(efx, GTxDefPkt, tx_deferred);
	FALCON_STAT(efx, GTxLateCol, tx_late_collision);
	FALCON_STAT(efx, GTxExDefPkt, tx_excessive_deferred);
	FALCON_STAT(efx, GTxPausePkt, tx_pause);
	FALCON_STAT(efx, GTxBadPkt, tx_bad);
	FALCON_STAT(efx, GTxUcastPkt, tx_unicast);
	FALCON_STAT(efx, GTxMcastPkt, tx_multicast);
	FALCON_STAT(efx, GTxBcastPkt, tx_broadcast);
	FALCON_STAT(efx, GTxLt64Pkt, tx_lt64);
	FALCON_STAT(efx, GTx64Pkt, tx_64);
	FALCON_STAT(efx, GTx65to127Pkt, tx_65_to_127);
	FALCON_STAT(efx, GTx128to255Pkt, tx_128_to_255);
	FALCON_STAT(efx, GTx256to511Pkt, tx_256_to_511);
	FALCON_STAT(efx, GTx512to1023Pkt, tx_512_to_1023);
	FALCON_STAT(efx, GTx1024to15xxPkt, tx_1024_to_15xx);
	FALCON_STAT(efx, GTx15xxtoJumboPkt, tx_15xx_to_jumbo);
	FALCON_STAT(efx, GTxGtJumboPkt, tx_gtjumbo);
	FALCON_STAT(efx, GTxNonTcpUdpPkt, tx_non_tcpudp);
	FALCON_STAT(efx, GTxMacSrcErrPkt, tx_mac_src_error);
	FALCON_STAT(efx, GTxIpSrcErrPkt, tx_ip_src_error);

	/* Pause frames are erroneously counted as errors (SFC bug 3269) */
	new_rx_pause = mac_stats->rx_pause;
	new_tx_pause = mac_stats->tx_pause;
	mac_stats->rx_bad -= (new_rx_pause - old_rx_pause);
	mac_stats->tx_bad -= (new_tx_pause - old_tx_pause);

	/* Derive stats that the MAC doesn't provide directly */
	mac_stats->tx_bad_bytes =
		mac_stats->tx_bytes - mac_stats->tx_good_bytes;
	mac_stats->tx_packets =
		mac_stats->tx_lt64 + mac_stats->tx_64 +
		mac_stats->tx_65_to_127 + mac_stats->tx_128_to_255 +
		mac_stats->tx_256_to_511 + mac_stats->tx_512_to_1023 +
		mac_stats->tx_1024_to_15xx + mac_stats->tx_15xx_to_jumbo +
		mac_stats->tx_gtjumbo;
	mac_stats->tx_collision =
		mac_stats->tx_single_collision +
		mac_stats->tx_multiple_collision +
		mac_stats->tx_excessive_collision +
		mac_stats->tx_late_collision;
	mac_stats->rx_bytes =
		mac_stats->rx_good_bytes + mac_stats->rx_bad_bytes;
	mac_stats->rx_packets =
		mac_stats->rx_good_lt64 + mac_stats->rx_bad_lt64 +
		mac_stats->rx_64 + mac_stats->rx_65_to_127 +
		mac_stats->rx_128_to_255 + mac_stats->rx_256_to_511 +
		mac_stats->rx_512_to_1023 + mac_stats->rx_1024_to_15xx +
		mac_stats->rx_15xx_to_jumbo + mac_stats->rx_gtjumbo;
	mac_stats->rx_good = mac_stats->rx_packets - mac_stats->rx_bad;
	mac_stats->rx_lt64 = mac_stats->rx_good_lt64 + mac_stats->rx_bad_lt64;
}

static bool falcon_gmac_check_fault(struct efx_nic *efx)
{
	return false;
}

struct efx_mac_operations falcon_gmac_operations = {
	.reconfigure	= falcon_reconfigure_gmac,
	.update_stats	= falcon_update_stats_gmac,
	.check_fault 	= falcon_gmac_check_fault,
};
