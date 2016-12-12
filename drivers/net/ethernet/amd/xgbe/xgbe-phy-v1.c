/*
 * AMD 10Gb Ethernet driver
 *
 * This file is available to you under your choice of the following two
 * licenses:
 *
 * License 1: GPLv2
 *
 * Copyright (c) 2016 Advanced Micro Devices, Inc.
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
 * Copyright (c) 2016 Advanced Micro Devices, Inc.
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
#include <linux/device.h>
#include <linux/property.h>
#include <linux/mdio.h>
#include <linux/phy.h>

#include "xgbe.h"
#include "xgbe-common.h"

#define XGBE_BLWC_PROPERTY		"amd,serdes-blwc"
#define XGBE_CDR_RATE_PROPERTY		"amd,serdes-cdr-rate"
#define XGBE_PQ_SKEW_PROPERTY		"amd,serdes-pq-skew"
#define XGBE_TX_AMP_PROPERTY		"amd,serdes-tx-amp"
#define XGBE_DFE_CFG_PROPERTY		"amd,serdes-dfe-tap-config"
#define XGBE_DFE_ENA_PROPERTY		"amd,serdes-dfe-tap-enable"

/* Default SerDes settings */
#define XGBE_SPEED_1000_BLWC		1
#define XGBE_SPEED_1000_CDR		0x2
#define XGBE_SPEED_1000_PLL		0x0
#define XGBE_SPEED_1000_PQ		0xa
#define XGBE_SPEED_1000_RATE		0x3
#define XGBE_SPEED_1000_TXAMP		0xf
#define XGBE_SPEED_1000_WORD		0x1
#define XGBE_SPEED_1000_DFE_TAP_CONFIG	0x3
#define XGBE_SPEED_1000_DFE_TAP_ENABLE	0x0

#define XGBE_SPEED_2500_BLWC		1
#define XGBE_SPEED_2500_CDR		0x2
#define XGBE_SPEED_2500_PLL		0x0
#define XGBE_SPEED_2500_PQ		0xa
#define XGBE_SPEED_2500_RATE		0x1
#define XGBE_SPEED_2500_TXAMP		0xf
#define XGBE_SPEED_2500_WORD		0x1
#define XGBE_SPEED_2500_DFE_TAP_CONFIG	0x3
#define XGBE_SPEED_2500_DFE_TAP_ENABLE	0x0

#define XGBE_SPEED_10000_BLWC		0
#define XGBE_SPEED_10000_CDR		0x7
#define XGBE_SPEED_10000_PLL		0x1
#define XGBE_SPEED_10000_PQ		0x12
#define XGBE_SPEED_10000_RATE		0x0
#define XGBE_SPEED_10000_TXAMP		0xa
#define XGBE_SPEED_10000_WORD		0x7
#define XGBE_SPEED_10000_DFE_TAP_CONFIG	0x1
#define XGBE_SPEED_10000_DFE_TAP_ENABLE	0x7f

/* Rate-change complete wait/retry count */
#define XGBE_RATECHANGE_COUNT		500

static const u32 xgbe_phy_blwc[] = {
	XGBE_SPEED_1000_BLWC,
	XGBE_SPEED_2500_BLWC,
	XGBE_SPEED_10000_BLWC,
};

static const u32 xgbe_phy_cdr_rate[] = {
	XGBE_SPEED_1000_CDR,
	XGBE_SPEED_2500_CDR,
	XGBE_SPEED_10000_CDR,
};

static const u32 xgbe_phy_pq_skew[] = {
	XGBE_SPEED_1000_PQ,
	XGBE_SPEED_2500_PQ,
	XGBE_SPEED_10000_PQ,
};

static const u32 xgbe_phy_tx_amp[] = {
	XGBE_SPEED_1000_TXAMP,
	XGBE_SPEED_2500_TXAMP,
	XGBE_SPEED_10000_TXAMP,
};

static const u32 xgbe_phy_dfe_tap_cfg[] = {
	XGBE_SPEED_1000_DFE_TAP_CONFIG,
	XGBE_SPEED_2500_DFE_TAP_CONFIG,
	XGBE_SPEED_10000_DFE_TAP_CONFIG,
};

