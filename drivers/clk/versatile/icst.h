/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  Copyright (C) 2003 Deep Blue Solutions, Ltd, All Rights Reserved.
 *
 *  Support functions for calculating clocks/divisors for the ICST
 *  clock generators.  See https://www.idt.com/ for more information
 *  on these devices.
 */
#ifndef ICST_H
#define ICST_H

struct icst_params {
	unsigned long	ref;
	unsigned long	vco_max;	/* inclusive */
	unsigned long	vco_min;	/* exclusive */
	unsigned short	vd_min;		/* inclusive */
	unsigned short	vd_max;		/* inclusive */
	unsigned char	rd_min;		/* inclusive */
	unsigned char	rd_max;		/* inclusive */
	const unsigned char *s2div;	/* chip specific s2div array */
	const unsigned char *idx2s;	/* chip specific idx2s array */
};

struct icst_vco {
	unsigned short	v;
	unsigned char	r;
	unsigned char	s;
};

unsigned long icst_hz(const struct icst_params *p, struct icst_vco vco);
struct icst_vco icst_hz_to_vco(const struct icst_params *p, unsigned long freq);

/*
 * ICST307 VCO frequency must be between 6MHz and 200MHz (3.3 or 5V).
 * This frequency is pre-output divider.
 */
#define ICST307_VCO_MIN	6000000
#define ICST307_VCO_MAX	200000000

extern const unsigned char icst307_s2div[];
extern const unsigned char icst307_idx2s[];

/*
 * ICST525 VCO frequency must be between 10MHz and 200MHz (3V) or 320MHz (5V).
 * This frequency is pre-output divider.
 */
#define ICST525_VCO_MIN		10000000
#define ICST525_VCO_MAX_3V	200000000
#define ICST525_VCO_MAX_5V	320000000

extern const unsigned char icst525_s2div[];
extern const unsigned char icst525_idx2s[];

#endif
