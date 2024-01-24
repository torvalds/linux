/*
 * TC956X ethernet driver.
 *
 * tc956xmac_hwtstamp.c - This implements all the API for managing
 *                        HW timestamp & PTP.
 *
 * Copyright (C) 2013  Vayavya Labs Pvt Ltd
 * Copyright (C) 2021 Toshiba Electronic Devices & Storage Corporation
 *
 * This file has been derived from the STMicro and Vayavya Linux driver,
 * and developed or modified for TC956X.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/*! History:
 *  20 Jan 2021 : Initial Version
 *  VERSION     : 00-01
 *
 *  15 Mar 2021 : Base lined
 *  VERSION     : 01-00
 */

#include <linux/io.h>
#include <linux/delay.h>
#include "common.h"
#include "tc956xmac_ptp.h"

#ifdef TC956X_SRIOV_PF
static u32 tc956xmac_get_ptp_subperiod(struct tc956xmac_priv *priv, void __iomem *ioaddr, u32 ptp_clock);
static u32 tc956xmac_get_ptp_period(struct tc956xmac_priv *priv, void __iomem *ioaddr, u32 ptp_clock);

static void config_hw_tstamping(struct tc956xmac_priv *priv, void __iomem *ioaddr,
					u32 data)
{
	writel(data, ioaddr + PTP_TCR);
}

static void config_sub_second_increment(struct tc956xmac_priv *priv,
					void __iomem *ioaddr, u32 ptp_clock,
					int ip, u32 *ssinc)
{
	u32 value = readl(ioaddr + PTP_TCR);
	u32 subns, ns;
	u32 reg_value;
	u64 tmp;
#ifndef CONFIG_ARCH_DMA_ADDR_T_64BIT
	u64 temp_quot;
	u32 temp_rem;
#endif

	/*KPRINT_INFO("--> %s\n", __func__);*/

	ns = tc956xmac_get_ptp_period(priv, ioaddr, ptp_clock);
	subns = tc956xmac_get_ptp_subperiod(priv, ioaddr, ptp_clock);

	/*  TSCTRLSSR always Enabled */
	/* 0.465ns accuracy */
#ifndef CONFIG_ARCH_DMA_ADDR_T_64BIT
	if (!(value & PTP_TCR_TSCTRLSSR)) {
		tmp = mul_u32_u32(ns, 1000);
		temp_quot = div_u64_rem(tmp, 465, &temp_rem);
		ns = DIV_ROUND_CLOSEST_ULL(tmp - temp_rem, 465);

		subns = DIV_ROUND_CLOSEST_ULL((tmp * 256) - mul_u32_u32(465 * ns, 256), 465);
	} else {
		subns = DIV_ROUND_CLOSEST_ULL(mul_u32_u32(subns, 256), 1000);
	}
#else
	if (!(value & PTP_TCR_TSCTRLSSR)) {
		tmp = ns * 1000;
		ns = DIV_ROUND_CLOSEST(tmp - (tmp % 465), 465);
		subns = DIV_ROUND_CLOSEST((tmp * 256) - (465 * ns * 256), 465);
	} else {
		subns = DIV_ROUND_CLOSEST(subns * 256, 1000);
	}
#endif

	ns &= PTP_SSIR_SSINC_MASK;
	subns &= PTP_SSIR_SNSINC_MASK;

	reg_value = ns;
	if (ip) {
		reg_value = reg_value << GMAC4_PTP_SSIR_SSINC_SHIFT;
		reg_value |= subns << GMAC4_PTP_SSIR_SNSINC_SHIFT;
	}

	writel(reg_value, ioaddr + PTP_SSIR);

	if (ssinc)
		*ssinc = ns;

}

static int init_systime(struct tc956xmac_priv *priv, void __iomem *ioaddr,
			u32 sec, u32 nsec)
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

static int config_addend(struct tc956xmac_priv *priv, void __iomem *ioaddr, u32 addend)
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

