#include <linux/init.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <asm/dma.h>
#include <asm/io.h>
#include <asm/processor.h>
#include <asm/timer.h>
#include <asm/pci-direct.h>
#include <asm/tsc.h>

#include "cpu.h"

/*
 * Read NSC/Cyrix DEVID registers (DIR) to get more detailed info. about the CPU
 */
static void __cpuinit do_cyrix_devid(unsigned char *dir0, unsigned char *dir1)
{
	unsigned char ccr2, ccr3;
	unsigned long flags;
	
	/* we test for DEVID by checking whether CCR3 is writable */
	local_irq_save(flags);
	ccr3 = getCx86(CX86_CCR3);
	setCx86(CX86_CCR3, ccr3 ^ 0x80);
	getCx86(0xc0);   /* dummy to change bus */

	if (getCx86(CX86_CCR3) == ccr3) {       /* no DEVID regs. */
		ccr2 = getCx86(CX86_CCR2);
		setCx86(CX86_CCR2, ccr2 ^ 0x04);
		getCx86(0xc0);  /* dummy */

		if (getCx86(CX86_CCR2) == ccr2) /* old Cx486SLC/DLC */
			*dir0 = 0xfd;
		else {                          /* Cx486S A step */
			setCx86(CX86_CCR2, ccr2);
			*dir0 = 0xfe;
		}
	}
	else {
		setCx86(CX86_CCR3, ccr3);  /* restore CCR3 */

		/* read DIR0 and DIR1 CPU registers */
		*dir0 = getCx86(CX86_DIR0);
		*dir1 = getCx86(CX86_DIR1);
	}
	local_irq_restore(flags);
}

/*
 * Cx86_dir0_msb is a HACK needed by check_cx686_cpuid/slop in bugs.h in
 * order to identify the Cyrix CPU model after we're out of setup.c
 *
 * Actually since bugs.h doesn't even reference this perhaps someone should
 * fix the documentation ???
 */
static unsigned char Cx86_dir0_msb __cpuinitdata = 0;

static char Cx86_model[][9] __cpuinitdata = {
	"Cx486", "Cx486", "5x86 ", "6x86", "MediaGX ", "6x86MX ",
	"M II ", "Unknown"
};
static char Cx486_name[][5] __cpuinitdata = {
	"SLC", "DLC", "SLC2", "DLC2", "SRx", "DRx",
	"SRx2", "DRx2"
};
static char Cx486S_name[][4] __cpuinitdata = {
	"S", "S2", "Se", "S2e"
};
static char Cx486D_name[][4] __cpuinitdata = {
	"DX", "DX2", "?", "?", "?", "DX4"
};
static char Cx86_cb[] __cpuinitdata = "?.5x Core/Bus Clock";
static char cyrix_model_mult1[] __cpuinitdata = "12??43";
static char cyrix_model_mult2[] __cpuinitdata = "12233445";

/*
 * Reset the slow-loop (SLOP) bit on the 686(L) which is set by some old
 * BIOSes for compatibility with DOS games.  This makes the udelay loop
 * work correctly, and improves performance.
 *
 * FIXME: our newer udelay uses the tsc. We don't need to frob with SLOP
 */

extern void calibrate_delay(void) __init;

static void __cpuinit check_cx686_slop(struct cpuinfo_x86 *c)
{
	unsigned long flags;
	
	if (Cx86_dir0_msb == 3) {
		unsigned char ccr3, ccr5;

		local_irq_save(flags);
		ccr3 = getCx86(CX86_CCR3);
		setCx86(CX86_CCR3, (ccr3 & 0x0f) | 0x10); /* enable MAPEN  */
		ccr5 = getCx86(CX86_CCR5);
		if (ccr5 & 2)
			setCx86(CX86_CCR5, ccr5 & 0xfd);  /* reset SLOP */
		setCx86(CX86_CCR3, ccr3);                 /* disable MAPEN */
		local_irq_restore(flags);

		if (ccr5 & 2) { /* possible wrong calibration done */
			printk(KERN_INFO "Recalibrating delay loop with SLOP bit reset\n");
			calibrate_delay();
			c->loops_per_jiffy = loops_per_jiffy;
		}
	}
}


