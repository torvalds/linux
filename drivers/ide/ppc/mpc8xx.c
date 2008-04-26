/*
 *  Copyright (C) 2000, 2001 Wolfgang Denk, wd@denx.de
 *  Modified for direct IDE interface
 *	by Thomas Lange, thomas@corelatus.com
 *  Modified for direct IDE interface on 8xx without using the PCMCIA
 *  controller
 *	by Steven.Scholz@imc-berlin.de
 *  Moved out of arch/ppc/kernel/m8xx_setup.c, other minor cleanups
 *	by Mathew Locke <mattl@mvista.com>
 */

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/user.h>
#include <linux/tty.h>
#include <linux/major.h>
#include <linux/interrupt.h>
#include <linux/reboot.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/ide.h>
#include <linux/bootmem.h>

#include <asm/mpc8xx.h>
#include <asm/mmu.h>
#include <asm/processor.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/ide.h>
#include <asm/8xx_immap.h>
#include <asm/machdep.h>
#include <asm/irq.h>

#define DRV_NAME "ide-mpc8xx"

static int identify  (volatile u8 *p);
static void print_fixed (volatile u8 *p);
static void print_funcid (int func);
static int check_ide_device (unsigned long base);

static void ide_interrupt_ack (void *dev);
static void m8xx_ide_set_pio_mode(ide_drive_t *drive, const u8 pio);

typedef	struct ide_ioport_desc {
	unsigned long	base_off;		/* Offset to PCMCIA memory	*/
	unsigned long	reg_off[IDE_NR_PORTS];	/* controller register offsets	*/
	int		irq;			/* IRQ				*/
} ide_ioport_desc_t;

ide_ioport_desc_t ioport_dsc[MAX_HWIFS] = {
#ifdef IDE0_BASE_OFFSET
	{ IDE0_BASE_OFFSET,
	    {
		IDE0_DATA_REG_OFFSET,
		IDE0_ERROR_REG_OFFSET,
		IDE0_NSECTOR_REG_OFFSET,
		IDE0_SECTOR_REG_OFFSET,
		IDE0_LCYL_REG_OFFSET,
		IDE0_HCYL_REG_OFFSET,
		IDE0_SELECT_REG_OFFSET,
		IDE0_STATUS_REG_OFFSET,
		IDE0_CONTROL_REG_OFFSET,
		IDE0_IRQ_REG_OFFSET,
	    },
	    IDE0_INTERRUPT,
	},
#ifdef IDE1_BASE_OFFSET
	{ IDE1_BASE_OFFSET,
	    {
		IDE1_DATA_REG_OFFSET,
		IDE1_ERROR_REG_OFFSET,
		IDE1_NSECTOR_REG_OFFSET,
		IDE1_SECTOR_REG_OFFSET,
		IDE1_LCYL_REG_OFFSET,
		IDE1_HCYL_REG_OFFSET,
		IDE1_SELECT_REG_OFFSET,
		IDE1_STATUS_REG_OFFSET,
		IDE1_CONTROL_REG_OFFSET,
		IDE1_IRQ_REG_OFFSET,
	    },
	    IDE1_INTERRUPT,
	},
#endif /* IDE1_BASE_OFFSET */
#endif	/* IDE0_BASE_OFFSET */
};

ide_pio_timings_t ide_pio_clocks[6];
int hold_time[6] =  {30, 20, 15, 10, 10, 10 };   /* PIO Mode 5 with IORDY (nonstandard) */

/*
 * Warning: only 1 (ONE) PCMCIA slot supported here,
 * which must be correctly initialized by the firmware (PPCBoot).
 */
static int _slot_ = -1;			/* will be read from PCMCIA registers   */

/* Make clock cycles and always round up */
#define PCMCIA_MK_CLKS( t, T ) (( (t) * ((T)/1000000) + 999U ) / 1000U )

#define M8XX_PCMCIA_CD2(slot)      (0x10000000 >> (slot << 4))
#define M8XX_PCMCIA_CD1(slot)      (0x08000000 >> (slot << 4))

/*
 * The TQM850L hardware has two pins swapped! Grrrrgh!
 */
