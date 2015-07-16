/*
 * AMD 10Gb Ethernet driver
 *
 * This file is available to you under your choice of the following two
 * licenses:
 *
 * License 1: GPLv2
 *
 * Copyright (c) 2014 Advanced Micro Devices, Inc.
 *
 * This file is free software; you may copy, redistribute and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or (at
 * your option) any later version.
 *
 * This file is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *     The Synopsys DWC ETHER XGMAC Software Driver and documentation
 *     (hereinafter "Software") is an unsupported proprietary work of Synopsys,
 *     Inc. unless otherwise expressly agreed to in writing between Synopsys
 *     and you.
 *
 *     The Software IS NOT an item of Licensed Software or Licensed Product
 *     under any End User Software License Agreement or Agreement for Licensed
 *     Product with Synopsys or any supplement thereto.  Permission is hereby
 *     granted, free of charge, to any person obtaining a copy of this software
 *     annotated with this license and the Software, to deal in the Software
 *     without restriction, including without limitation the rights to use,
 *     copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 *     of the Software, and to permit persons to whom the Software is furnished
 *     to do so, subject to the following conditions:
 *
 *     The above copyright notice and this permission notice shall be included
 *     in all copies or substantial portions of the Software.
 *
 *     THIS SOFTWARE IS BEING DISTRIBUTED BY SYNOPSYS SOLELY ON AN "AS IS"
 *     BASIS AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 *     TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 *     PARTICULAR PURPOSE ARE HEREBY DISCLAIMED. IN NO EVENT SHALL SYNOPSYS
 *     BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *     CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *     SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *     INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *     CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *     ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 *     THE POSSIBILITY OF SUCH DAMAGE.
 *
 *
 * License 2: Modified BSD
 *
 * Copyright (c) 2014 Advanced Micro Devices, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Advanced Micro Devices, Inc. nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *     The Synopsys DWC ETHER XGMAC Software Driver and documentation
 *     (hereinafter "Software") is an unsupported proprietary work of Synopsys,
 *     Inc. unless otherwise expressly agreed to in writing between Synopsys
 *     and you.
 *
 *     The Software IS NOT an item of Licensed Software or Licensed Product
 *     under any End User Software License Agreement or Agreement for Licensed
 *     Product with Synopsys or any supplement thereto.  Permission is hereby
 *     granted, free of charge, to any person obtaining a copy of this software
 *     annotated with this license and the Software, to deal in the Software
 *     without restriction, including without limitation the rights to use,
 *     copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 *     of the Software, and to permit persons to whom the Software is furnished
 *     to do so, subject to the following conditions:
 *
 *     The above copyright notice and this permission notice shall be included
 *     in all copies or substantial portions of the Software.
 *
 *     THIS SOFTWARE IS BEING DISTRIBUTED BY SYNOPSYS SOLELY ON AN "AS IS"
 *     BASIS AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 *     TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 *     PARTICULAR PURPOSE ARE HEREBY DISCLAIMED. IN NO EVENT SHALL SYNOPSYS
 *     BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *     CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *     SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *     INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *     CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *     ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 *     THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <linux/module.h>
#include <linux/kmod.h>
#include <linux/mdio.h>
#include <linux/phy.h>
#include <linux/of.h>
#include <linux/bitops.h>
#include <linux/jiffies.h>

#include "xgbe.h"
#include "xgbe-common.h"

static void xgbe_an_enable_kr_training(struct xgbe_prv_data *pdata)
{
	unsigned int reg;

	reg = XMDIO_READ(pdata, MDIO_MMD_PMAPMD, MDIO_PMA_10GBR_PMD_CTRL);

	reg |= XGBE_KR_TRAINING_ENABLE;
	XMDIO_WRITE(pdata, MDIO_MMD_PMAPMD, MDIO_PMA_10GBR_PMD_CTRL, reg);
}

static void xgbe_an_disable_kr_training(struct xgbe_prv_data *pdata)
{
	unsigned int reg;

	reg = XMDIO_READ(pdata, MDIO_MMD_PMAPMD, MDIO_PMA_10GBR_PMD_CTRL);

	reg &= ~XGBE_KR_TRAINING_ENABLE;
	XMDIO_WRITE(pdata, MDIO_MMD_PMAPMD, MDIO_PMA_10GBR_PMD_CTRL, reg);
}

static void xgbe_pcs_power_cycle(struct xgbe_prv_data *pdata)
{
	unsigned int reg;

	reg = XMDIO_READ(pdata, MDIO_MMD_PCS, MDIO_CTRL1);

	reg |= MDIO_CTRL1_LPOWER;
	XMDIO_WRITE(pdata, MDIO_MMD_PCS, MDIO_CTRL1, reg);

	usleep_range(75, 100);

	reg &= ~MDIO_CTRL1_LPOWER;
	XMDIO_WRITE(pdata, MDIO_MMD_PCS, MDIO_CTRL1, reg);
}

static void xgbe_serdes_start_ratechange(struct xgbe_prv_data *pdata)
{
	/* Assert Rx and Tx ratechange */
	XSIR1_IOWRITE_BITS(pdata, SIR1_SPEED, RATECHANGE, 1);
}

static void xgbe_serdes_complete_ratechange(struct xgbe_prv_data *pdata)
{
	unsigned int wait;
	u16 status;

	/* Release Rx and Tx ratechange */
	XSIR1_IOWRITE_BITS(pdata, SIR1_SPEED, RATECHANGE, 0);

	/* Wait for Rx and Tx ready */
	wait = XGBE_RATECHANGE_COUNT;
	while (wait--) {
		usleep_range(50, 75);

		status = XSIR0_IOREAD(pdata, SIR0_STATUS);
		if (XSIR_GET_BITS(status, SIR0_STATUS, RX_READY) &&
		    XSIR_GET_BITS(status, SIR0_STATUS, TX_READY))
			goto rx_reset;
	}

	netif_dbg(pdata, link, pdata->netdev, "SerDes rx/tx not ready (%#hx)\n",
		  status);

rx_reset:
	/* Perform Rx reset for the DFE changes */
	XRXTX_IOWRITE_BITS(pdata, RXTX_REG6, RESETB_RXD, 0);
	XRXTX_IOWRITE_BITS(pdata, RXTX_REG6, RESETB_RXD, 1);
}

