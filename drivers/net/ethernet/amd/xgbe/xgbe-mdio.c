/*
 * AMD 10Gb Ethernet driver
 *
 * This file is available to you under your choice of the following two
 * licenses:
 *
 * License 1: GPLv2
 *
 * Copyright (c) 2014-2016 Advanced Micro Devices, Inc.
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
 * Copyright (c) 2014-2016 Advanced Micro Devices, Inc.
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

static void xgbe_an37_clear_interrupts(struct xgbe_prv_data *pdata)
{
	int reg;

	reg = XMDIO_READ(pdata, MDIO_MMD_VEND2, MDIO_VEND2_AN_STAT);
	reg &= ~XGBE_AN_CL37_INT_MASK;
	XMDIO_WRITE(pdata, MDIO_MMD_VEND2, MDIO_VEND2_AN_STAT, reg);
}

static void xgbe_an37_disable_interrupts(struct xgbe_prv_data *pdata)
{
	int reg;

	reg = XMDIO_READ(pdata, MDIO_MMD_VEND2, MDIO_VEND2_AN_CTRL);
	reg &= ~XGBE_AN_CL37_INT_MASK;
	XMDIO_WRITE(pdata, MDIO_MMD_VEND2, MDIO_VEND2_AN_CTRL, reg);

	reg = XMDIO_READ(pdata, MDIO_MMD_PCS, MDIO_PCS_DIG_CTRL);
	reg &= ~XGBE_PCS_CL37_BP;
	XMDIO_WRITE(pdata, MDIO_MMD_PCS, MDIO_PCS_DIG_CTRL, reg);
}

static void xgbe_an37_enable_interrupts(struct xgbe_prv_data *pdata)
{
	int reg;

	reg = XMDIO_READ(pdata, MDIO_MMD_PCS, MDIO_PCS_DIG_CTRL);
	reg |= XGBE_PCS_CL37_BP;
	XMDIO_WRITE(pdata, MDIO_MMD_PCS, MDIO_PCS_DIG_CTRL, reg);

	reg = XMDIO_READ(pdata, MDIO_MMD_VEND2, MDIO_VEND2_AN_CTRL);
	reg |= XGBE_AN_CL37_INT_MASK;
	XMDIO_WRITE(pdata, MDIO_MMD_VEND2, MDIO_VEND2_AN_CTRL, reg);
}

static void xgbe_an73_clear_interrupts(struct xgbe_prv_data *pdata)
{
	XMDIO_WRITE(pdata, MDIO_MMD_AN, MDIO_AN_INT, 0);
}

static void xgbe_an73_disable_interrupts(struct xgbe_prv_data *pdata)
{
	XMDIO_WRITE(pdata, MDIO_MMD_AN, MDIO_AN_INTMASK, 0);
}

static void xgbe_an73_enable_interrupts(struct xgbe_prv_data *pdata)
{
	XMDIO_WRITE(pdata, MDIO_MMD_AN, MDIO_AN_INTMASK, XGBE_AN_CL73_INT_MASK);
}

static void xgbe_an_enable_interrupts(struct xgbe_prv_data *pdata)
{
	switch (pdata->an_mode) {
	case XGBE_AN_MODE_CL73:
	case XGBE_AN_MODE_CL73_REDRV:
		xgbe_an73_enable_interrupts(pdata);
		break;
	case XGBE_AN_MODE_CL37:
	case XGBE_AN_MODE_CL37_SGMII:
		xgbe_an37_enable_interrupts(pdata);
		break;
	default:
		break;
	}
}

static void xgbe_an_clear_interrupts_all(struct xgbe_prv_data *pdata)
{
	xgbe_an73_clear_interrupts(pdata);
	xgbe_an37_clear_interrupts(pdata);
}

static void xgbe_an73_enable_kr_training(struct xgbe_prv_data *pdata)
{
	unsigned int reg;

	reg = XMDIO_READ(pdata, MDIO_MMD_PMAPMD, MDIO_PMA_10GBR_PMD_CTRL);

	reg |= XGBE_KR_TRAINING_ENABLE;
	XMDIO_WRITE(pdata, MDIO_MMD_PMAPMD, MDIO_PMA_10GBR_PMD_CTRL, reg);
}

static void xgbe_an73_disable_kr_training(struct xgbe_prv_data *pdata)
{
	unsigned int reg;

	reg = XMDIO_READ(pdata, MDIO_MMD_PMAPMD, MDIO_PMA_10GBR_PMD_CTRL);

	reg &= ~XGBE_KR_TRAINING_ENABLE;
	XMDIO_WRITE(pdata, MDIO_MMD_PMAPMD, MDIO_PMA_10GBR_PMD_CTRL, reg);
}

static void xgbe_kr_mode(struct xgbe_prv_data *pdata)
{
	/* Enable KR training */
	xgbe_an73_enable_kr_training(pdata);

	/* Set MAC to 10G speed */
	pdata->hw_if.set_speed(pdata, SPEED_10000);

	/* Call PHY implementation support to complete rate change */
	pdata->phy_if.phy_impl.set_mode(pdata, XGBE_MODE_KR);
}

static void xgbe_kx_2500_mode(struct xgbe_prv_data *pdata)
{
	/* Disable KR training */
	xgbe_an73_disable_kr_training(pdata);

	/* Set MAC to 2.5G speed */
	pdata->hw_if.set_speed(pdata, SPEED_2500);

	/* Call PHY implementation support to complete rate change */
	pdata->phy_if.phy_impl.set_mode(pdata, XGBE_MODE_KX_2500);
}

