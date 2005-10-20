#include <linux/init.h>
#include <linux/bitops.h>
#include <linux/mm.h>
#include <asm/io.h>
#include <asm/processor.h>

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

static void __init init_amd(struct cpuinfo_x86 *c)
{
	u32 l, h;
	int mbytes = num_physpages >> (20-PAGE_SHIFT);
	int r;

#ifdef CONFIG_SMP
	unsigned long long value;

	/* Disable TLB flush filter by setting HWCR.FFDIS on K8
	 * bit 6 of msr C001_0015
	 *
	 * Errata 63 for SH-B3 steppings
	 * Errata 122 for all steppings (F+ have it disabled by default)
	 */
	if (c->x86 == 15) {
		rdmsrl(MSR_K7_HWCR, value);
		value |= 1 << 6;
		wrmsrl(MSR_K7_HWCR, value);
	}
#endif

	/*
	 *	FIXME: We should handle the K5 here. Set up the write
	 *	range and also turn on MSR 83 bits 4 and 31 (write alloc,
	 *	no bus pipeline)
	 */

	/* Bit 31 in normal CPUID used for nonstandard 3DNow ID;
	   3DNow is IDd by bit 31 in extended CPUID (1*32+31) anyway */
	clear_bit(0*32+31, c->x86_capability);
	
	r = get_model_name(c);

	switch(c->x86)
	{
		case 4:
		/*
		 * General Systems BIOSen alias the cpu frequency registers
		 * of the Elan at 0x000df000. Unfortuantly, one of the Linux
		 * drivers subsequently pokes it, and changes the CPU speed.
		 * Workaround : Remove the unneeded alias.
		 */
#define CBAR		(0xfffc) /* Configuration Base Address  (32-bit) */
#define CBAR_ENB	(0x80000000)
#define CBAR_KEY	(0X000000CB)
			if (c->x86_model==9 || c->x86_model == 10) {
				if (inl (CBAR) & CBAR_ENB)
					outl (0 | CBAR_KEY, CBAR);
			}
			break;
		case 5:
			if( c->x86_model < 6 )
			{
				/* Based on AMD doc 20734R - June 2000 */
				if ( c->x86_model == 0 ) {
					clear_bit(X86_FEATURE_APIC, c->x86_capability);
					set_bit(X86_FEATURE_PGE, c->x86_capability);
				}
				break;
			}
			
			if ( c->x86_model == 6 && c->x86_mask == 1 ) {
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
				
				/* Knock these two lines out if it debugs out ok */
				printk(KERN_INFO "AMD K6 stepping B detected - ");
				/* -- cut here -- */
				if (d > 20*K6_BUG_LOOP) 
					printk("system stability may be impaired when more than 32 MB are used.\n");
				else 
					printk("probably OK (after B9730xxxx).\n");
				printk(KERN_INFO "Please see http://membres.lycos.fr/poulot/k6bug.html\n");
			}

			/* K6 with old style WHCR */
			if (c->x86_model < 8 ||
			   (c->x86_model== 8 && c->x86_mask < 8)) {
				/* We can only write allocate on the low 508Mb */
				if(mbytes>508)
					mbytes=508;

				rdmsr(MSR_K6_WHCR, l, h);
				if ((l&0x0000FFFF)==0) {
					unsigned long flags;
					l=(1<<0)|((mbytes/4)<<1);
					local_irq_save(flags);
					wbinvd();
					wrmsr(MSR_K6_WHCR, l, h);
					local_irq_restore(flags);
					printk(KERN_INFO "Enabling old style K6 write allocation for %d Mb\n",
						mbytes);
				}
				break;
			}

			if ((c->x86_model == 8 && c->x86_mask >7) ||
			     c->x86_model == 9 || c->x86_model == 13) {
				/* The more serious chips .. */

				if(mbytes>4092)
					mbytes=4092;

				rdmsr(MSR_K6_WHCR, l, h);
				if ((l&0xFFFF0000)==0) {
					unsigned long flags;
					l=((mbytes>>2)<<22)|(1<<16);
					local_irq_save(flags);
					wbinvd();
					wrmsr(MSR_K6_WHCR, l, h);
					local_irq_restore(flags);
					printk(KERN_INFO "Enabling new style K6 write allocation for %d Mb\n",
						mbytes);
				}

				/*  Set MTRR capability flag if appropriate */
				if (c->x86_model == 13 || c->x86_model == 9 ||
				   (c->x86_model == 8 && c->x86_mask >= 8))
					set_bit(X86_FEATURE_K6_MTRR, c->x86_capability);
				break;
			}
			break;

		case 6: /* An Athlon/Duron */
 
			/* Bit 15 of Athlon specific MSR 15, needs to be 0
 			 * to enable SSE on Palomino/Morgan/Barton CPU's.
			 * If the BIOS didn't enable it already, enable it here.
			 */
			if (c->x86_model >= 6 && c->x86_model <= 10) {
				if (!cpu_has(c, X86_FEATURE_XMM)) {
					printk(KERN_INFO "Enabling disabled K7/SSE Support.\n");
					rdmsr(MSR_K7_HWCR, l, h);
					l &= ~0x00008000;
					wrmsr(MSR_K7_HWCR, l, h);
					set_bit(X86_FEATURE_XMM, c->x86_capability);
				}
			}

			/* It's been determined by AMD that Athlons since model 8 stepping 1
			 * are more robust with CLK_CTL set to 200xxxxx instead of 600xxxxx
			 * As per AMD technical note 27212 0.2
			 */
			if ((c->x86_model == 8 && c->x86_mask>=1) || (c->x86_model > 8)) {
				rdmsr(MSR_K7_CLK_CTL, l, h);
				if ((l & 0xfff00000) != 0x20000000) {
					printk ("CPU: CLK_CTL MSR was %x. Reprogramming to %x\n", l,
						((l & 0x000fffff)|0x20000000));
					wrmsr(MSR_K7_CLK_CTL, (l & 0x000fffff)|0x20000000, h);
				}
			}
			break;
	}

	switch (c->x86) {
	case 15:
		set_bit(X86_FEATURE_K8, c->x86_capability);
		break;
	case 6:
		set_bit(X86_FEATURE_K7, c->x86_capability); 
		break;
	}

	display_cacheinfo(c);

	if (cpuid_eax(0x80000000) >= 0x80000008) {
		c->x86_num_cores = (cpuid_ecx(0x80000008) & 0xff) + 1;
		if (c->x86_num_cores & (c->x86_num_cores - 1))
			c->x86_num_cores = 1;
	}

#ifdef CONFIG_X86_HT
	/*
	 * On a AMD dual core setup the lower bits of the APIC id
	 * distingush the cores.  Assumes number of cores is a power
	 * of two.
	 */
	if (c->x86_num_cores > 1) {
		int cpu = smp_processor_id();
		unsigned bits = 0;
		while ((1 << bits) < c->x86_num_cores)
			bits++;
		cpu_core_id[cpu] = phys_proc_id[cpu] & ((1<<bits)-1);
		phys_proc_id[cpu] >>= bits;
		printk(KERN_INFO "CPU %d(%d) -> Core %d\n",
		       cpu, c->x86_num_cores, cpu_core_id[cpu]);
	}
#endif
}

static unsigned int amd_size_cache(struct cpuinfo_x86 * c, unsigned int size)
{
	/* AMD errata T13 (order #21922) */
	if ((c->x86 == 6)) {
		if (c->x86_model == 3 && c->x86_mask == 0)	/* Duron Rev A0 */
			size = 64;
		if (c->x86_model == 4 &&
		    (c->x86_mask==0 || c->x86_mask==1))	/* Tbird rev A1/A2 */
			size = 256;
	}
	return size;
}

static struct cpu_dev amd_cpu_dev __initdata = {
	.c_vendor	= "AMD",
	.c_ident 	= { "AuthenticAMD" },
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
	.c_init		= init_amd,
	.c_identify	= generic_identify,
	.c_size_cache	= amd_size_cache,
};

int __init amd_init_cpu(void)
{
	cpu_devs[X86_VENDOR_AMD] = &amd_cpu_dev;
	return 0;
}

//early_arch_initcall(amd_init_cpu);