static const u32 xgbe_phy_dfe_tap_ena[] = {
	XGBE_SPEED_1000_DFE_TAP_ENABLE,
	XGBE_SPEED_2500_DFE_TAP_ENABLE,
	XGBE_SPEED_10000_DFE_TAP_ENABLE,
};

struct xgbe_phy_data {
	/* 1000/10000 vs 2500/10000 indicator */
	unsigned int speed_set;

	/* SerDes UEFI configurable settings.
	 *   Switching between modes/speeds requires new values for some
	 *   SerDes settings.  The values can be supplied as device
	 *   properties in array format.  The first array entry is for
	 *   1GbE, second for 2.5GbE and third for 10GbE
	 */
	u32 blwc[XGBE_SPEEDS];
	u32 cdr_rate[XGBE_SPEEDS];
	u32 pq_skew[XGBE_SPEEDS];
	u32 tx_amp[XGBE_SPEEDS];
	u32 dfe_tap_cfg[XGBE_SPEEDS];
	u32 dfe_tap_ena[XGBE_SPEEDS];
};

static void xgbe_phy_kr_training_pre(struct xgbe_prv_data *pdata)
{
		XSIR0_IOWRITE_BITS(pdata, SIR0_KR_RT_1, RESET, 1);
}

static void xgbe_phy_kr_training_post(struct xgbe_prv_data *pdata)
{
		XSIR0_IOWRITE_BITS(pdata, SIR0_KR_RT_1, RESET, 0);
}

static enum xgbe_mode xgbe_phy_an_outcome(struct xgbe_prv_data *pdata)
{
	struct xgbe_phy_data *phy_data = pdata->phy_data;
	enum xgbe_mode mode;
	unsigned int ad_reg, lp_reg;

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
		if (phy_data->speed_set == XGBE_SPEEDSET_2500_10000)
			pdata->phy.lp_advertising |= ADVERTISED_2500baseX_Full;
		else
			pdata->phy.lp_advertising |= ADVERTISED_1000baseKX_Full;
	}

	ad_reg &= lp_reg;
	if (ad_reg & 0x80) {
		mode = XGBE_MODE_KR;
	} else if (ad_reg & 0x20) {
		if (phy_data->speed_set == XGBE_SPEEDSET_2500_10000)
			mode = XGBE_MODE_KX_2500;
		else
			mode = XGBE_MODE_KX_1000;
	} else {
		mode = XGBE_MODE_UNKNOWN;
	}

	/* Compare Advertisement and Link Partner register 3 */
	ad_reg = XMDIO_READ(pdata, MDIO_MMD_AN, MDIO_AN_ADVERTISE + 2);
	lp_reg = XMDIO_READ(pdata, MDIO_MMD_AN, MDIO_AN_LPA + 2);
	if (lp_reg & 0xc000)
		pdata->phy.lp_advertising |= ADVERTISED_10000baseR_FEC;

	return mode;
}

static unsigned int xgbe_phy_an_advertising(struct xgbe_prv_data *pdata)
{
	return pdata->phy.advertising;
}

static int xgbe_phy_an_config(struct xgbe_prv_data *pdata)
{
	/* Nothing uniquely required for an configuration */
	return 0;
}

static enum xgbe_an_mode xgbe_phy_an_mode(struct xgbe_prv_data *pdata)
{
	return XGBE_AN_MODE_CL73;
}

static void xgbe_phy_pcs_power_cycle(struct xgbe_prv_data *pdata)
{
	unsigned int reg;

	reg = XMDIO_READ(pdata, MDIO_MMD_PCS, MDIO_CTRL1);

	reg |= MDIO_CTRL1_LPOWER;
	XMDIO_WRITE(pdata, MDIO_MMD_PCS, MDIO_CTRL1, reg);

	usleep_range(75, 100);

	reg &= ~MDIO_CTRL1_LPOWER;
	XMDIO_WRITE(pdata, MDIO_MMD_PCS, MDIO_CTRL1, reg);
}

static void xgbe_phy_start_ratechange(struct xgbe_prv_data *pdata)
{
	/* Assert Rx and Tx ratechange */
	XSIR1_IOWRITE_BITS(pdata, SIR1_SPEED, RATECHANGE, 1);
}

