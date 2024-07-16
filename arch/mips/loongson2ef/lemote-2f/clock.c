/*
 * Copyright (C) 2006 - 2008 Lemote Inc. & Institute of Computing Technology
 * Author: Yanhua, yanh@lemote.com
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/cpufreq.h>
#include <linux/errno.h>
#include <linux/export.h>

#include <asm/mach-loongson2ef/loongson.h>

enum {
	DC_ZERO, DC_25PT = 2, DC_37PT, DC_50PT, DC_62PT, DC_75PT,
	DC_87PT, DC_DISABLE, DC_RESV
};

struct cpufreq_frequency_table loongson2_clockmod_table[] = {
	{0, DC_RESV, CPUFREQ_ENTRY_INVALID},
	{0, DC_ZERO, CPUFREQ_ENTRY_INVALID},
	{0, DC_25PT, 0},
	{0, DC_37PT, 0},
	{0, DC_50PT, 0},
	{0, DC_62PT, 0},
	{0, DC_75PT, 0},
	{0, DC_87PT, 0},
	{0, DC_DISABLE, 0},
	{0, DC_RESV, CPUFREQ_TABLE_END},
};
EXPORT_SYMBOL_GPL(loongson2_clockmod_table);

int loongson2_cpu_set_rate(unsigned long rate_khz)
{
	struct cpufreq_frequency_table *pos;
	int regval;

	cpufreq_for_each_valid_entry(pos, loongson2_clockmod_table)
		if (rate_khz == pos->frequency)
			break;
	if (rate_khz != pos->frequency)
		return -ENOTSUPP;

	regval = readl(LOONGSON_CHIPCFG);
	regval = (regval & ~0x7) | (pos->driver_data - 1);
	writel(regval, LOONGSON_CHIPCFG);

	return 0;
}
EXPORT_SYMBOL_GPL(loongson2_cpu_set_rate);