static void xgbe_xgmii_mode(struct xgbe_prv_data *pdata)
{
	unsigned int reg;

	/* Enable KR training */
	xgbe_an_enable_kr_training(pdata);

	/* Set MAC to 10G speed */
	pdata->hw_if.set_xgmii_speed(pdata);

	/* Set PCS to KR/10G speed */
	reg = XMDIO_READ(pdata, MDIO_MMD_PCS, MDIO_CTRL2);
	reg &= ~MDIO_PCS_CTRL2_TYPE;
	reg |= MDIO_PCS_CTRL2_10GBR;
	XMDIO_WRITE(pdata, MDIO_MMD_PCS, MDIO_CTRL2, reg);

	reg = XMDIO_READ(pdata, MDIO_MMD_PCS, MDIO_CTRL1);
	reg &= ~MDIO_CTRL1_SPEEDSEL;
	reg |= MDIO_CTRL1_SPEED10G;
	XMDIO_WRITE(pdata, MDIO_MMD_PCS, MDIO_CTRL1, reg);

	xgbe_pcs_power_cycle(pdata);

	/* Set SerDes to 10G speed */
	xgbe_serdes_start_ratechange(pdata);

	XSIR1_IOWRITE_BITS(pdata, SIR1_SPEED, DATARATE, XGBE_SPEED_10000_RATE);
	XSIR1_IOWRITE_BITS(pdata, SIR1_SPEED, WORDMODE, XGBE_SPEED_10000_WORD);
	XSIR1_IOWRITE_BITS(pdata, SIR1_SPEED, PLLSEL, XGBE_SPEED_10000_PLL);

	XSIR1_IOWRITE_BITS(pdata, SIR1_SPEED, CDR_RATE,
			   pdata->serdes_cdr_rate[XGBE_SPEED_10000]);
	XSIR1_IOWRITE_BITS(pdata, SIR1_SPEED, TXAMP,
			   pdata->serdes_tx_amp[XGBE_SPEED_10000]);
	XRXTX_IOWRITE_BITS(pdata, RXTX_REG20, BLWC_ENA,
			   pdata->serdes_blwc[XGBE_SPEED_10000]);
	XRXTX_IOWRITE_BITS(pdata, RXTX_REG114, PQ_REG,
			   pdata->serdes_pq_skew[XGBE_SPEED_10000]);
	XRXTX_IOWRITE_BITS(pdata, RXTX_REG129, RXDFE_CONFIG,
			   pdata->serdes_dfe_tap_cfg[XGBE_SPEED_10000]);
	XRXTX_IOWRITE(pdata, RXTX_REG22,
		      pdata->serdes_dfe_tap_ena[XGBE_SPEED_10000]);

	xgbe_serdes_complete_ratechange(pdata);

	netif_dbg(pdata, link, pdata->netdev, "10GbE KR mode set\n");
}

static void xgbe_gmii_2500_mode(struct xgbe_prv_data *pdata)
{
	unsigned int reg;

	/* Disable KR training */
	xgbe_an_disable_kr_training(pdata);

	/* Set MAC to 2.5G speed */
	pdata->hw_if.set_gmii_2500_speed(pdata);

	/* Set PCS to KX/1G speed */
	reg = XMDIO_READ(pdata, MDIO_MMD_PCS, MDIO_CTRL2);
	reg &= ~MDIO_PCS_CTRL2_TYPE;
	reg |= MDIO_PCS_CTRL2_10GBX;
	XMDIO_WRITE(pdata, MDIO_MMD_PCS, MDIO_CTRL2, reg);

	reg = XMDIO_READ(pdata, MDIO_MMD_PCS, MDIO_CTRL1);
	reg &= ~MDIO_CTRL1_SPEEDSEL;
	reg |= MDIO_CTRL1_SPEED1G;
	XMDIO_WRITE(pdata, MDIO_MMD_PCS, MDIO_CTRL1, reg);

	xgbe_pcs_power_cycle(pdata);

	/* Set SerDes to 2.5G speed */
	xgbe_serdes_start_ratechange(pdata);

	XSIR1_IOWRITE_BITS(pdata, SIR1_SPEED, DATARATE, XGBE_SPEED_2500_RATE);
	XSIR1_IOWRITE_BITS(pdata, SIR1_SPEED, WORDMODE, XGBE_SPEED_2500_WORD);
	XSIR1_IOWRITE_BITS(pdata, SIR1_SPEED, PLLSEL, XGBE_SPEED_2500_PLL);

	XSIR1_IOWRITE_BITS(pdata, SIR1_SPEED, CDR_RATE,
			   pdata->serdes_cdr_rate[XGBE_SPEED_2500]);
	XSIR1_IOWRITE_BITS(pdata, SIR1_SPEED, TXAMP,
			   pdata->serdes_tx_amp[XGBE_SPEED_2500]);
	XRXTX_IOWRITE_BITS(pdata, RXTX_REG20, BLWC_ENA,
			   pdata->serdes_blwc[XGBE_SPEED_2500]);
	XRXTX_IOWRITE_BITS(pdata, RXTX_REG114, PQ_REG,
			   pdata->serdes_pq_skew[XGBE_SPEED_2500]);
	XRXTX_IOWRITE_BITS(pdata, RXTX_REG129, RXDFE_CONFIG,
			   pdata->serdes_dfe_tap_cfg[XGBE_SPEED_2500]);
	XRXTX_IOWRITE(pdata, RXTX_REG22,
		      pdata->serdes_dfe_tap_ena[XGBE_SPEED_2500]);

	xgbe_serdes_complete_ratechange(pdata);

	netif_dbg(pdata, link, pdata->netdev, "2.5GbE KX mode set\n");
}

static void xgbe_gmii_mode(struct xgbe_prv_data *pdata)
{
	unsigned int reg;

	/* Disable KR training */
	xgbe_an_disable_kr_training(pdata);

	/* Set MAC to 1G speed */
	pdata->hw_if.set_gmii_speed(pdata);

	/* Set PCS to KX/1G speed */
	reg = XMDIO_READ(pdata, MDIO_MMD_PCS, MDIO_CTRL2);
	reg &= ~MDIO_PCS_CTRL2_TYPE;
	reg |= MDIO_PCS_CTRL2_10GBX;
	XMDIO_WRITE(pdata, MDIO_MMD_PCS, MDIO_CTRL2, reg);

	reg = XMDIO_READ(pdata, MDIO_MMD_PCS, MDIO_CTRL1);
	reg &= ~MDIO_CTRL1_SPEEDSEL;
	reg |= MDIO_CTRL1_SPEED1G;
	XMDIO_WRITE(pdata, MDIO_MMD_PCS, MDIO_CTRL1, reg);

	xgbe_pcs_power_cycle(pdata);

	/* Set SerDes to 1G speed */
	xgbe_serdes_start_ratechange(pdata);

	XSIR1_IOWRITE_BITS(pdata, SIR1_SPEED, DATARATE, XGBE_SPEED_1000_RATE);
	XSIR1_IOWRITE_BITS(pdata, SIR1_SPEED, WORDMODE, XGBE_SPEED_1000_WORD);
	XSIR1_IOWRITE_BITS(pdata, SIR1_SPEED, PLLSEL, XGBE_SPEED_1000_PLL);

	XSIR1_IOWRITE_BITS(pdata, SIR1_SPEED, CDR_RATE,
			   pdata->serdes_cdr_rate[XGBE_SPEED_1000]);
	XSIR1_IOWRITE_BITS(pdata, SIR1_SPEED, TXAMP,
			   pdata->serdes_tx_amp[XGBE_SPEED_1000]);
	XRXTX_IOWRITE_BITS(pdata, RXTX_REG20, BLWC_ENA,
			   pdata->serdes_blwc[XGBE_SPEED_1000]);
	XRXTX_IOWRITE_BITS(pdata, RXTX_REG114, PQ_REG,
			   pdata->serdes_pq_skew[XGBE_SPEED_1000]);
	XRXTX_IOWRITE_BITS(pdata, RXTX_REG129, RXDFE_CONFIG,
			   pdata->serdes_dfe_tap_cfg[XGBE_SPEED_1000]);
	XRXTX_IOWRITE(pdata, RXTX_REG22,
		      pdata->serdes_dfe_tap_ena[XGBE_SPEED_1000]);

	xgbe_serdes_complete_ratechange(pdata);

	netif_dbg(pdata, link, pdata->netdev, "1GbE KX mode set\n");
}