static void xgbe_phy_complete_ratechange(struct xgbe_prv_data *pdata)
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

static void xgbe_phy_kr_mode(struct xgbe_prv_data *pdata)
{
	struct xgbe_phy_data *phy_data = pdata->phy_data;
	unsigned int reg;

	/* Set PCS to KR/10G speed */
	reg = XMDIO_READ(pdata, MDIO_MMD_PCS, MDIO_CTRL2);
	reg &= ~MDIO_PCS_CTRL2_TYPE;
	reg |= MDIO_PCS_CTRL2_10GBR;
	XMDIO_WRITE(pdata, MDIO_MMD_PCS, MDIO_CTRL2, reg);

	reg = XMDIO_READ(pdata, MDIO_MMD_PCS, MDIO_CTRL1);
	reg &= ~MDIO_CTRL1_SPEEDSEL;
	reg |= MDIO_CTRL1_SPEED10G;
	XMDIO_WRITE(pdata, MDIO_MMD_PCS, MDIO_CTRL1, reg);

	xgbe_phy_pcs_power_cycle(pdata);

	/* Set SerDes to 10G speed */
	xgbe_phy_start_ratechange(pdata);

	XSIR1_IOWRITE_BITS(pdata, SIR1_SPEED, DATARATE, XGBE_SPEED_10000_RATE);
	XSIR1_IOWRITE_BITS(pdata, SIR1_SPEED, WORDMODE, XGBE_SPEED_10000_WORD);
	XSIR1_IOWRITE_BITS(pdata, SIR1_SPEED, PLLSEL, XGBE_SPEED_10000_PLL);

	XSIR1_IOWRITE_BITS(pdata, SIR1_SPEED, CDR_RATE,
			   phy_data->cdr_rate[XGBE_SPEED_10000]);
	XSIR1_IOWRITE_BITS(pdata, SIR1_SPEED, TXAMP,
			   phy_data->tx_amp[XGBE_SPEED_10000]);
	XRXTX_IOWRITE_BITS(pdata, RXTX_REG20, BLWC_ENA,
			   phy_data->blwc[XGBE_SPEED_10000]);
	XRXTX_IOWRITE_BITS(pdata, RXTX_REG114, PQ_REG,
			   phy_data->pq_skew[XGBE_SPEED_10000]);
	XRXTX_IOWRITE_BITS(pdata, RXTX_REG129, RXDFE_CONFIG,
			   phy_data->dfe_tap_cfg[XGBE_SPEED_10000]);
	XRXTX_IOWRITE(pdata, RXTX_REG22,
		      phy_data->dfe_tap_ena[XGBE_SPEED_10000]);

	xgbe_phy_complete_ratechange(pdata);

	netif_dbg(pdata, link, pdata->netdev, "10GbE KR mode set\n");
}

