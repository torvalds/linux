/* linux/arch/arm/plat-s3c24xx/s3c2410-iotiming.c
 *
 * Copyright (c) 2006-2009 Simtec Electronics
 *	http://armlinux.simtec.co.uk/
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * S3C24XX CPU Frequency scaling - IO timing for S3C2410/S3C2440/S3C2442
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/cpufreq.h>
#include <linux/seq_file.h>
#include <linux/io.h>

#include <mach/map.h>
#include <mach/regs-mem.h>
#include <mach/regs-clock.h>

#include <plat/cpu-freq-core.h>

#define print_ns(x) ((x) / 10), ((x) % 10)

/**
 * s3c2410_print_timing - print bank timing data for debug purposes
 * @pfx: The prefix to put on the output
 * @timings: The timing inforamtion to print.
*/
static void s3c2410_print_timing(const char *pfx,
				 struct s3c_iotimings *timings)
{
	struct s3c2410_iobank_timing *bt;
	int bank;

	for (bank = 0; bank < MAX_BANKS; bank++) {
		bt = timings->bank[bank].io_2410;
		if (!bt)
			continue;

		printk(KERN_DEBUG "%s %d: Tacs=%d.%d, Tcos=%d.%d, Tacc=%d.%d, "
		       "Tcoh=%d.%d, Tcah=%d.%d\n", pfx, bank,
		       print_ns(bt->tacs),
		       print_ns(bt->tcos),
		       print_ns(bt->tacc),
		       print_ns(bt->tcoh),
		       print_ns(bt->tcah));
	}
}

/**
 * bank_reg - convert bank number to pointer to the control register.
 * @bank: The IO bank number.
 */
static inline void __iomem *bank_reg(unsigned int bank)
{
	return S3C2410_BANKCON0 + (bank << 2);
}

/**
 * bank_is_io - test whether bank is used for IO
 * @bankcon: The bank control register.
 *
 * This is a simplistic test to see if any BANKCON[x] is not an IO
 * bank. It currently does not take into account whether BWSCON has
 * an illegal width-setting in it, or if the pin connected to nCS[x]
 * is actually being handled as a chip-select.
 */
static inline int bank_is_io(unsigned long bankcon)
{
	return !(bankcon & S3C2410_BANKCON_SDRAM);
}

/**
 * to_div - convert cycle time to divisor
 * @cyc: The cycle time, in 10ths of nanoseconds.
 * @hclk_tns: The cycle time for HCLK, in 10ths of nanoseconds.
 *
 * Convert the given cycle time into the divisor to use to obtain it from
 * HCLK.
*/
static inline unsigned int to_div(unsigned int cyc, unsigned int hclk_tns)
{
	if (cyc == 0)
		return 0;

	return DIV_ROUND_UP(cyc, hclk_tns);
}

/**
 * calc_0124 - calculate divisor control for divisors that do /0, /1. /2 and /4
 * @cyc: The cycle time, in 10ths of nanoseconds.
 * @hclk_tns: The cycle time for HCLK, in 10ths of nanoseconds.
 * @v: Pointer to register to alter.
 * @shift: The shift to get to the control bits.
 *
 * Calculate the divisor, and turn it into the correct control bits to
 * set in the result, @v.
 */
static unsigned int calc_0124(unsigned int cyc, unsigned long hclk_tns,
			      unsigned long *v, int shift)
{
	unsigned int div = to_div(cyc, hclk_tns);
	unsigned long val;

	s3c_freq_iodbg("%s: cyc=%d, hclk=%lu, shift=%d => div %d\n",
		       __func__, cyc, hclk_tns, shift, div);

	switch (div) {
	case 0:
		val = 0;
		break;
	case 1:
		val = 1;
		break;
	case 2:
		val = 2;
		break;
	case 3:
	case 4:
		val = 3;
		break;
	default:
		return -1;
	}

	*v |= val << shift;
	return 0;
}