static void xgbe_cur_mode(struct xgbe_prv_data *pdata,
			  enum xgbe_mode *mode)
{
	unsigned int reg;

	reg = XMDIO_READ(pdata, MDIO_MMD_PCS, MDIO_CTRL2);
	if ((reg & MDIO_PCS_CTRL2_TYPE) == MDIO_PCS_CTRL2_10GBR)
		*mode = XGBE_MODE_KR;
	else
		*mode = XGBE_MODE_KX;
}

static bool xgbe_in_kr_mode(struct xgbe_prv_data *pdata)
{
	enum xgbe_mode mode;

	xgbe_cur_mode(pdata, &mode);

	return (mode == XGBE_MODE_KR);
}

static void xgbe_switch_mode(struct xgbe_prv_data *pdata)
{
	/* If we are in KR switch to KX, and vice-versa */
	if (xgbe_in_kr_mode(pdata)) {
		if (pdata->speed_set == XGBE_SPEEDSET_1000_10000)
			xgbe_gmii_mode(pdata);
		else
			xgbe_gmii_2500_mode(pdata);
	} else {
		xgbe_xgmii_mode(pdata);
	}
}

static void xgbe_set_mode(struct xgbe_prv_data *pdata,
			  enum xgbe_mode mode)
{
	enum xgbe_mode cur_mode;

	xgbe_cur_mode(pdata, &cur_mode);
	if (mode != cur_mode)
		xgbe_switch_mode(pdata);
}

static bool xgbe_use_xgmii_mode(struct xgbe_prv_data *pdata)
{
	if (pdata->phy.autoneg == AUTONEG_ENABLE) {
		if (pdata->phy.advertising & ADVERTISED_10000baseKR_Full)
			return true;
	} else {
		if (pdata->phy.speed == SPEED_10000)
			return true;
	}

	return false;
}

static bool xgbe_use_gmii_2500_mode(struct xgbe_prv_data *pdata)
{
	if (pdata->phy.autoneg == AUTONEG_ENABLE) {
		if (pdata->phy.advertising & ADVERTISED_2500baseX_Full)
			return true;
	} else {
		if (pdata->phy.speed == SPEED_2500)
			return true;
	}

	return false;
}

static bool xgbe_use_gmii_mode(struct xgbe_prv_data *pdata)
{
	if (pdata->phy.autoneg == AUTONEG_ENABLE) {
		if (pdata->phy.advertising & ADVERTISED_1000baseKX_Full)
			return true;
	} else {
		if (pdata->phy.speed == SPEED_1000)
			return true;
	}

	return false;
}

static void xgbe_set_an(struct xgbe_prv_data *pdata, bool enable, bool restart)
{
	unsigned int reg;

	reg = XMDIO_READ(pdata, MDIO_MMD_AN, MDIO_CTRL1);
	reg &= ~MDIO_AN_CTRL1_ENABLE;

	if (enable)
		reg |= MDIO_AN_CTRL1_ENABLE;

	if (restart)
		reg |= MDIO_AN_CTRL1_RESTART;

	XMDIO_WRITE(pdata, MDIO_MMD_AN, MDIO_CTRL1, reg);
}

static void xgbe_restart_an(struct xgbe_prv_data *pdata)
{
	xgbe_set_an(pdata, true, true);

	netif_dbg(pdata, link, pdata->netdev, "AN enabled/restarted\n");
}

static void xgbe_disable_an(struct xgbe_prv_data *pdata)
{
	xgbe_set_an(pdata, false, false);

	netif_dbg(pdata, link, pdata->netdev, "AN disabled\n");
}

static enum xgbe_an xgbe_an_tx_training(struct xgbe_prv_data *pdata,
					enum xgbe_rx *state)
{
	unsigned int ad_reg, lp_reg, reg;

	*state = XGBE_RX_COMPLETE;

	/* If we're not in KR mode then we're done */
	if (!xgbe_in_kr_mode(pdata))
		return XGBE_AN_PAGE_RECEIVED;

	/* Enable/Disable FEC */
	ad_reg = XMDIO_READ(pdata, MDIO_MMD_AN, MDIO_AN_ADVERTISE + 2);
	lp_reg = XMDIO_READ(pdata, MDIO_MMD_AN, MDIO_AN_LPA + 2);

	reg = XMDIO_READ(pdata, MDIO_MMD_PMAPMD, MDIO_PMA_10GBR_FECCTRL);
	reg &= ~(MDIO_PMA_10GBR_FECABLE_ABLE | MDIO_PMA_10GBR_FECABLE_ERRABLE);
	if ((ad_reg & 0xc000) && (lp_reg & 0xc000))
		reg |= pdata->fec_ability;

	XMDIO_WRITE(pdata, MDIO_MMD_PMAPMD, MDIO_PMA_10GBR_FECCTRL, reg);

	/* Start KR training */
	reg = XMDIO_READ(pdata, MDIO_MMD_PMAPMD, MDIO_PMA_10GBR_PMD_CTRL);
	if (reg & XGBE_KR_TRAINING_ENABLE) {
		XSIR0_IOWRITE_BITS(pdata, SIR0_KR_RT_1, RESET, 1);

		reg |= XGBE_KR_TRAINING_START;
		XMDIO_WRITE(pdata, MDIO_MMD_PMAPMD, MDIO_PMA_10GBR_PMD_CTRL,
			    reg);

		XSIR0_IOWRITE_BITS(pdata, SIR0_KR_RT_1, RESET, 0);

		netif_dbg(pdata, link, pdata->netdev,
			  "KR training initiated\n");
	}

