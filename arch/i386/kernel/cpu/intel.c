#include <linux/config.h>
#include <linux/init.h>
#include <linux/kernel.h>

#include <linux/string.h>
#include <linux/bitops.h>
#include <linux/smp.h>
#include <linux/thread_info.h>

#include <asm/processor.h>
#include <asm/msr.h>
#include <asm/uaccess.h>

#include "cpu.h"

#ifdef CONFIG_X86_LOCAL_APIC
#include <asm/mpspec.h>
#include <asm/apic.h>
#include <mach_apic.h>
#endif

extern int trap_init_f00f_bug(void);

#ifdef CONFIG_X86_INTEL_USERCOPY
/*
 * Alignment at which movsl is preferred for bulk memory copies.
 */
struct movsl_mask movsl_mask;
#endif

void __init early_intel_workaround(struct cpuinfo_x86 *c)
{
	if (c->x86_vendor != X86_VENDOR_INTEL)
		return;
	/* Netburst reports 64 bytes clflush size, but does IO in 128 bytes */
	if (c->x86 == 15 && c->x86_cache_alignment == 64)
		c->x86_cache_alignment = 128;
}

/*
 *	Early probe support logic for ppro memory erratum #50
 *
 *	This is called before we do cpu ident work
 */
 
int __init ppro_with_ram_bug(void)
{
	/* Uses data from early_cpu_detect now */
	if (boot_cpu_data.x86_vendor == X86_VENDOR_INTEL &&
	    boot_cpu_data.x86 == 6 &&
	    boot_cpu_data.x86_model == 1 &&
	    boot_cpu_data.x86_mask < 8) {
		printk(KERN_INFO "Pentium Pro with Errata#50 detected. Taking evasive action.\n");
		return 1;
	}
	return 0;
}
	

/*
 * P4 Xeon errata 037 workaround.
 * Hardware prefetcher may cause stale data to be loaded into the cache.
 */
static void __init Intel_errata_workarounds(struct cpuinfo_x86 *c)
{
	unsigned long lo, hi;

	if ((c->x86 == 15) && (c->x86_model == 1) && (c->x86_mask == 1)) {
		rdmsr (MSR_IA32_MISC_ENABLE, lo, hi);
		if ((lo & (1<<9)) == 0) {
			printk (KERN_INFO "CPU: C0 stepping P4 Xeon detected.\n");
			printk (KERN_INFO "CPU: Disabling hardware prefetching (Errata 037)\n");
			lo |= (1<<9);	/* Disable hw prefetching */
			wrmsr (MSR_IA32_MISC_ENABLE, lo, hi);
		}
	}
}


static void __init init_intel(struct cpuinfo_x86 *c)
{
	unsigned int l2 = 0;
	char *p = NULL;

#ifdef CONFIG_X86_F00F_BUG
	/*
	 * All current models of Pentium and Pentium with MMX technology CPUs
	 * have the F0 0F bug, which lets nonprivileged users lock up the system.
	 * Note that the workaround only should be initialized once...
	 */
	c->f00f_bug = 0;
	if ( c->x86 == 5 ) {
		static int f00f_workaround_enabled = 0;

		c->f00f_bug = 1;
		if ( !f00f_workaround_enabled ) {
			trap_init_f00f_bug();
			printk(KERN_NOTICE "Intel Pentium with F0 0F bug - workaround enabled.\n");
			f00f_workaround_enabled = 1;
		}
	}
#endif

	select_idle_routine(c);
	l2 = init_intel_cacheinfo(c);

	/* SEP CPUID bug: Pentium Pro reports SEP but doesn't have it until model 3 mask 3 */
	if ((c->x86<<8 | c->x86_model<<4 | c->x86_mask) < 0x633)
		clear_bit(X86_FEATURE_SEP, c->x86_capability);

	/* Names for the Pentium II/Celeron processors 
	   detectable only by also checking the cache size.
	   Dixon is NOT a Celeron. */
	if (c->x86 == 6) {
		switch (c->x86_model) {
		case 5:
			if (c->x86_mask == 0) {
				if (l2 == 0)
					p = "Celeron (Covington)";
				else if (l2 == 256)
					p = "Mobile Pentium II (Dixon)";
			}
			break;
			
		case 6:
			if (l2 == 128)
				p = "Celeron (Mendocino)";
			else if (c->x86_mask == 0 || c->x86_mask == 5)
				p = "Celeron-A";
			break;
			
		case 8:
			if (l2 == 128)
				p = "Celeron (Coppermine)";
			break;
		}
	}

	if ( p )
		strcpy(c->x86_model_id, p);
	
	detect_ht(c);

	/* Work around errata */
	Intel_errata_workarounds(c);

#ifdef CONFIG_X86_INTEL_USERCOPY
	/*
	 * Set up the preferred alignment for movsl bulk memory moves
	 */
	switch (c->x86) {
	case 4:		/* 486: untested */
		break;
	case 5:		/* Old Pentia: untested */
		break;
	case 6:		/* PII/PIII only like movsl with 8-byte alignment */
		movsl_mask.mask = 7;
		break;
	case 15:	/* P4 is OK down to 8-byte alignment */
		movsl_mask.mask = 7;
		break;
	}
#endif

	if (c->x86 == 15) 
		set_bit(X86_FEATURE_P4, c->x86_capability);
	if (c->x86 == 6) 
		set_bit(X86_FEATURE_P3, c->x86_capability);
}