static int adjust_systime(struct tc956xmac_priv *priv, void __iomem *ioaddr,
				u32 sec, u32 nsec, int add_sub, int gmac4)
{
	u32 value;
	int limit;

	if (add_sub) {
		/* If the new sec value needs to be subtracted with
		 * the system time, then MAC_STSUR reg should be
		 * programmed with (2^32 – <new_sec_value>)
		 */
		if (gmac4)
			sec = -sec;

		value = readl(ioaddr + PTP_TCR);
		if (value & PTP_TCR_TSCTRLSSR)
			nsec = (PTP_DIGITAL_ROLLOVER_MODE - nsec);
		else
			nsec = (PTP_BINARY_ROLLOVER_MODE - nsec);
	}

	writel(sec, ioaddr + PTP_STSUR);
	value = (add_sub << PTP_STNSUR_ADDSUB_SHIFT) | nsec;
	writel(value, ioaddr + PTP_STNSUR);

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
#endif
static void get_systime(struct tc956xmac_priv *priv, void __iomem *ioaddr, u64 *systime)
{

	u64 ns;
	u32 sec, sec_rollover_read;
	u32 nsec, nsec_rollover_read;

	sec = readl(ioaddr + PTP_STSR);
	nsec = readl(ioaddr + PTP_STNSR);

	sec_rollover_read = readl(ioaddr + PTP_STSR);
	nsec_rollover_read = readl(ioaddr + PTP_STNSR);

	if (sec != sec_rollover_read) {
		sec = sec_rollover_read;
		nsec = nsec_rollover_read;
	}

	/* Get the TSSS value */
	ns = nsec;
	/* Get the TSS and convert sec time value to nanosecond */
	ns += sec * 1000000000ULL;

	if (systime)
		*systime = ns;

}
#ifdef TC956X_SRIOV_PF
static u32 tc956xmac_get_ptp_period(struct tc956xmac_priv *priv, void __iomem *ioaddr, u32 ptp_clock)
{
	u32 value = readl(ioaddr + PTP_TCR);
	u64 data;
#ifndef CONFIG_ARCH_DMA_ADDR_T_64BIT
	u32 remainder;
#endif
	/*KPRINT_INFO( "--> %s\n", __func__);*/

	/* For GMAC3.x, 4.x versions, convert the ptp_clock to nano second
	 *	formula = (1/ptp_clock) * 1000000000
	 * where ptp_clock is 50MHz if fine method is used to update system
	 */
	/*  TSCFUPDT always Fine method */
	/*  ptp_clock should be > 3.921568 MHz */
#ifndef CONFIG_ARCH_DMA_ADDR_T_64BIT
	if (value & PTP_TCR_TSCFUPDT)
		data = div_u64_rem(1000000000ULL, ptp_clock, &remainder);
	else
		data = div_u64_rem(1000000000ULL, 250000000, &remainder);

#else
	if (value & PTP_TCR_TSCFUPDT)
		data = (1000000000ULL / ptp_clock);
	else
		data = (1000000000ULL / 250000000);
#endif

	/*KPRINT_INFO("<-- %s\n", __func__);*/
	return (u32)data;
}

static u32 tc956xmac_get_ptp_subperiod(struct tc956xmac_priv *priv, void __iomem *ioaddr, u32 ptp_clock)
{
	u32 value = readl(ioaddr + PTP_TCR);
	u64 data;
#ifndef CONFIG_ARCH_DMA_ADDR_T_64BIT
	u32 remainder;
#endif
	/*KPRINT_INFO( "--> %s\n", __func__);*/

	/* For GMAC3.x, 4.x versions, convert the ptp_clock to nano second
	 *	formula = (1/ptp_clock) * 1000000000
	 * where ptp_clock is 50MHz if fine method is used to update system
	 */
	 /* TSCFUPDT always Fine method */
#ifndef CONFIG_ARCH_DMA_ADDR_T_64BIT
	if (value & PTP_TCR_TSCFUPDT)
		data = div_u64_rem(1000000000ULL * 1000ULL, ptp_clock, &remainder);
	else
		data = div_u64_rem(1000000000ULL * 1000ULL, 250000000, &remainder);

#else
	if (value & PTP_TCR_TSCFUPDT)
		data = (1000000000ULL * 1000ULL / ptp_clock);
	else
		data = (1000000000ULL * 1000ULL / 250000000);
#endif

	/*KPRINT_INFO( "<-- %s\n", __func__);*/
	return  data - tc956xmac_get_ptp_period(priv, ioaddr, ptp_clock) * 1000;
}

const struct tc956xmac_hwtimestamp tc956xmac_ptp = {
	.config_hw_tstamping = config_hw_tstamping,
	.init_systime = init_systime,
	.config_sub_second_increment = config_sub_second_increment,
	.config_addend = config_addend,
	.adjust_systime = adjust_systime,
	.get_systime = get_systime,
};
#else
const struct tc956xmac_hwtimestamp tc956xmac_ptp = {
	.config_hw_tstamping = NULL,
	.init_systime = NULL,
	.config_sub_second_increment = NULL,
	.config_addend = NULL,
	.adjust_systime = NULL,
	.get_systime = get_systime,
};
#endif