static void xgbe_phy_kx_2500_mode(struct xgbe_prv_data *pdata)
{
	struct xgbe_phy_data *phy_data = pdata->phy_data;
	unsigned int reg;

	/* Set PCS to KX/1G speed */
	reg = XMDIO_READ(pdata, MDIO_MMD_PCS, MDIO_CTRL2);
	reg &= ~MDIO_PCS_CTRL2_TYPE;
	reg |= MDIO_PCS_CTRL2_10GBX;
	XMDIO_WRITE(pdata, MDIO_MMD_PCS, MDIO_CTRL2, reg);

	reg = XMDIO_READ(pdata, MDIO_MMD_PCS, MDIO_CTRL1);
	reg &= ~MDIO_CTRL1_SPEEDSEL;
	reg |= MDIO_CTRL1_SPEED1G;
	XMDIO_WRITE(pdata, MDIO_MMD_PCS, MDIO_CTRL1, reg);

	xgbe_phy_pcs_power_cycle(pdata);

	/* Set SerDes to 2.5G speed */
	xgbe_phy_start_ratechange(pdata);

	XSIR1_IOWRITE_BITS(pdata, SIR1_SPEED, DATARATE, XGBE_SPEED_2500_RATE);
	XSIR1_IOWRITE_BITS(pdata, SIR1_SPEED, WORDMODE, XGBE_SPEED_2500_WORD);
	XSIR1_IOWRITE_BITS(pdata, SIR1_SPEED, PLLSEL, XGBE_SPEED_2500_PLL);

	XSIR1_IOWRITE_BITS(pdata, SIR1_SPEED, CDR_RATE,
			   phy_data->cdr_rate[XGBE_SPEED_2500]);
	XSIR1_IOWRITE_BITS(pdata, SIR1_SPEED, TXAMP,
			   phy_data->tx_amp[XGBE_SPEED_2500]);
	XRXTX_IOWRITE_BITS(pdata, RXTX_REG20, BLWC_ENA,
			   phy_data->blwc[XGBE_SPEED_2500]);
	XRXTX_IOWRITE_BITS(pdata, RXTX_REG114, PQ_REG,
			   phy_data->pq_skew[XGBE_SPEED_2500]);
	XRXTX_IOWRITE_BITS(pdata, RXTX_REG129, RXDFE_CONFIG,
			   phy_data->dfe_tap_cfg[XGBE_SPEED_2500]);
	XRXTX_IOWRITE(pdata, RXTX_REG22,
		      phy_data->dfe_tap_ena[XGBE_SPEED_2500]);

	xgbe_phy_complete_ratechange(pdata);

	netif_dbg(pdata, link, pdata->netdev, "2.5GbE KX mode set\n");
}

static void xgbe_phy_kx_1000_mode(struct xgbe_prv_data *pdata)
{
	struct xgbe_phy_data *phy_data = pdata->phy_data;
	unsigned int reg;

	/* Set PCS to KX/1G speed */
	reg = XMDIO_READ(pdata, MDIO_MMD_PCS, MDIO_CTRL2);
	reg &= ~MDIO_PCS_CTRL2_TYPE;
	reg |= MDIO_PCS_CTRL2_10GBX;
	XMDIO_WRITE(pdata, MDIO_MMD_PCS, MDIO_CTRL2, reg);

	reg = XMDIO_READ(pdata, MDIO_MMD_PCS, MDIO_CTRL1);
	reg &= ~MDIO_CTRL1_SPEEDSEL;
	reg |= MDIO_CTRL1_SPEED1G;
	XMDIO_WRITE(pdata, MDIO_MMD_PCS, MDIO_CTRL1, reg);

	xgbe_phy_pcs_power_cycle(pdata);

	/* Set SerDes to 1G speed */
	xgbe_phy_start_ratechange(pdata);

	XSIR1_IOWRITE_BITS(pdata, SIR1_SPEED, DATARATE, XGBE_SPEED_1000_RATE);
	XSIR1_IOWRITE_BITS(pdata, SIR1_SPEED, WORDMODE, XGBE_SPEED_1000_WORD);
	XSIR1_IOWRITE_BITS(pdata, SIR1_SPEED, PLLSEL, XGBE_SPEED_1000_PLL);

	XSIR1_IOWRITE_BITS(pdata, SIR1_SPEED, CDR_RATE,
			   phy_data->cdr_rate[XGBE_SPEED_1000]);
	XSIR1_IOWRITE_BITS(pdata, SIR1_SPEED, TXAMP,
			   phy_data->tx_amp[XGBE_SPEED_1000]);
	XRXTX_IOWRITE_BITS(pdata, RXTX_REG20, BLWC_ENA,
			   phy_data->blwc[XGBE_SPEED_1000]);
	XRXTX_IOWRITE_BITS(pdata, RXTX_REG114, PQ_REG,
			   phy_data->pq_skew[XGBE_SPEED_1000]);
	XRXTX_IOWRITE_BITS(pdata, RXTX_REG129, RXDFE_CONFIG,
			   phy_data->dfe_tap_cfg[XGBE_SPEED_1000]);
	XRXTX_IOWRITE(pdata, RXTX_REG22,
		      phy_data->dfe_tap_ena[XGBE_SPEED_1000]);

	xgbe_phy_complete_ratechange(pdata);

	netif_dbg(pdata, link, pdata->netdev, "1GbE KX mode set\n");
}