static unsigned int intel_size_cache(struct cpuinfo_x86 * c, unsigned int size)
{
	/* Intel PIII Tualatin. This comes in two flavours.
	 * One has 256kb of cache, the other 512. We have no way
	 * to determine which, so we use a boottime override
	 * for the 512kb model, and assume 256 otherwise.
	 */
	if ((c->x86 == 6) && (c->x86_model == 11) && (size == 0))
		size = 256;
	return size;
}

static struct cpu_dev intel_cpu_dev __initdata = {
	.c_vendor	= "Intel",
	.c_ident 	= { "GenuineIntel" },
	.c_models = {
		{ .vendor = X86_VENDOR_INTEL, .family = 4, .model_names = 
		  { 
			  [0] = "486 DX-25/33", 
			  [1] = "486 DX-50", 
			  [2] = "486 SX", 
			  [3] = "486 DX/2", 
			  [4] = "486 SL", 
			  [5] = "486 SX/2", 
			  [7] = "486 DX/2-WB", 
			  [8] = "486 DX/4", 
			  [9] = "486 DX/4-WB"
		  }
		},
		{ .vendor = X86_VENDOR_INTEL, .family = 5, .model_names =
		  { 
			  [0] = "Pentium 60/66 A-step", 
			  [1] = "Pentium 60/66", 
			  [2] = "Pentium 75 - 200",
			  [3] = "OverDrive PODP5V83", 
			  [4] = "Pentium MMX",
			  [7] = "Mobile Pentium 75 - 200", 
			  [8] = "Mobile Pentium MMX"
		  }
		},
		{ .vendor = X86_VENDOR_INTEL, .family = 6, .model_names =
		  { 
			  [0] = "Pentium Pro A-step",
			  [1] = "Pentium Pro", 
			  [3] = "Pentium II (Klamath)", 
			  [4] = "Pentium II (Deschutes)", 
			  [5] = "Pentium II (Deschutes)", 
			  [6] = "Mobile Pentium II",
			  [7] = "Pentium III (Katmai)", 
			  [8] = "Pentium III (Coppermine)", 
			  [10] = "Pentium III (Cascades)",
			  [11] = "Pentium III (Tualatin)",
		  }
		},
		{ .vendor = X86_VENDOR_INTEL, .family = 15, .model_names =
		  {
			  [0] = "Pentium 4 (Unknown)",
			  [1] = "Pentium 4 (Willamette)",
			  [2] = "Pentium 4 (Northwood)",
			  [4] = "Pentium 4 (Foster)",
			  [5] = "Pentium 4 (Foster)",
		  }
		},
	},
	.c_init		= init_intel,
	.c_identify	= generic_identify,
	.c_size_cache	= intel_size_cache,
};

__init int intel_cpu_init(void)
{
	cpu_devs[X86_VENDOR_INTEL] = &intel_cpu_dev;
	return 0;
}

// arch_initcall(intel_cpu_init);

