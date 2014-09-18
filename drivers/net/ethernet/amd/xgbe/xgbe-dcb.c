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

#include <linux/netdevice.h>
#include <net/dcbnl.h>

#include "xgbe.h"
#include "xgbe-common.h"


static int xgbe_dcb_ieee_getets(struct net_device *netdev,
				struct ieee_ets *ets)
{
	struct xgbe_prv_data *pdata = netdev_priv(netdev);

	/* Set number of supported traffic classes */
	ets->ets_cap = pdata->hw_feat.tc_cnt;

	if (pdata->ets) {
		ets->cbs = pdata->ets->cbs;
		memcpy(ets->tc_tx_bw, pdata->ets->tc_tx_bw,
		       sizeof(ets->tc_tx_bw));
		memcpy(ets->tc_tsa, pdata->ets->tc_tsa,
		       sizeof(ets->tc_tsa));
		memcpy(ets->prio_tc, pdata->ets->prio_tc,
		       sizeof(ets->prio_tc));
	}

	return 0;
}

static int xgbe_dcb_ieee_setets(struct net_device *netdev,
				struct ieee_ets *ets)
{
	struct xgbe_prv_data *pdata = netdev_priv(netdev);
	unsigned int i, tc_ets, tc_ets_weight;

	tc_ets = 0;
	tc_ets_weight = 0;
	for (i = 0; i < IEEE_8021QAZ_MAX_TCS; i++) {
		DBGPR("  TC%u: tx_bw=%hhu, rx_bw=%hhu, tsa=%hhu\n", i,
		      ets->tc_tx_bw[i], ets->tc_rx_bw[i], ets->tc_tsa[i]);
		DBGPR("  PRIO%u: TC=%hhu\n", i, ets->prio_tc[i]);

		if ((ets->tc_tx_bw[i] || ets->tc_tsa[i]) &&
		    (i >= pdata->hw_feat.tc_cnt))
				return -EINVAL;

		if (ets->prio_tc[i] >= pdata->hw_feat.tc_cnt)
			return -EINVAL;

		switch (ets->tc_tsa[i]) {
		case IEEE_8021QAZ_TSA_STRICT:
			break;
		case IEEE_8021QAZ_TSA_ETS:
			tc_ets = 1;
			tc_ets_weight += ets->tc_tx_bw[i];
			break;

		default:
			return -EINVAL;
		}
	}

	/* Weights must add up to 100% */
	if (tc_ets && (tc_ets_weight != 100))
		return -EINVAL;

	if (!pdata->ets) {
		pdata->ets = devm_kzalloc(pdata->dev, sizeof(*pdata->ets),
					  GFP_KERNEL);
		if (!pdata->ets)
			return -ENOMEM;
	}

	memcpy(pdata->ets, ets, sizeof(*pdata->ets));

	pdata->hw_if.config_dcb_tc(pdata);

	return 0;
}

static int xgbe_dcb_ieee_getpfc(struct net_device *netdev,
				struct ieee_pfc *pfc)
{
	struct xgbe_prv_data *pdata = netdev_priv(netdev);

	/* Set number of supported PFC traffic classes */
	pfc->pfc_cap = pdata->hw_feat.tc_cnt;

	if (pdata->pfc) {
		pfc->pfc_en = pdata->pfc->pfc_en;
		pfc->mbc = pdata->pfc->mbc;
		pfc->delay = pdata->pfc->delay;
	}

	return 0;
}

static int xgbe_dcb_ieee_setpfc(struct net_device *netdev,
				struct ieee_pfc *pfc)
{
	struct xgbe_prv_data *pdata = netdev_priv(netdev);

	DBGPR("  cap=%hhu, en=%hhx, mbc=%hhu, delay=%hhu\n",
	      pfc->pfc_cap, pfc->pfc_en, pfc->mbc, pfc->delay);

	if (!pdata->pfc) {
		pdata->pfc = devm_kzalloc(pdata->dev, sizeof(*pdata->pfc),
					  GFP_KERNEL);
		if (!pdata->pfc)
			return -ENOMEM;
	}

	memcpy(pdata->pfc, pfc, sizeof(*pdata->pfc));

	pdata->hw_if.config_dcb_pfc(pdata);

	return 0;
}

static u8 xgbe_dcb_getdcbx(struct net_device *netdev)
{
	return DCB_CAP_DCBX_HOST | DCB_CAP_DCBX_VER_IEEE;
}

static u8 xgbe_dcb_setdcbx(struct net_device *netdev, u8 dcbx)
{
	u8 support = xgbe_dcb_getdcbx(netdev);

	DBGPR("  DCBX=%#hhx\n", dcbx);

	if (dcbx & ~support)
		return 1;

	if ((dcbx & support) != support)
		return 1;

	return 0;
}

static const struct dcbnl_rtnl_ops xgbe_dcbnl_ops = {
	/* IEEE 802.1Qaz std */
	.ieee_getets = xgbe_dcb_ieee_getets,
	.ieee_setets = xgbe_dcb_ieee_setets,
	.ieee_getpfc = xgbe_dcb_ieee_getpfc,
	.ieee_setpfc = xgbe_dcb_ieee_setpfc,

	/* DCBX configuration */
	.getdcbx     = xgbe_dcb_getdcbx,
	.setdcbx     = xgbe_dcb_setdcbx,
};

const struct dcbnl_rtnl_ops *xgbe_get_dcbnl_ops(void)
{
	return &xgbe_dcbnl_ops;
}