#ifdef	CONFIG_TQM850L
#define __MY_PCMCIA_GCRX_CXRESET	PCMCIA_GCRX_CXOE
#define __MY_PCMCIA_GCRX_CXOE		PCMCIA_GCRX_CXRESET
#else
#define __MY_PCMCIA_GCRX_CXRESET	PCMCIA_GCRX_CXRESET
#define __MY_PCMCIA_GCRX_CXOE		PCMCIA_GCRX_CXOE
#endif

#if defined(CONFIG_BLK_DEV_MPC8xx_IDE) && defined(CONFIG_IDE_8xx_PCCARD)
#define PCMCIA_SCHLVL IDE0_INTERRUPT	/* Status Change Interrupt Level	*/
static int pcmcia_schlvl = PCMCIA_SCHLVL;
#endif

/*
 * See include/linux/ide.h for definition of hw_regs_t (p, base)
 */

/*
 * m8xx_ide_init_ports() for a direct IDE interface _using_
 * MPC8xx's internal PCMCIA interface
 */
#if defined(CONFIG_IDE_8xx_PCCARD) || defined(CONFIG_IDE_8xx_DIRECT)
static int __init m8xx_ide_init_ports(hw_regs_t *hw, unsigned long data_port)
{
	unsigned long *p = hw->io_ports;
	int i;

	typedef struct {
		ulong br;
		ulong or;
	} pcmcia_win_t;
	volatile pcmcia_win_t *win;
	volatile pcmconf8xx_t *pcmp;

	uint *pgcrx;
	u32 pcmcia_phy_base;
	u32 pcmcia_phy_end;
	static unsigned long pcmcia_base = 0;
	unsigned long base;

	*p = 0;

	pcmp = (pcmconf8xx_t *)(&(((immap_t *)IMAP_ADDR)->im_pcmcia));

	if (!pcmcia_base) {
                /*
                 * Read out PCMCIA registers. Since the reset values
                 * are undefined, we sure hope that they have been
                 * set up by firmware
		 */

		/* Scan all registers for valid settings */
		pcmcia_phy_base = 0xFFFFFFFF;
		pcmcia_phy_end = 0;
		/* br0 is start of brX and orX regs */
		win = (pcmcia_win_t *) \
			(&(((immap_t *)IMAP_ADDR)->im_pcmcia.pcmc_pbr0));
		for (i = 0; i < 8; i++) {
			if (win->or & 1) {	/* This bank is marked as valid */
				if (win->br < pcmcia_phy_base) {
					pcmcia_phy_base = win->br;
				}
				if ((win->br + PCMCIA_MEM_SIZE) > pcmcia_phy_end) {
					pcmcia_phy_end  = win->br + PCMCIA_MEM_SIZE;
				}
				/* Check which slot that has been defined */
				_slot_ = (win->or >> 2) & 1;

			}					/* Valid bank */
			win++;
		}						/* for */

		printk ("PCMCIA slot %c: phys mem %08x...%08x (size %08x)\n",
			'A' + _slot_,
			pcmcia_phy_base, pcmcia_phy_end,
			pcmcia_phy_end - pcmcia_phy_base);

		if (!request_mem_region(pcmcia_phy_base,
					pcmcia_phy_end - pcmcia_phy_base,
					DRV_NAME)) {
			printk(KERN_ERR "%s: resources busy\n", DRV_NAME);
			return -EBUSY;
		}

		pcmcia_base=(unsigned long)ioremap(pcmcia_phy_base,
						   pcmcia_phy_end-pcmcia_phy_base);

#ifdef DEBUG
		printk ("PCMCIA virt base: %08lx\n", pcmcia_base);
#endif
		/* Compute clock cycles for PIO timings */
		for (i=0; i<6; ++i) {
			bd_t	*binfo = (bd_t *)__res;

			hold_time[i]   =
				PCMCIA_MK_CLKS (hold_time[i],
						binfo->bi_busfreq);
			ide_pio_clocks[i].setup_time  =
				PCMCIA_MK_CLKS (ide_pio_timings[i].setup_time,
						binfo->bi_busfreq);
			ide_pio_clocks[i].active_time =
				PCMCIA_MK_CLKS (ide_pio_timings[i].active_time,
						binfo->bi_busfreq);
			ide_pio_clocks[i].cycle_time  =
				PCMCIA_MK_CLKS (ide_pio_timings[i].cycle_time,
						binfo->bi_busfreq);
#if 0
			printk ("PIO mode %d timings: %d/%d/%d => %d/%d/%d\n",
				i,
				ide_pio_clocks[i].setup_time,
				ide_pio_clocks[i].active_time,
				ide_pio_clocks[i].hold_time,
				ide_pio_clocks[i].cycle_time,
				ide_pio_timings[i].setup_time,
				ide_pio_timings[i].active_time,
				ide_pio_timings[i].hold_time,
				ide_pio_timings[i].cycle_time);
#endif
		}
	}

	if (_slot_ == -1) {
		printk ("PCMCIA slot has not been defined! Using A as default\n");
		_slot_ = 0;
	}

#ifdef CONFIG_IDE_8xx_PCCARD

#ifdef DEBUG
	printk ("PIPR = 0x%08X  slot %c ==> mask = 0x%X\n",
		pcmp->pcmc_pipr,
		'A' + _slot_,
		M8XX_PCMCIA_CD1(_slot_) | M8XX_PCMCIA_CD2(_slot_) );
#endif /* DEBUG */

	if (pcmp->pcmc_pipr & (M8XX_PCMCIA_CD1(_slot_)|M8XX_PCMCIA_CD2(_slot_))) {
		printk ("No card in slot %c: PIPR=%08x\n",
			'A' + _slot_, (u32) pcmp->pcmc_pipr);
		return -ENODEV;		/* No card in slot */
	}

	check_ide_device (pcmcia_base);

#endif	/* CONFIG_IDE_8xx_PCCARD */

	base = pcmcia_base + ioport_dsc[data_port].base_off;
#ifdef DEBUG
	printk ("base: %08x + %08x = %08x\n",
			pcmcia_base, ioport_dsc[data_port].base_off, base);
#endif

	for (i = 0; i < IDE_NR_PORTS; ++i) {
#ifdef DEBUG
		printk ("port[%d]: %08x + %08x = %08x\n",
			i,
			base,
			ioport_dsc[data_port].reg_off[i],
			i, base + ioport_dsc[data_port].reg_off[i]);
#endif
	 	*p++ = base + ioport_dsc[data_port].reg_off[i];
	}

	hw->irq = ioport_dsc[data_port].irq;
	hw->ack_intr = (ide_ack_intr_t *)ide_interrupt_ack;

#ifdef CONFIG_IDE_8xx_PCCARD
	{
		unsigned int reg;

		if (_slot_)
			pgcrx = &((immap_t *) IMAP_ADDR)->im_pcmcia.pcmc_pgcrb;
		else
			pgcrx = &((immap_t *) IMAP_ADDR)->im_pcmcia.pcmc_pgcra;

		reg = *pgcrx;
		reg |= mk_int_int_mask (pcmcia_schlvl) << 24;
		reg |= mk_int_int_mask (pcmcia_schlvl) << 16;
		*pgcrx = reg;
	}
#endif	/* CONFIG_IDE_8xx_PCCARD */

	/* Enable Harddisk Interrupt,
	 * and make it edge sensitive
	 */
	/* (11-18) Set edge detect for irq, no wakeup from low power mode */
	((immap_t *)IMAP_ADDR)->im_siu_conf.sc_siel |=
					(0x80000000 >> ioport_dsc[data_port].irq);

#ifdef CONFIG_IDE_8xx_PCCARD
	/* Make sure we don't get garbage irq */
	((immap_t *) IMAP_ADDR)->im_pcmcia.pcmc_pscr = 0xFFFF;

	/* Enable falling edge irq */
	pcmp->pcmc_per = 0x100000 >> (16 * _slot_);
#endif	/* CONFIG_IDE_8xx_PCCARD */

	return 0;
}
#endif /* CONFIG_IDE_8xx_PCCARD || CONFIG_IDE_8xx_DIRECT */

