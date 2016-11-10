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
#include <linux/mdio.h>
#include <linux/phy.h>

#include "xgbe.h"
#include "xgbe-common.h"

#define XGBE_PHY_PORT_SPEED_100		BIT(0)
#define XGBE_PHY_PORT_SPEED_1000	BIT(1)
#define XGBE_PHY_PORT_SPEED_2500	BIT(2)
#define XGBE_PHY_PORT_SPEED_10000	BIT(3)

/* Rate-change complete wait/retry count */
#define XGBE_RATECHANGE_COUNT		500

enum xgbe_port_mode {
	XGBE_PORT_MODE_RSVD = 0,
	XGBE_PORT_MODE_BACKPLANE,
	XGBE_PORT_MODE_BACKPLANE_2500,
	XGBE_PORT_MODE_1000BASE_T,
	XGBE_PORT_MODE_1000BASE_X,
	XGBE_PORT_MODE_NBASE_T,
	XGBE_PORT_MODE_10GBASE_T,
	XGBE_PORT_MODE_10GBASE_R,
	XGBE_PORT_MODE_SFP,
	XGBE_PORT_MODE_MAX,
};

enum xgbe_conn_type {
	XGBE_CONN_TYPE_NONE = 0,
	XGBE_CONN_TYPE_SFP,
	XGBE_CONN_TYPE_MDIO,
	XGBE_CONN_TYPE_BACKPLANE,
	XGBE_CONN_TYPE_MAX,
};

/* PHY related configuration information */
struct xgbe_phy_data {
	enum xgbe_port_mode port_mode;

	unsigned int port_id;

	unsigned int port_speeds;

	enum xgbe_conn_type conn_type;

	enum xgbe_mode cur_mode;
	enum xgbe_mode start_mode;

	unsigned int rrc_count;
};

static enum xgbe_mode xgbe_phy_an_outcome(struct xgbe_prv_data *pdata)
{
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
	if (lp_reg & 0x20)
		pdata->phy.lp_advertising |= ADVERTISED_1000baseKX_Full;

	ad_reg &= lp_reg;
	if (ad_reg & 0x80)
		mode = XGBE_MODE_KR;
	else if (ad_reg & 0x20)
		mode = XGBE_MODE_KX_1000;
	else
		mode = XGBE_MODE_UNKNOWN;

	/* Compare Advertisement and Link Partner register 3 */
	ad_reg = XMDIO_READ(pdata, MDIO_MMD_AN, MDIO_AN_ADVERTISE + 2);
	lp_reg = XMDIO_READ(pdata, MDIO_MMD_AN, MDIO_AN_LPA + 2);
	if (lp_reg & 0xc000)
		pdata->phy.lp_advertising |= ADVERTISED_10000baseR_FEC;

	return mode;
}

static enum xgbe_an_mode xgbe_phy_an_mode(struct xgbe_prv_data *pdata)
{
	struct xgbe_phy_data *phy_data = pdata->phy_data;

	switch (phy_data->port_mode) {
	case XGBE_PORT_MODE_BACKPLANE:
		return XGBE_AN_MODE_CL73;
	case XGBE_PORT_MODE_BACKPLANE_2500:
		return XGBE_AN_MODE_NONE;
	case XGBE_PORT_MODE_1000BASE_T:
	case XGBE_PORT_MODE_1000BASE_X:
	case XGBE_PORT_MODE_NBASE_T:
	case XGBE_PORT_MODE_10GBASE_T:
	case XGBE_PORT_MODE_10GBASE_R:
	case XGBE_PORT_MODE_SFP:
	default:
		return XGBE_AN_MODE_NONE;
	}
}

static void xgbe_phy_start_ratechange(struct xgbe_prv_data *pdata)
{
	if (!XP_IOREAD_BITS(pdata, XP_DRIVER_INT_RO, STATUS))
		return;

	/* Log if a previous command did not complete */
	netif_dbg(pdata, link, pdata->netdev,
		  "firmware mailbox not ready for command\n");
}