static void __cpuinit set_cx86_reorder(void)
{
	u8 ccr3;

	printk(KERN_INFO "Enable Memory access reorder on Cyrix/NSC processor.\n");
	ccr3 = getCx86(CX86_CCR3);
	setCx86(CX86_CCR3, (ccr3 & 0x0f) | 0x10); /* enable MAPEN  */

	/* Load/Store Serialize to mem access disable (=reorder it)  */
	setCx86(CX86_PCR0, getCx86(CX86_PCR0) & ~0x80);
	/* set load/store serialize from 1GB to 4GB */
	ccr3 |= 0xe0;
	setCx86(CX86_CCR3, ccr3);
}

static void __cpuinit set_cx86_memwb(void)
{
	u32 cr0;

	printk(KERN_INFO "Enable Memory-Write-back mode on Cyrix/NSC processor.\n");

	/* CCR2 bit 2: unlock NW bit */
	setCx86(CX86_CCR2, getCx86(CX86_CCR2) & ~0x04);
	/* set 'Not Write-through' */
	cr0 = 0x20000000;
	write_cr0(read_cr0() | cr0);
	/* CCR2 bit 2: lock NW bit and set WT1 */
	setCx86(CX86_CCR2, getCx86(CX86_CCR2) | 0x14 );
}

static void __cpuinit set_cx86_inc(void)
{
	unsigned char ccr3;

	printk(KERN_INFO "Enable Incrementor on Cyrix/NSC processor.\n");

	ccr3 = getCx86(CX86_CCR3);
	setCx86(CX86_CCR3, (ccr3 & 0x0f) | 0x10); /* enable MAPEN  */
	/* PCR1 -- Performance Control */
	/* Incrementor on, whatever that is */
	setCx86(CX86_PCR1, getCx86(CX86_PCR1) | 0x02);
	/* PCR0 -- Performance Control */
	/* Incrementor Margin 10 */
	setCx86(CX86_PCR0, getCx86(CX86_PCR0) | 0x04); 
	setCx86(CX86_CCR3, ccr3);	/* disable MAPEN */
}

/*
 *	Configure later MediaGX and/or Geode processor.
 */

static void __cpuinit geode_configure(void)
{
	unsigned long flags;
	u8 ccr3;
	local_irq_save(flags);

	/* Suspend on halt power saving and enable #SUSP pin */
	setCx86(CX86_CCR2, getCx86(CX86_CCR2) | 0x88);

	ccr3 = getCx86(CX86_CCR3);
	setCx86(CX86_CCR3, (ccr3 & 0x0f) | 0x10);	/* enable MAPEN */
	

	/* FPU fast, DTE cache, Mem bypass */
	setCx86(CX86_CCR4, getCx86(CX86_CCR4) | 0x38);
	setCx86(CX86_CCR3, ccr3);			/* disable MAPEN */
	
	set_cx86_memwb();
	set_cx86_reorder();	
	set_cx86_inc();
	
	local_irq_restore(flags);
}