	return XGBE_AN_PAGE_RECEIVED;
}

static enum xgbe_an xgbe_an_tx_xnp(struct xgbe_prv_data *pdata,
				   enum xgbe_rx *state)
{
	u16 msg;

	*state = XGBE_RX_XNP;

	msg = XGBE_XNP_MCF_NULL_MESSAGE;
	msg |= XGBE_XNP_MP_FORMATTED;

	XMDIO_WRITE(pdata, MDIO_MMD_AN, MDIO_AN_XNP + 2, 0);
	XMDIO_WRITE(pdata, MDIO_MMD_AN, MDIO_AN_XNP + 1, 0);
	XMDIO_WRITE(pdata, MDIO_MMD_AN, MDIO_AN_XNP, msg);

	return XGBE_AN_PAGE_RECEIVED;
}

static enum xgbe_an xgbe_an_rx_bpa(struct xgbe_prv_data *pdata,
				   enum xgbe_rx *state)
{
	unsigned int link_support;
	unsigned int reg, ad_reg, lp_reg;

	/* Read Base Ability register 2 first */
	reg = XMDIO_READ(pdata, MDIO_MMD_AN, MDIO_AN_LPA + 1);

	/* Check for a supported mode, otherwise restart in a different one */
	link_support = xgbe_in_kr_mode(pdata) ? 0x80 : 0x20;
	if (!(reg & link_support))
		return XGBE_AN_INCOMPAT_LINK;

	/* Check Extended Next Page support */
	ad_reg = XMDIO_READ(pdata, MDIO_MMD_AN, MDIO_AN_ADVERTISE);
	lp_reg = XMDIO_READ(pdata, MDIO_MMD_AN, MDIO_AN_LPA);

	return ((ad_reg & XGBE_XNP_NP_EXCHANGE) ||
		(lp_reg & XGBE_XNP_NP_EXCHANGE))
	       ? xgbe_an_tx_xnp(pdata, state)
	       : xgbe_an_tx_training(pdata, state);
}

static enum xgbe_an xgbe_an_rx_xnp(struct xgbe_prv_data *pdata,
				   enum xgbe_rx *state)
{
	unsigned int ad_reg, lp_reg;

	/* Check Extended Next Page support */
	ad_reg = XMDIO_READ(pdata, MDIO_MMD_AN, MDIO_AN_XNP);
	lp_reg = XMDIO_READ(pdata, MDIO_MMD_AN, MDIO_AN_LPX);

	return ((ad_reg & XGBE_XNP_NP_EXCHANGE) ||
		(lp_reg & XGBE_XNP_NP_EXCHANGE))
	       ? xgbe_an_tx_xnp(pdata, state)
	       : xgbe_an_tx_training(pdata, state);
}

static enum xgbe_an xgbe_an_page_received(struct xgbe_prv_data *pdata)
{
	enum xgbe_rx *state;
	unsigned long an_timeout;
	enum xgbe_an ret;

	if (!pdata->an_start) {
		pdata->an_start = jiffies;
	} else {
		an_timeout = pdata->an_start +
			     msecs_to_jiffies(XGBE_AN_MS_TIMEOUT);
		if (time_after(jiffies, an_timeout)) {
			/* Auto-negotiation timed out, reset state */
			pdata->kr_state = XGBE_RX_BPA;
			pdata->kx_state = XGBE_RX_BPA;

			pdata->an_start = jiffies;

			netif_dbg(pdata, link, pdata->netdev,
				  "AN timed out, resetting state\n");
		}
	}

	state = xgbe_in_kr_mode(pdata) ? &pdata->kr_state
					   : &pdata->kx_state;

	switch (*state) {
	case XGBE_RX_BPA:
		ret = xgbe_an_rx_bpa(pdata, state);
		break;

	case XGBE_RX_XNP:
		ret = xgbe_an_rx_xnp(pdata, state);
		break;

	default:
		ret = XGBE_AN_ERROR;
	}

	return ret;
}

static enum xgbe_an xgbe_an_incompat_link(struct xgbe_prv_data *pdata)
{
	/* Be sure we aren't looping trying to negotiate */
	if (xgbe_in_kr_mode(pdata)) {
		pdata->kr_state = XGBE_RX_ERROR;

		if (!(pdata->phy.advertising & ADVERTISED_1000baseKX_Full) &&
		    !(pdata->phy.advertising & ADVERTISED_2500baseX_Full))
			return XGBE_AN_NO_LINK;

		if (pdata->kx_state != XGBE_RX_BPA)
			return XGBE_AN_NO_LINK;
	} else {
		pdata->kx_state = XGBE_RX_ERROR;

		if (!(pdata->phy.advertising & ADVERTISED_10000baseKR_Full))
			return XGBE_AN_NO_LINK;

		if (pdata->kr_state != XGBE_RX_BPA)
			return XGBE_AN_NO_LINK;
	}

	xgbe_disable_an(pdata);

	xgbe_switch_mode(pdata);

	xgbe_restart_an(pdata);

	return XGBE_AN_INCOMPAT_LINK;
}

static irqreturn_t xgbe_an_isr(int irq, void *data)
{
	struct xgbe_prv_data *pdata = (struct xgbe_prv_data *)data;

	netif_dbg(pdata, intr, pdata->netdev, "AN interrupt received\n");

	/* Interrupt reason must be read and cleared outside of IRQ context */
	disable_irq_nosync(pdata->an_irq);

	queue_work(pdata->an_workqueue, &pdata->an_irq_work);

	return IRQ_HANDLED;
}

static void xgbe_an_irq_work(struct work_struct *work)
{
	struct xgbe_prv_data *pdata = container_of(work,
						   struct xgbe_prv_data,
						   an_irq_work);

	/* Avoid a race between enabling the IRQ and exiting the work by
	 * waiting for the work to finish and then queueing it
	 */
	flush_work(&pdata->an_work);
	queue_work(pdata->an_workqueue, &pdata->an_work);
}

static const char *xgbe_state_as_string(enum xgbe_an state)
{
	switch (state) {
	case XGBE_AN_READY:
		return "Ready";
	case XGBE_AN_PAGE_RECEIVED:
		return "Page-Received";
	case XGBE_AN_INCOMPAT_LINK:
		return "Incompatible-Link";
	case XGBE_AN_COMPLETE:
		return "Complete";
	case XGBE_AN_NO_LINK:
		return "No-Link";
	case XGBE_AN_ERROR:
		return "Error";
	default:
		return "Undefined";
	}
}

