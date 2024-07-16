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

#include <linux/clk.h>
#include <linux/clocksource.h>
#include <linux/ptp_clock_kernel.h>
#include <linux/net_tstamp.h>

#include "xgbe.h"
#include "xgbe-common.h"

static u64 xgbe_cc_read(const struct cyclecounter *cc)
{
	struct xgbe_prv_data *pdata = container_of(cc,
						   struct xgbe_prv_data,
						   tstamp_cc);
	u64 nsec;

	nsec = pdata->hw_if.get_tstamp_time(pdata);

	return nsec;
}

static int xgbe_adjfreq(struct ptp_clock_info *info, s32 delta)
{
	struct xgbe_prv_data *pdata = container_of(info,
						   struct xgbe_prv_data,
						   ptp_clock_info);
	unsigned long flags;
	u64 adjust;
	u32 addend, diff;
	unsigned int neg_adjust = 0;

	if (delta < 0) {
		neg_adjust = 1;
		delta = -delta;
	}

	adjust = pdata->tstamp_addend;
	adjust *= delta;
	diff = div_u64(adjust, 1000000000UL);

	addend = (neg_adjust) ? pdata->tstamp_addend - diff :
				pdata->tstamp_addend + diff;

	spin_lock_irqsave(&pdata->tstamp_lock, flags);

	pdata->hw_if.update_tstamp_addend(pdata, addend);

	spin_unlock_irqrestore(&pdata->tstamp_lock, flags);

	return 0;
}

static int xgbe_adjtime(struct ptp_clock_info *info, s64 delta)
{
	struct xgbe_prv_data *pdata = container_of(info,
						   struct xgbe_prv_data,
						   ptp_clock_info);
	unsigned long flags;

	spin_lock_irqsave(&pdata->tstamp_lock, flags);
	timecounter_adjtime(&pdata->tstamp_tc, delta);
	spin_unlock_irqrestore(&pdata->tstamp_lock, flags);

	return 0;
}

static int xgbe_gettime(struct ptp_clock_info *info, struct timespec64 *ts)
{
	struct xgbe_prv_data *pdata = container_of(info,
						   struct xgbe_prv_data,
						   ptp_clock_info);
	unsigned long flags;
	u64 nsec;

	spin_lock_irqsave(&pdata->tstamp_lock, flags);

	nsec = timecounter_read(&pdata->tstamp_tc);

	spin_unlock_irqrestore(&pdata->tstamp_lock, flags);

	*ts = ns_to_timespec64(nsec);

	return 0;
}

static int xgbe_settime(struct ptp_clock_info *info,
			const struct timespec64 *ts)
{
	struct xgbe_prv_data *pdata = container_of(info,
						   struct xgbe_prv_data,
						   ptp_clock_info);
	unsigned long flags;
	u64 nsec;

	nsec = timespec64_to_ns(ts);

	spin_lock_irqsave(&pdata->tstamp_lock, flags);

	timecounter_init(&pdata->tstamp_tc, &pdata->tstamp_cc, nsec);

	spin_unlock_irqrestore(&pdata->tstamp_lock, flags);

	return 0;
}

static int xgbe_enable(struct ptp_clock_info *info,
		       struct ptp_clock_request *request, int on)
{
	return -EOPNOTSUPP;
}

void xgbe_ptp_register(struct xgbe_prv_data *pdata)
{
	struct ptp_clock_info *info = &pdata->ptp_clock_info;
	struct ptp_clock *clock;
	struct cyclecounter *cc = &pdata->tstamp_cc;
	u64 dividend;

	snprintf(info->name, sizeof(info->name), "%s",
		 netdev_name(pdata->netdev));
	info->owner = THIS_MODULE;
	info->max_adj = pdata->ptpclk_rate;
	info->adjfreq = xgbe_adjfreq;
	info->adjtime = xgbe_adjtime;
	info->gettime64 = xgbe_gettime;
	info->settime64 = xgbe_settime;
	info->enable = xgbe_enable;

	clock = ptp_clock_register(info, pdata->dev);
	if (IS_ERR(clock)) {
		dev_err(pdata->dev, "ptp_clock_register failed\n");
		return;
	}

	pdata->ptp_clock = clock;

	/* Calculate the addend:
	 *   addend = 2^32 / (PTP ref clock / 50Mhz)
	 *          = (2^32 * 50Mhz) / PTP ref clock
	 */
	dividend = 50000000;
	dividend <<= 32;
	pdata->tstamp_addend = div_u64(dividend, pdata->ptpclk_rate);

	/* Setup the timecounter */
	cc->read = xgbe_cc_read;
	cc->mask = CLOCKSOURCE_MASK(64);
	cc->mult = 1;
	cc->shift = 0;

	timecounter_init(&pdata->tstamp_tc, &pdata->tstamp_cc,
			 ktime_to_ns(ktime_get_real()));

	/* Disable all timestamping to start */
	XGMAC_IOWRITE(pdata, MAC_TSCR, 0);
	pdata->tstamp_config.tx_type = HWTSTAMP_TX_OFF;
	pdata->tstamp_config.rx_filter = HWTSTAMP_FILTER_NONE;
}

void xgbe_ptp_unregister(struct xgbe_prv_data *pdata)
{
	if (pdata->ptp_clock)
		ptp_clock_unregister(pdata->ptp_clock);
}
