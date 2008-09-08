#include <linux/init.h>
#include <linux/bitops.h>
#include <linux/mm.h>

#include <asm/io.h>
#include <asm/processor.h>
#include <asm/apic.h>

#ifdef CONFIG_X86_64
# include <asm/numa_64.h>
# include <asm/mmconfig.h>
# include <asm/cacheflush.h>
#endif

#include <mach_apic.h>

#include "cpu.h"

/*
 *	B step AMD K6 before B 9730xxxx have hardware bugs that can cause
 *	misexecution of code under Linux. Owners of such processors should
 *	contact AMD for precise details and a CPU swap.
 *
 *	See	http://www.multimania.com/poulot/k6bug.html
 *		http://www.amd.com/K6/k6docs/revgd.html
 *
 *	The following test is erm.. interesting. AMD neglected to up
 *	the chip setting when fixing the bug but they also tweaked some
 *	performance at the same time..
 */

extern void vide(void);
__asm__(".align 4\nvide: ret");

static void __cpuinit init_amd_k5(struct cpuinfo_x86 *c)
{
/*
 * General Systems BIOSen alias the cpu frequency registers
 * of the Elan at 0x000df000. Unfortuantly, one of the Linux
 * drivers subsequently pokes it, and changes the CPU speed.
 * Workaround : Remove the unneeded alias.
 */
#define CBAR		(0xfffc) /* Configuration Base Address  (32-bit) */
#define CBAR_ENB	(0x80000000)
#define CBAR_KEY	(0X000000CB)
	if (c->x86_model == 9 || c->x86_model == 10) {
		if (inl (CBAR) & CBAR_ENB)
			outl (0 | CBAR_KEY, CBAR);
	}
}


static void __cpuinit init_amd_k6(struct cpuinfo_x86 *c)
{
	u32 l, h;
	int mbytes = num_physpages >> (20-PAGE_SHIFT);

	if (c->x86_model < 6) {
		/* Based on AMD doc 20734R - June 2000 */
		if (c->x86_model == 0) {
			clear_cpu_cap(c, X86_FEATURE_APIC);
			set_cpu_cap(c, X86_FEATURE_PGE);
		}
		return;
	}

	if (c->x86_model == 6 && c->x86_mask == 1) {
		const int K6_BUG_LOOP = 1000000;
		int n;
		void (*f_vide)(void);
		unsigned long d, d2;

		printk(KERN_INFO "AMD K6 stepping B detected - ");

		/*
		 * It looks like AMD fixed the 2.6.2 bug and improved indirect
		 * calls at the same time.
		 */

		n = K6_BUG_LOOP;
		f_vide = vide;
		rdtscl(d);
		while (n--)
			f_vide();
		rdtscl(d2);
		d = d2-d;

		if (d > 20*K6_BUG_LOOP)
			printk("system stability may be impaired when more than 32 MB are used.\n");
		else
			printk("probably OK (after B9730xxxx).\n");
		printk(KERN_INFO "Please see http://membres.lycos.fr/poulot/k6bug.html\n");
	}

	/* K6 with old style WHCR */
	if (c->x86_model < 8 ||
	   (c->x86_model == 8 && c->x86_mask < 8)) {
		/* We can only write allocate on the low 508Mb */
		if (mbytes > 508)
			mbytes = 508;

		rdmsr(MSR_K6_WHCR, l, h);
		if ((l&0x0000FFFF) == 0) {
			unsigned long flags;
			l = (1<<0)|((mbytes/4)<<1);
			local_irq_save(flags);
			wbinvd();
			wrmsr(MSR_K6_WHCR, l, h);
			local_irq_restore(flags);
			printk(KERN_INFO "Enabling old style K6 write allocation for %d Mb\n",
				mbytes);
		}
		return;
	}

	if ((c->x86_model == 8 && c->x86_mask > 7) ||
	     c->x86_model == 9 || c->x86_model == 13) {
		/* The more serious chips .. */

		if (mbytes > 4092)
			mbytes = 4092;

		rdmsr(MSR_K6_WHCR, l, h);
		if ((l&0xFFFF0000) == 0) {
			unsigned long flags;
			l = ((mbytes>>2)<<22)|(1<<16);
			local_irq_save(flags);
			wbinvd();
			wrmsr(MSR_K6_WHCR, l, h);
			local_irq_restore(flags);
			printk(KERN_INFO "Enabling new style K6 write allocation for %d Mb\n",
				mbytes);
		}

		return;
	}

	if (c->x86_model == 10) {
		/* AMD Geode LX is model 10 */
		/* placeholder for any needed mods */
		return;
	}
}

