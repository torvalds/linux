/*
 * Static Memory Controller for AT32 chips
 *
 * Copyright (C) 2006 Atmel Corporation
 *
 * Inspired by the OMAP2 General-Purpose Memory Controller interface
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __ARCH_AT32AP_SMC_H
#define __ARCH_AT32AP_SMC_H

/*
 * All timing parameters are in nanoseconds.
 */
struct smc_timing {
	/* Delay from address valid to assertion of given strobe */
	int ncs_read_setup;
	int nrd_setup;
	int ncs_write_setup;
	int nwe_setup;

	/* Pulse length of given strobe */
	int ncs_read_pulse;
	int nrd_pulse;
	int ncs_write_pulse;
	int nwe_pulse;

	/* Total cycle length of given operation */
	int read_cycle;
	int write_cycle;

	/* Minimal recovery times, will extend cycle if needed */
	int ncs_read_recover;
	int nrd_recover;
	int ncs_write_recover;
	int nwe_recover;
};

/*
 * All timing parameters are in clock cycles.
 */
struct smc_config {

	/* Delay from address valid to assertion of given strobe */
	u8		ncs_read_setup;
	u8		nrd_setup;
	u8		ncs_write_setup;
	u8		nwe_setup;

	/* Pulse length of given strobe */
	u8		ncs_read_pulse;
	u8		nrd_pulse;
	u8		ncs_write_pulse;
	u8		nwe_pulse;

	/* Total cycle length of given operation */
	u8		read_cycle;
	u8		write_cycle;

	/* Bus width in bytes */
	u8		bus_width;

	/*
	 * 0: Data is sampled on rising edge of NCS
	 * 1: Data is sampled on rising edge of NRD
	 */
	unsigned int	nrd_controlled:1;

	/*
	 * 0: Data is driven on falling edge of NCS
	 * 1: Data is driven on falling edge of NWR
	 */
	unsigned int	nwe_controlled:1;

	/*
	 * 0: NWAIT is disabled
	 * 1: Reserved
	 * 2: NWAIT is frozen mode
	 * 3: NWAIT in ready mode
	 */
	unsigned int	nwait_mode:2;

	/*
	 * 0: Byte select access type
	 * 1: Byte write access type
	 */
	unsigned int	byte_write:1;

	/*
	 * Number of clock cycles before data is released after
	 * the rising edge of the read controlling signal
	 *
	 * Total cycles from SMC is tdf_cycles + 1
	 */
	unsigned int	tdf_cycles:4;

	/*
	 * 0: TDF optimization disabled
	 * 1: TDF optimization enabled
	 */
	unsigned int	tdf_mode:1;
};

extern void smc_set_timing(struct smc_config *config,
			   const struct smc_timing *timing);

extern int smc_set_configuration(int cs, const struct smc_config *config);
extern struct smc_config *smc_get_configuration(int cs);

#endif /* __ARCH_AT32AP_SMC_H */
