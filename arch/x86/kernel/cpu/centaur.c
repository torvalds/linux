#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/bitops.h>

#include <asm/processor.h>
#include <asm/msr.h>
#include <asm/e820.h>
#include <asm/mtrr.h>

#include "cpu.h"

#ifdef CONFIG_X86_OOSTORE

static u32 __cpuinit power2(u32 x)
{
	u32 s = 1;

	while (s <= x)
		s <<= 1;

	return s >>= 1;
}


/*
 * Set up an actual MCR
 */
static void __cpuinit centaur_mcr_insert(int reg, u32 base, u32 size, int key)
{
	u32 lo, hi;

	hi = base & ~0xFFF;
	lo = ~(size-1);		/* Size is a power of 2 so this makes a mask */
	lo &= ~0xFFF;		/* Remove the ctrl value bits */
	lo |= key;		/* Attribute we wish to set */
	wrmsr(reg+MSR_IDT_MCR0, lo, hi);
	mtrr_centaur_report_mcr(reg, lo, hi);	/* Tell the mtrr driver */
}

/*
 * Figure what we can cover with MCR's
 *
 * Shortcut: We know you can't put 4Gig of RAM on a winchip
 */
static u32 __cpuinit ramtop(void)
{
	u32 clip = 0xFFFFFFFFUL;
	u32 top = 0;
	int i;

	for (i = 0; i < e820.nr_map; i++) {
		unsigned long start, end;

		if (e820.map[i].addr > 0xFFFFFFFFUL)
			continue;
		/*
		 * Don't MCR over reserved space. Ignore the ISA hole
		 * we frob around that catastrophe already
		 */
		if (e820.map[i].type == E820_RESERVED) {
			if (e820.map[i].addr >= 0x100000UL &&
			    e820.map[i].addr < clip)
				clip = e820.map[i].addr;
			continue;
		}
		start = e820.map[i].addr;
		end = e820.map[i].addr + e820.map[i].size;
		if (start >= end)
			continue;
		if (end > top)
			top = end;
	}
	/*
	 * Everything below 'top' should be RAM except for the ISA hole.
	 * Because of the limited MCR's we want to map NV/ACPI into our
	 * MCR range for gunk in RAM
	 *
	 * Clip might cause us to MCR insufficient RAM but that is an
	 * acceptable failure mode and should only bite obscure boxes with
	 * a VESA hole at 15Mb
	 *
	 * The second case Clip sometimes kicks in is when the EBDA is marked
	 * as reserved. Again we fail safe with reasonable results
	 */
	if (top > clip)
		top = clip;

	return top;
}

/*
 * Compute a set of MCR's to give maximum coverage
 */
static int __cpuinit centaur_mcr_compute(int nr, int key)
{
	u32 mem = ramtop();
	u32 root = power2(mem);
	u32 base = root;
	u32 top = root;
	u32 floor = 0;
	int ct = 0;

	while (ct < nr) {
		u32 fspace = 0;
		u32 high;
		u32 low;

		/*
		 * Find the largest block we will fill going upwards
		 */
		high = power2(mem-top);

		/*
		 * Find the largest block we will fill going downwards
		 */
		low = base/2;

		/*
		 * Don't fill below 1Mb going downwards as there
		 * is an ISA hole in the way.
		 */
		if (base <= 1024*1024)
			low = 0;

		/*
		 * See how much space we could cover by filling below
		 * the ISA hole
		 */

		if (floor == 0)
			fspace = 512*1024;
		else if (floor == 512*1024)
			fspace = 128*1024;

		/* And forget ROM space */

		/*
		 * Now install the largest coverage we get
		 */
		if (fspace > high && fspace > low) {
			centaur_mcr_insert(ct, floor, fspace, key);
			floor += fspace;
		} else if (high > low) {
			centaur_mcr_insert(ct, top, high, key);
			top += high;
		} else if (low > 0) {
			base -= low;
			centaur_mcr_insert(ct, base, low, key);
		} else
			break;
		ct++;
	}
	/*
	 * We loaded ct values. We now need to set the mask. The caller
	 * must do this bit.
	 */
	return ct;
}

static void __cpuinit centaur_create_optimal_mcr(void)
{
	int used;
	int i;

	/*
	 * Allocate up to 6 mcrs to mark as much of ram as possible
	 * as write combining and weak write ordered.
	 *
	 * To experiment with: Linux never uses stack operations for
	 * mmio spaces so we could globally enable stack operation wc
	 *
	 * Load the registers with type 31 - full write combining, all
	 * writes weakly ordered.
	 */
	used = centaur_mcr_compute(6, 31);

	/*
	 * Wipe unused MCRs
	 */
	for (i = used; i < 8; i++)
		wrmsr(MSR_IDT_MCR0+i, 0, 0);
}