static void xgbe_an_state_machine(struct work_struct *work)
{
	struct xgbe_prv_data *pdata = container_of(work,
						   struct xgbe_prv_data,
						   an_work);
	enum xgbe_an cur_state = pdata->an_state;
	unsigned int int_reg, int_mask;

	mutex_lock(&pdata->an_mutex);

	/* Read the interrupt */
	int_reg = XMDIO_READ(pdata, MDIO_MMD_AN, MDIO_AN_INT);
	if (!int_reg)
		goto out;

next_int:
	if (int_reg & XGBE_AN_PG_RCV) {
		pdata->an_state = XGBE_AN_PAGE_RECEIVED;
		int_mask = XGBE_AN_PG_RCV;
	} else if (int_reg & XGBE_AN_INC_LINK) {
		pdata->an_state = XGBE_AN_INCOMPAT_LINK;
		int_mask = XGBE_AN_INC_LINK;
	} else if (int_reg & XGBE_AN_INT_CMPLT) {
		pdata->an_state = XGBE_AN_COMPLETE;
		int_mask = XGBE_AN_INT_CMPLT;
	} else {
		pdata->an_state = XGBE_AN_ERROR;
		int_mask = 0;
	}

	/* Clear the interrupt to be processed */
	int_reg &= ~int_mask;
	XMDIO_WRITE(pdata, MDIO_MMD_AN, MDIO_AN_INT, int_reg);

	pdata->an_result = pdata->an_state;

again:
	netif_dbg(pdata, link, pdata->netdev, "AN %s\n",
		  xgbe_state_as_string(pdata->an_state));

	cur_state = pdata->an_state;

	switch (pdata->an_state) {
	case XGBE_AN_READY:
		pdata->an_supported = 0;
		break;

	case XGBE_AN_PAGE_RECEIVED:
		pdata->an_state = xgbe_an_page_received(pdata);
		pdata->an_supported++;
		break;

	case XGBE_AN_INCOMPAT_LINK:
		pdata->an_supported = 0;
		pdata->parallel_detect = 0;
		pdata->an_state = xgbe_an_incompat_link(pdata);
		break;

	case XGBE_AN_COMPLETE:
		pdata->parallel_detect = pdata->an_supported ? 0 : 1;
		netif_dbg(pdata, link, pdata->netdev, "%s successful\n",
			  pdata->an_supported ? "Auto negotiation"
					      : "Parallel detection");
		break;

	case XGBE_AN_NO_LINK:
		break;

	default:
		pdata->an_state = XGBE_AN_ERROR;
	}

	if (pdata->an_state == XGBE_AN_NO_LINK) {
		int_reg = 0;
		XMDIO_WRITE(pdata, MDIO_MMD_AN, MDIO_AN_INT, 0);
	} else if (pdata->an_state == XGBE_AN_ERROR) {
		netdev_err(pdata->netdev,
			   "error during auto-negotiation, state=%u\n",
			   cur_state);

		int_reg = 0;
		XMDIO_WRITE(pdata, MDIO_MMD_AN, MDIO_AN_INT, 0);
	}

	if (pdata->an_state >= XGBE_AN_COMPLETE) {
		pdata->an_result = pdata->an_state;
		pdata->an_state = XGBE_AN_READY;
		pdata->kr_state = XGBE_RX_BPA;
		pdata->kx_state = XGBE_RX_BPA;
		pdata->an_start = 0;

		netif_dbg(pdata, link, pdata->netdev, "AN result: %s\n",
			  xgbe_state_as_string(pdata->an_result));
	}

	if (cur_state != pdata->an_state)
		goto again;

	if (int_reg)
		goto next_int;

out:
	enable_irq(pdata->an_irq);

	mutex_unlock(&pdata->an_mutex);
}

static void xgbe_an_init(struct xgbe_prv_data *pdata)
{
	unsigned int reg;

	/* Set up Advertisement register 3 first */
	reg = XMDIO_READ(pdata, MDIO_MMD_AN, MDIO_AN_ADVERTISE + 2);
	if (pdata->phy.advertising & ADVERTISED_10000baseR_FEC)
		reg |= 0xc000;
	else
		reg &= ~0xc000;

	XMDIO_WRITE(pdata, MDIO_MMD_AN, MDIO_AN_ADVERTISE + 2, reg);

	/* Set up Advertisement register 2 next */
	reg = XMDIO_READ(pdata, MDIO_MMD_AN, MDIO_AN_ADVERTISE + 1);
	if (pdata->phy.advertising & ADVERTISED_10000baseKR_Full)
		reg |= 0x80;
	else
		reg &= ~0x80;

	if ((pdata->phy.advertising & ADVERTISED_1000baseKX_Full) ||
	    (pdata->phy.advertising & ADVERTISED_2500baseX_Full))
		reg |= 0x20;
	else
		reg &= ~0x20;

	XMDIO_WRITE(pdata, MDIO_MMD_AN, MDIO_AN_ADVERTISE + 1, reg);

	/* Set up Advertisement register 1 last */
	reg = XMDIO_READ(pdata, MDIO_MMD_AN, MDIO_AN_ADVERTISE);
	if (pdata->phy.advertising & ADVERTISED_Pause)
		reg |= 0x400;
	else
		reg &= ~0x400;

	if (pdata->phy.advertising & ADVERTISED_Asym_Pause)
		reg |= 0x800;
	else
		reg &= ~0x800;

	/* We don't intend to perform XNP */
	reg &= ~XGBE_XNP_NP_EXCHANGE;

	XMDIO_WRITE(pdata, MDIO_MMD_AN, MDIO_AN_ADVERTISE, reg);

	netif_dbg(pdata, link, pdata->netdev, "AN initialized\n");
}

static const char *xgbe_phy_fc_string(struct xgbe_prv_data *pdata)
{
	if (pdata->tx_pause && pdata->rx_pause)
		return "rx/tx";
	else if (pdata->rx_pause)
		return "rx";
	else if (pdata->tx_pause)
		return "tx";
	else
		return "off";
}

static const char *xgbe_phy_speed_string(int speed)
{
	switch (speed) {
	case SPEED_1000:
		return "1Gbps";
	case SPEED_2500:
		return "2.5Gbps";
	case SPEED_10000:
		return "10Gbps";
	case SPEED_UNKNOWN:
		return "Unknown";
	default:
		return "Unsupported";
	}
}

static void xgbe_phy_print_status(struct xgbe_prv_data *pdata)
{
	if (pdata->phy.link)
		netdev_info(pdata->netdev,
			    "Link is Up - %s/%s - flow control %s\n",
			    xgbe_phy_speed_string(pdata->phy.speed),
			    pdata->phy.duplex == DUPLEX_FULL ? "Full" : "Half",
			    xgbe_phy_fc_string(pdata));
	else
		netdev_info(pdata->netdev, "Link is Down\n");
}