static void __cpuinit init_cyrix(struct cpuinfo_x86 *c)
{
	unsigned char dir0, dir0_msn, dir0_lsn, dir1 = 0;
	char *buf = c->x86_model_id;
	const char *p = NULL;

	/* Bit 31 in normal CPUID used for nonstandard 3DNow ID;
	   3DNow is IDd by bit 31 in extended CPUID (1*32+31) anyway */
	clear_bit(0*32+31, c->x86_capability);

	/* Cyrix used bit 24 in extended (AMD) CPUID for Cyrix MMX extensions */
	if ( test_bit(1*32+24, c->x86_capability) ) {
		clear_bit(1*32+24, c->x86_capability);
		set_bit(X86_FEATURE_CXMMX, c->x86_capability);
	}

	do_cyrix_devid(&dir0, &dir1);

	check_cx686_slop(c);

	Cx86_dir0_msb = dir0_msn = dir0 >> 4; /* identifies CPU "family"   */
	dir0_lsn = dir0 & 0xf;                /* model or clock multiplier */

	/* common case step number/rev -- exceptions handled below */
	c->x86_model = (dir1 >> 4) + 1;
	c->x86_mask = dir1 & 0xf;

	/* Now cook; the original recipe is by Channing Corn, from Cyrix.
	 * We do the same thing for each generation: we work out
	 * the model, multiplier and stepping.  Black magic included,
	 * to make the silicon step/rev numbers match the printed ones.
	 */
	 
	switch (dir0_msn) {
		unsigned char tmp;

	case 0: /* Cx486SLC/DLC/SRx/DRx */
		p = Cx486_name[dir0_lsn & 7];
		break;

	case 1: /* Cx486S/DX/DX2/DX4 */
		p = (dir0_lsn & 8) ? Cx486D_name[dir0_lsn & 5]
			: Cx486S_name[dir0_lsn & 3];
		break;

	case 2: /* 5x86 */
		Cx86_cb[2] = cyrix_model_mult1[dir0_lsn & 5];
		p = Cx86_cb+2;
		break;

	case 3: /* 6x86/6x86L */
		Cx86_cb[1] = ' ';
		Cx86_cb[2] = cyrix_model_mult1[dir0_lsn & 5];
		if (dir1 > 0x21) { /* 686L */
			Cx86_cb[0] = 'L';
			p = Cx86_cb;
			(c->x86_model)++;
		} else             /* 686 */
			p = Cx86_cb+1;
		/* Emulate MTRRs using Cyrix's ARRs. */
		set_bit(X86_FEATURE_CYRIX_ARR, c->x86_capability);
		/* 6x86's contain this bug */
		c->coma_bug = 1;
		break;

	case 4: /* MediaGX/GXm or Geode GXM/GXLV/GX1 */
#ifdef CONFIG_PCI
	{
		u32 vendor, device;
		/* It isn't really a PCI quirk directly, but the cure is the
		   same. The MediaGX has deep magic SMM stuff that handles the
		   SB emulation. It thows away the fifo on disable_dma() which
		   is wrong and ruins the audio. 

		   Bug2: VSA1 has a wrap bug so that using maximum sized DMA 
		   causes bad things. According to NatSemi VSA2 has another
		   bug to do with 'hlt'. I've not seen any boards using VSA2
		   and X doesn't seem to support it either so who cares 8).
		   VSA1 we work around however.
		*/

		printk(KERN_INFO "Working around Cyrix MediaGX virtual DMA bugs.\n");
		isa_dma_bridge_buggy = 2;

		/* We do this before the PCI layer is running. However we
		   are safe here as we know the bridge must be a Cyrix
		   companion and must be present */
		vendor = read_pci_config_16(0, 0, 0x12, PCI_VENDOR_ID);
		device = read_pci_config_16(0, 0, 0x12, PCI_DEVICE_ID);

		/*
		 *  The 5510/5520 companion chips have a funky PIT.
		 */  
		if (vendor == PCI_VENDOR_ID_CYRIX &&
	 (device == PCI_DEVICE_ID_CYRIX_5510 || device == PCI_DEVICE_ID_CYRIX_5520))
			mark_tsc_unstable("cyrix 5510/5520 detected");
	}
#endif
		c->x86_cache_size=16;	/* Yep 16K integrated cache thats it */

		/* GXm supports extended cpuid levels 'ala' AMD */
		if (c->cpuid_level == 2) {
			/* Enable cxMMX extensions (GX1 Datasheet 54) */
			setCx86(CX86_CCR7, getCx86(CX86_CCR7) | 1);
			
			/*
			 * GXm : 0x30 ... 0x5f GXm  datasheet 51
			 * GXlv: 0x6x          GXlv datasheet 54
			 *  ?  : 0x7x
			 * GX1 : 0x8x          GX1  datasheet 56
			 */
			if((0x30 <= dir1 && dir1 <= 0x6f) || (0x80 <=dir1 && dir1 <= 0x8f))
				geode_configure();
			get_model_name(c);  /* get CPU marketing name */
			return;
		}
		else {  /* MediaGX */
			Cx86_cb[2] = (dir0_lsn & 1) ? '3' : '4';
			p = Cx86_cb+2;
			c->x86_model = (dir1 & 0x20) ? 1 : 2;
		}
		break;

        case 5: /* 6x86MX/M II */
		if (dir1 > 7)
		{
			dir0_msn++;  /* M II */
			/* Enable MMX extensions (App note 108) */
			setCx86(CX86_CCR7, getCx86(CX86_CCR7)|1);
		}
		else
		{
			c->coma_bug = 1;      /* 6x86MX, it has the bug. */
		}
		tmp = (!(dir0_lsn & 7) || dir0_lsn & 1) ? 2 : 0;
		Cx86_cb[tmp] = cyrix_model_mult2[dir0_lsn & 7];
		p = Cx86_cb+tmp;
        	if (((dir1 & 0x0f) > 4) || ((dir1 & 0xf0) == 0x20))
			(c->x86_model)++;
		/* Emulate MTRRs using Cyrix's ARRs. */
		set_bit(X86_FEATURE_CYRIX_ARR, c->x86_capability);
		break;

	case 0xf:  /* Cyrix 486 without DEVID registers */
		switch (dir0_lsn) {
		case 0xd:  /* either a 486SLC or DLC w/o DEVID */
			dir0_msn = 0;
			p = Cx486_name[(c->hard_math) ? 1 : 0];
			break;

		case 0xe:  /* a 486S A step */
			dir0_msn = 0;
			p = Cx486S_name[0];
			break;
		}
		break;

	default:  /* unknown (shouldn't happen, we know everyone ;-) */
		dir0_msn = 7;
		break;
	}
	strcpy(buf, Cx86_model[dir0_msn & 7]);
	if (p) strcat(buf, p);
	return;
}