static void xgbe_phy_complete_ratechange(struct xgbe_prv_data *pdata)
{
	unsigned int wait;

	/* Wait for command to complete */
	wait = XGBE_RATECHANGE_COUNT;
	while (wait--) {
		if (!XP_IOREAD_BITS(pdata, XP_DRIVER_INT_RO, STATUS))
			return;

		usleep_range(1000, 2000);
	}

	netif_dbg(pdata, link, pdata->netdev,
		  "firmware mailbox command did not complete\n");
}

static void xgbe_phy_rrc(struct xgbe_prv_data *pdata)
{
	unsigned int s0;

	xgbe_phy_start_ratechange(pdata);

	/* Receiver Reset Cycle */
	s0 = 0;
	XP_SET_BITS(s0, XP_DRIVER_SCRATCH_0, COMMAND, 5);
	XP_SET_BITS(s0, XP_DRIVER_SCRATCH_0, SUB_COMMAND, 0);

	/* Call FW to make the change */
	XP_IOWRITE(pdata, XP_DRIVER_SCRATCH_0, s0);
	XP_IOWRITE(pdata, XP_DRIVER_SCRATCH_1, 0);
	XP_IOWRITE_BITS(pdata, XP_DRIVER_INT_REQ, REQUEST, 1);

	xgbe_phy_complete_ratechange(pdata);

	netif_dbg(pdata, link, pdata->netdev, "receiver reset complete\n");
}

static void xgbe_phy_power_off(struct xgbe_prv_data *pdata)
{
	struct xgbe_phy_data *phy_data = pdata->phy_data;

	xgbe_phy_start_ratechange(pdata);

	/* Call FW to make the change */
	XP_IOWRITE(pdata, XP_DRIVER_SCRATCH_0, 0);
	XP_IOWRITE(pdata, XP_DRIVER_SCRATCH_1, 0);
	XP_IOWRITE_BITS(pdata, XP_DRIVER_INT_REQ, REQUEST, 1);

	xgbe_phy_complete_ratechange(pdata);

	phy_data->cur_mode = XGBE_MODE_UNKNOWN;

	netif_dbg(pdata, link, pdata->netdev, "phy powered off\n");
}

static void xgbe_phy_kr_mode(struct xgbe_prv_data *pdata)
{
	struct xgbe_phy_data *phy_data = pdata->phy_data;
	unsigned int s0;

	xgbe_phy_start_ratechange(pdata);

	/* 10G/KR */
	s0 = 0;
	XP_SET_BITS(s0, XP_DRIVER_SCRATCH_0, COMMAND, 4);
	XP_SET_BITS(s0, XP_DRIVER_SCRATCH_0, SUB_COMMAND, 0);

	/* Call FW to make the change */
	XP_IOWRITE(pdata, XP_DRIVER_SCRATCH_0, s0);
	XP_IOWRITE(pdata, XP_DRIVER_SCRATCH_1, 0);
	XP_IOWRITE_BITS(pdata, XP_DRIVER_INT_REQ, REQUEST, 1);

	xgbe_phy_complete_ratechange(pdata);

	phy_data->cur_mode = XGBE_MODE_KR;

	netif_dbg(pdata, link, pdata->netdev, "10GbE KR mode set\n");
}

static void xgbe_phy_kx_2500_mode(struct xgbe_prv_data *pdata)
{
	struct xgbe_phy_data *phy_data = pdata->phy_data;
	unsigned int s0;

	xgbe_phy_start_ratechange(pdata);

	/* 2.5G/KX */
	s0 = 0;
	XP_SET_BITS(s0, XP_DRIVER_SCRATCH_0, COMMAND, 2);
	XP_SET_BITS(s0, XP_DRIVER_SCRATCH_0, SUB_COMMAND, 0);

	/* Call FW to make the change */
	XP_IOWRITE(pdata, XP_DRIVER_SCRATCH_0, s0);
	XP_IOWRITE(pdata, XP_DRIVER_SCRATCH_1, 0);
	XP_IOWRITE_BITS(pdata, XP_DRIVER_INT_REQ, REQUEST, 1);

	xgbe_phy_complete_ratechange(pdata);

	phy_data->cur_mode = XGBE_MODE_KX_2500;

	netif_dbg(pdata, link, pdata->netdev, "2.5GbE KX mode set\n");
}