static void xgbe_phy_adjust_link(struct xgbe_prv_data *pdata)
{
	int new_state = 0;

	if (pdata->phy.link) {
		/* Flow control support */
		pdata->pause_autoneg = pdata->phy.pause_autoneg;

		if (pdata->tx_pause != pdata->phy.tx_pause) {
			new_state = 1;
			pdata->hw_if.config_tx_flow_control(pdata);
			pdata->tx_pause = pdata->phy.tx_pause;
		}

		if (pdata->rx_pause != pdata->phy.rx_pause) {
			new_state = 1;
			pdata->hw_if.config_rx_flow_control(pdata);
			pdata->rx_pause = pdata->phy.rx_pause;
		}

		/* Speed support */
		if (pdata->phy_speed != pdata->phy.speed) {
			new_state = 1;
			pdata->phy_speed = pdata->phy.speed;
		}

		if (pdata->phy_link != pdata->phy.link) {
			new_state = 1;
			pdata->phy_link = pdata->phy.link;
		}
	} else if (pdata->phy_link) {
		new_state = 1;
		pdata->phy_link = 0;
		pdata->phy_speed = SPEED_UNKNOWN;
	}

	if (new_state && netif_msg_link(pdata))
		xgbe_phy_print_status(pdata);
}

static int xgbe_phy_config_fixed(struct xgbe_prv_data *pdata)
{
	netif_dbg(pdata, link, pdata->netdev, "fixed PHY configuration\n");

	/* Disable auto-negotiation */
	xgbe_disable_an(pdata);

	/* Validate/Set specified speed */
	switch (pdata->phy.speed) {
	case SPEED_10000:
		xgbe_set_mode(pdata, XGBE_MODE_KR);
		break;

	case SPEED_2500:
	case SPEED_1000:
		xgbe_set_mode(pdata, XGBE_MODE_KX);
		break;

	default:
		return -EINVAL;
	}

	/* Validate duplex mode */
	if (pdata->phy.duplex != DUPLEX_FULL)
		return -EINVAL;

	return 0;
}

static int __xgbe_phy_config_aneg(struct xgbe_prv_data *pdata)
{
	set_bit(XGBE_LINK_INIT, &pdata->dev_state);
	pdata->link_check = jiffies;

	if (pdata->phy.autoneg != AUTONEG_ENABLE)
		return xgbe_phy_config_fixed(pdata);

	netif_dbg(pdata, link, pdata->netdev, "AN PHY configuration\n");

	/* Disable auto-negotiation interrupt */
	disable_irq(pdata->an_irq);

	/* Start auto-negotiation in a supported mode */
	if (pdata->phy.advertising & ADVERTISED_10000baseKR_Full) {
		xgbe_set_mode(pdata, XGBE_MODE_KR);
	} else if ((pdata->phy.advertising & ADVERTISED_1000baseKX_Full) ||
		   (pdata->phy.advertising & ADVERTISED_2500baseX_Full)) {
		xgbe_set_mode(pdata, XGBE_MODE_KX);
	} else {
		enable_irq(pdata->an_irq);
		return -EINVAL;
	}

	/* Disable and stop any in progress auto-negotiation */
	xgbe_disable_an(pdata);

	/* Clear any auto-negotitation interrupts */
	XMDIO_WRITE(pdata, MDIO_MMD_AN, MDIO_AN_INT, 0);

	pdata->an_result = XGBE_AN_READY;
	pdata->an_state = XGBE_AN_READY;
	pdata->kr_state = XGBE_RX_BPA;
	pdata->kx_state = XGBE_RX_BPA;

	/* Re-enable auto-negotiation interrupt */
	enable_irq(pdata->an_irq);

	/* Set up advertisement registers based on current settings */
	xgbe_an_init(pdata);

	/* Enable and start auto-negotiation */
	xgbe_restart_an(pdata);

	return 0;
}

static int xgbe_phy_config_aneg(struct xgbe_prv_data *pdata)
{
	int ret;

	mutex_lock(&pdata->an_mutex);

	ret = __xgbe_phy_config_aneg(pdata);
	if (ret)
		set_bit(XGBE_LINK_ERR, &pdata->dev_state);
	else
		clear_bit(XGBE_LINK_ERR, &pdata->dev_state);

	mutex_unlock(&pdata->an_mutex);

	return ret;
}

static bool xgbe_phy_aneg_done(struct xgbe_prv_data *pdata)
{
	return (pdata->an_result == XGBE_AN_COMPLETE);
}

static void xgbe_check_link_timeout(struct xgbe_prv_data *pdata)
{
	unsigned long link_timeout;

	link_timeout = pdata->link_check + (XGBE_LINK_TIMEOUT * HZ);
	if (time_after(jiffies, link_timeout)) {
		netif_dbg(pdata, link, pdata->netdev, "AN link timeout\n");
		xgbe_phy_config_aneg(pdata);
	}
}

static void xgbe_phy_status_force(struct xgbe_prv_data *pdata)
{
	if (xgbe_in_kr_mode(pdata)) {
		pdata->phy.speed = SPEED_10000;
	} else {
		switch (pdata->speed_set) {
		case XGBE_SPEEDSET_1000_10000:
			pdata->phy.speed = SPEED_1000;
			break;

		case XGBE_SPEEDSET_2500_10000:
			pdata->phy.speed = SPEED_2500;
			break;
		}
	}
	pdata->phy.duplex = DUPLEX_FULL;
}