static enum xgbe_mode xgbe_phy_cur_mode(struct xgbe_prv_data *pdata)
{
	struct xgbe_phy_data *phy_data = pdata->phy_data;
	enum xgbe_mode mode;
	unsigned int reg;

	reg = XMDIO_READ(pdata, MDIO_MMD_PCS, MDIO_CTRL2);
	reg &= MDIO_PCS_CTRL2_TYPE;

	if (reg == MDIO_PCS_CTRL2_10GBR) {
		mode = XGBE_MODE_KR;
	} else {
		if (phy_data->speed_set == XGBE_SPEEDSET_2500_10000)
			mode = XGBE_MODE_KX_2500;
		else
			mode = XGBE_MODE_KX_1000;
	}

	return mode;
}

static enum xgbe_mode xgbe_phy_switch_mode(struct xgbe_prv_data *pdata)
{
	struct xgbe_phy_data *phy_data = pdata->phy_data;
	enum xgbe_mode mode;

	/* If we are in KR switch to KX, and vice-versa */
	if (xgbe_phy_cur_mode(pdata) == XGBE_MODE_KR) {
		if (phy_data->speed_set == XGBE_SPEEDSET_2500_10000)
			mode = XGBE_MODE_KX_2500;
		else
			mode = XGBE_MODE_KX_1000;
	} else {
		mode = XGBE_MODE_KR;
	}

	return mode;
}

static enum xgbe_mode xgbe_phy_get_mode(struct xgbe_prv_data *pdata,
					int speed)
{
	struct xgbe_phy_data *phy_data = pdata->phy_data;

	switch (speed) {
	case SPEED_1000:
		return (phy_data->speed_set == XGBE_SPEEDSET_1000_10000)
			? XGBE_MODE_KX_1000 : XGBE_MODE_UNKNOWN;
	case SPEED_2500:
		return (phy_data->speed_set == XGBE_SPEEDSET_2500_10000)
			? XGBE_MODE_KX_2500 : XGBE_MODE_UNKNOWN;
	case SPEED_10000:
		return XGBE_MODE_KR;
	default:
		return XGBE_MODE_UNKNOWN;
	}
}

static void xgbe_phy_set_mode(struct xgbe_prv_data *pdata, enum xgbe_mode mode)
{
	switch (mode) {
	case XGBE_MODE_KX_1000:
		xgbe_phy_kx_1000_mode(pdata);
		break;
	case XGBE_MODE_KX_2500:
		xgbe_phy_kx_2500_mode(pdata);
		break;
	case XGBE_MODE_KR:
		xgbe_phy_kr_mode(pdata);
		break;
	default:
		break;
	}
}

static bool xgbe_phy_check_mode(struct xgbe_prv_data *pdata,
				enum xgbe_mode mode, u32 advert)
{
	if (pdata->phy.autoneg == AUTONEG_ENABLE) {
		if (pdata->phy.advertising & advert)
			return true;
	} else {
		enum xgbe_mode cur_mode;

		cur_mode = xgbe_phy_get_mode(pdata, pdata->phy.speed);
		if (cur_mode == mode)
			return true;
	}

	return false;
}

static bool xgbe_phy_use_mode(struct xgbe_prv_data *pdata, enum xgbe_mode mode)
{
	switch (mode) {
	case XGBE_MODE_KX_1000:
		return xgbe_phy_check_mode(pdata, mode,
					   ADVERTISED_1000baseKX_Full);
	case XGBE_MODE_KX_2500:
		return xgbe_phy_check_mode(pdata, mode,
					   ADVERTISED_2500baseX_Full);
	case XGBE_MODE_KR:
		return xgbe_phy_check_mode(pdata, mode,
					   ADVERTISED_10000baseKR_Full);
	default:
		return false;
	}
}

static bool xgbe_phy_valid_speed(struct xgbe_prv_data *pdata, int speed)
{
	struct xgbe_phy_data *phy_data = pdata->phy_data;

	switch (speed) {
	case SPEED_1000:
		if (phy_data->speed_set != XGBE_SPEEDSET_1000_10000)
			return false;
		return true;
	case SPEED_2500:
		if (phy_data->speed_set != XGBE_SPEEDSET_2500_10000)
			return false;
		return true;
	case SPEED_10000:
		return true;
	default:
		return false;
	}
}