static void __cpuinit winchip2_create_optimal_mcr(void)
{
	u32 lo, hi;
	int used;
	int i;

	/*
	 * Allocate up to 6 mcrs to mark as much of ram as possible
	 * as write combining, weak store ordered.
	 *
	 * Load the registers with type 25
	 *	8	-	weak write ordering
	 *	16	-	weak read ordering
	 *	1	-	write combining
	 */
	used = centaur_mcr_compute(6, 25);

	/*
	 * Mark the registers we are using.
	 */
	rdmsr(MSR_IDT_MCR_CTRL, lo, hi);
	for (i = 0; i < used; i++)
		lo |= 1<<(9+i);
	wrmsr(MSR_IDT_MCR_CTRL, lo, hi);

	/*
	 * Wipe unused MCRs
	 */

	for (i = used; i < 8; i++)
		wrmsr(MSR_IDT_MCR0+i, 0, 0);
}

/*
 * Handle the MCR key on the Winchip 2.
 */
static void __cpuinit winchip2_unprotect_mcr(void)
{
	u32 lo, hi;
	u32 key;

	rdmsr(MSR_IDT_MCR_CTRL, lo, hi);
	lo &= ~0x1C0;	/* blank bits 8-6 */
	key = (lo>>17) & 7;
	lo |= key<<6;	/* replace with unlock key */
	wrmsr(MSR_IDT_MCR_CTRL, lo, hi);
}

static void __cpuinit winchip2_protect_mcr(void)
{
	u32 lo, hi;

	rdmsr(MSR_IDT_MCR_CTRL, lo, hi);
	lo &= ~0x1C0;	/* blank bits 8-6 */
	wrmsr(MSR_IDT_MCR_CTRL, lo, hi);
}
#endif /* CONFIG_X86_OOSTORE */

#define ACE_PRESENT	(1 << 6)
#define ACE_ENABLED	(1 << 7)
#define ACE_FCR		(1 << 28)	/* MSR_VIA_FCR */

#define RNG_PRESENT	(1 << 2)
#define RNG_ENABLED	(1 << 3)
#define RNG_ENABLE	(1 << 6)	/* MSR_VIA_RNG */

static void __cpuinit init_c3(struct cpuinfo_x86 *c)
{
	u32  lo, hi;

	/* Test for Centaur Extended Feature Flags presence */
	if (cpuid_eax(0xC0000000) >= 0xC0000001) {
		u32 tmp = cpuid_edx(0xC0000001);

		/* enable ACE unit, if present and disabled */
		if ((tmp & (ACE_PRESENT | ACE_ENABLED)) == ACE_PRESENT) {
			rdmsr(MSR_VIA_FCR, lo, hi);
			lo |= ACE_FCR;		/* enable ACE unit */
			wrmsr(MSR_VIA_FCR, lo, hi);
			printk(KERN_INFO "CPU: Enabled ACE h/w crypto\n");
		}

		/* enable RNG unit, if present and disabled */
		if ((tmp & (RNG_PRESENT | RNG_ENABLED)) == RNG_PRESENT) {
			rdmsr(MSR_VIA_RNG, lo, hi);
			lo |= RNG_ENABLE;	/* enable RNG unit */
			wrmsr(MSR_VIA_RNG, lo, hi);
			printk(KERN_INFO "CPU: Enabled h/w RNG\n");
		}

		/* store Centaur Extended Feature Flags as
		 * word 5 of the CPU capability bit array
		 */
		c->x86_capability[5] = cpuid_edx(0xC0000001);
	}

	/* Cyrix III family needs CX8 & PGE explicitly enabled. */
	if (c->x86_model >= 6 && c->x86_model <= 9) {
		rdmsr(MSR_VIA_FCR, lo, hi);
		lo |= (1<<1 | 1<<7);
		wrmsr(MSR_VIA_FCR, lo, hi);
		set_cpu_cap(c, X86_FEATURE_CX8);
	}

	/* Before Nehemiah, the C3's had 3dNOW! */
	if (c->x86_model >= 6 && c->x86_model < 9)
		set_cpu_cap(c, X86_FEATURE_3DNOW);

	get_model_name(c);
	display_cacheinfo(c);
}

enum {
		ECX8		= 1<<1,
		EIERRINT	= 1<<2,
		DPM		= 1<<3,
		DMCE		= 1<<4,
		DSTPCLK		= 1<<5,
		ELINEAR		= 1<<6,
		DSMC		= 1<<7,
		DTLOCK		= 1<<8,
		EDCTLB		= 1<<8,
		EMMX		= 1<<9,
		DPDC		= 1<<11,
		EBRPRED		= 1<<12,
		DIC		= 1<<13,
		DDC		= 1<<14,
		DNA		= 1<<15,
		ERETSTK		= 1<<16,
		E2MMX		= 1<<19,
		EAMD3D		= 1<<20,
};