/*
 * m8xx_ide_init_ports() for a direct IDE interface _not_ using
 * MPC8xx's internal PCMCIA interface
 */
#if defined(CONFIG_IDE_EXT_DIRECT)
static int __init m8xx_ide_init_ports(hw_regs_t *hw, unsigned long data_port)
{
	unsigned long *p = hw->io_ports;
	int i;

	u32 ide_phy_base;
	u32 ide_phy_end;
	static unsigned long ide_base = 0;
	unsigned long base;

	*p = 0;

	if (!ide_base) {

		/* TODO:
		 * - add code to read ORx, BRx
		 */
		ide_phy_base = CFG_ATA_BASE_ADDR;
		ide_phy_end  = CFG_ATA_BASE_ADDR + 0x200;

		printk ("IDE phys mem : %08x...%08x (size %08x)\n",
			ide_phy_base, ide_phy_end,
			ide_phy_end - ide_phy_base);

		if (!request_mem_region(ide_phy_base, 0x200, DRV_NAME)) {
			printk(KERN_ERR "%s: resources busy\n", DRV_NAME);
			return -EBUSY;
		}

		ide_base=(unsigned long)ioremap(ide_phy_base,
						ide_phy_end-ide_phy_base);

#ifdef DEBUG
		printk ("IDE virt base: %08lx\n", ide_base);
#endif
	}

	base = ide_base + ioport_dsc[data_port].base_off;
#ifdef DEBUG
	printk ("base: %08x + %08x = %08x\n",
		ide_base, ioport_dsc[data_port].base_off, base);
#endif

	for (i = 0; i < IDE_NR_PORTS; ++i) {
#ifdef DEBUG
		printk ("port[%d]: %08x + %08x = %08x\n",
			i,
			base,
			ioport_dsc[data_port].reg_off[i],
			i, base + ioport_dsc[data_port].reg_off[i]);
#endif
	 	*p++ = base + ioport_dsc[data_port].reg_off[i];
	}

	/* direct connected IDE drive, i.e. external IRQ */
	hw->irq = ioport_dsc[data_port].irq;
	hw->ack_intr = (ide_ack_intr_t *)ide_interrupt_ack;

	/* Enable Harddisk Interrupt,
	 * and make it edge sensitive
	 */
	/* (11-18) Set edge detect for irq, no wakeup from low power mode */
	((immap_t *) IMAP_ADDR)->im_siu_conf.sc_siel |=
			(0x80000000 >> ioport_dsc[data_port].irq);

	return 0;
}
#endif	/* CONFIG_IDE_8xx_DIRECT */