static void __cpuinit init_amd_k7(struct cpuinfo_x86 *c)
{
	u32 l, h;

	/*
	 * Bit 15 of Athlon specific MSR 15, needs to be 0
	 * to enable SSE on Palomino/Morgan/Barton CPU's.
	 * If the BIOS didn't enable it already, enable it here.
	 */
	if (c->x86_model >= 6 && c->x86_model <= 10) {
		if (!cpu_has(c, X86_FEATURE_XMM)) {
			printk(KERN_INFO "Enabling disabled K7/SSE Support.\n");
			rdmsr(MSR_K7_HWCR, l, h);
			l &= ~0x00008000;
			wrmsr(MSR_K7_HWCR, l, h);
			set_cpu_cap(c, X86_FEATURE_XMM);
		}
	}

	/*
	 * It's been determined by AMD that Athlons since model 8 stepping 1
	 * are more robust with CLK_CTL set to 200xxxxx instead of 600xxxxx
	 * As per AMD technical note 27212 0.2
	 */
	if ((c->x86_model == 8 && c->x86_mask >= 1) || (c->x86_model > 8)) {
		rdmsr(MSR_K7_CLK_CTL, l, h);
		if ((l & 0xfff00000) != 0x20000000) {
			printk ("CPU: CLK_CTL MSR was %x. Reprogramming to %x\n", l,
				((l & 0x000fffff)|0x20000000));
			wrmsr(MSR_K7_CLK_CTL, (l & 0x000fffff)|0x20000000, h);
		}
	}

	set_cpu_cap(c, X86_FEATURE_K7);
}

/*
 * On a AMD dual core setup the lower bits of the APIC id distingush the cores.
 * Assumes number of cores is a power of two.
 */
static void __cpuinit amd_detect_cmp(struct cpuinfo_x86 *c)
{
#ifdef CONFIG_X86_HT
	unsigned bits;

	bits = c->x86_coreid_bits;

	/* Low order bits define the core id (index of core in socket) */
	c->cpu_core_id = c->initial_apicid & ((1 << bits)-1);
	/* Convert the initial APIC ID into the socket ID */
	c->phys_proc_id = c->initial_apicid >> bits;
#endif
}

static void __cpuinit early_init_amd_mc(struct cpuinfo_x86 *c)
{
#ifdef CONFIG_X86_HT
	unsigned bits, ecx;

	/* Multi core CPU? */
	if (c->extended_cpuid_level < 0x80000008)
		return;

	ecx = cpuid_ecx(0x80000008);

	c->x86_max_cores = (ecx & 0xff) + 1;

	/* CPU telling us the core id bits shift? */
	bits = (ecx >> 12) & 0xF;

	/* Otherwise recompute */
	if (bits == 0) {
		while ((1 << bits) < c->x86_max_cores)
			bits++;
	}

	c->x86_coreid_bits = bits;
#endif
}

static void __cpuinit early_init_amd(struct cpuinfo_x86 *c)
{
	early_init_amd_mc(c);

	if (c->x86_power & (1<<8))
		set_cpu_cap(c, X86_FEATURE_CONSTANT_TSC);

	/*  Set MTRR capability flag if appropriate */
	if (c->x86_model == 13 || c->x86_model == 9 ||
	   (c->x86_model == 8 && c->x86_mask >= 8))
		set_cpu_cap(c, X86_FEATURE_K6_MTRR);
}