int calc_tacp(unsigned int cyc, unsigned long hclk, unsigned long *v)
{
	/* Currently no support for Tacp calculations. */
	return 0;
}

/**
 * calc_tacc - calculate divisor control for tacc.
 * @cyc: The cycle time, in 10ths of nanoseconds.
 * @nwait_en: IS nWAIT enabled for this bank.
 * @hclk_tns: The cycle time for HCLK, in 10ths of nanoseconds.
 * @v: Pointer to register to alter.
 *
 * Calculate the divisor control for tACC, taking into account whether
 * the bank has nWAIT enabled. The result is used to modify the value
 * pointed to by @v.
*/
static int calc_tacc(unsigned int cyc, int nwait_en,
		     unsigned long hclk_tns, unsigned long *v)
{
	unsigned int div = to_div(cyc, hclk_tns);
	unsigned long val;

	s3c_freq_iodbg("%s: cyc=%u, nwait=%d, hclk=%lu => div=%u\n",
		       __func__, cyc, nwait_en, hclk_tns, div);

	/* if nWait enabled on an bank, Tacc must be at-least 4 cycles. */
	if (nwait_en && div < 4)
		div = 4;

	switch (div) {
	case 0:
		val = 0;
		break;

	case 1:
	case 2:
	case 3:
	case 4:
		val = div - 1;
		break;

	case 5:
	case 6:
		val = 4;
		break;

	case 7:
	case 8:
		val = 5;
		break;

	case 9:
	case 10:
		val = 6;
		break;

	case 11:
	case 12:
	case 13:
	case 14:
		val = 7;
		break;

	default:
		return -1;
	}

	*v |= val << 8;
	return 0;
}

/**
 * s3c2410_calc_bank - calculate bank timing infromation
 * @cfg: The configuration we need to calculate for.
 * @bt: The bank timing information.
 *
 * Given the cycle timine for a bank @bt, calculate the new BANKCON
 * setting for the @cfg timing. This updates the timing information
 * ready for the cpu frequency change.
 */
static int s3c2410_calc_bank(struct s3c_cpufreq_config *cfg,
			     struct s3c2410_iobank_timing *bt)
{
	unsigned long hclk = cfg->freq.hclk_tns;
	unsigned long res;
	int ret;

	res  = bt->bankcon;
	res &= (S3C2410_BANKCON_SDRAM | S3C2410_BANKCON_PMC16);

	/* tacp: 2,3,4,5 */
	/* tcah: 0,1,2,4 */
	/* tcoh: 0,1,2,4 */
	/* tacc: 1,2,3,4,6,7,10,14 (>4 for nwait) */
	/* tcos: 0,1,2,4 */
	/* tacs: 0,1,2,4 */

	ret  = calc_0124(bt->tacs, hclk, &res, S3C2410_BANKCON_Tacs_SHIFT);
	ret |= calc_0124(bt->tcos, hclk, &res, S3C2410_BANKCON_Tcos_SHIFT);
	ret |= calc_0124(bt->tcah, hclk, &res, S3C2410_BANKCON_Tcah_SHIFT);
	ret |= calc_0124(bt->tcoh, hclk, &res, S3C2410_BANKCON_Tcoh_SHIFT);

	if (ret)
		return -EINVAL;

	ret |= calc_tacp(bt->tacp, hclk, &res);
	ret |= calc_tacc(bt->tacc, bt->nwait_en, hclk, &res);

	if (ret)
		return -EINVAL;

	bt->bankcon = res;
	return 0;
}

static unsigned int tacc_tab[] = {
	[0]	= 1,
	[1]	= 2,
	[2]	= 3,
	[3]	= 4,
	[4]	= 6,
	[5]	= 9,
	[6]	= 10,
	[7]	= 14,
};

/**
 * get_tacc - turn tACC value into cycle time
 * @hclk_tns: The cycle time for HCLK, in 10ths of nanoseconds.
 * @val: The bank timing register value, shifed down.
 */
