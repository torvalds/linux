/*
 * Copyright (c) 2006-2008 Simtec Electronics
 *	http://armlinux.simtec.co.uk/
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * S3C2412/S3C2443 (PL093 based) IO timing support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/init.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/cpufreq.h>
#include <linux/seq_file.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/slab.h>

#include <linux/amba/pl093.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <mach/regs-s3c2412-mem.h>

#include <plat/cpu.h>
#include <plat/cpu-freq-core.h>
#include <plat/clock.h>

#define print_ns(x) ((x) / 10), ((x) % 10)

/**
 * s3c2412_print_timing - print timing infromation via printk.
 * @pfx: The prefix to print each line with.
 * @iot: The IO timing information
 */
static void s3c2412_print_timing(const char *pfx, struct s3c_iotimings *iot)
{
	struct s3c2412_iobank_timing *bt;
	unsigned int bank;

	for (bank = 0; bank < MAX_BANKS; bank++) {
		bt = iot->bank[bank].io_2412;
		if (!bt)
			continue;

		printk(KERN_DEBUG "%s: %d: idcy=%d.%d wstrd=%d.%d wstwr=%d,%d"
		       "wstoen=%d.%d wstwen=%d.%d wstbrd=%d.%d\n", pfx, bank,
		       print_ns(bt->idcy),
		       print_ns(bt->wstrd),
		       print_ns(bt->wstwr),
		       print_ns(bt->wstoen),
		       print_ns(bt->wstwen),
		       print_ns(bt->wstbrd));
	}
}

/**
 * to_div - turn a cycle length into a divisor setting.
 * @cyc_tns: The cycle time in 10ths of nanoseconds.
 * @clk_tns: The clock period in 10ths of nanoseconds.
 */
static inline unsigned int to_div(unsigned int cyc_tns, unsigned int clk_tns)
{
	return cyc_tns ? DIV_ROUND_UP(cyc_tns, clk_tns) : 0;
}

/**
 * calc_timing - calculate timing divisor value and check in range.
 * @hwtm: The hardware timing in 10ths of nanoseconds.
 * @clk_tns: The clock period in 10ths of nanoseconds.
 * @err: Pointer to err variable to update in event of failure.
 */
static unsigned int calc_timing(unsigned int hwtm, unsigned int clk_tns,
				unsigned int *err)
{
	unsigned int ret = to_div(hwtm, clk_tns);

	if (ret > 0xf)
		*err = -EINVAL;

	return ret;
}

/**
 * s3c2412_calc_bank - calculate the bank divisor settings.
 * @cfg: The current frequency configuration.
 * @bt: The bank timing.
 */
static int s3c2412_calc_bank(struct s3c_cpufreq_config *cfg,
			     struct s3c2412_iobank_timing *bt)
{
	unsigned int hclk = cfg->freq.hclk_tns;
	int err = 0;

	bt->smbidcyr = calc_timing(bt->idcy, hclk, &err);
	bt->smbwstrd = calc_timing(bt->wstrd, hclk, &err);
	bt->smbwstwr = calc_timing(bt->wstwr, hclk, &err);
	bt->smbwstoen = calc_timing(bt->wstoen, hclk, &err);
	bt->smbwstwen = calc_timing(bt->wstwen, hclk, &err);
	bt->smbwstbrd = calc_timing(bt->wstbrd, hclk, &err);

	return err;
}

/**
 * s3c2412_iotiming_debugfs - debugfs show io bank timing information
 * @seq: The seq_file to write output to using seq_printf().
 * @cfg: The current configuration.
 * @iob: The IO bank information to decode.
*/
void s3c2412_iotiming_debugfs(struct seq_file *seq,
			      struct s3c_cpufreq_config *cfg,
			      union s3c_iobank *iob)
{
	struct s3c2412_iobank_timing *bt = iob->io_2412;

	seq_printf(seq,
		   "\tRead: idcy=%d.%d wstrd=%d.%d wstwr=%d,%d"
		   "wstoen=%d.%d wstwen=%d.%d wstbrd=%d.%d\n",
		   print_ns(bt->idcy),
		   print_ns(bt->wstrd),
		   print_ns(bt->wstwr),
		   print_ns(bt->wstoen),
		   print_ns(bt->wstwen),
		   print_ns(bt->wstbrd));
}

/**
 * s3c2412_iotiming_calc - calculate all the bank divisor settings.
 * @cfg: The current frequency configuration.
 * @iot: The bank timing information.
 *
 * Calculate the timing information for all the banks that are
 * configured as IO, using s3c2412_calc_bank().
 */
int s3c2412_iotiming_calc(struct s3c_cpufreq_config *cfg,
			  struct s3c_iotimings *iot)
{
	struct s3c2412_iobank_timing *bt;
	int bank;
	int ret;