static void __cpuinit init_amd(struct cpuinfo_x86 *c)
{
#ifdef CONFIG_SMP
	unsigned long long value;

	/*
	 * Disable TLB flush filter by setting HWCR.FFDIS on K8
	 * bit 6 of msr C001_0015
	 *
	 * Errata 63 for SH-B3 steppings
	 * Errata 122 for all steppings (F+ have it disabled by default)
	 */
	if (c->x86 == 0xf) {
		rdmsrl(MSR_K7_HWCR, value);
		value |= 1 << 6;
		wrmsrl(MSR_K7_HWCR, value);
	}
#endif

	early_init_amd(c);

	/*
	 *	FIXME: We should handle the K5 here. Set up the write
	 *	range and also turn on MSR 83 bits 4 and 31 (write alloc,
	 *	no bus pipeline)
	 */

	/*
	 * Bit 31 in normal CPUID used for nonstandard 3DNow ID;
	 * 3DNow is IDd by bit 31 in extended CPUID (1*32+31) anyway
	 */
	clear_cpu_cap(c, 0*32+31);

	switch (c->x86) {
	case 4:
		init_amd_k5(c);
		break;
	case 5:
		init_amd_k6(c);
		break;
	case 6: /* An Athlon/Duron */
		init_amd_k7(c);
		break;
	}

	/* K6s reports MCEs but don't actually have all the MSRs */
	if (c->x86 < 6)
		clear_cpu_cap(c, X86_FEATURE_MCE);

	if (c->x86 >= 6)
		set_cpu_cap(c, X86_FEATURE_FXSAVE_LEAK);

	if (!c->x86_model_id[0]) {
		switch (c->x86) {
		case 0xf:
			/* Should distinguish Models here, but this is only
			   a fallback anyways. */
			strcpy(c->x86_model_id, "Hammer");
			break;
		}
	}

	display_cacheinfo(c);

	/* Multi core CPU? */
	if (c->extended_cpuid_level >= 0x80000008)
		amd_detect_cmp(c);

	detect_ht(c);

	if (c->extended_cpuid_level >= 0x80000006) {
		if ((c->x86 >= 0x0f) && (cpuid_edx(0x80000006) & 0xf000))
			num_cache_leaves = 4;
		else
			num_cache_leaves = 3;
	}

	if (c->x86 >= 0xf && c->x86 <= 0x11)
		set_cpu_cap(c, X86_FEATURE_K8);

	if (cpu_has_xmm2) {
		/* MFENCE stops RDTSC speculation */
		set_cpu_cap(c, X86_FEATURE_MFENCE_RDTSC);
	}
}

static unsigned int __cpuinit amd_size_cache(struct cpuinfo_x86 *c, unsigned int size)
{
	/* AMD errata T13 (order #21922) */
	if ((c->x86 == 6)) {
		if (c->x86_model == 3 && c->x86_mask == 0)	/* Duron Rev A0 */
			size = 64;
		if (c->x86_model == 4 &&
		    (c->x86_mask == 0 || c->x86_mask == 1))	/* Tbird rev A1/A2 */
			size = 256;
	}
	return size;
}

static struct cpu_dev amd_cpu_dev __cpuinitdata = {
	.c_vendor	= "AMD",
	.c_ident	= { "AuthenticAMD" },
	.c_models = {
		{ .vendor = X86_VENDOR_AMD, .family = 4, .model_names =
		  {
			  [3] = "486 DX/2",
			  [7] = "486 DX/2-WB",
			  [8] = "486 DX/4",
			  [9] = "486 DX/4-WB",
			  [14] = "Am5x86-WT",
			  [15] = "Am5x86-WB"
		  }
		},
	},
	.c_early_init   = early_init_amd,
	.c_init		= init_amd,
	.c_size_cache	= amd_size_cache,
	.c_x86_vendor	= X86_VENDOR_AMD,
};

cpu_dev_register(amd_cpu_dev);