/* -------------------------------------------------------------------- */


/* PCMCIA Timing */
#ifndef	PCMCIA_SHT
#define PCMCIA_SHT(t)	((t & 0x0F)<<16)	/* Strobe Hold  Time 	*/
#define PCMCIA_SST(t)	((t & 0x0F)<<12)	/* Strobe Setup Time	*/
#define PCMCIA_SL(t) ((t==32) ? 0 : ((t & 0x1F)<<7)) /* Strobe Length	*/
#endif

/* Calculate PIO timings */
static void m8xx_ide_set_pio_mode(ide_drive_t *drive, const u8 pio)
{
#if defined(CONFIG_IDE_8xx_PCCARD) || defined(CONFIG_IDE_8xx_DIRECT)
	volatile pcmconf8xx_t	*pcmp;
	ulong timing, mask, reg;

	pcmp = (pcmconf8xx_t *)(&(((immap_t *)IMAP_ADDR)->im_pcmcia));

	mask = ~(PCMCIA_SHT(0xFF) | PCMCIA_SST(0xFF) | PCMCIA_SL(0xFF));

	timing  = PCMCIA_SHT(hold_time[pio]  )
		| PCMCIA_SST(ide_pio_clocks[pio].setup_time )
		| PCMCIA_SL (ide_pio_clocks[pio].active_time)
		;

#if 1
	printk ("Setting timing bits 0x%08lx in PCMCIA controller\n", timing);
#endif
	if ((reg = pcmp->pcmc_por0 & mask) != 0)
		pcmp->pcmc_por0 = reg | timing;

	if ((reg = pcmp->pcmc_por1 & mask) != 0)
		pcmp->pcmc_por1 = reg | timing;

	if ((reg = pcmp->pcmc_por2 & mask) != 0)
		pcmp->pcmc_por2 = reg | timing;

	if ((reg = pcmp->pcmc_por3 & mask) != 0)
		pcmp->pcmc_por3 = reg | timing;

	if ((reg = pcmp->pcmc_por4 & mask) != 0)
		pcmp->pcmc_por4 = reg | timing;

	if ((reg = pcmp->pcmc_por5 & mask) != 0)
		pcmp->pcmc_por5 = reg | timing;

	if ((reg = pcmp->pcmc_por6 & mask) != 0)
		pcmp->pcmc_por6 = reg | timing;

	if ((reg = pcmp->pcmc_por7 & mask) != 0)
		pcmp->pcmc_por7 = reg | timing;

#elif defined(CONFIG_IDE_EXT_DIRECT)

	printk("%s[%d] %s: not implemented yet!\n",
		__FILE__,__LINE__,__FUNCTION__);
#endif /* defined(CONFIG_IDE_8xx_PCCARD) || defined(CONFIG_IDE_8xx_PCMCIA */
}