static void xgbe_phy_kx_1000_mode(struct xgbe_prv_data *pdata)
{
	struct xgbe_phy_data *phy_data = pdata->phy_data;
	unsigned int s0;

	xgbe_phy_start_ratechange(pdata);

	/* 1G/KX */
	s0 = 0;
	XP_SET_BITS(s0, XP_DRIVER_SCRATCH_0, COMMAND, 1);
	XP_SET_BITS(s0, XP_DRIVER_SCRATCH_0, SUB_COMMAND, 3);

	/* Call FW to make the change */
	XP_IOWRITE(pdata, XP_DRIVER_SCRATCH_0, s0);
	XP_IOWRITE(pdata, XP_DRIVER_SCRATCH_1, 0);
	XP_IOWRITE_BITS(pdata, XP_DRIVER_INT_REQ, REQUEST, 1);

	xgbe_phy_complete_ratechange(pdata);

	phy_data->cur_mode = XGBE_MODE_KX_1000;

	netif_dbg(pdata, link, pdata->netdev, "1GbE KX mode set\n");
}

static enum xgbe_mode xgbe_phy_cur_mode(struct xgbe_prv_data *pdata)
{
	struct xgbe_phy_data *phy_data = pdata->phy_data;

	return phy_data->cur_mode;
}

static enum xgbe_mode xgbe_phy_switch_bp_2500_mode(struct xgbe_prv_data *pdata)
{
	return XGBE_MODE_KX_2500;
}

static enum xgbe_mode xgbe_phy_switch_bp_mode(struct xgbe_prv_data *pdata)
{
	/* If we are in KR switch to KX, and vice-versa */
	switch (xgbe_phy_cur_mode(pdata)) {
	case XGBE_MODE_KX_1000:
		return XGBE_MODE_KR;
	case XGBE_MODE_KR:
	default:
		return XGBE_MODE_KX_1000;
	}
}

static enum xgbe_mode xgbe_phy_switch_mode(struct xgbe_prv_data *pdata)
{
	struct xgbe_phy_data *phy_data = pdata->phy_data;

	switch (phy_data->port_mode) {
	case XGBE_PORT_MODE_BACKPLANE:
		return xgbe_phy_switch_bp_mode(pdata);
	case XGBE_PORT_MODE_BACKPLANE_2500:
		return xgbe_phy_switch_bp_2500_mode(pdata);
	case XGBE_PORT_MODE_1000BASE_T:
	case XGBE_PORT_MODE_1000BASE_X:
	case XGBE_PORT_MODE_NBASE_T:
	case XGBE_PORT_MODE_10GBASE_T:
	case XGBE_PORT_MODE_10GBASE_R:
	case XGBE_PORT_MODE_SFP:
	default:
		return XGBE_MODE_UNKNOWN;
	}
}

static enum xgbe_mode xgbe_phy_get_bp_2500_mode(int speed)
{
	switch (speed) {
	case SPEED_2500:
		return XGBE_MODE_KX_2500;
	default:
		return XGBE_MODE_UNKNOWN;
	}
}

static enum xgbe_mode xgbe_phy_get_bp_mode(int speed)
{
	switch (speed) {
	case SPEED_1000:
		return XGBE_MODE_KX_1000;
	case SPEED_10000:
		return XGBE_MODE_KR;
	default:
		return XGBE_MODE_UNKNOWN;
	}
}

