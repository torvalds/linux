/*
 * Copyright 2010 Freescale Semiconductor, Inc. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/delay.h>
#include <linux/err.h>
#include <linux/mutex.h>

#include <asm/processor.h>	/* for cpu_relax() */

#include <mach/mxs.h>
#include <mach/common.h>

#define OCOTP_WORD_OFFSET		0x20
#define OCOTP_WORD_COUNT		0x20

#define BM_OCOTP_CTRL_BUSY		(1 << 8)
#define BM_OCOTP_CTRL_ERROR		(1 << 9)
#define BM_OCOTP_CTRL_RD_BANK_OPEN	(1 << 12)

static DEFINE_MUTEX(ocotp_mutex);
static u32 ocotp_words[OCOTP_WORD_COUNT];

const u32 *mxs_get_ocotp(void)
{
	void __iomem *ocotp_base = MXS_IO_ADDRESS(MXS_OCOTP_BASE_ADDR);
	int timeout = 0x400;
	size_t i;
	static int once = 0;

	if (once)
		return ocotp_words;

	mutex_lock(&ocotp_mutex);

	/*
	 * clk_enable(hbus_clk) for ocotp can be skipped
	 * as it must be on when system is running.
	 */

	/* try to clear ERROR bit */
	__mxs_clrl(BM_OCOTP_CTRL_ERROR, ocotp_base);

	/* check both BUSY and ERROR cleared */
	while ((__raw_readl(ocotp_base) &
		(BM_OCOTP_CTRL_BUSY | BM_OCOTP_CTRL_ERROR)) && --timeout)
		cpu_relax();

	if (unlikely(!timeout))
		goto error_unlock;

	/* open OCOTP banks for read */
	__mxs_setl(BM_OCOTP_CTRL_RD_BANK_OPEN, ocotp_base);

	/* approximately wait 32 hclk cycles */
	udelay(1);

	/* poll BUSY bit becoming cleared */
	timeout = 0x400;
	while ((__raw_readl(ocotp_base) & BM_OCOTP_CTRL_BUSY) && --timeout)
		cpu_relax();

	if (unlikely(!timeout))
		goto error_unlock;

	for (i = 0; i < OCOTP_WORD_COUNT; i++)
		ocotp_words[i] = __raw_readl(ocotp_base + OCOTP_WORD_OFFSET +
						i * 0x10);

	/* close banks for power saving */
	__mxs_clrl(BM_OCOTP_CTRL_RD_BANK_OPEN, ocotp_base);

	once = 1;

	mutex_unlock(&ocotp_mutex);

	return ocotp_words;

error_unlock:
	mutex_unlock(&ocotp_mutex);
	pr_err("%s: timeout in reading OCOTP\n", __func__);
	return NULL;
}