static void
ide_interrupt_ack (void *dev)
{
#ifdef CONFIG_IDE_8xx_PCCARD
	u_int pscr, pipr;

#if (PCMCIA_SOCKETS_NO == 2)
	u_int _slot_;
#endif

	/* get interrupt sources */

	pscr = ((volatile immap_t *)IMAP_ADDR)->im_pcmcia.pcmc_pscr;
	pipr = ((volatile immap_t *)IMAP_ADDR)->im_pcmcia.pcmc_pipr;

	/*
	 * report only if both card detect signals are the same
	 * not too nice done,
	 * we depend on that CD2 is the bit to the left of CD1...
	 */

	if(_slot_==-1){
	  printk("PCMCIA slot has not been defined! Using A as default\n");
	  _slot_=0;
	}

	if(((pipr & M8XX_PCMCIA_CD2(_slot_)) >> 1) ^
	   (pipr & M8XX_PCMCIA_CD1(_slot_))         ) {
	  printk ("card detect interrupt\n");
	}
	/* clear the interrupt sources */
	((immap_t *)IMAP_ADDR)->im_pcmcia.pcmc_pscr = pscr;

#else /* ! CONFIG_IDE_8xx_PCCARD */
	/*
	 * Only CONFIG_IDE_8xx_PCCARD is using the interrupt of the
	 * MPC8xx's PCMCIA controller, so there is nothing to be done here
	 * for CONFIG_IDE_8xx_DIRECT and CONFIG_IDE_EXT_DIRECT.
	 * The interrupt is handled somewhere else.	-- Steven
	 */
#endif /* CONFIG_IDE_8xx_PCCARD */
}



/*
 * CIS Tupel codes
 */
#define CISTPL_NULL		0x00
#define CISTPL_DEVICE		0x01
#define CISTPL_LONGLINK_CB	0x02
#define CISTPL_INDIRECT		0x03
#define CISTPL_CONFIG_CB	0x04
#define CISTPL_CFTABLE_ENTRY_CB 0x05
#define CISTPL_LONGLINK_MFC	0x06
#define CISTPL_BAR		0x07
#define CISTPL_PWR_MGMNT	0x08
#define CISTPL_EXTDEVICE	0x09
#define CISTPL_CHECKSUM		0x10
#define CISTPL_LONGLINK_A	0x11
#define CISTPL_LONGLINK_C	0x12
#define CISTPL_LINKTARGET	0x13
#define CISTPL_NO_LINK		0x14
#define CISTPL_VERS_1		0x15
#define CISTPL_ALTSTR		0x16
#define CISTPL_DEVICE_A		0x17
#define CISTPL_JEDEC_C		0x18
#define CISTPL_JEDEC_A		0x19
#define CISTPL_CONFIG		0x1a
#define CISTPL_CFTABLE_ENTRY	0x1b
#define CISTPL_DEVICE_OC	0x1c
#define CISTPL_DEVICE_OA	0x1d
#define CISTPL_DEVICE_GEO	0x1e
#define CISTPL_DEVICE_GEO_A	0x1f
#define CISTPL_MANFID		0x20
#define CISTPL_FUNCID		0x21
#define CISTPL_FUNCE		0x22
#define CISTPL_SWIL		0x23
#define CISTPL_END		0xff

/*
 * CIS Function ID codes
 */
#define CISTPL_FUNCID_MULTI	0x00
#define CISTPL_FUNCID_MEMORY	0x01
#define CISTPL_FUNCID_SERIAL	0x02
#define CISTPL_FUNCID_PARALLEL	0x03
#define CISTPL_FUNCID_FIXED	0x04
#define CISTPL_FUNCID_VIDEO	0x05
#define CISTPL_FUNCID_NETWORK	0x06
#define CISTPL_FUNCID_AIMS	0x07
#define CISTPL_FUNCID_SCSI	0x08