static void xgbe_kx_1000_mode(struct xgbe_prv_data *pdata)
{
	/* Disable KR training */
	xgbe_an73_disable_kr_training(pdata);

	/* Set MAC to 1G speed */
	pdata->hw_if.set_speed(pdata, SPEED_1000);

	/* Call PHY implementation support to complete rate change */
	pdata->phy_if.phy_impl.set_mode(pdata, XGBE_MODE_KX_1000);
}

static void xgbe_sfi_mode(struct xgbe_prv_data *pdata)
{
	/* If a KR re-driver is present, change to KR mode instead */
	if (pdata->kr_redrv)
		return xgbe_kr_mode(pdata);

	/* Disable KR training */
	xgbe_an73_disable_kr_training(pdata);

	/* Set MAC to 10G speed */
	pdata->hw_if.set_speed(pdata, SPEED_10000);

	/* Call PHY implementation support to complete rate change */
	pdata->phy_if.phy_impl.set_mode(pdata, XGBE_MODE_SFI);
}

static void xgbe_x_mode(struct xgbe_prv_data *pdata)
{
	/* Disable KR training */
	xgbe_an73_disable_kr_training(pdata);

	/* Set MAC to 1G speed */
	pdata->hw_if.set_speed(pdata, SPEED_1000);

	/* Call PHY implementation support to complete rate change */
	pdata->phy_if.phy_impl.set_mode(pdata, XGBE_MODE_X);
}

static void xgbe_sgmii_1000_mode(struct xgbe_prv_data *pdata)
{
	/* Disable KR training */
	xgbe_an73_disable_kr_training(pdata);

	/* Set MAC to 1G speed */
	pdata->hw_if.set_speed(pdata, SPEED_1000);

	/* Call PHY implementation support to complete rate change */
	pdata->phy_if.phy_impl.set_mode(pdata, XGBE_MODE_SGMII_1000);
}

static void xgbe_sgmii_100_mode(struct xgbe_prv_data *pdata)
{
	/* Disable KR training */
	xgbe_an73_disable_kr_training(pdata);

	/* Set MAC to 1G speed */
	pdata->hw_if.set_speed(pdata, SPEED_1000);

	/* Call PHY implementation support to complete rate change */
	pdata->phy_if.phy_impl.set_mode(pdata, XGBE_MODE_SGMII_100);
}

static enum xgbe_mode xgbe_cur_mode(struct xgbe_prv_data *pdata)
{
	return pdata->phy_if.phy_impl.cur_mode(pdata);
}

static bool xgbe_in_kr_mode(struct xgbe_prv_data *pdata)
{
	return (xgbe_cur_mode(pdata) == XGBE_MODE_KR);
}

static void xgbe_change_mode(struct xgbe_prv_data *pdata,
			     enum xgbe_mode mode)
{
	switch (mode) {
	case XGBE_MODE_KX_1000:
		xgbe_kx_1000_mode(pdata);
		break;
	case XGBE_MODE_KX_2500:
		xgbe_kx_2500_mode(pdata);
		break;
	case XGBE_MODE_KR:
		xgbe_kr_mode(pdata);
		break;
	case XGBE_MODE_SGMII_100:
		xgbe_sgmii_100_mode(pdata);
		break;
	case XGBE_MODE_SGMII_1000:
		xgbe_sgmii_1000_mode(pdata);
		break;
	case XGBE_MODE_X:
		xgbe_x_mode(pdata);
		break;
	case XGBE_MODE_SFI:
		xgbe_sfi_mode(pdata);
		break;
	case XGBE_MODE_UNKNOWN:
		break;
	default:
		netif_dbg(pdata, link, pdata->netdev,
			  "invalid operation mode requested (%u)\n", mode);
	}
}

static void xgbe_switch_mode(struct xgbe_prv_data *pdata)
{
	xgbe_change_mode(pdata, pdata->phy_if.phy_impl.switch_mode(pdata));
}

static void xgbe_set_mode(struct xgbe_prv_data *pdata,
			  enum xgbe_mode mode)
{
	if (mode == xgbe_cur_mode(pdata))
		return;

	xgbe_change_mode(pdata, mode);
}

static bool xgbe_use_mode(struct xgbe_prv_data *pdata,
			  enum xgbe_mode mode)
{
	return pdata->phy_if.phy_impl.use_mode(pdata, mode);
}

static void xgbe_an37_set(struct xgbe_prv_data *pdata, bool enable,
			  bool restart)
{
	unsigned int reg;

	reg = XMDIO_READ(pdata, MDIO_MMD_VEND2, MDIO_CTRL1);
	reg &= ~MDIO_VEND2_CTRL1_AN_ENABLE;

	if (enable)
		reg |= MDIO_VEND2_CTRL1_AN_ENABLE;

	if (restart)
		reg |= MDIO_VEND2_CTRL1_AN_RESTART;

	XMDIO_WRITE(pdata, MDIO_MMD_VEND2, MDIO_CTRL1, reg);
}

static void xgbe_an37_restart(struct xgbe_prv_data *pdata)
{
	xgbe_an37_enable_interrupts(pdata);
	xgbe_an37_set(pdata, true, true);

	netif_dbg(pdata, link, pdata->netdev, "CL37 AN enabled/restarted\n");
}

static void xgbe_an37_disable(struct xgbe_prv_data *pdata)
{
	xgbe_an37_set(pdata, false, false);
	xgbe_an37_disable_interrupts(pdata);

	netif_dbg(pdata, link, pdata->netdev, "CL37 AN disabled\n");
}

