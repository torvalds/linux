/* arch/arm/plat-samsung/include/plat/cpu-freq-core.h
 *
 * Copyright (c) 2006-2009 Simtec Electronics
 *	http://armlinux.simtec.co.uk/
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * S3C CPU frequency scaling support - core support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <plat/cpu-freq.h>

struct seq_file;

#define MAX_BANKS (8)
#define S3C2412_MAX_IO	(8)

/**
 * struct s3c2410_iobank_timing - IO bank timings for S3C2410 style timings
 * @bankcon: The cached version of settings in this structure.
 * @tacp:
 * @tacs: Time from address valid to nCS asserted.
 * @tcos: Time from nCS asserted to nOE or nWE asserted.
 * @tacc: Time that nOE or nWE is asserted.
 * @tcoh: Time nCS is held after nOE or nWE are released.
 * @tcah: Time address is held for after
 * @nwait_en: Whether nWAIT is enabled for this bank.
 *
 * This structure represents the IO timings for a S3C2410 style IO bank
 * used by the CPU frequency support if it needs to change the settings
 * of the IO.
 */
struct s3c2410_iobank_timing {
	unsigned long	bankcon;
	unsigned int	tacp;
	unsigned int	tacs;
	unsigned int	tcos;
	unsigned int	tacc;
	unsigned int	tcoh;		/* nCS hold afrer nOE/nWE */
	unsigned int	tcah;		/* Address hold after nCS */
	unsigned char	nwait_en;	/* nWait enabled for bank. */
};

/**
 * struct s3c2412_iobank_timing - io timings for PL092 (S3C2412) style IO
 * @idcy: The idle cycle time between transactions.
 * @wstrd: nCS release to end of read cycle.
 * @wstwr: nCS release to end of write cycle.
 * @wstoen: nCS assertion to nOE assertion time.
 * @wstwen: nCS assertion to nWE assertion time.
 * @wstbrd: Burst ready delay.
 * @smbidcyr: Register cache for smbidcyr value.
 * @smbwstrd: Register cache for smbwstrd value.
 * @smbwstwr: Register cache for smbwstwr value.
 * @smbwstoen: Register cache for smbwstoen value.
 * @smbwstwen: Register cache for smbwstwen value.
 * @smbwstbrd: Register cache for smbwstbrd value.
 *
 * Timing information for a IO bank on an S3C2412 or similar system which
 * uses a PL093 block.
 */
struct s3c2412_iobank_timing {
	unsigned int	idcy;
	unsigned int	wstrd;
	unsigned int	wstwr;
	unsigned int	wstoen;
	unsigned int	wstwen;
	unsigned int	wstbrd;

	/* register cache */
	unsigned char	smbidcyr;
	unsigned char	smbwstrd;
	unsigned char	smbwstwr;
	unsigned char	smbwstoen;
	unsigned char	smbwstwen;
	unsigned char	smbwstbrd;
};

union s3c_iobank {
	struct s3c2410_iobank_timing	*io_2410;
	struct s3c2412_iobank_timing	*io_2412;
};

/**
 * struct s3c_iotimings - Chip IO timings holder
 * @bank: The timings for each IO bank.
 */
struct s3c_iotimings {
	union s3c_iobank	bank[MAX_BANKS];
};

/**
 * struct s3c_plltab - PLL table information.
 * @vals: List of PLL values.
 * @size: Size of the PLL table @vals.
 */
struct s3c_plltab {
	struct s3c_pllval	*vals;
	int			 size;
};

/**
 * struct s3c_cpufreq_config - current cpu frequency configuration
 * @freq: The current settings for the core clocks.
 * @max: Maxium settings, derived from core, board and user settings.
 * @pll: The PLL table entry for the current PLL settings.
 * @divs: The divisor settings for the core clocks.
 * @info: The current core driver information.
 * @board: The information for the board we are running on.
 * @lock_pll: Set if the PLL settings cannot be changed.
 *
 * This is for the core drivers that need to know information about
 * the current settings and values. It should not be needed by any
 * device drivers.
*/
struct s3c_cpufreq_config {
	struct s3c_freq		freq;
	struct s3c_freq		max;
	struct cpufreq_frequency_table pll;
	struct s3c_clkdivs	divs;
	struct s3c_cpufreq_info *info;	/* for core, not drivers */
	struct s3c_cpufreq_board *board;

	unsigned int	lock_pll:1;
};

/**
 * struct s3c_cpufreq_info - Information for the CPU frequency driver.
 * @name: The name of this implementation.
 * @max: The maximum frequencies for the system.
 * @latency: Transition latency to give to cpufreq.
 * @locktime_m: The lock-time in uS for the MPLL.
 * @locktime_u: The lock-time in uS for the UPLL.
 * @locttime_bits: The number of bits each LOCKTIME field.
 * @need_pll: Set if this driver needs to change the PLL values to achieve
 *	any frequency changes. This is really only need by devices like the
 *	S3C2410 where there is no or limited divider between the PLL and the
 *	ARMCLK.
 * @resume_clocks: Update the clocks on resume.
 * @get_iotiming: Get the current IO timing data, mainly for use at start.
 * @set_iotiming: Update the IO timings from the cached copies calculated
 *	from the @calc_iotiming entry when changing the frequency.
 * @calc_iotiming: Calculate and update the cached copies of the IO timings
 *	from the newly calculated frequencies.
 * @calc_freqtable: Calculate (fill in) the given frequency table from the
 *	current frequency configuration. If the table passed in is NULL,
 *	then the return is the number of elements to be filled for allocation
 *	of the table.
 * @set_refresh: Set the memory refresh configuration.
 * @set_fvco: Set the PLL frequencies.
 * @set_divs: Update the clock divisors.
 * @calc_divs: Calculate the clock divisors.
 */