	for (bank = 0; bank < MAX_BANKS; bank++) {
		bt = iot->bank[bank].io_2412;
		if (!bt)
			continue;

		ret = s3c2412_calc_bank(cfg, bt);
		if (ret) {
			printk(KERN_ERR "%s: cannot calculate bank %d io\n",
			       __func__, bank);
			goto err;
		}
	}

	return 0;
 err:
	return ret;
}

/**
 * s3c2412_iotiming_set - set the timing information
 * @cfg: The current frequency configuration.
 * @iot: The bank timing information.
 *
 * Set the IO bank information from the details calculated earlier from
 * calling s3c2412_iotiming_calc().
 */
void s3c2412_iotiming_set(struct s3c_cpufreq_config *cfg,
			  struct s3c_iotimings *iot)
{
	struct s3c2412_iobank_timing *bt;
	void __iomem *regs;
	int bank;

	/* set the io timings from the specifier */

	for (bank = 0; bank < MAX_BANKS; bank++) {
		bt = iot->bank[bank].io_2412;
		if (!bt)
			continue;

		regs = S3C2412_SSMC_BANK(bank);

		__raw_writel(bt->smbidcyr, regs + SMBIDCYR);
		__raw_writel(bt->smbwstrd, regs + SMBWSTRDR);
		__raw_writel(bt->smbwstwr, regs + SMBWSTWRR);
		__raw_writel(bt->smbwstoen, regs + SMBWSTOENR);
		__raw_writel(bt->smbwstwen, regs + SMBWSTWENR);
		__raw_writel(bt->smbwstbrd, regs + SMBWSTBRDR);
	}
}

static inline unsigned int s3c2412_decode_timing(unsigned int clock, u32 reg)
{
	return (reg & 0xf) * clock;
}

static void s3c2412_iotiming_getbank(struct s3c_cpufreq_config *cfg,
				     struct s3c2412_iobank_timing *bt,
				     unsigned int bank)
{
	unsigned long clk = cfg->freq.hclk_tns;  /* ssmc clock??? */
	void __iomem *regs = S3C2412_SSMC_BANK(bank);

	bt->idcy = s3c2412_decode_timing(clk, __raw_readl(regs + SMBIDCYR));
	bt->wstrd = s3c2412_decode_timing(clk, __raw_readl(regs + SMBWSTRDR));
	bt->wstoen = s3c2412_decode_timing(clk, __raw_readl(regs + SMBWSTOENR));
	bt->wstwen = s3c2412_decode_timing(clk, __raw_readl(regs + SMBWSTWENR));
	bt->wstbrd = s3c2412_decode_timing(clk, __raw_readl(regs + SMBWSTBRDR));
}

/**
 * bank_is_io - return true if bank is (possibly) IO.
 * @bank: The bank number.
 * @bankcfg: The value of S3C2412_EBI_BANKCFG.
 */
static inline bool bank_is_io(unsigned int bank, u32 bankcfg)
{
	if (bank < 2)
		return true;

	return !(bankcfg & (1 << bank));
}

int s3c2412_iotiming_get(struct s3c_cpufreq_config *cfg,
			 struct s3c_iotimings *timings)
{
	struct s3c2412_iobank_timing *bt;
	u32 bankcfg = __raw_readl(S3C2412_EBI_BANKCFG);
	unsigned int bank;

	/* look through all banks to see what is currently set. */

	for (bank = 0; bank < MAX_BANKS; bank++) {
		if (!bank_is_io(bank, bankcfg))
			continue;

		bt = kzalloc(sizeof(struct s3c2412_iobank_timing), GFP_KERNEL);
		if (!bt) {
			printk(KERN_ERR "%s: no memory for bank\n", __func__);
			return -ENOMEM;
		}

		timings->bank[bank].io_2412 = bt;
		s3c2412_iotiming_getbank(cfg, bt, bank);
	}

	s3c2412_print_timing("get", timings);
	return 0;
}

/* this is in here as it is so small, it doesn't currently warrant a file
 * to itself. We expect that any s3c24xx needing this is going to also
 * need the iotiming support.
 */
void s3c2412_cpufreq_setrefresh(struct s3c_cpufreq_config *cfg)
{
	struct s3c_cpufreq_board *board = cfg->board;
	u32 refresh;

	WARN_ON(board == NULL);

	/* Reduce both the refresh time (in ns) and the frequency (in MHz)
	 * down to ensure that we do not overflow 32 bit numbers.
	 *
	 * This should work for HCLK up to 133MHz and refresh period up
	 * to 30usec.
	 */

	refresh = (cfg->freq.hclk / 100) * (board->refresh / 10);
	refresh = DIV_ROUND_UP(refresh, (1000 * 1000)); /* apply scale  */
	refresh &= ((1 << 16) - 1);

	s3c_freq_dbg("%s: refresh value %u\n", __func__, (unsigned int)refresh);

	__raw_writel(refresh, S3C2412_REFRESH);
}