static unsigned int get_tacc(unsigned long hclk_tns,
			     unsigned long val)
{
	val &= 7;
	return hclk_tns * tacc_tab[val];
}

/**
 * get_0124 - turn 0/1/2/4 divider into cycle time
 * @hclk_tns: The cycle time for HCLK, in 10ths of nanoseconds.
 * @val: The bank timing register value, shifed down.
 */
static unsigned int get_0124(unsigned long hclk_tns,
			     unsigned long val)
{
	val &= 3;
	return hclk_tns * ((val == 3) ? 4 : val);
}

/**
 * s3c2410_iotiming_getbank - turn BANKCON into cycle time information
 * @cfg: The frequency configuration
 * @bt: The bank timing to fill in (uses cached BANKCON)
 *
 * Given the BANKCON setting in @bt and the current frequency settings
 * in @cfg, update the cycle timing information.
 */
void s3c2410_iotiming_getbank(struct s3c_cpufreq_config *cfg,
			      struct s3c2410_iobank_timing *bt)
{
	unsigned long bankcon = bt->bankcon;
	unsigned long hclk = cfg->freq.hclk_tns;

	bt->tcah = get_0124(hclk, bankcon >> S3C2410_BANKCON_Tcah_SHIFT);
	bt->tcoh = get_0124(hclk, bankcon >> S3C2410_BANKCON_Tcoh_SHIFT);
	bt->tcos = get_0124(hclk, bankcon >> S3C2410_BANKCON_Tcos_SHIFT);
	bt->tacs = get_0124(hclk, bankcon >> S3C2410_BANKCON_Tacs_SHIFT);
	bt->tacc = get_tacc(hclk, bankcon >> S3C2410_BANKCON_Tacc_SHIFT);
}

/**
 * s3c2410_iotiming_debugfs - debugfs show io bank timing information
 * @seq: The seq_file to write output to using seq_printf().
 * @cfg: The current configuration.
 * @iob: The IO bank information to decode.
 */
void s3c2410_iotiming_debugfs(struct seq_file *seq,
			      struct s3c_cpufreq_config *cfg,
			      union s3c_iobank *iob)
{
	struct s3c2410_iobank_timing *bt = iob->io_2410;
	unsigned long bankcon = bt->bankcon;
	unsigned long hclk = cfg->freq.hclk_tns;
	unsigned int tacs;
	unsigned int tcos;
	unsigned int tacc;
	unsigned int tcoh;
	unsigned int tcah;

	seq_printf(seq, "BANKCON=0x%08lx\n", bankcon);

	tcah = get_0124(hclk, bankcon >> S3C2410_BANKCON_Tcah_SHIFT);
	tcoh = get_0124(hclk, bankcon >> S3C2410_BANKCON_Tcoh_SHIFT);
	tcos = get_0124(hclk, bankcon >> S3C2410_BANKCON_Tcos_SHIFT);
	tacs = get_0124(hclk, bankcon >> S3C2410_BANKCON_Tacs_SHIFT);
	tacc = get_tacc(hclk, bankcon >> S3C2410_BANKCON_Tacc_SHIFT);

	seq_printf(seq,
		   "\tRead: Tacs=%d.%d, Tcos=%d.%d, Tacc=%d.%d, Tcoh=%d.%d, Tcah=%d.%d\n",
		   print_ns(bt->tacs),
		   print_ns(bt->tcos),
		   print_ns(bt->tacc),
		   print_ns(bt->tcoh),
		   print_ns(bt->tcah));

	seq_printf(seq,
		   "\t Set: Tacs=%d.%d, Tcos=%d.%d, Tacc=%d.%d, Tcoh=%d.%d, Tcah=%d.%d\n",
		   print_ns(tacs),
		   print_ns(tcos),
		   print_ns(tacc),
		   print_ns(tcoh),
		   print_ns(tcah));
}