static enum xgbe_mode xgbe_phy_get_mode(struct xgbe_prv_data *pdata,
					int speed)
{
	struct xgbe_phy_data *phy_data = pdata->phy_data;

	switch (phy_data->port_mode) {
	case XGBE_PORT_MODE_BACKPLANE:
		return xgbe_phy_get_bp_mode(speed);
	case XGBE_PORT_MODE_BACKPLANE_2500:
		return xgbe_phy_get_bp_2500_mode(speed);
	case XGBE_PORT_MODE_1000BASE_T:
	case XGBE_PORT_MODE_1000BASE_X:
	case XGBE_PORT_MODE_NBASE_T:
	case XGBE_PORT_MODE_10GBASE_T:
	case XGBE_PORT_MODE_10GBASE_R:
	case XGBE_PORT_MODE_SFP:
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

static bool xgbe_phy_use_bp_2500_mode(struct xgbe_prv_data *pdata,
				      enum xgbe_mode mode)
{
	switch (mode) {
	case XGBE_MODE_KX_2500:
		return xgbe_phy_check_mode(pdata, mode,
					   ADVERTISED_2500baseX_Full);
	default:
		return false;
	}
}

static bool xgbe_phy_use_bp_mode(struct xgbe_prv_data *pdata,
				 enum xgbe_mode mode)
{
	switch (mode) {
	case XGBE_MODE_KX_1000:
		return xgbe_phy_check_mode(pdata, mode,
					   ADVERTISED_1000baseKX_Full);
	case XGBE_MODE_KR:
		return xgbe_phy_check_mode(pdata, mode,
					   ADVERTISED_10000baseKR_Full);
	default:
		return false;
	}
}

static bool xgbe_phy_use_mode(struct xgbe_prv_data *pdata, enum xgbe_mode mode)
{
	struct xgbe_phy_data *phy_data = pdata->phy_data;

	switch (phy_data->port_mode) {
	case XGBE_PORT_MODE_BACKPLANE:
		return xgbe_phy_use_bp_mode(pdata, mode);
	case XGBE_PORT_MODE_BACKPLANE_2500:
		return xgbe_phy_use_bp_2500_mode(pdata, mode);
	case XGBE_PORT_MODE_1000BASE_T:
	case XGBE_PORT_MODE_1000BASE_X:
	case XGBE_PORT_MODE_NBASE_T:
	case XGBE_PORT_MODE_10GBASE_T:
	case XGBE_PORT_MODE_10GBASE_R:
	case XGBE_PORT_MODE_SFP:
	default:
		return false;
	}
}

static bool xgbe_phy_valid_speed_bp_2500_mode(int speed)
{
	switch (speed) {
	case SPEED_2500:
		return true;
	default:
		return false;
	}
}

static bool xgbe_phy_valid_speed_bp_mode(int speed)
{
	switch (speed) {
	case SPEED_1000:
	case SPEED_10000:
		return true;
	default:
		return false;
	}
}

static bool xgbe_phy_valid_speed(struct xgbe_prv_data *pdata, int speed)
{
	struct xgbe_phy_data *phy_data = pdata->phy_data;

	switch (phy_data->port_mode) {
	case XGBE_PORT_MODE_BACKPLANE:
		return xgbe_phy_valid_speed_bp_mode(speed);
	case XGBE_PORT_MODE_BACKPLANE_2500:
		return xgbe_phy_valid_speed_bp_2500_mode(speed);
	case XGBE_PORT_MODE_1000BASE_T:
	case XGBE_PORT_MODE_1000BASE_X:
	case XGBE_PORT_MODE_NBASE_T:
	case XGBE_PORT_MODE_10GBASE_T:
	case XGBE_PORT_MODE_10GBASE_R:
	case XGBE_PORT_MODE_SFP:
	default:
		return false;
	}
}

static int xgbe_phy_link_status(struct xgbe_prv_data *pdata)
{
	struct xgbe_phy_data *phy_data = pdata->phy_data;
	unsigned int reg;

	/* Link status is latched low, so read once to clear
	 * and then read again to get current state
	 */
	reg = XMDIO_READ(pdata, MDIO_MMD_PCS, MDIO_STAT1);
	reg = XMDIO_READ(pdata, MDIO_MMD_PCS, MDIO_STAT1);
	if (reg & MDIO_STAT1_LSTATUS)
		return 1;

	/* No link, attempt a receiver reset cycle */
	if (phy_data->rrc_count++) {
		phy_data->rrc_count = 0;
		xgbe_phy_rrc(pdata);
	}

	return 0;
}

static bool xgbe_phy_port_mode_mismatch(struct xgbe_prv_data *pdata)
{
	struct xgbe_phy_data *phy_data = pdata->phy_data;

	switch (phy_data->port_mode) {
	case XGBE_PORT_MODE_BACKPLANE:
		if ((phy_data->port_speeds & XGBE_PHY_PORT_SPEED_1000) ||
		    (phy_data->port_speeds & XGBE_PHY_PORT_SPEED_10000))
			return false;
		break;
	case XGBE_PORT_MODE_BACKPLANE_2500:
		if (phy_data->port_speeds & XGBE_PHY_PORT_SPEED_2500)
			return false;
		break;
	case XGBE_PORT_MODE_1000BASE_T:
		if ((phy_data->port_speeds & XGBE_PHY_PORT_SPEED_100) ||
		    (phy_data->port_speeds & XGBE_PHY_PORT_SPEED_1000))
			return false;
		break;
	case XGBE_PORT_MODE_1000BASE_X:
		if (phy_data->port_speeds & XGBE_PHY_PORT_SPEED_1000)
			return false;
		break;
	case XGBE_PORT_MODE_NBASE_T:
		if ((phy_data->port_speeds & XGBE_PHY_PORT_SPEED_100) ||
		    (phy_data->port_speeds & XGBE_PHY_PORT_SPEED_1000) ||
		    (phy_data->port_speeds & XGBE_PHY_PORT_SPEED_2500))
			return false;
		break;
	case XGBE_PORT_MODE_10GBASE_T:
		if ((phy_data->port_speeds & XGBE_PHY_PORT_SPEED_1000) ||
		    (phy_data->port_speeds & XGBE_PHY_PORT_SPEED_10000))
			return false;
		break;
	case XGBE_PORT_MODE_10GBASE_R:
		if (phy_data->port_speeds & XGBE_PHY_PORT_SPEED_10000)
			return false;
		break;
	case XGBE_PORT_MODE_SFP:
		if ((phy_data->port_speeds & XGBE_PHY_PORT_SPEED_100) ||
		    (phy_data->port_speeds & XGBE_PHY_PORT_SPEED_1000) ||
		    (phy_data->port_speeds & XGBE_PHY_PORT_SPEED_10000))
			return false;
		break;
	default:
		break;
	}

	return true;
}

static bool xgbe_phy_conn_type_mismatch(struct xgbe_prv_data *pdata)
{
	struct xgbe_phy_data *phy_data = pdata->phy_data;

	switch (phy_data->port_mode) {
	case XGBE_PORT_MODE_BACKPLANE:
	case XGBE_PORT_MODE_BACKPLANE_2500:
		if (phy_data->conn_type == XGBE_CONN_TYPE_BACKPLANE)
			return false;
		break;
	case XGBE_PORT_MODE_1000BASE_T:
	case XGBE_PORT_MODE_1000BASE_X:
	case XGBE_PORT_MODE_NBASE_T:
	case XGBE_PORT_MODE_10GBASE_T:
	case XGBE_PORT_MODE_10GBASE_R:
		if (phy_data->conn_type == XGBE_CONN_TYPE_MDIO)
			return false;
		break;
	case XGBE_PORT_MODE_SFP:
		if (phy_data->conn_type == XGBE_CONN_TYPE_SFP)
			return false;
		break;
	default:
		break;
	}

	return true;
}

static bool xgbe_phy_port_enabled(struct xgbe_prv_data *pdata)
{
	unsigned int reg;

	reg = XP_IOREAD(pdata, XP_PROP_0);
	if (!XP_GET_BITS(reg, XP_PROP_0, PORT_SPEEDS))
		return false;
	if (!XP_GET_BITS(reg, XP_PROP_0, CONN_TYPE))
		return false;

	return true;
}

static void xgbe_phy_stop(struct xgbe_prv_data *pdata)
{
	/* Power off the PHY */
	xgbe_phy_power_off(pdata);
}

static int xgbe_phy_start(struct xgbe_prv_data *pdata)
{
	struct xgbe_phy_data *phy_data = pdata->phy_data;

	/* Start in highest supported mode */
	xgbe_phy_set_mode(pdata, phy_data->start_mode);

	return 0;
}

static int xgbe_phy_reset(struct xgbe_prv_data *pdata)
{
	struct xgbe_phy_data *phy_data = pdata->phy_data;
	enum xgbe_mode cur_mode;

	/* Reset by power cycling the PHY */
	cur_mode = phy_data->cur_mode;
	xgbe_phy_power_off(pdata);
	xgbe_phy_set_mode(pdata, cur_mode);

	return 0;
}

static void xgbe_phy_exit(struct xgbe_prv_data *pdata)
{
	/* Nothing uniquely required for exit */
}

static int xgbe_phy_init(struct xgbe_prv_data *pdata)
{
	struct xgbe_phy_data *phy_data;
	unsigned int reg;

	/* Check if enabled */
	if (!xgbe_phy_port_enabled(pdata)) {
		dev_info(pdata->dev, "device is not enabled\n");
		return -ENODEV;
	}

	phy_data = devm_kzalloc(pdata->dev, sizeof(*phy_data), GFP_KERNEL);
	if (!phy_data)
		return -ENOMEM;
	pdata->phy_data = phy_data;

	reg = XP_IOREAD(pdata, XP_PROP_0);
	phy_data->port_mode = XP_GET_BITS(reg, XP_PROP_0, PORT_MODE);
	phy_data->port_id = XP_GET_BITS(reg, XP_PROP_0, PORT_ID);
	phy_data->port_speeds = XP_GET_BITS(reg, XP_PROP_0, PORT_SPEEDS);
	phy_data->conn_type = XP_GET_BITS(reg, XP_PROP_0, CONN_TYPE);
	if (netif_msg_probe(pdata)) {
		dev_dbg(pdata->dev, "port mode=%u\n", phy_data->port_mode);
		dev_dbg(pdata->dev, "port id=%u\n", phy_data->port_id);
		dev_dbg(pdata->dev, "port speeds=%#x\n", phy_data->port_speeds);
		dev_dbg(pdata->dev, "conn type=%u\n", phy_data->conn_type);
	}

	/* Validate the connection requested */
	if (xgbe_phy_conn_type_mismatch(pdata)) {
		dev_err(pdata->dev, "phy mode/connection mismatch (%#x/%#x)\n",
			phy_data->port_mode, phy_data->conn_type);
	}

	/* Validate the mode requested */
	if (xgbe_phy_port_mode_mismatch(pdata)) {
		dev_err(pdata->dev, "phy mode/speed mismatch (%#x/%#x)\n",
			phy_data->port_mode, phy_data->port_speeds);
		return -EINVAL;
	}

	/* Indicate current mode is unknown */
	phy_data->cur_mode = XGBE_MODE_UNKNOWN;

	/* Initialize supported features */
	pdata->phy.supported = 0;

	switch (phy_data->port_mode) {
	case XGBE_PORT_MODE_BACKPLANE:
		pdata->phy.supported |= SUPPORTED_Autoneg;
		pdata->phy.supported |= SUPPORTED_Pause | SUPPORTED_Asym_Pause;
		pdata->phy.supported |= SUPPORTED_Backplane;
		if (phy_data->port_speeds & XGBE_PHY_PORT_SPEED_1000) {
			pdata->phy.supported |= SUPPORTED_1000baseKX_Full;
			phy_data->start_mode = XGBE_MODE_KX_1000;
		}
		if (phy_data->port_speeds & XGBE_PHY_PORT_SPEED_10000) {
			pdata->phy.supported |= SUPPORTED_10000baseKR_Full;
			if (pdata->fec_ability & MDIO_PMA_10GBR_FECABLE_ABLE)
				pdata->phy.supported |=
					SUPPORTED_10000baseR_FEC;
			phy_data->start_mode = XGBE_MODE_KR;
		}
		break;
	case XGBE_PORT_MODE_BACKPLANE_2500:
		pdata->phy.supported |= SUPPORTED_Pause | SUPPORTED_Asym_Pause;
		pdata->phy.supported |= SUPPORTED_Backplane;
		pdata->phy.supported |= SUPPORTED_2500baseX_Full;
		phy_data->start_mode = XGBE_MODE_KX_2500;
		break;
	case XGBE_PORT_MODE_1000BASE_T:
	case XGBE_PORT_MODE_1000BASE_X:
	case XGBE_PORT_MODE_NBASE_T:
	case XGBE_PORT_MODE_10GBASE_T:
	case XGBE_PORT_MODE_10GBASE_R:
	case XGBE_PORT_MODE_SFP:
	default:
		return -EINVAL;
	}

	if (netif_msg_probe(pdata))
		dev_dbg(pdata->dev, "phy supported=%#x\n",
			pdata->phy.supported);

	return 0;
}

void xgbe_init_function_ptrs_phy_v2(struct xgbe_phy_if *phy_if)
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

	phy_impl->an_outcome		= xgbe_phy_an_outcome;
}