/*
 * Handle National Semiconductor branded processors
 */
static void __cpuinit init_nsc(struct cpuinfo_x86 *c)
{
	/* There may be GX1 processors in the wild that are branded
	 * NSC and not Cyrix.
	 *
	 * This function only handles the GX processor, and kicks every
	 * thing else to the Cyrix init function above - that should
	 * cover any processors that might have been branded differently
	 * after NSC acquired Cyrix.
	 *
	 * If this breaks your GX1 horribly, please e-mail
	 * info-linux@ldcmail.amd.com to tell us.
	 */

	/* Handle the GX (Formally known as the GX2) */

	if (c->x86 == 5 && c->x86_model == 5)
		display_cacheinfo(c);
	else
		init_cyrix(c);
}

/*
 * Cyrix CPUs without cpuid or with cpuid not yet enabled can be detected
 * by the fact that they preserve the flags across the division of 5/2.
 * PII and PPro exhibit this behavior too, but they have cpuid available.
 */
 
/*
 * Perform the Cyrix 5/2 test. A Cyrix won't change
 * the flags, while other 486 chips will.
 */
static inline int test_cyrix_52div(void)
{
	unsigned int test;

	__asm__ __volatile__(
	     "sahf\n\t"		/* clear flags (%eax = 0x0005) */
	     "div %b2\n\t"	/* divide 5 by 2 */
	     "lahf"		/* store flags into %ah */
	     : "=a" (test)
	     : "0" (5), "q" (2)
	     : "cc");

	/* AH is 0x02 on Cyrix after the divide.. */
	return (unsigned char) (test >> 8) == 0x02;
}

static void __cpuinit cyrix_identify(struct cpuinfo_x86 * c)
{
	/* Detect Cyrix with disabled CPUID */
	if ( c->x86 == 4 && test_cyrix_52div() ) {
		unsigned char dir0, dir1;
		
		strcpy(c->x86_vendor_id, "CyrixInstead");
	        c->x86_vendor = X86_VENDOR_CYRIX;
	        
	        /* Actually enable cpuid on the older cyrix */
	    
	    	/* Retrieve CPU revisions */
	    	
		do_cyrix_devid(&dir0, &dir1);

		dir0>>=4;		
		
		/* Check it is an affected model */
		
   	        if (dir0 == 5 || dir0 == 3)
   	        {
			unsigned char ccr3;
			unsigned long flags;
			printk(KERN_INFO "Enabling CPUID on Cyrix processor.\n");
			local_irq_save(flags);
			ccr3 = getCx86(CX86_CCR3);
			setCx86(CX86_CCR3, (ccr3 & 0x0f) | 0x10);       /* enable MAPEN  */
			setCx86(CX86_CCR4, getCx86(CX86_CCR4) | 0x80);  /* enable cpuid  */
			setCx86(CX86_CCR3, ccr3);                       /* disable MAPEN */
			local_irq_restore(flags);
		}
	}
}

static struct cpu_dev cyrix_cpu_dev __cpuinitdata = {
	.c_vendor	= "Cyrix",
	.c_ident 	= { "CyrixInstead" },
	.c_init		= init_cyrix,
	.c_identify	= cyrix_identify,
};

int __init cyrix_init_cpu(void)
{
	cpu_devs[X86_VENDOR_CYRIX] = &cyrix_cpu_dev;
	return 0;
}

static struct cpu_dev nsc_cpu_dev __cpuinitdata = {
	.c_vendor	= "NSC",
	.c_ident 	= { "Geode by NSC" },
	.c_init		= init_nsc,
};

int __init nsc_init_cpu(void)
{
	cpu_devs[X86_VENDOR_NSC] = &nsc_cpu_dev;
	return 0;
}