/**
 * s3c2410_iotiming_calc - Calculate bank timing for frequency change.
 * @cfg: The frequency configuration
 * @iot: The IO timing information to fill out.
 *
 * Calculate the new values for the banks in @iot based on the new
 * frequency information in @cfg. This is then used by s3c2410_iotiming_set()
 * to update the timing when necessary.
 */
int s3c2410_iotiming_calc(struct s3c_cpufreq_config *cfg,
			  struct s3c_iotimings *iot)
{
	struct s3c2410_iobank_timing *bt;
	unsigned long bankcon;
	int bank;
	int ret;

	for (bank = 0; bank < MAX_BANKS; bank++) {
		bankcon = __raw_readl(bank_reg(bank));
		bt = iot->bank[bank].io_2410;

		if (!bt)
			continue;

		bt->bankcon = bankcon;

		ret = s3c2410_calc_bank(cfg, bt);
		if (ret) {
			printk(KERN_ERR "%s: cannot calculate bank %d io\n",
			       __func__, bank);
			goto err;
		}

		s3c_freq_iodbg("%s: bank %d: con=%08lx\n",
			       __func__, bank, bt->bankcon);
	}

	return 0;
 err:
	return ret;
}

/**
 * s3c2410_iotiming_set - set the IO timings from the given setup.
 * @cfg: The frequency configuration
 * @iot: The IO timing information to use.
 *
 * Set all the currently used IO bank timing information generated
 * by s3c2410_iotiming_calc() once the core has validated that all
 * the new values are within permitted bounds.
 */
void s3c2410_iotiming_set(struct s3c_cpufreq_config *cfg,
			  struct s3c_iotimings *iot)
{
	struct s3c2410_iobank_timing *bt;
	int bank;

	/* set the io timings from the specifier */

	for (bank = 0; bank < MAX_BANKS; bank++) {
		bt = iot->bank[bank].io_2410;
		if (!bt)
			continue;

		__raw_writel(bt->bankcon, bank_reg(bank));
	}
}

/**
 * s3c2410_iotiming_get - Get the timing information from current registers.
 * @cfg: The frequency configuration
 * @timings: The IO timing information to fill out.
 *
 * Calculate the @timings timing information from the current frequency
 * information in @cfg, and the new frequency configur
 * through all the IO banks, reading the state and then updating @iot
 * as necessary.
 *
 * This is used at the moment on initialisation to get the current
 * configuration so that boards do not have to carry their own setup
 * if the timings are correct on initialisation.
 */

int s3c2410_iotiming_get(struct s3c_cpufreq_config *cfg,
			 struct s3c_iotimings *timings)
{
	struct s3c2410_iobank_timing *bt;
	unsigned long bankcon;
	unsigned long bwscon;
	int bank;

	bwscon = __raw_readl(S3C2410_BWSCON);

	/* look through all banks to see what is currently set. */

	for (bank = 0; bank < MAX_BANKS; bank++) {
		bankcon = __raw_readl(bank_reg(bank));

		if (!bank_is_io(bankcon))
			continue;

		s3c_freq_iodbg("%s: bank %d: con %08lx\n",
			       __func__, bank, bankcon);

		bt = kzalloc(sizeof(struct s3c2410_iobank_timing), GFP_KERNEL);
		if (!bt) {
			printk(KERN_ERR "%s: no memory for bank\n", __func__);
			return -ENOMEM;
		}

		/* find out in nWait is enabled for bank. */

		if (bank != 0) {
			unsigned long tmp  = S3C2410_BWSCON_GET(bwscon, bank);
			if (tmp & S3C2410_BWSCON_WS)
				bt->nwait_en = 1;
		}

		timings->bank[bank].io_2410 = bt;
		bt->bankcon = bankcon;

		s3c2410_iotiming_getbank(cfg, bt);
	}

	s3c2410_print_timing("get", timings);
	return 0;
}