static void __cpuinit init_centaur(struct cpuinfo_x86 *c)
{

	char *name;
	u32  fcr_set = 0;
	u32  fcr_clr = 0;
	u32  lo, hi, newlo;
	u32  aa, bb, cc, dd;

	/*
	 * Bit 31 in normal CPUID used for nonstandard 3DNow ID;
	 * 3DNow is IDd by bit 31 in extended CPUID (1*32+31) anyway
	 */
	clear_cpu_cap(c, 0*32+31);

	switch (c->x86) {
	case 5:
		switch (c->x86_model) {
		case 4:
			name = "C6";
			fcr_set = ECX8|DSMC|EDCTLB|EMMX|ERETSTK;
			fcr_clr = DPDC;
			printk(KERN_NOTICE "Disabling bugged TSC.\n");
			clear_cpu_cap(c, X86_FEATURE_TSC);
#ifdef CONFIG_X86_OOSTORE
			centaur_create_optimal_mcr();
			/*
			 * Enable:
			 *	write combining on non-stack, non-string
			 *	write combining on string, all types
			 *	weak write ordering
			 *
			 * The C6 original lacks weak read order
			 *
			 * Note 0x120 is write only on Winchip 1
			 */
			wrmsr(MSR_IDT_MCR_CTRL, 0x01F0001F, 0);
#endif
			break;
		case 8:
			switch (c->x86_mask) {
			default:
			name = "2";
				break;
			case 7 ... 9:
				name = "2A";
				break;
			case 10 ... 15:
				name = "2B";
				break;
			}
			fcr_set = ECX8|DSMC|DTLOCK|EMMX|EBRPRED|ERETSTK|
				  E2MMX|EAMD3D;
			fcr_clr = DPDC;
#ifdef CONFIG_X86_OOSTORE
			winchip2_unprotect_mcr();
			winchip2_create_optimal_mcr();
			rdmsr(MSR_IDT_MCR_CTRL, lo, hi);
			/*
			 * Enable:
			 *	write combining on non-stack, non-string
			 *	write combining on string, all types
			 *	weak write ordering
			 */
			lo |= 31;
			wrmsr(MSR_IDT_MCR_CTRL, lo, hi);
			winchip2_protect_mcr();
#endif
			break;
		case 9:
			name = "3";
			fcr_set = ECX8|DSMC|DTLOCK|EMMX|EBRPRED|ERETSTK|
				  E2MMX|EAMD3D;
			fcr_clr = DPDC;
#ifdef CONFIG_X86_OOSTORE
			winchip2_unprotect_mcr();
			winchip2_create_optimal_mcr();
			rdmsr(MSR_IDT_MCR_CTRL, lo, hi);
			/*
			 * Enable:
			 *	write combining on non-stack, non-string
			 *	write combining on string, all types
			 *	weak write ordering
			 */
			lo |= 31;
			wrmsr(MSR_IDT_MCR_CTRL, lo, hi);
			winchip2_protect_mcr();
#endif
			break;
		default:
			name = "??";
		}

		rdmsr(MSR_IDT_FCR1, lo, hi);
		newlo = (lo|fcr_set) & (~fcr_clr);

		if (newlo != lo) {
			printk(KERN_INFO "Centaur FCR was 0x%X now 0x%X\n",
				lo, newlo);
			wrmsr(MSR_IDT_FCR1, newlo, hi);
		} else {
			printk(KERN_INFO "Centaur FCR is 0x%X\n", lo);
		}
		/* Emulate MTRRs using Centaur's MCR. */
		set_cpu_cap(c, X86_FEATURE_CENTAUR_MCR);
		/* Report CX8 */
		set_cpu_cap(c, X86_FEATURE_CX8);
		/* Set 3DNow! on Winchip 2 and above. */
		if (c->x86_model >= 8)
			set_cpu_cap(c, X86_FEATURE_3DNOW);
		/* See if we can find out some more. */
		if (cpuid_eax(0x80000000) >= 0x80000005) {
			/* Yes, we can. */
			cpuid(0x80000005, &aa, &bb, &cc, &dd);
			/* Add L1 data and code cache sizes. */
			c->x86_cache_size = (cc>>24)+(dd>>24);
		}
		sprintf(c->x86_model_id, "WinChip %s", name);
		break;

	case 6:
		init_c3(c);
		break;
	}
}

static unsigned int __cpuinit
centaur_size_cache(struct cpuinfo_x86 *c, unsigned int size)
{
	/* VIA C3 CPUs (670-68F) need further shifting. */
	if ((c->x86 == 6) && ((c->x86_model == 7) || (c->x86_model == 8)))
		size >>= 8;

	/*
	 * There's also an erratum in Nehemiah stepping 1, which
	 * returns '65KB' instead of '64KB'
	 *  - Note, it seems this may only be in engineering samples.
	 */
	if ((c->x86 == 6) && (c->x86_model == 9) &&
				(c->x86_mask == 1) && (size == 65))
		size -= 1;

	return size;
}

static struct cpu_dev centaur_cpu_dev __cpuinitdata = {
	.c_vendor	= "Centaur",
	.c_ident	= { "CentaurHauls" },
	.c_init		= init_centaur,
	.c_size_cache	= centaur_size_cache,
};

cpu_vendor_dev_register(X86_VENDOR_CENTAUR, &centaur_cpu_dev);