static void xgbe_an73_set(struct xgbe_prv_data *pdata, bool enable,
			  bool restart)
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

static void xgbe_an73_restart(struct xgbe_prv_data *pdata)
{
	xgbe_an73_enable_interrupts(pdata);
	xgbe_an73_set(pdata, true, true);

	netif_dbg(pdata, link, pdata->netdev, "CL73 AN enabled/restarted\n");
}

static void xgbe_an73_disable(struct xgbe_prv_data *pdata)
{
	xgbe_an73_set(pdata, false, false);
	xgbe_an73_disable_interrupts(pdata);

	netif_dbg(pdata, link, pdata->netdev, "CL73 AN disabled\n");
}

static void xgbe_an_restart(struct xgbe_prv_data *pdata)
{
	switch (pdata->an_mode) {
	case XGBE_AN_MODE_CL73:
	case XGBE_AN_MODE_CL73_REDRV:
		xgbe_an73_restart(pdata);
		break;
	case XGBE_AN_MODE_CL37:
	case XGBE_AN_MODE_CL37_SGMII:
		xgbe_an37_restart(pdata);
		break;
	default:
		break;
	}
}

static void xgbe_an_disable(struct xgbe_prv_data *pdata)
{
	switch (pdata->an_mode) {
	case XGBE_AN_MODE_CL73:
	case XGBE_AN_MODE_CL73_REDRV:
		xgbe_an73_disable(pdata);
		break;
	case XGBE_AN_MODE_CL37:
	case XGBE_AN_MODE_CL37_SGMII:
		xgbe_an37_disable(pdata);
		break;
	default:
		break;
	}
}

static void xgbe_an_disable_all(struct xgbe_prv_data *pdata)
{
	xgbe_an73_disable(pdata);
	xgbe_an37_disable(pdata);
}

static enum xgbe_an xgbe_an73_tx_training(struct xgbe_prv_data *pdata,
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
		if (pdata->phy_if.phy_impl.kr_training_pre)
			pdata->phy_if.phy_impl.kr_training_pre(pdata);

		reg |= XGBE_KR_TRAINING_START;
		XMDIO_WRITE(pdata, MDIO_MMD_PMAPMD, MDIO_PMA_10GBR_PMD_CTRL,
			    reg);

		if (pdata->phy_if.phy_impl.kr_training_post)
			pdata->phy_if.phy_impl.kr_training_post(pdata);

		netif_dbg(pdata, link, pdata->netdev,
			  "KR training initiated\n");
	}

	return XGBE_AN_PAGE_RECEIVED;
}

static enum xgbe_an xgbe_an73_tx_xnp(struct xgbe_prv_data *pdata,
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

static enum xgbe_an xgbe_an73_rx_bpa(struct xgbe_prv_data *pdata,
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
	       ? xgbe_an73_tx_xnp(pdata, state)
	       : xgbe_an73_tx_training(pdata, state);
}

static enum xgbe_an xgbe_an73_rx_xnp(struct xgbe_prv_data *pdata,
				     enum xgbe_rx *state)
{
	unsigned int ad_reg, lp_reg;

	/* Check Extended Next Page support */
	ad_reg = XMDIO_READ(pdata, MDIO_MMD_AN, MDIO_AN_XNP);
	lp_reg = XMDIO_READ(pdata, MDIO_MMD_AN, MDIO_AN_LPX);

	return ((ad_reg & XGBE_XNP_NP_EXCHANGE) ||
		(lp_reg & XGBE_XNP_NP_EXCHANGE))
	       ? xgbe_an73_tx_xnp(pdata, state)
	       : xgbe_an73_tx_training(pdata, state);
}

static enum xgbe_an xgbe_an73_page_received(struct xgbe_prv_data *pdata)
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
				  "CL73 AN timed out, resetting state\n");
		}
	}

	state = xgbe_in_kr_mode(pdata) ? &pdata->kr_state
				       : &pdata->kx_state;

	switch (*state) {
	case XGBE_RX_BPA:
		ret = xgbe_an73_rx_bpa(pdata, state);
		break;

	case XGBE_RX_XNP:
		ret = xgbe_an73_rx_xnp(pdata, state);
		break;

	default:
		ret = XGBE_AN_ERROR;
	}

	return ret;
}

static enum xgbe_an xgbe_an73_incompat_link(struct xgbe_prv_data *pdata)
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

	xgbe_an73_disable(pdata);

	xgbe_switch_mode(pdata);

	xgbe_an73_restart(pdata);

	return XGBE_AN_INCOMPAT_LINK;
}

static void xgbe_an37_isr(struct xgbe_prv_data *pdata)
{
	unsigned int reg;

	/* Disable AN interrupts */
	xgbe_an37_disable_interrupts(pdata);

	/* Save the interrupt(s) that fired */
	reg = XMDIO_READ(pdata, MDIO_MMD_VEND2, MDIO_VEND2_AN_STAT);
	pdata->an_int = reg & XGBE_AN_CL37_INT_MASK;
	pdata->an_status = reg & ~XGBE_AN_CL37_INT_MASK;

	if (pdata->an_int) {
		/* Clear the interrupt(s) that fired and process them */
		reg &= ~XGBE_AN_CL37_INT_MASK;
		XMDIO_WRITE(pdata, MDIO_MMD_VEND2, MDIO_VEND2_AN_STAT, reg);

		queue_work(pdata->an_workqueue, &pdata->an_irq_work);
	} else {
		/* Enable AN interrupts */
		xgbe_an37_enable_interrupts(pdata);
	}
}