struct s3c_cpufreq_info {
	const char		*name;
	struct s3c_freq		max;

	unsigned int		latency;

	unsigned int		locktime_m;
	unsigned int		locktime_u;
	unsigned char		locktime_bits;

	unsigned int		need_pll:1;

	/* driver routines */

	void		(*resume_clocks)(void);

	int		(*get_iotiming)(struct s3c_cpufreq_config *cfg,
					struct s3c_iotimings *timings);

	void		(*set_iotiming)(struct s3c_cpufreq_config *cfg,
					struct s3c_iotimings *timings);

	int		(*calc_iotiming)(struct s3c_cpufreq_config *cfg,
					 struct s3c_iotimings *timings);

	int		(*calc_freqtable)(struct s3c_cpufreq_config *cfg,
					  struct cpufreq_frequency_table *t,
					  size_t table_size);

	void		(*debug_io_show)(struct seq_file *seq,
					 struct s3c_cpufreq_config *cfg,
					 union s3c_iobank *iob);

	void		(*set_refresh)(struct s3c_cpufreq_config *cfg);
	void		(*set_fvco)(struct s3c_cpufreq_config *cfg);
	void		(*set_divs)(struct s3c_cpufreq_config *cfg);
	int		(*calc_divs)(struct s3c_cpufreq_config *cfg);
};

extern int s3c_cpufreq_register(struct s3c_cpufreq_info *info);

extern int s3c_plltab_register(struct cpufreq_frequency_table *plls,
			       unsigned int plls_no);

/* exports and utilities for debugfs */
extern struct s3c_cpufreq_config *s3c_cpufreq_getconfig(void);
extern struct s3c_iotimings *s3c_cpufreq_getiotimings(void);

extern void s3c2410_iotiming_debugfs(struct seq_file *seq,
				     struct s3c_cpufreq_config *cfg,
				     union s3c_iobank *iob);

extern void s3c2412_iotiming_debugfs(struct seq_file *seq,
				     struct s3c_cpufreq_config *cfg,
				     union s3c_iobank *iob);

#ifdef CONFIG_CPU_FREQ_S3C24XX_DEBUGFS
#define s3c_cpufreq_debugfs_call(x) x
#else
#define s3c_cpufreq_debugfs_call(x) NULL
#endif

/* Useful utility functions. */

extern struct clk *s3c_cpufreq_clk_get(struct device *, const char *);

/* S3C2410 and compatible exported functions */

extern void s3c2410_cpufreq_setrefresh(struct s3c_cpufreq_config *cfg);
extern void s3c2410_set_fvco(struct s3c_cpufreq_config *cfg);

#ifdef CONFIG_S3C2410_IOTIMING
extern int s3c2410_iotiming_calc(struct s3c_cpufreq_config *cfg,
				 struct s3c_iotimings *iot);

extern int s3c2410_iotiming_get(struct s3c_cpufreq_config *cfg,
				struct s3c_iotimings *timings);

extern void s3c2410_iotiming_set(struct s3c_cpufreq_config *cfg,
				 struct s3c_iotimings *iot);
#else
#define s3c2410_iotiming_calc NULL
#define s3c2410_iotiming_get NULL
#define s3c2410_iotiming_set NULL
#endif /* CONFIG_S3C2410_IOTIMING */

/* S3C2412 compatible routines */

extern int s3c2412_iotiming_get(struct s3c_cpufreq_config *cfg,
				struct s3c_iotimings *timings);

extern int s3c2412_iotiming_get(struct s3c_cpufreq_config *cfg,
				struct s3c_iotimings *timings);

extern int s3c2412_iotiming_calc(struct s3c_cpufreq_config *cfg,
				 struct s3c_iotimings *iot);

extern void s3c2412_iotiming_set(struct s3c_cpufreq_config *cfg,
				 struct s3c_iotimings *iot);

#ifdef CONFIG_CPU_FREQ_S3C24XX_DEBUG
#define s3c_freq_dbg(x...) printk(KERN_INFO x)
#else
#define s3c_freq_dbg(x...) do { if (0) printk(x); } while (0)
#endif /* CONFIG_CPU_FREQ_S3C24XX_DEBUG */

#ifdef CONFIG_CPU_FREQ_S3C24XX_IODEBUG
#define s3c_freq_iodbg(x...) printk(KERN_INFO x)
#else
#define s3c_freq_iodbg(x...) do { if (0) printk(x); } while (0)
#endif /* CONFIG_CPU_FREQ_S3C24XX_IODEBUG */

static inline int s3c_cpufreq_addfreq(struct cpufreq_frequency_table *table,
				      int index, size_t table_size,
				      unsigned int freq)
{
	if (index < 0)
		return index;

	if (table) {
		if (index >= table_size)
			return -ENOMEM;

		s3c_freq_dbg("%s: { %d = %u kHz }\n",
			     __func__, index, freq);

		table[index].index = index;
		table[index].frequency = freq;
	}

	return index + 1;
}