/*
 * Fixed Disk FUNCE codes
 */
#define CISTPL_IDE_INTERFACE	0x01

#define CISTPL_FUNCE_IDE_IFACE	0x01
#define CISTPL_FUNCE_IDE_MASTER	0x02
#define CISTPL_FUNCE_IDE_SLAVE	0x03

/* First feature byte */
#define CISTPL_IDE_SILICON	0x04
#define CISTPL_IDE_UNIQUE	0x08
#define CISTPL_IDE_DUAL		0x10

/* Second feature byte */
#define CISTPL_IDE_HAS_SLEEP	0x01
#define CISTPL_IDE_HAS_STANDBY	0x02
#define CISTPL_IDE_HAS_IDLE	0x04
#define CISTPL_IDE_LOW_POWER	0x08
#define CISTPL_IDE_REG_INHIBIT	0x10
#define CISTPL_IDE_HAS_INDEX	0x20
#define CISTPL_IDE_IOIS16	0x40


/* -------------------------------------------------------------------- */


#define	MAX_TUPEL_SZ	512
#define MAX_FEATURES	4

static int check_ide_device (unsigned long base)
{
	volatile u8 *ident = NULL;
	volatile u8 *feature_p[MAX_FEATURES];
	volatile u8 *p, *start;
	int n_features = 0;
	u8 func_id = ~0;
	u8 code, len;
	unsigned short config_base = 0;
	int found = 0;
	int i;

#ifdef DEBUG
	printk ("PCMCIA MEM: %08lX\n", base);
#endif
	start = p = (volatile u8 *) base;

	while ((p - start) < MAX_TUPEL_SZ) {

		code = *p; p += 2;

		if (code == 0xFF) { /* End of chain */
			break;
		}

		len = *p; p += 2;
#ifdef	DEBUG_PCMCIA
		{ volatile u8 *q = p;
			printk ("\nTuple code %02x  length %d\n\tData:",
				code, len);

			for (i = 0; i < len; ++i) {
				printk (" %02x", *q);
				q+= 2;
			}
		}
#endif	/* DEBUG_PCMCIA */
		switch (code) {
		case CISTPL_VERS_1:
			ident = p + 4;
			break;
		case CISTPL_FUNCID:
			func_id = *p;
			break;
		case CISTPL_FUNCE:
			if (n_features < MAX_FEATURES)
				feature_p[n_features++] = p;
			break;
		case CISTPL_CONFIG:
			config_base = (*(p+6) << 8) + (*(p+4));
		default:
			break;
		}
		p += 2 * len;
	}

	found = identify (ident);

	if (func_id != ((u8)~0)) {
		print_funcid (func_id);

		if (func_id == CISTPL_FUNCID_FIXED)
			found = 1;
		else
			return (1);	/* no disk drive */
	}

	for (i=0; i<n_features; ++i) {
		print_fixed (feature_p[i]);
	}

	if (!found) {
		printk ("unknown card type\n");
		return (1);
	}

	/* set level mode irq and I/O mapped device in config reg*/
	*((u8 *)(base + config_base)) = 0x41;

	return (0);
}

/* ------------------------------------------------------------------------- */

static void print_funcid (int func)
{
	switch (func) {
	case CISTPL_FUNCID_MULTI:
		printk (" Multi-Function");
		break;
	case CISTPL_FUNCID_MEMORY:
		printk (" Memory");
		break;
	case CISTPL_FUNCID_SERIAL:
		printk (" Serial Port");
		break;
	case CISTPL_FUNCID_PARALLEL:
		printk (" Parallel Port");
		break;
	case CISTPL_FUNCID_FIXED:
		printk (" Fixed Disk");
		break;
	case CISTPL_FUNCID_VIDEO:
		printk (" Video Adapter");
		break;
	case CISTPL_FUNCID_NETWORK:
		printk (" Network Adapter");
		break;
	case CISTPL_FUNCID_AIMS:
		printk (" AIMS Card");
		break;
	case CISTPL_FUNCID_SCSI:
		printk (" SCSI Adapter");
		break;
	default:
		printk (" Unknown");
		break;
	}
	printk (" Card\n");
}