static int xgbe_phy_link_status(struct xgbe_prv_data *pdata, int *an_restart)
{
	unsigned int reg;

	*an_restart = 0;

	/* Link status is latched low, so read once to clear
	 * and then read again to get current state
	 */
	reg = XMDIO_READ(pdata, MDIO_MMD_PCS, MDIO_STAT1);
	reg = XMDIO_READ(pdata, MDIO_MMD_PCS, MDIO_STAT1);

	return (reg & MDIO_STAT1_LSTATUS) ? 1 : 0;
}

static void xgbe_phy_stop(struct xgbe_prv_data *pdata)
{
	/* Nothing uniquely required for stop */
}

static int xgbe_phy_start(struct xgbe_prv_data *pdata)
{
	/* Nothing uniquely required for start */
	return 0;
}

static int xgbe_phy_reset(struct xgbe_prv_data *pdata)
{
	unsigned int reg, count;

	/* Perform a software reset of the PCS */
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

	return 0;
}

static void xgbe_phy_exit(struct xgbe_prv_data *pdata)
{
	/* Nothing uniquely required for exit */
}

static int xgbe_phy_init(struct xgbe_prv_data *pdata)
{
	struct xgbe_phy_data *phy_data;
	int ret;

	phy_data = devm_kzalloc(pdata->dev, sizeof(*phy_data), GFP_KERNEL);
	if (!phy_data)
		return -ENOMEM;

	/* Retrieve the PHY speedset */
	ret = device_property_read_u32(pdata->phy_dev, XGBE_SPEEDSET_PROPERTY,
				       &phy_data->speed_set);
	if (ret) {
		dev_err(pdata->dev, "invalid %s property\n",
			XGBE_SPEEDSET_PROPERTY);
		return ret;
	}

	switch (phy_data->speed_set) {
	case XGBE_SPEEDSET_1000_10000:
	case XGBE_SPEEDSET_2500_10000:
		break;
	default:
		dev_err(pdata->dev, "invalid %s property\n",
			XGBE_SPEEDSET_PROPERTY);
		return -EINVAL;
	}

	/* Retrieve the PHY configuration properties */
	if (device_property_present(pdata->phy_dev, XGBE_BLWC_PROPERTY)) {
		ret = device_property_read_u32_array(pdata->phy_dev,
						     XGBE_BLWC_PROPERTY,
						     phy_data->blwc,
						     XGBE_SPEEDS);
		if (ret) {
			dev_err(pdata->dev, "invalid %s property\n",
				XGBE_BLWC_PROPERTY);
			return ret;
		}
	} else {
		memcpy(phy_data->blwc, xgbe_phy_blwc,
		       sizeof(phy_data->blwc));
	}

	if (device_property_present(pdata->phy_dev, XGBE_CDR_RATE_PROPERTY)) {
		ret = device_property_read_u32_array(pdata->phy_dev,
						     XGBE_CDR_RATE_PROPERTY,
						     phy_data->cdr_rate,
						     XGBE_SPEEDS);
		if (ret) {
			dev_err(pdata->dev, "invalid %s property\n",
				XGBE_CDR_RATE_PROPERTY);
			return ret;
		}
	} else {
		memcpy(phy_data->cdr_rate, xgbe_phy_cdr_rate,
		       sizeof(phy_data->cdr_rate));
	}

	if (device_property_present(pdata->phy_dev, XGBE_PQ_SKEW_PROPERTY)) {
		ret = device_property_read_u32_array(pdata->phy_dev,
						     XGBE_PQ_SKEW_PROPERTY,
						     phy_data->pq_skew,
						     XGBE_SPEEDS);
		if (ret) {
			dev_err(pdata->dev, "invalid %s property\n",
				XGBE_PQ_SKEW_PROPERTY);
			return ret;
		}
	} else {
		memcpy(phy_data->pq_skew, xgbe_phy_pq_skew,
		       sizeof(phy_data->pq_skew));
	}

	if (device_property_present(pdata->phy_dev, XGBE_TX_AMP_PROPERTY)) {
		ret = device_property_read_u32_array(pdata->phy_dev,
						     XGBE_TX_AMP_PROPERTY,
						     phy_data->tx_amp,
						     XGBE_SPEEDS);
		if (ret) {
			dev_err(pdata->dev, "invalid %s property\n",
				XGBE_TX_AMP_PROPERTY);
			return ret;
		}
	} else {
		memcpy(phy_data->tx_amp, xgbe_phy_tx_amp,
		       sizeof(phy_data->tx_amp));
	}

	if (device_property_present(pdata->phy_dev, XGBE_DFE_CFG_PROPERTY)) {
		ret = device_property_read_u32_array(pdata->phy_dev,
						     XGBE_DFE_CFG_PROPERTY,
						     phy_data->dfe_tap_cfg,
						     XGBE_SPEEDS);
		if (ret) {
			dev_err(pdata->dev, "invalid %s property\n",
				XGBE_DFE_CFG_PROPERTY);
			return ret;
		}
	} else {
		memcpy(phy_data->dfe_tap_cfg, xgbe_phy_dfe_tap_cfg,
		       sizeof(phy_data->dfe_tap_cfg));
	}

	if (device_property_present(pdata->phy_dev, XGBE_DFE_ENA_PROPERTY)) {
		ret = device_property_read_u32_array(pdata->phy_dev,
						     XGBE_DFE_ENA_PROPERTY,
						     phy_data->dfe_tap_ena,
						     XGBE_SPEEDS);
		if (ret) {
			dev_err(pdata->dev, "invalid %s property\n",
				XGBE_DFE_ENA_PROPERTY);
			return ret;
		}
	} else {
		memcpy(phy_data->dfe_tap_ena, xgbe_phy_dfe_tap_ena,
		       sizeof(phy_data->dfe_tap_ena));
	}

	/* Initialize supported features */
	pdata->phy.supported = SUPPORTED_Autoneg;
	pdata->phy.supported |= SUPPORTED_Pause | SUPPORTED_Asym_Pause;
	pdata->phy.supported |= SUPPORTED_Backplane;
	pdata->phy.supported |= SUPPORTED_10000baseKR_Full;
	switch (phy_data->speed_set) {
	case XGBE_SPEEDSET_1000_10000:
		pdata->phy.supported |= SUPPORTED_1000baseKX_Full;
		break;
	case XGBE_SPEEDSET_2500_10000:
		pdata->phy.supported |= SUPPORTED_2500baseX_Full;
		break;
	}

	if (pdata->fec_ability & MDIO_PMA_10GBR_FECABLE_ABLE)
		pdata->phy.supported |= SUPPORTED_10000baseR_FEC;

	pdata->phy_data = phy_data;

	return 0;
}