static void xgbe_phy_status_aneg(struct xgbe_prv_data *pdata)
{
	unsigned int ad_reg, lp_reg;

	pdata->phy.lp_advertising = 0;

	if ((pdata->phy.autoneg != AUTONEG_ENABLE) || pdata->parallel_detect)
		return xgbe_phy_status_force(pdata);

	pdata->phy.lp_advertising |= ADVERTISED_Autoneg;
	pdata->phy.lp_advertising |= ADVERTISED_Backplane;

	/* Compare Advertisement and Link Partner register 1 */
	ad_reg = XMDIO_READ(pdata, MDIO_MMD_AN, MDIO_AN_ADVERTISE);
	lp_reg = XMDIO_READ(pdata, MDIO_MMD_AN, MDIO_AN_LPA);
	if (lp_reg & 0x400)
		pdata->phy.lp_advertising |= ADVERTISED_Pause;
	if (lp_reg & 0x800)
		pdata->phy.lp_advertising |= ADVERTISED_Asym_Pause;

	if (pdata->phy.pause_autoneg) {
		/* Set flow control based on auto-negotiation result */
		pdata->phy.tx_pause = 0;
		pdata->phy.rx_pause = 0;

		if (ad_reg & lp_reg & 0x400) {
			pdata->phy.tx_pause = 1;
			pdata->phy.rx_pause = 1;
		} else if (ad_reg & lp_reg & 0x800) {
			if (ad_reg & 0x400)
				pdata->phy.rx_pause = 1;
			else if (lp_reg & 0x400)
				pdata->phy.tx_pause = 1;
		}
	}

	/* Compare Advertisement and Link Partner register 2 */
	ad_reg = XMDIO_READ(pdata, MDIO_MMD_AN, MDIO_AN_ADVERTISE + 1);
	lp_reg = XMDIO_READ(pdata, MDIO_MMD_AN, MDIO_AN_LPA + 1);
	if (lp_reg & 0x80)
		pdata->phy.lp_advertising |= ADVERTISED_10000baseKR_Full;
	if (lp_reg & 0x20) {
		switch (pdata->speed_set) {
		case XGBE_SPEEDSET_1000_10000:
			pdata->phy.lp_advertising |= ADVERTISED_1000baseKX_Full;
			break;
		case XGBE_SPEEDSET_2500_10000:
			pdata->phy.lp_advertising |= ADVERTISED_2500baseX_Full;
			break;
		}
	}

	ad_reg &= lp_reg;
	if (ad_reg & 0x80) {
		pdata->phy.speed = SPEED_10000;
		xgbe_set_mode(pdata, XGBE_MODE_KR);
	} else if (ad_reg & 0x20) {
		switch (pdata->speed_set) {
		case XGBE_SPEEDSET_1000_10000:
			pdata->phy.speed = SPEED_1000;
			break;

		case XGBE_SPEEDSET_2500_10000:
			pdata->phy.speed = SPEED_2500;
			break;
		}

		xgbe_set_mode(pdata, XGBE_MODE_KX);
	} else {
		pdata->phy.speed = SPEED_UNKNOWN;
	}

	/* Compare Advertisement and Link Partner register 3 */
	ad_reg = XMDIO_READ(pdata, MDIO_MMD_AN, MDIO_AN_ADVERTISE + 2);
	lp_reg = XMDIO_READ(pdata, MDIO_MMD_AN, MDIO_AN_LPA + 2);
	if (lp_reg & 0xc000)
		pdata->phy.lp_advertising |= ADVERTISED_10000baseR_FEC;

	pdata->phy.duplex = DUPLEX_FULL;
}

static void xgbe_phy_status(struct xgbe_prv_data *pdata)
{
	unsigned int reg, link_aneg;

	if (test_bit(XGBE_LINK_ERR, &pdata->dev_state)) {
		if (test_and_clear_bit(XGBE_LINK, &pdata->dev_state))
			netif_carrier_off(pdata->netdev);

		pdata->phy.link = 0;
		goto adjust_link;
	}

	link_aneg = (pdata->phy.autoneg == AUTONEG_ENABLE);

	/* Get the link status. Link status is latched low, so read
	 * once to clear and then read again to get current state
	 */
	reg = XMDIO_READ(pdata, MDIO_MMD_PCS, MDIO_STAT1);
	reg = XMDIO_READ(pdata, MDIO_MMD_PCS, MDIO_STAT1);
	pdata->phy.link = (reg & MDIO_STAT1_LSTATUS) ? 1 : 0;

	if (pdata->phy.link) {
		if (link_aneg && !xgbe_phy_aneg_done(pdata)) {
			xgbe_check_link_timeout(pdata);
			return;
		}

		xgbe_phy_status_aneg(pdata);

		if (test_bit(XGBE_LINK_INIT, &pdata->dev_state))
			clear_bit(XGBE_LINK_INIT, &pdata->dev_state);

		if (!test_bit(XGBE_LINK, &pdata->dev_state)) {
			set_bit(XGBE_LINK, &pdata->dev_state);
			netif_carrier_on(pdata->netdev);
		}
	} else {
		if (test_bit(XGBE_LINK_INIT, &pdata->dev_state)) {
			xgbe_check_link_timeout(pdata);

			if (link_aneg)
				return;
		}

		xgbe_phy_status_aneg(pdata);

		if (test_bit(XGBE_LINK, &pdata->dev_state)) {
			clear_bit(XGBE_LINK, &pdata->dev_state);
			netif_carrier_off(pdata->netdev);
		}
	}

adjust_link:
	xgbe_phy_adjust_link(pdata);
}

static void xgbe_phy_stop(struct xgbe_prv_data *pdata)
{
	netif_dbg(pdata, link, pdata->netdev, "stopping PHY\n");

	/* Disable auto-negotiation */
	xgbe_disable_an(pdata);

	/* Disable auto-negotiation interrupts */
	XMDIO_WRITE(pdata, MDIO_MMD_AN, MDIO_AN_INTMASK, 0);

	devm_free_irq(pdata->dev, pdata->an_irq, pdata);

	pdata->phy.link = 0;
	if (test_and_clear_bit(XGBE_LINK, &pdata->dev_state))
		netif_carrier_off(pdata->netdev);

	xgbe_phy_adjust_link(pdata);
}

static int xgbe_phy_start(struct xgbe_prv_data *pdata)
{
	struct net_device *netdev = pdata->netdev;
	int ret;

	netif_dbg(pdata, link, pdata->netdev, "starting PHY\n");

	ret = devm_request_irq(pdata->dev, pdata->an_irq,
			       xgbe_an_isr, 0, pdata->an_name,
			       pdata);
	if (ret) {
		netdev_err(netdev, "phy irq request failed\n");
		return ret;
	}

	/* Set initial mode - call the mode setting routines
	 * directly to insure we are properly configured
	 */
	if (xgbe_use_xgmii_mode(pdata)) {
		xgbe_xgmii_mode(pdata);
	} else if (xgbe_use_gmii_mode(pdata)) {
		xgbe_gmii_mode(pdata);
	} else if (xgbe_use_gmii_2500_mode(pdata)) {
		xgbe_gmii_2500_mode(pdata);
	} else {
		ret = -EINVAL;
		goto err_irq;
	}

	/* Set up advertisement registers based on current settings */
	xgbe_an_init(pdata);

	/* Enable auto-negotiation interrupts */
	XMDIO_WRITE(pdata, MDIO_MMD_AN, MDIO_AN_INTMASK, 0x07);

	return xgbe_phy_config_aneg(pdata);

err_irq:
	devm_free_irq(pdata->dev, pdata->an_irq, pdata);

	return ret;
}