/* ------------------------------------------------------------------------- */

static void print_fixed (volatile u8 *p)
{
	if (p == NULL)
		return;

	switch (*p) {
	case CISTPL_FUNCE_IDE_IFACE:
	    {   u8 iface = *(p+2);

		printk ((iface == CISTPL_IDE_INTERFACE) ? " IDE" : " unknown");
		printk (" interface ");
		break;
	    }
	case CISTPL_FUNCE_IDE_MASTER:
	case CISTPL_FUNCE_IDE_SLAVE:
	    {   u8 f1 = *(p+2);
		u8 f2 = *(p+4);

		printk ((f1 & CISTPL_IDE_SILICON) ? " [silicon]" : " [rotating]");

		if (f1 & CISTPL_IDE_UNIQUE)
			printk (" [unique]");

		printk ((f1 & CISTPL_IDE_DUAL) ? " [dual]" : " [single]");

		if (f2 & CISTPL_IDE_HAS_SLEEP)
			printk (" [sleep]");

		if (f2 & CISTPL_IDE_HAS_STANDBY)
			printk (" [standby]");

		if (f2 & CISTPL_IDE_HAS_IDLE)
			printk (" [idle]");

		if (f2 & CISTPL_IDE_LOW_POWER)
			printk (" [low power]");

		if (f2 & CISTPL_IDE_REG_INHIBIT)
			printk (" [reg inhibit]");

		if (f2 & CISTPL_IDE_HAS_INDEX)
			printk (" [index]");

		if (f2 & CISTPL_IDE_IOIS16)
			printk (" [IOis16]");

		break;
	    }
	}
	printk ("\n");
}

/* ------------------------------------------------------------------------- */


#define MAX_IDENT_CHARS		64
#define	MAX_IDENT_FIELDS	4

static u8 *known_cards[] = {
	"ARGOSY PnPIDE D5",
	NULL
};

static int identify  (volatile u8 *p)
{
	u8 id_str[MAX_IDENT_CHARS];
	u8 data;
	u8 *t;
	u8 **card;
	int i, done;

	if (p == NULL)
		return (0);	/* Don't know */

	t = id_str;
	done =0;

	for (i=0; i<=4 && !done; ++i, p+=2) {
		while ((data = *p) != '\0') {
			if (data == 0xFF) {
				done = 1;
				break;
			}
			*t++ = data;
			if (t == &id_str[MAX_IDENT_CHARS-1]) {
				done = 1;
				break;
			}
			p += 2;
		}
		if (!done)
			*t++ = ' ';
	}
	*t = '\0';
	while (--t > id_str) {
		if (*t == ' ')
			*t = '\0';
		else
			break;
	}
	printk ("Card ID: %s\n", id_str);

	for (card=known_cards; *card; ++card) {
		if (strcmp(*card, id_str) == 0) {	/* found! */
			return (1);
		}
	}

	return (0);	/* don't know */
}

static int __init mpc8xx_ide_probe(void)
{
	hw_regs_t hw;
	u8 idx[4] = { 0xff, 0xff, 0xff, 0xff };

#ifdef IDE0_BASE_OFFSET
	memset(&hw, 0, sizeof(hw));
	if (!m8xx_ide_init_ports(&hw, 0)) {
		ide_hwif_t *hwif = &ide_hwifs[0];

		ide_init_port_hw(hwif, &hw);
		hwif->mmio = 1;
		hwif->pio_mask = ATA_PIO4;
		hwif->set_pio_mode = m8xx_ide_set_pio_mode;

		idx[0] = 0;
	}
#ifdef IDE1_BASE_OFFSET
	memset(&hw, 0, sizeof(hw));
	if (!m8xx_ide_init_ports(&hw, 1)) {
		ide_hwif_t *mate = &ide_hwifs[1];

		ide_init_port_hw(mate, &hw);
		mate->mmio = 1;
		mate->pio_mask = ATA_PIO4;
		mate->set_pio_mode = m8xx_ide_set_pio_mode;

		idx[1] = 1;
	}
#endif
#endif

	ide_device_add(idx, NULL);

	return 0;
}

module_init(mpc8xx_ide_probe);

MODULE_LICENSE("GPL");
