/*******************************************************************************
  Copyright (C) 2013  Vayavya Labs Pvt Ltd

  This implements all the API for managing HW timestamp & PTP.

  This program is free software; you can redistribute it and/or modify it
  under the terms and conditions of the GNU General Public License,
  version 2, as published by the Free Software Foundation.

  This program is distributed in the hope it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
  more details.

  You should have received a copy of the GNU General Public License along with
  this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.

  The full GNU General Public License is included in this distribution in
  the file called "COPYING".

  Author: Rayagond Kokatanur <rayagond@vayavyalabs.com>
  Author: Giuseppe Cavallaro <peppe.cavallaro@st.com>
*******************************************************************************/

#include <linux/io.h>
#include <linux/delay.h>
#include "common.h"
#include "stmmac_ptp.h"

static void stmmac_config_hw_tstamping(void __iomem *ioaddr, u32 data)
{
	writel(data, ioaddr + PTP_TCR);
}

static void stmmac_config_sub_second_increment(void __iomem *ioaddr)
{
	u32 value = readl(ioaddr + PTP_TCR);
	unsigned long data;

	/* Convert the ptp_clock to nano second
	 * formula = (1/ptp_clock) * 1000000000
	 * where, ptp_clock = 50MHz.
	 */
	data = (1000000000ULL / 50000000);

	/* 0.465ns accuracy */
	if (value & PTP_TCR_TSCTRLSSR)
		data = (data * 100) / 465;

	writel(data, ioaddr + PTP_SSIR);
}

static int stmmac_init_systime(void __iomem *ioaddr, u32 sec, u32 nsec)
{
	int limit;
	u32 value;

	writel(sec, ioaddr + PTP_STSUR);
	writel(nsec, ioaddr + PTP_STNSUR);
	/* issue command to initialize the system time value */
	value = readl(ioaddr + PTP_TCR);
	value |= PTP_TCR_TSINIT;
	writel(value, ioaddr + PTP_TCR);

	/* wait for present system time initialize to complete */
	limit = 10;
	while (limit--) {
		if (!(readl(ioaddr + PTP_TCR) & PTP_TCR_TSINIT))
			break;
		mdelay(10);
	}
	if (limit < 0)
		return -EBUSY;

	return 0;
}

static int stmmac_config_addend(void __iomem *ioaddr, u32 addend)
{
	u32 value;
	int limit;

	writel(addend, ioaddr + PTP_TAR);
	/* issue command to update the addend value */
	value = readl(ioaddr + PTP_TCR);
	value |= PTP_TCR_TSADDREG;
	writel(value, ioaddr + PTP_TCR);

	/* wait for present addend update to complete */
	limit = 10;
	while (limit--) {
		if (!(readl(ioaddr + PTP_TCR) & PTP_TCR_TSADDREG))
			break;
		mdelay(10);
	}
	if (limit < 0)
		return -EBUSY;

	return 0;
}

static int stmmac_adjust_systime(void __iomem *ioaddr, u32 sec, u32 nsec,
				 int add_sub)
{
	u32 value;
	int limit;

	writel(sec, ioaddr + PTP_STSUR);
	writel(((add_sub << PTP_STNSUR_ADDSUB_SHIFT) | nsec),
		ioaddr + PTP_STNSUR);
	/* issue command to initialize the system time value */
	value = readl(ioaddr + PTP_TCR);
	value |= PTP_TCR_TSUPDT;
	writel(value, ioaddr + PTP_TCR);

	/* wait for present system time adjust/update to complete */
	limit = 10;
	while (limit--) {
		if (!(readl(ioaddr + PTP_TCR) & PTP_TCR_TSUPDT))
			break;
		mdelay(10);
	}
	if (limit < 0)
		return -EBUSY;

	return 0;
}

static u64 stmmac_get_systime(void __iomem *ioaddr)
{
	u64 ns;

	ns = readl(ioaddr + PTP_STNSR);
	/* convert sec time value to nanosecond */
	ns += readl(ioaddr + PTP_STSR) * 1000000000ULL;

	return ns;
}

const struct stmmac_hwtimestamp stmmac_ptp = {
	.config_hw_tstamping = stmmac_config_hw_tstamping,
	.init_systime = stmmac_init_systime,
	.config_sub_second_increment = stmmac_config_sub_second_increment,
	.config_addend = stmmac_config_addend,
	.adjust_systime = stmmac_adjust_systime,
	.get_systime = stmmac_get_systime,
};