void xgbe_init_function_ptrs_phy_v1(struct xgbe_phy_if *phy_if)
{
	struct xgbe_phy_impl_if *phy_impl = &phy_if->phy_impl;

	phy_impl->init			= xgbe_phy_init;
	phy_impl->exit			= xgbe_phy_exit;

	phy_impl->reset			= xgbe_phy_reset;
	phy_impl->start			= xgbe_phy_start;
	phy_impl->stop			= xgbe_phy_stop;

	phy_impl->link_status		= xgbe_phy_link_status;

	phy_impl->valid_speed		= xgbe_phy_valid_speed;

	phy_impl->use_mode		= xgbe_phy_use_mode;
	phy_impl->set_mode		= xgbe_phy_set_mode;
	phy_impl->get_mode		= xgbe_phy_get_mode;
	phy_impl->switch_mode		= xgbe_phy_switch_mode;
	phy_impl->cur_mode		= xgbe_phy_cur_mode;

	phy_impl->an_mode		= xgbe_phy_an_mode;

	phy_impl->an_config		= xgbe_phy_an_config;

	phy_impl->an_advertising	= xgbe_phy_an_advertising;

	phy_impl->an_outcome		= xgbe_phy_an_outcome;

	phy_impl->kr_training_pre	= xgbe_phy_kr_training_pre;
	phy_impl->kr_training_post	= xgbe_phy_kr_training_post;
}