static void xgbe_an73_isr(struct xgbe_prv_data *pdata)
{
	/* Disable AN interrupts */
	xgbe_an73_disable_interrupts(pdata);

	/* Save the interrupt(s) that fired */
	pdata->an_int = XMDIO_READ(pdata, MDIO_MMD_AN, MDIO_AN_INT);

	if (pdata->an_int) {
		/* Clear the interrupt(s) that fired and process them */
		XMDIO_WRITE(pdata, MDIO_MMD_AN, MDIO_AN_INT, ~pdata->an_int);

		queue_work(pdata->an_workqueue, &pdata->an_irq_work);
	} else {
		/* Enable AN interrupts */
		xgbe_an73_enable_interrupts(pdata);
	}
}

static irqreturn_t xgbe_an_isr(int irq, void *data)
{
	struct xgbe_prv_data *pdata = (struct xgbe_prv_data *)data;

	netif_dbg(pdata, intr, pdata->netdev, "AN interrupt received\n");

	switch (pdata->an_mode) {
	case XGBE_AN_MODE_CL73:
	case XGBE_AN_MODE_CL73_REDRV:
		xgbe_an73_isr(pdata);
		break;
	case XGBE_AN_MODE_CL37:
	case XGBE_AN_MODE_CL37_SGMII:
		xgbe_an37_isr(pdata);
		break;
	default:
		break;
	}

	return IRQ_HANDLED;
}

