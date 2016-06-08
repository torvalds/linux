/*
 * MIPS SPRAM support
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * Copyright (C) 2007, 2008 MIPS Technologies, Inc.
 */
#include <linux/kernel.h>
#include <linux/ptrace.h>
#include <linux/stddef.h>

#include <asm/fpu.h>
#include <asm/mipsregs.h>
#include <asm/r4kcache.h>
#include <asm/hazards.h>

/*
 * These definitions are correct for the 24K/34K/74K SPRAM sample
 * implementation. The 4KS interpreted the tags differently...
 */
#define SPRAM_TAG0_ENABLE	0x00000080
#define SPRAM_TAG0_PA_MASK	0xfffff000
#define SPRAM_TAG1_SIZE_MASK	0xfffff000

#define SPRAM_TAG_STRIDE	8

#define ERRCTL_SPRAM		(1 << 28)

/* errctl access */
#define read_c0_errctl(x) read_c0_ecc(x)
#define write_c0_errctl(x) write_c0_ecc(x)

/*
 * Different semantics to the set_c0_* function built by __BUILD_SET_C0
 */
static unsigned int bis_c0_errctl(unsigned int set)
{
	unsigned int res;
	res = read_c0_errctl();
	write_c0_errctl(res | set);
	return res;
}

static void ispram_store_tag(unsigned int offset, unsigned int data)
{
	unsigned int errctl;

	/* enable SPRAM tag access */
	errctl = bis_c0_errctl(ERRCTL_SPRAM);
	ehb();

	write_c0_taglo(data);
	ehb();

	cache_op(Index_Store_Tag_I, CKSEG0|offset);
	ehb();

	write_c0_errctl(errctl);
	ehb();
}


static unsigned int ispram_load_tag(unsigned int offset)
{
	unsigned int data;
	unsigned int errctl;

	/* enable SPRAM tag access */
	errctl = bis_c0_errctl(ERRCTL_SPRAM);
	ehb();
	cache_op(Index_Load_Tag_I, CKSEG0 | offset);
	ehb();
	data = read_c0_taglo();
	ehb();
	write_c0_errctl(errctl);
	ehb();

	return data;
}

static void dspram_store_tag(unsigned int offset, unsigned int data)
{
	unsigned int errctl;

	/* enable SPRAM tag access */
	errctl = bis_c0_errctl(ERRCTL_SPRAM);
	ehb();
	write_c0_dtaglo(data);
	ehb();
	cache_op(Index_Store_Tag_D, CKSEG0 | offset);
	ehb();
	write_c0_errctl(errctl);
	ehb();
}


static unsigned int dspram_load_tag(unsigned int offset)
{
	unsigned int data;
	unsigned int errctl;

	errctl = bis_c0_errctl(ERRCTL_SPRAM);
	ehb();
	cache_op(Index_Load_Tag_D, CKSEG0 | offset);
	ehb();
	data = read_c0_dtaglo();
	ehb();
	write_c0_errctl(errctl);
	ehb();

	return data;
}

static void probe_spram(char *type,
	    unsigned int base,
	    unsigned int (*read)(unsigned int),
	    void (*write)(unsigned int, unsigned int))
{
	unsigned int firstsize = 0, lastsize = 0;
	unsigned int firstpa = 0, lastpa = 0, pa = 0;
	unsigned int offset = 0;
	unsigned int size, tag0, tag1;
	unsigned int enabled;
	int i;

	/*
	 * The limit is arbitrary but avoids the loop running away if
	 * the SPRAM tags are implemented differently
	 */

	for (i = 0; i < 8; i++) {
		tag0 = read(offset);
		tag1 = read(offset+SPRAM_TAG_STRIDE);
		pr_debug("DBG %s%d: tag0=%08x tag1=%08x\n",
			 type, i, tag0, tag1);

		size = tag1 & SPRAM_TAG1_SIZE_MASK;

		if (size == 0)
			break;

		if (i != 0) {
			/* tags may repeat... */
			if ((pa == firstpa && size == firstsize) ||
			    (pa == lastpa && size == lastsize))
				break;
		}

		/* Align base with size */
		base = (base + size - 1) & ~(size-1);

		/* reprogram the base address base address and enable */
		tag0 = (base & SPRAM_TAG0_PA_MASK) | SPRAM_TAG0_ENABLE;
		write(offset, tag0);

		base += size;

		/* reread the tag */
		tag0 = read(offset);
		pa = tag0 & SPRAM_TAG0_PA_MASK;
		enabled = tag0 & SPRAM_TAG0_ENABLE;

		if (i == 0) {
			firstpa = pa;
			firstsize = size;
		}

		lastpa = pa;
		lastsize = size;

		if (strcmp(type, "DSPRAM") == 0) {
			unsigned int *vp = (unsigned int *)(CKSEG1 | pa);
			unsigned int v;
#define TDAT	0x5a5aa5a5
			vp[0] = TDAT;
			vp[1] = ~TDAT;

			mb();

			v = vp[0];
			if (v != TDAT)
				printk(KERN_ERR "vp=%p wrote=%08x got=%08x\n",
				       vp, TDAT, v);
			v = vp[1];
			if (v != ~TDAT)
				printk(KERN_ERR "vp=%p wrote=%08x got=%08x\n",
				       vp+1, ~TDAT, v);
		}

		pr_info("%s%d: PA=%08x,Size=%08x%s\n",
			type, i, pa, size, enabled ? ",enabled" : "");
		offset += 2 * SPRAM_TAG_STRIDE;
	}
}
void spram_config(void)
{
	unsigned int config0;

	switch (current_cpu_type()) {
	case CPU_24K:
	case CPU_34K:
	case CPU_74K:
	case CPU_1004K:
	case CPU_1074K:
	case CPU_INTERAPTIV:
	case CPU_PROAPTIV:
	case CPU_P5600:
	case CPU_QEMU_GENERIC:
	case CPU_I6400:
	case CPU_P6600:
		config0 = read_c0_config();
		/* FIXME: addresses are Malta specific */
		if (config0 & (1<<24)) {
			probe_spram("ISPRAM", 0x1c000000,
				    &ispram_load_tag, &ispram_store_tag);
		}
		if (config0 & (1<<23))
			probe_spram("DSPRAM", 0x1c100000,
				    &dspram_load_tag, &dspram_store_tag);
	}
}