static int xgbe_phy_reset(struct xgbe_prv_data *pdata)
{
	unsigned int count, reg;

	reg = XMDIO_READ(pdata, MDIO_MMD_PCS, MDIO_CTRL1);
	reg |= MDIO_CTRL1_RESET;
	XMDIO_WRITE(pdata, MDIO_MMD_PCS, MDIO_CTRL1, reg);

	count = 50;
	do {
		msleep(20);
		reg = XMDIO_READ(pdata, MDIO_MMD_PCS, MDIO_CTRL1);
	} while ((reg & MDIO_CTRL1_RESET) && --count);

	if (reg & MDIO_CTRL1_RESET)
		return -ETIMEDOUT;

	/* Disable auto-negotiation for now */
	xgbe_disable_an(pdata);

	/* Clear auto-negotiation interrupts */
	XMDIO_WRITE(pdata, MDIO_MMD_AN, MDIO_AN_INT, 0);

	return 0;
}

static void xgbe_dump_phy_registers(struct xgbe_prv_data *pdata)
{
	struct device *dev = pdata->dev;

	dev_dbg(dev, "\n************* PHY Reg dump **********************\n");

	dev_dbg(dev, "PCS Control Reg (%#04x) = %#04x\n", MDIO_CTRL1,
		XMDIO_READ(pdata, MDIO_MMD_PCS, MDIO_CTRL1));
	dev_dbg(dev, "PCS Status Reg (%#04x) = %#04x\n", MDIO_STAT1,
		XMDIO_READ(pdata, MDIO_MMD_PCS, MDIO_STAT1));
	dev_dbg(dev, "Phy Id (PHYS ID 1 %#04x)= %#04x\n", MDIO_DEVID1,
		XMDIO_READ(pdata, MDIO_MMD_PCS, MDIO_DEVID1));
	dev_dbg(dev, "Phy Id (PHYS ID 2 %#04x)= %#04x\n", MDIO_DEVID2,
		XMDIO_READ(pdata, MDIO_MMD_PCS, MDIO_DEVID2));
	dev_dbg(dev, "Devices in Package (%#04x)= %#04x\n", MDIO_DEVS1,
		XMDIO_READ(pdata, MDIO_MMD_PCS, MDIO_DEVS1));
	dev_dbg(dev, "Devices in Package (%#04x)= %#04x\n", MDIO_DEVS2,
		XMDIO_READ(pdata, MDIO_MMD_PCS, MDIO_DEVS2));

	dev_dbg(dev, "Auto-Neg Control Reg (%#04x) = %#04x\n", MDIO_CTRL1,
		XMDIO_READ(pdata, MDIO_MMD_AN, MDIO_CTRL1));
	dev_dbg(dev, "Auto-Neg Status Reg (%#04x) = %#04x\n", MDIO_STAT1,
		XMDIO_READ(pdata, MDIO_MMD_AN, MDIO_STAT1));
	dev_dbg(dev, "Auto-Neg Ad Reg 1 (%#04x) = %#04x\n",
		MDIO_AN_ADVERTISE,
		XMDIO_READ(pdata, MDIO_MMD_AN, MDIO_AN_ADVERTISE));
	dev_dbg(dev, "Auto-Neg Ad Reg 2 (%#04x) = %#04x\n",
		MDIO_AN_ADVERTISE + 1,
		XMDIO_READ(pdata, MDIO_MMD_AN, MDIO_AN_ADVERTISE + 1));
	dev_dbg(dev, "Auto-Neg Ad Reg 3 (%#04x) = %#04x\n",
		MDIO_AN_ADVERTISE + 2,
		XMDIO_READ(pdata, MDIO_MMD_AN, MDIO_AN_ADVERTISE + 2));
	dev_dbg(dev, "Auto-Neg Completion Reg (%#04x) = %#04x\n",
		MDIO_AN_COMP_STAT,
		XMDIO_READ(pdata, MDIO_MMD_AN, MDIO_AN_COMP_STAT));

	dev_dbg(dev, "\n*************************************************\n");
}

static void xgbe_phy_init(struct xgbe_prv_data *pdata)
{
	mutex_init(&pdata->an_mutex);
	INIT_WORK(&pdata->an_irq_work, xgbe_an_irq_work);
	INIT_WORK(&pdata->an_work, xgbe_an_state_machine);
	pdata->mdio_mmd = MDIO_MMD_PCS;

	/* Initialize supported features */
	pdata->phy.supported = SUPPORTED_Autoneg;
	pdata->phy.supported |= SUPPORTED_Pause | SUPPORTED_Asym_Pause;
	pdata->phy.supported |= SUPPORTED_Backplane;
	pdata->phy.supported |= SUPPORTED_10000baseKR_Full;
	switch (pdata->speed_set) {
	case XGBE_SPEEDSET_1000_10000:
		pdata->phy.supported |= SUPPORTED_1000baseKX_Full;
		break;
	case XGBE_SPEEDSET_2500_10000:
		pdata->phy.supported |= SUPPORTED_2500baseX_Full;
		break;
	}

	pdata->fec_ability = XMDIO_READ(pdata, MDIO_MMD_PMAPMD,
					MDIO_PMA_10GBR_FECABLE);
	pdata->fec_ability &= (MDIO_PMA_10GBR_FECABLE_ABLE |
			       MDIO_PMA_10GBR_FECABLE_ERRABLE);
	if (pdata->fec_ability & MDIO_PMA_10GBR_FECABLE_ABLE)
		pdata->phy.supported |= SUPPORTED_10000baseR_FEC;

	pdata->phy.advertising = pdata->phy.supported;

	pdata->phy.address = 0;

	pdata->phy.autoneg = AUTONEG_ENABLE;
	pdata->phy.speed = SPEED_UNKNOWN;
	pdata->phy.duplex = DUPLEX_UNKNOWN;

	pdata->phy.link = 0;

	pdata->phy.pause_autoneg = pdata->pause_autoneg;
	pdata->phy.tx_pause = pdata->tx_pause;
	pdata->phy.rx_pause = pdata->rx_pause;

	/* Fix up Flow Control advertising */
	pdata->phy.advertising &= ~ADVERTISED_Pause;
	pdata->phy.advertising &= ~ADVERTISED_Asym_Pause;

	if (pdata->rx_pause) {
		pdata->phy.advertising |= ADVERTISED_Pause;
		pdata->phy.advertising |= ADVERTISED_Asym_Pause;
	}

	if (pdata->tx_pause)
		pdata->phy.advertising ^= ADVERTISED_Asym_Pause;

	if (netif_msg_drv(pdata))
		xgbe_dump_phy_registers(pdata);
}

void xgbe_init_function_ptrs_phy(struct xgbe_phy_if *phy_if)
{
	phy_if->phy_init        = xgbe_phy_init;

	phy_if->phy_reset       = xgbe_phy_reset;
	phy_if->phy_start       = xgbe_phy_start;
	phy_if->phy_stop        = xgbe_phy_stop;

	phy_if->phy_status      = xgbe_phy_status;
	phy_if->phy_config_aneg = xgbe_phy_config_aneg;
}