static irqreturn_t xgbe_an_combined_isr(int irq, struct xgbe_prv_data *pdata)
{
	return xgbe_an_isr(irq, pdata);
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

static void xgbe_an37_state_machine(struct xgbe_prv_data *pdata)
{
	enum xgbe_an cur_state = pdata->an_state;

	if (!pdata->an_int)
		return;

	if (pdata->an_int & XGBE_AN_CL37_INT_CMPLT) {
		pdata->an_state = XGBE_AN_COMPLETE;
		pdata->an_int &= ~XGBE_AN_CL37_INT_CMPLT;

		/* If SGMII is enabled, check the link status */
		if ((pdata->an_mode == XGBE_AN_MODE_CL37_SGMII) &&
		    !(pdata->an_status & XGBE_SGMII_AN_LINK_STATUS))
			pdata->an_state = XGBE_AN_NO_LINK;
	}

	netif_dbg(pdata, link, pdata->netdev, "CL37 AN %s\n",
		  xgbe_state_as_string(pdata->an_state));

	cur_state = pdata->an_state;

	switch (pdata->an_state) {
	case XGBE_AN_READY:
		break;

	case XGBE_AN_COMPLETE:
		netif_dbg(pdata, link, pdata->netdev,
			  "Auto negotiation successful\n");
		break;

	case XGBE_AN_NO_LINK:
		break;

	default:
		pdata->an_state = XGBE_AN_ERROR;
	}

	if (pdata->an_state == XGBE_AN_ERROR) {
		netdev_err(pdata->netdev,
			   "error during auto-negotiation, state=%u\n",
			   cur_state);

		pdata->an_int = 0;
		xgbe_an37_clear_interrupts(pdata);
	}

	if (pdata->an_state >= XGBE_AN_COMPLETE) {
		pdata->an_result = pdata->an_state;
		pdata->an_state = XGBE_AN_READY;

		netif_dbg(pdata, link, pdata->netdev, "CL37 AN result: %s\n",
			  xgbe_state_as_string(pdata->an_result));
	}

	xgbe_an37_enable_interrupts(pdata);
}

static void xgbe_an73_state_machine(struct xgbe_prv_data *pdata)
{
	enum xgbe_an cur_state = pdata->an_state;

	if (!pdata->an_int)
		return;

next_int:
	if (pdata->an_int & XGBE_AN_CL73_PG_RCV) {
		pdata->an_state = XGBE_AN_PAGE_RECEIVED;
		pdata->an_int &= ~XGBE_AN_CL73_PG_RCV;
	} else if (pdata->an_int & XGBE_AN_CL73_INC_LINK) {
		pdata->an_state = XGBE_AN_INCOMPAT_LINK;
		pdata->an_int &= ~XGBE_AN_CL73_INC_LINK;
	} else if (pdata->an_int & XGBE_AN_CL73_INT_CMPLT) {
		pdata->an_state = XGBE_AN_COMPLETE;
		pdata->an_int &= ~XGBE_AN_CL73_INT_CMPLT;
	} else {
		pdata->an_state = XGBE_AN_ERROR;
	}

again:
	netif_dbg(pdata, link, pdata->netdev, "CL73 AN %s\n",
		  xgbe_state_as_string(pdata->an_state));

	cur_state = pdata->an_state;

	switch (pdata->an_state) {
	case XGBE_AN_READY:
		pdata->an_supported = 0;
		break;

	case XGBE_AN_PAGE_RECEIVED:
		pdata->an_state = xgbe_an73_page_received(pdata);
		pdata->an_supported++;
		break;

	case XGBE_AN_INCOMPAT_LINK:
		pdata->an_supported = 0;
		pdata->parallel_detect = 0;
		pdata->an_state = xgbe_an73_incompat_link(pdata);
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
		pdata->an_int = 0;
		xgbe_an73_clear_interrupts(pdata);
	} else if (pdata->an_state == XGBE_AN_ERROR) {
		netdev_err(pdata->netdev,
			   "error during auto-negotiation, state=%u\n",
			   cur_state);

		pdata->an_int = 0;
		xgbe_an73_clear_interrupts(pdata);
	}

	if (pdata->an_state >= XGBE_AN_COMPLETE) {
		pdata->an_result = pdata->an_state;
		pdata->an_state = XGBE_AN_READY;
		pdata->kr_state = XGBE_RX_BPA;
		pdata->kx_state = XGBE_RX_BPA;
		pdata->an_start = 0;

		netif_dbg(pdata, link, pdata->netdev, "CL73 AN result: %s\n",
			  xgbe_state_as_string(pdata->an_result));
	}

	if (cur_state != pdata->an_state)
		goto again;

	if (pdata->an_int)
		goto next_int;

	xgbe_an73_enable_interrupts(pdata);
}

static void xgbe_an_state_machine(struct work_struct *work)
{
	struct xgbe_prv_data *pdata = container_of(work,
						   struct xgbe_prv_data,
						   an_work);

	mutex_lock(&pdata->an_mutex);

	switch (pdata->an_mode) {
	case XGBE_AN_MODE_CL73:
	case XGBE_AN_MODE_CL73_REDRV:
		xgbe_an73_state_machine(pdata);
		break;
	case XGBE_AN_MODE_CL37:
	case XGBE_AN_MODE_CL37_SGMII:
		xgbe_an37_state_machine(pdata);
		break;
	default:
		break;
	}

	mutex_unlock(&pdata->an_mutex);
}

static void xgbe_an37_init(struct xgbe_prv_data *pdata)
{
	unsigned int advertising, reg;

	advertising = pdata->phy_if.phy_impl.an_advertising(pdata);

	/* Set up Advertisement register */
	reg = XMDIO_READ(pdata, MDIO_MMD_VEND2, MDIO_VEND2_AN_ADVERTISE);
	if (advertising & ADVERTISED_Pause)
		reg |= 0x100;
	else
		reg &= ~0x100;

	if (advertising & ADVERTISED_Asym_Pause)
		reg |= 0x80;
	else
		reg &= ~0x80;

	/* Full duplex, but not half */
	reg |= XGBE_AN_CL37_FD_MASK;
	reg &= ~XGBE_AN_CL37_HD_MASK;

	XMDIO_WRITE(pdata, MDIO_MMD_VEND2, MDIO_VEND2_AN_ADVERTISE, reg);

	/* Set up the Control register */
	reg = XMDIO_READ(pdata, MDIO_MMD_VEND2, MDIO_VEND2_AN_CTRL);
	reg &= XGBE_AN_CL37_TX_CONFIG_MASK;
	reg &= XGBE_AN_CL37_PCS_MODE_MASK;

	switch (pdata->an_mode) {
	case XGBE_AN_MODE_CL37:
		reg |= XGBE_AN_CL37_PCS_MODE_BASEX;
		break;
	case XGBE_AN_MODE_CL37_SGMII:
		reg |= XGBE_AN_CL37_PCS_MODE_SGMII;
		break;
	default:
		break;
	}

	XMDIO_WRITE(pdata, MDIO_MMD_VEND2, MDIO_VEND2_AN_CTRL, reg);

	netif_dbg(pdata, link, pdata->netdev, "CL37 AN (%s) initialized\n",
		  (pdata->an_mode == XGBE_AN_MODE_CL37) ? "BaseX" : "SGMII");
}

static void xgbe_an73_init(struct xgbe_prv_data *pdata)
{
	unsigned int advertising, reg;

	advertising = pdata->phy_if.phy_impl.an_advertising(pdata);

	/* Set up Advertisement register 3 first */
	reg = XMDIO_READ(pdata, MDIO_MMD_AN, MDIO_AN_ADVERTISE + 2);
	if (advertising & ADVERTISED_10000baseR_FEC)
		reg |= 0xc000;
	else
		reg &= ~0xc000;

	XMDIO_WRITE(pdata, MDIO_MMD_AN, MDIO_AN_ADVERTISE + 2, reg);

	/* Set up Advertisement register 2 next */
	reg = XMDIO_READ(pdata, MDIO_MMD_AN, MDIO_AN_ADVERTISE + 1);
	if (advertising & ADVERTISED_10000baseKR_Full)
		reg |= 0x80;
	else
		reg &= ~0x80;

	if ((advertising & ADVERTISED_1000baseKX_Full) ||
	    (advertising & ADVERTISED_2500baseX_Full))
		reg |= 0x20;
	else
		reg &= ~0x20;

	XMDIO_WRITE(pdata, MDIO_MMD_AN, MDIO_AN_ADVERTISE + 1, reg);

	/* Set up Advertisement register 1 last */
	reg = XMDIO_READ(pdata, MDIO_MMD_AN, MDIO_AN_ADVERTISE);
	if (advertising & ADVERTISED_Pause)
		reg |= 0x400;
	else
		reg &= ~0x400;

	if (advertising & ADVERTISED_Asym_Pause)
		reg |= 0x800;
	else
		reg &= ~0x800;

	/* We don't intend to perform XNP */
	reg &= ~XGBE_XNP_NP_EXCHANGE;

	XMDIO_WRITE(pdata, MDIO_MMD_AN, MDIO_AN_ADVERTISE, reg);

	netif_dbg(pdata, link, pdata->netdev, "CL73 AN initialized\n");
}

static void xgbe_an_init(struct xgbe_prv_data *pdata)
{
	/* Set up advertisement registers based on current settings */
	pdata->an_mode = pdata->phy_if.phy_impl.an_mode(pdata);
	switch (pdata->an_mode) {
	case XGBE_AN_MODE_CL73:
	case XGBE_AN_MODE_CL73_REDRV:
		xgbe_an73_init(pdata);
		break;
	case XGBE_AN_MODE_CL37:
	case XGBE_AN_MODE_CL37_SGMII:
		xgbe_an37_init(pdata);
		break;
	default:
		break;
	}
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
	case SPEED_100:
		return "100Mbps";
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

static bool xgbe_phy_valid_speed(struct xgbe_prv_data *pdata, int speed)
{
	return pdata->phy_if.phy_impl.valid_speed(pdata, speed);
}

static int xgbe_phy_config_fixed(struct xgbe_prv_data *pdata)
{
	enum xgbe_mode mode;

	netif_dbg(pdata, link, pdata->netdev, "fixed PHY configuration\n");

	/* Disable auto-negotiation */
	xgbe_an_disable(pdata);

	/* Set specified mode for specified speed */
	mode = pdata->phy_if.phy_impl.get_mode(pdata, pdata->phy.speed);
	switch (mode) {
	case XGBE_MODE_KX_1000:
	case XGBE_MODE_KX_2500:
	case XGBE_MODE_KR:
	case XGBE_MODE_SGMII_100:
	case XGBE_MODE_SGMII_1000:
	case XGBE_MODE_X:
	case XGBE_MODE_SFI:
		break;
	case XGBE_MODE_UNKNOWN:
	default:
		return -EINVAL;
	}

	/* Validate duplex mode */
	if (pdata->phy.duplex != DUPLEX_FULL)
		return -EINVAL;

	xgbe_set_mode(pdata, mode);

	return 0;
}

static int __xgbe_phy_config_aneg(struct xgbe_prv_data *pdata)
{
	int ret;

	set_bit(XGBE_LINK_INIT, &pdata->dev_state);
	pdata->link_check = jiffies;

	ret = pdata->phy_if.phy_impl.an_config(pdata);
	if (ret)
		return ret;

	if (pdata->phy.autoneg != AUTONEG_ENABLE) {
		ret = xgbe_phy_config_fixed(pdata);
		if (ret || !pdata->kr_redrv)
			return ret;

		netif_dbg(pdata, link, pdata->netdev, "AN redriver support\n");
	} else {
		netif_dbg(pdata, link, pdata->netdev, "AN PHY configuration\n");
	}

	/* Disable auto-negotiation interrupt */
	disable_irq(pdata->an_irq);

	/* Start auto-negotiation in a supported mode */
	if (xgbe_use_mode(pdata, XGBE_MODE_KR)) {
		xgbe_set_mode(pdata, XGBE_MODE_KR);
	} else if (xgbe_use_mode(pdata, XGBE_MODE_KX_2500)) {
		xgbe_set_mode(pdata, XGBE_MODE_KX_2500);
	} else if (xgbe_use_mode(pdata, XGBE_MODE_KX_1000)) {
		xgbe_set_mode(pdata, XGBE_MODE_KX_1000);
	} else if (xgbe_use_mode(pdata, XGBE_MODE_SFI)) {
		xgbe_set_mode(pdata, XGBE_MODE_SFI);
	} else if (xgbe_use_mode(pdata, XGBE_MODE_X)) {
		xgbe_set_mode(pdata, XGBE_MODE_X);
	} else if (xgbe_use_mode(pdata, XGBE_MODE_SGMII_1000)) {
		xgbe_set_mode(pdata, XGBE_MODE_SGMII_1000);
	} else if (xgbe_use_mode(pdata, XGBE_MODE_SGMII_100)) {
		xgbe_set_mode(pdata, XGBE_MODE_SGMII_100);
	} else {
		enable_irq(pdata->an_irq);
		return -EINVAL;
	}

	/* Disable and stop any in progress auto-negotiation */
	xgbe_an_disable_all(pdata);

	/* Clear any auto-negotitation interrupts */
	xgbe_an_clear_interrupts_all(pdata);

	pdata->an_result = XGBE_AN_READY;
	pdata->an_state = XGBE_AN_READY;
	pdata->kr_state = XGBE_RX_BPA;
	pdata->kx_state = XGBE_RX_BPA;

	/* Re-enable auto-negotiation interrupt */
	enable_irq(pdata->an_irq);

	xgbe_an_init(pdata);
	xgbe_an_restart(pdata);

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

static enum xgbe_mode xgbe_phy_status_aneg(struct xgbe_prv_data *pdata)
{
	return pdata->phy_if.phy_impl.an_outcome(pdata);
}

static void xgbe_phy_status_result(struct xgbe_prv_data *pdata)
{
	enum xgbe_mode mode;

	pdata->phy.lp_advertising = 0;

	if ((pdata->phy.autoneg != AUTONEG_ENABLE) || pdata->parallel_detect)
		mode = xgbe_cur_mode(pdata);
	else
		mode = xgbe_phy_status_aneg(pdata);

	switch (mode) {
	case XGBE_MODE_SGMII_100:
		pdata->phy.speed = SPEED_100;
		break;
	case XGBE_MODE_X:
	case XGBE_MODE_KX_1000:
	case XGBE_MODE_SGMII_1000:
		pdata->phy.speed = SPEED_1000;
		break;
	case XGBE_MODE_KX_2500:
		pdata->phy.speed = SPEED_2500;
		break;
	case XGBE_MODE_KR:
	case XGBE_MODE_SFI:
		pdata->phy.speed = SPEED_10000;
		break;
	case XGBE_MODE_UNKNOWN:
	default:
		pdata->phy.speed = SPEED_UNKNOWN;
	}

	pdata->phy.duplex = DUPLEX_FULL;

	xgbe_set_mode(pdata, mode);
}

static void xgbe_phy_status(struct xgbe_prv_data *pdata)
{
	unsigned int link_aneg;
	int an_restart;

	if (test_bit(XGBE_LINK_ERR, &pdata->dev_state)) {
		netif_carrier_off(pdata->netdev);

		pdata->phy.link = 0;
		goto adjust_link;
	}

	link_aneg = (pdata->phy.autoneg == AUTONEG_ENABLE);

	pdata->phy.link = pdata->phy_if.phy_impl.link_status(pdata,
							     &an_restart);
	if (an_restart) {
		xgbe_phy_config_aneg(pdata);
		return;
	}

	if (pdata->phy.link) {
		if (link_aneg && !xgbe_phy_aneg_done(pdata)) {
			xgbe_check_link_timeout(pdata);
			return;
		}

		xgbe_phy_status_result(pdata);

		if (test_bit(XGBE_LINK_INIT, &pdata->dev_state))
			clear_bit(XGBE_LINK_INIT, &pdata->dev_state);

		netif_carrier_on(pdata->netdev);
	} else {
		if (test_bit(XGBE_LINK_INIT, &pdata->dev_state)) {
			xgbe_check_link_timeout(pdata);

			if (link_aneg)
				return;
		}

		xgbe_phy_status_result(pdata);

		netif_carrier_off(pdata->netdev);
	}

adjust_link:
	xgbe_phy_adjust_link(pdata);
}

static void xgbe_phy_stop(struct xgbe_prv_data *pdata)
{
	netif_dbg(pdata, link, pdata->netdev, "stopping PHY\n");

	if (!pdata->phy_started)
		return;

	/* Indicate the PHY is down */
	pdata->phy_started = 0;

	/* Disable auto-negotiation */
	xgbe_an_disable_all(pdata);

	if (pdata->dev_irq != pdata->an_irq)
		devm_free_irq(pdata->dev, pdata->an_irq, pdata);

	pdata->phy_if.phy_impl.stop(pdata);

	pdata->phy.link = 0;
	netif_carrier_off(pdata->netdev);

	xgbe_phy_adjust_link(pdata);
}

static int xgbe_phy_start(struct xgbe_prv_data *pdata)
{
	struct net_device *netdev = pdata->netdev;
	int ret;

	netif_dbg(pdata, link, pdata->netdev, "starting PHY\n");

	ret = pdata->phy_if.phy_impl.start(pdata);
	if (ret)
		return ret;

	/* If we have a separate AN irq, enable it */
	if (pdata->dev_irq != pdata->an_irq) {
		ret = devm_request_irq(pdata->dev, pdata->an_irq,
				       xgbe_an_isr, 0, pdata->an_name,
				       pdata);
		if (ret) {
			netdev_err(netdev, "phy irq request failed\n");
			goto err_stop;
		}
	}

	/* Set initial mode - call the mode setting routines
	 * directly to insure we are properly configured
	 */
	if (xgbe_use_mode(pdata, XGBE_MODE_KR)) {
		xgbe_kr_mode(pdata);
	} else if (xgbe_use_mode(pdata, XGBE_MODE_KX_2500)) {
		xgbe_kx_2500_mode(pdata);
	} else if (xgbe_use_mode(pdata, XGBE_MODE_KX_1000)) {
		xgbe_kx_1000_mode(pdata);
	} else if (xgbe_use_mode(pdata, XGBE_MODE_SFI)) {
		xgbe_sfi_mode(pdata);
	} else if (xgbe_use_mode(pdata, XGBE_MODE_X)) {
		xgbe_x_mode(pdata);
	} else if (xgbe_use_mode(pdata, XGBE_MODE_SGMII_1000)) {
		xgbe_sgmii_1000_mode(pdata);
	} else if (xgbe_use_mode(pdata, XGBE_MODE_SGMII_100)) {
		xgbe_sgmii_100_mode(pdata);
	} else {
		ret = -EINVAL;
		goto err_irq;
	}

	/* Indicate the PHY is up and running */
	pdata->phy_started = 1;

	xgbe_an_init(pdata);
	xgbe_an_enable_interrupts(pdata);

	return xgbe_phy_config_aneg(pdata);

err_irq:
	if (pdata->dev_irq != pdata->an_irq)
		devm_free_irq(pdata->dev, pdata->an_irq, pdata);

err_stop:
	pdata->phy_if.phy_impl.stop(pdata);

	return ret;
}

static int xgbe_phy_reset(struct xgbe_prv_data *pdata)
{
	int ret;

	ret = pdata->phy_if.phy_impl.reset(pdata);
	if (ret)
		return ret;

	/* Disable auto-negotiation for now */
	xgbe_an_disable_all(pdata);

	/* Clear auto-negotiation interrupts */
	xgbe_an_clear_interrupts_all(pdata);

	return 0;
}

static void xgbe_dump_phy_registers(struct xgbe_prv_data *pdata)
{
	struct device *dev = pdata->dev;

	dev_dbg(dev, "\n************* PHY Reg dump **********************\n");

	dev_dbg(dev, "PCS Control Reg (%#06x) = %#06x\n", MDIO_CTRL1,
		XMDIO_READ(pdata, MDIO_MMD_PCS, MDIO_CTRL1));
	dev_dbg(dev, "PCS Status Reg (%#06x) = %#06x\n", MDIO_STAT1,
		XMDIO_READ(pdata, MDIO_MMD_PCS, MDIO_STAT1));
	dev_dbg(dev, "Phy Id (PHYS ID 1 %#06x)= %#06x\n", MDIO_DEVID1,
		XMDIO_READ(pdata, MDIO_MMD_PCS, MDIO_DEVID1));
	dev_dbg(dev, "Phy Id (PHYS ID 2 %#06x)= %#06x\n", MDIO_DEVID2,
		XMDIO_READ(pdata, MDIO_MMD_PCS, MDIO_DEVID2));
	dev_dbg(dev, "Devices in Package (%#06x)= %#06x\n", MDIO_DEVS1,
		XMDIO_READ(pdata, MDIO_MMD_PCS, MDIO_DEVS1));
	dev_dbg(dev, "Devices in Package (%#06x)= %#06x\n", MDIO_DEVS2,
		XMDIO_READ(pdata, MDIO_MMD_PCS, MDIO_DEVS2));

	dev_dbg(dev, "Auto-Neg Control Reg (%#06x) = %#06x\n", MDIO_CTRL1,
		XMDIO_READ(pdata, MDIO_MMD_AN, MDIO_CTRL1));
	dev_dbg(dev, "Auto-Neg Status Reg (%#06x) = %#06x\n", MDIO_STAT1,
		XMDIO_READ(pdata, MDIO_MMD_AN, MDIO_STAT1));
	dev_dbg(dev, "Auto-Neg Ad Reg 1 (%#06x) = %#06x\n",
		MDIO_AN_ADVERTISE,
		XMDIO_READ(pdata, MDIO_MMD_AN, MDIO_AN_ADVERTISE));
	dev_dbg(dev, "Auto-Neg Ad Reg 2 (%#06x) = %#06x\n",
		MDIO_AN_ADVERTISE + 1,
		XMDIO_READ(pdata, MDIO_MMD_AN, MDIO_AN_ADVERTISE + 1));
	dev_dbg(dev, "Auto-Neg Ad Reg 3 (%#06x) = %#06x\n",
		MDIO_AN_ADVERTISE + 2,
		XMDIO_READ(pdata, MDIO_MMD_AN, MDIO_AN_ADVERTISE + 2));
	dev_dbg(dev, "Auto-Neg Completion Reg (%#06x) = %#06x\n",
		MDIO_AN_COMP_STAT,
		XMDIO_READ(pdata, MDIO_MMD_AN, MDIO_AN_COMP_STAT));

	dev_dbg(dev, "\n*************************************************\n");
}

static int xgbe_phy_best_advertised_speed(struct xgbe_prv_data *pdata)
{
	if (pdata->phy.advertising & ADVERTISED_10000baseKR_Full)
		return SPEED_10000;
	else if (pdata->phy.advertising & ADVERTISED_10000baseT_Full)
		return SPEED_10000;
	else if (pdata->phy.advertising & ADVERTISED_2500baseX_Full)
		return SPEED_2500;
	else if (pdata->phy.advertising & ADVERTISED_1000baseKX_Full)
		return SPEED_1000;
	else if (pdata->phy.advertising & ADVERTISED_1000baseT_Full)
		return SPEED_1000;
	else if (pdata->phy.advertising & ADVERTISED_100baseT_Full)
		return SPEED_100;

	return SPEED_UNKNOWN;
}

static void xgbe_phy_exit(struct xgbe_prv_data *pdata)
{
	xgbe_phy_stop(pdata);

	pdata->phy_if.phy_impl.exit(pdata);
}

static int xgbe_phy_init(struct xgbe_prv_data *pdata)
{
	int ret;

	mutex_init(&pdata->an_mutex);
	INIT_WORK(&pdata->an_irq_work, xgbe_an_irq_work);
	INIT_WORK(&pdata->an_work, xgbe_an_state_machine);
	pdata->mdio_mmd = MDIO_MMD_PCS;

	/* Check for FEC support */
	pdata->fec_ability = XMDIO_READ(pdata, MDIO_MMD_PMAPMD,
					MDIO_PMA_10GBR_FECABLE);
	pdata->fec_ability &= (MDIO_PMA_10GBR_FECABLE_ABLE |
			       MDIO_PMA_10GBR_FECABLE_ERRABLE);

	/* Setup the phy (including supported features) */
	ret = pdata->phy_if.phy_impl.init(pdata);
	if (ret)
		return ret;
	pdata->phy.advertising = pdata->phy.supported;

	pdata->phy.address = 0;

	if (pdata->phy.advertising & ADVERTISED_Autoneg) {
		pdata->phy.autoneg = AUTONEG_ENABLE;
		pdata->phy.speed = SPEED_UNKNOWN;
		pdata->phy.duplex = DUPLEX_UNKNOWN;
	} else {
		pdata->phy.autoneg = AUTONEG_DISABLE;
		pdata->phy.speed = xgbe_phy_best_advertised_speed(pdata);
		pdata->phy.duplex = DUPLEX_FULL;
	}

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

	return 0;
}

void xgbe_init_function_ptrs_phy(struct xgbe_phy_if *phy_if)
{
	phy_if->phy_init        = xgbe_phy_init;
	phy_if->phy_exit        = xgbe_phy_exit;

	phy_if->phy_reset       = xgbe_phy_reset;
	phy_if->phy_start       = xgbe_phy_start;
	phy_if->phy_stop        = xgbe_phy_stop;

	phy_if->phy_status      = xgbe_phy_status;
	phy_if->phy_config_aneg = xgbe_phy_config_aneg;

	phy_if->phy_valid_speed = xgbe_phy_valid_speed;

	phy_if->an_isr          = xgbe_an_combined_isr;
}
