/*****************************************************************************
* sdladrv.c	SDLA Support Module.  Main module.
*
*		This module is a library of common hardware-specific functions
*		used by all Sangoma drivers.
*
* Author:	Gideon Hack	
*
* Copyright:	(c) 1995-2000 Sangoma Technologies Inc.
*
*		This program is free software; you can redistribute it and/or
*		modify it under the terms of the GNU General Public License
*		as published by the Free Software Foundation; either version
*		2 of the License, or (at your option) any later version.
* ============================================================================
* Mar 20, 2001  Nenad Corbic	Added the auto_pci_cfg filed, to support
*                               the PCISLOT #0. 
* Apr 04, 2000  Nenad Corbic	Fixed the auto memory detection code.
*                               The memory test at address 0xC8000.
* Mar 09, 2000  Nenad Corbic 	Added Gideon's Bug Fix: clear pci
*                               interrupt flags on initial load.
* Jun 02, 1999  Gideon Hack     Added support for the S514 adapter.
*				Updates for Linux 2.2.X kernels.	
* Sep 17, 1998	Jaspreet Singh	Updates for linux 2.2.X kernels
* Dec 20, 1996	Gene Kozin	Version 3.0.0. Complete overhaul.
* Jul 12, 1996	Gene Kozin	Changes for Linux 2.0 compatibility.
* Jun 12, 1996	Gene Kozin 	Added support for S503 card.
* Apr 30, 1996	Gene Kozin	SDLA hardware interrupt is acknowledged before
*				calling protocolspecific ISR.
*				Register I/O ports with Linux kernel.
*				Miscellaneous bug fixes.
* Dec 20, 1995	Gene Kozin	Fixed a bug in interrupt routine.
* Oct 14, 1995	Gene Kozin	Initial version.
*****************************************************************************/

/*****************************************************************************
 * Notes:
 * ------
 * 1. This code is ment to be system-independent (as much as possible).  To
 *    achive this, various macros are used to hide system-specific interfaces.
 *    To compile this code, one of the following constants must be defined:
 *
 *	Platform	Define
 *	--------	------
 *	Linux		_LINUX_
 *	SCO Unix	_SCO_UNIX_
 *
 * 2. Supported adapter types:
 *
 *	S502A
 *	ES502A (S502E)
 *	S503
 *	S507
 *	S508 (S509)
 *
 * 3. S502A Notes:
 *
 *	There is no separate DPM window enable/disable control in S502A.  It
 *	opens immediately after a window number it written to the HMCR
 *	register.  To close the window, HMCR has to be written a value
 *	????1111b (e.g. 0x0F or 0xFF).
 *
 *	S502A DPM window cannot be located at offset E000 (e.g. 0xAE000).
 *
 *	There should be a delay of ??? before reading back S502A status
 *	register.
 *
 * 4. S502E Notes:
 *
 *	S502E has a h/w bug: although default IRQ line state is HIGH, enabling
 *	interrupts by setting bit 1 of the control register (BASE) to '1'
 *	causes it to go LOW! Therefore, disabling interrupts by setting that
 *	bit to '0' causes low-to-high transition on IRQ line (ghosty
 *	interrupt). The same occurs when disabling CPU by resetting bit 0 of
 *	CPU control register (BASE+3) - see the next note.
 *
 *	S502E CPU and DPM control is limited:
 *
 *	o CPU cannot be stopped independently. Resetting bit 0 of the CPUi
 *	  control register (BASE+3) shuts the board down entirely, including
 *	  DPM;
 *
 *	o DPM access cannot be controlled dynamically. Ones CPU is started,
 *	  bit 1 of the control register (BASE) is used to enable/disable IRQ,
 *	  so that access to shared memory cannot be disabled while CPU is
 *	  running.
 ****************************************************************************/

#define	_LINUX_

#if	defined(_LINUX_)	/****** Linux *******************************/

#include <linux/config.h>
#include <linux/kernel.h>	/* printk(), and other useful stuff */
#include <linux/stddef.h>	/* offsetof(), etc. */
#include <linux/errno.h>	/* return codes */
#include <linux/string.h>	/* inline memset(), etc. */
#include <linux/module.h>	/* support for loadable modules */
#include <linux/jiffies.h>	/* for jiffies, HZ, etc. */
#include <linux/sdladrv.h>	/* API definitions */
#include <linux/sdlasfm.h>	/* SDLA firmware module definitions */
#include <linux/sdlapci.h>	/* SDLA PCI hardware definitions */
#include <linux/pci.h>		/* PCI defines and function prototypes */
#include <asm/io.h>		/* for inb(), outb(), etc. */

#define _INB(port)		(inb(port))
#define _OUTB(port, byte)	(outb((byte),(port)))
#define	SYSTEM_TICK		jiffies

#include <linux/init.h>


#elif	defined(_SCO_UNIX_)	/****** SCO Unix ****************************/

#if	!defined(INKERNEL)
#error	This code MUST be compiled in kernel mode!
#endif
#include <sys/sdladrv.h>	/* API definitions */
#include <sys/sdlasfm.h>	/* SDLA firmware module definitions */
#include <sys/inline.h>		/* for inb(), outb(), etc. */
#define _INB(port)		(inb(port))
#define _OUTB(port, byte)	(outb((port),(byte)))
#define	SYSTEM_TICK		lbolt

#else
#error	Unknown system type!
#endif

#define	MOD_VERSION	3
#define	MOD_RELEASE	0

#define	SDLA_IODELAY	100	/* I/O Rd/Wr delay, 10 works for 486DX2-66 */
#define	EXEC_DELAY	20	/* shared memory access delay, mks */
#define	EXEC_TIMEOUT	(HZ*2)	/* command timeout, in ticks */

/* I/O port address range */
#define S502A_IORANGE	3
#define S502E_IORANGE	4
#define S503_IORANGE	3
#define S507_IORANGE	4
#define S508_IORANGE	4

/* Maximum amount of memory */
#define S502_MAXMEM	0x10000L
#define S503_MAXMEM	0x10000L
#define S507_MAXMEM	0x40000L
#define S508_MAXMEM	0x40000L

/* Minimum amount of memory */
#define S502_MINMEM	0x8000L
#define S503_MINMEM	0x8000L
#define S507_MINMEM	0x20000L
#define S508_MINMEM	0x20000L
#define NO_PORT         -1





/****** Function Prototypes *************************************************/

/* Hardware-specific functions */
static int sdla_detect	(sdlahw_t* hw);
static int sdla_autodpm	(sdlahw_t* hw);
static int sdla_setdpm	(sdlahw_t* hw);
static int sdla_load	(sdlahw_t* hw, sfm_t* sfm, unsigned len);
static int sdla_init	(sdlahw_t* hw);
static unsigned long sdla_memtest (sdlahw_t* hw);
static int sdla_bootcfg	(sdlahw_t* hw, sfm_info_t* sfminfo);
static unsigned char make_config_byte (sdlahw_t* hw);
static int sdla_start	(sdlahw_t* hw, unsigned addr);

static int init_s502a	(sdlahw_t* hw);
static int init_s502e	(sdlahw_t* hw);
static int init_s503	(sdlahw_t* hw);
static int init_s507	(sdlahw_t* hw);
static int init_s508	(sdlahw_t* hw);
            
static int detect_s502a	(int port);
static int detect_s502e	(int port);
static int detect_s503	(int port);
static int detect_s507	(int port);
static int detect_s508	(int port);
static int detect_s514  (sdlahw_t* hw);
static int find_s514_adapter(sdlahw_t* hw, char find_first_S514_card);

/* Miscellaneous functions */
static void peek_by_4 (unsigned long src, void* buf, unsigned len);
static void poke_by_4 (unsigned long dest, void* buf, unsigned len);
static int calibrate_delay (int mks);
static int get_option_index (unsigned* optlist, unsigned optval);
static unsigned check_memregion (void* ptr, unsigned len);
static unsigned	test_memregion (void* ptr, unsigned len);
static unsigned short checksum (unsigned char* buf, unsigned len);
static int init_pci_slot(sdlahw_t *);

static int pci_probe(sdlahw_t *hw);

/****** Global Data **********************************************************
 * Note: All data must be explicitly initialized!!!
 */

static struct pci_device_id sdladrv_pci_tbl[] = {
	{ V3_VENDOR_ID, V3_DEVICE_ID, PCI_ANY_ID, PCI_ANY_ID, },
	{ }			/* Terminating entry */
};
MODULE_DEVICE_TABLE(pci, sdladrv_pci_tbl);

MODULE_LICENSE("GPL");

/* private data */
static char modname[]	= "sdladrv";
static char fullname[]	= "SDLA Support Module";
static char copyright[]	= "(c) 1995-1999 Sangoma Technologies Inc.";
static unsigned	exec_idle;

/* Hardware configuration options.
 * These are arrays of configuration options used by verification routines.
 * The first element of each array is its size (i.e. number of options).
 */
static unsigned	s502_port_options[] =
	{ 4, 0x250, 0x300, 0x350, 0x360 }
;
static unsigned	s503_port_options[] =
	{ 8, 0x250, 0x254, 0x300, 0x304, 0x350, 0x354, 0x360, 0x364 }
;
static unsigned	s508_port_options[] =
	{ 8, 0x250, 0x270, 0x280, 0x300, 0x350, 0x360, 0x380, 0x390 }
;

static unsigned s502a_irq_options[] = { 0 };
static unsigned s502e_irq_options[] = { 4, 2, 3, 5, 7 };
static unsigned s503_irq_options[]  = { 5, 2, 3, 4, 5, 7 };
static unsigned s508_irq_options[]  = { 8, 3, 4, 5, 7, 10, 11, 12, 15 };

static unsigned s502a_dpmbase_options[] =
{
	28,
	0xA0000, 0xA2000, 0xA4000, 0xA6000, 0xA8000, 0xAA000, 0xAC000,
	0xC0000, 0xC2000, 0xC4000, 0xC6000, 0xC8000, 0xCA000, 0xCC000,
	0xD0000, 0xD2000, 0xD4000, 0xD6000, 0xD8000, 0xDA000, 0xDC000,
	0xE0000, 0xE2000, 0xE4000, 0xE6000, 0xE8000, 0xEA000, 0xEC000,
};
static unsigned s507_dpmbase_options[] =
{
	32,
	0xA0000, 0xA2000, 0xA4000, 0xA6000, 0xA8000, 0xAA000, 0xAC000, 0xAE000,
	0xB0000, 0xB2000, 0xB4000, 0xB6000, 0xB8000, 0xBA000, 0xBC000, 0xBE000,
	0xC0000, 0xC2000, 0xC4000, 0xC6000, 0xC8000, 0xCA000, 0xCC000, 0xCE000,
	0xE0000, 0xE2000, 0xE4000, 0xE6000, 0xE8000, 0xEA000, 0xEC000, 0xEE000,
};
static unsigned s508_dpmbase_options[] =	/* incl. S502E and S503 */
{
	32,
	0xA0000, 0xA2000, 0xA4000, 0xA6000, 0xA8000, 0xAA000, 0xAC000, 0xAE000,
	0xC0000, 0xC2000, 0xC4000, 0xC6000, 0xC8000, 0xCA000, 0xCC000, 0xCE000,
	0xD0000, 0xD2000, 0xD4000, 0xD6000, 0xD8000, 0xDA000, 0xDC000, 0xDE000,
	0xE0000, 0xE2000, 0xE4000, 0xE6000, 0xE8000, 0xEA000, 0xEC000, 0xEE000,
};

/*
static unsigned	s502_dpmsize_options[] = { 2, 0x2000, 0x10000 };
static unsigned	s507_dpmsize_options[] = { 2, 0x2000, 0x4000 };
static unsigned	s508_dpmsize_options[] = { 1, 0x2000 };
*/

static unsigned	s502a_pclk_options[] = { 2, 3600, 7200 };
static unsigned	s502e_pclk_options[] = { 5, 3600, 5000, 7200, 8000, 10000 };
static unsigned	s503_pclk_options[]  = { 3, 7200, 8000, 10000 };
static unsigned	s507_pclk_options[]  = { 1, 12288 };
static unsigned	s508_pclk_options[]  = { 1, 16000 };

/* Host memory control register masks */
static unsigned char s502a_hmcr[] =
{
	0x10, 0x12, 0x14, 0x16, 0x18, 0x1A, 0x1C,	/* A0000 - AC000 */
	0x20, 0x22, 0x24, 0x26, 0x28, 0x2A, 0x2C,	/* C0000 - CC000 */
	0x00, 0x02, 0x04, 0x06, 0x08, 0x0A, 0x0C,	/* D0000 - DC000 */
	0x30, 0x32, 0x34, 0x36, 0x38, 0x3A, 0x3C,	/* E0000 - EC000 */
};
static unsigned char s502e_hmcr[] =
{
	0x10, 0x12, 0x14, 0x16, 0x18, 0x1A, 0x1C, 0x1E,	/* A0000 - AE000 */
	0x20, 0x22, 0x24, 0x26, 0x28, 0x2A, 0x2C, 0x2E,	/* C0000 - CE000 */
	0x00, 0x02, 0x04, 0x06, 0x08, 0x0A, 0x0C, 0x0E,	/* D0000 - DE000 */
	0x30, 0x32, 0x34, 0x36, 0x38, 0x3A, 0x3C, 0x3E,	/* E0000 - EE000 */
};
static unsigned char s507_hmcr[] =
{
	0x00, 0x02, 0x04, 0x06, 0x08, 0x0A, 0x0C, 0x0E,	/* A0000 - AE000 */
	0x40, 0x42, 0x44, 0x46, 0x48, 0x4A, 0x4C, 0x4E,	/* B0000 - BE000 */
	0x80, 0x82, 0x84, 0x86, 0x88, 0x8A, 0x8C, 0x8E,	/* C0000 - CE000 */
	0xC0, 0xC2, 0xC4, 0xC6, 0xC8, 0xCA, 0xCC, 0xCE,	/* E0000 - EE000 */
};
static unsigned char s508_hmcr[] =
{
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,	/* A0000 - AE000 */
	0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,	/* C0000 - CE000 */
	0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,	/* D0000 - DE000 */
	0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F,	/* E0000 - EE000 */
};

static unsigned char s507_irqmask[] =
{
	0x00, 0x20, 0x40, 0x60, 0x80, 0xA0, 0xC0, 0xE0
};

static int pci_slot_ar[MAX_S514_CARDS];

/******* Kernel Loadable Module Entry Points ********************************/

/*============================================================================
 * Module 'insert' entry point.
 * o print announcement
 * o initialize static data
 * o calibrate SDLA shared memory access delay.
 *
 * Return:	0	Ok
 *		< 0	error.
 * Context:	process
 */

static int __init sdladrv_init(void)
{
	int i=0;

	printk(KERN_INFO "%s v%u.%u %s\n",
		fullname, MOD_VERSION, MOD_RELEASE, copyright);
	exec_idle = calibrate_delay(EXEC_DELAY);
#ifdef WANDEBUG	
	printk(KERN_DEBUG "%s: exec_idle = %d\n", modname, exec_idle);
#endif	

	/* Initialize the PCI Card array, which
         * will store flags, used to mark 
         * card initialization state */
	for (i=0; i<MAX_S514_CARDS; i++)
		pci_slot_ar[i] = 0xFF;

	return 0;
}

/*============================================================================
 * Module 'remove' entry point.
 * o release all remaining system resources
 */
static void __exit sdladrv_cleanup(void)
{
}

module_init(sdladrv_init);
module_exit(sdladrv_cleanup);

/******* Kernel APIs ********************************************************/

/*============================================================================
 * Set up adapter.
 * o detect adapter type
 * o verify hardware configuration options
 * o check for hardware conflicts
 * o set up adapter shared memory
 * o test adapter memory
 * o load firmware
 * Return:	0	ok.
 *		< 0	error
 */

EXPORT_SYMBOL(sdla_setup);

int sdla_setup (sdlahw_t* hw, void* sfm, unsigned len)
{
	unsigned* irq_opt	= NULL;	/* IRQ options */
	unsigned* dpmbase_opt	= NULL;	/* DPM window base options */
	unsigned* pclk_opt	= NULL;	/* CPU clock rate options */
	int err=0;

	if (sdla_detect(hw)) {
                if(hw->type != SDLA_S514)
                        printk(KERN_INFO "%s: no SDLA card found at port 0x%X\n",
                        modname, hw->port);
		return -EINVAL;
	}

	if(hw->type != SDLA_S514) {
                printk(KERN_INFO "%s: found S%04u card at port 0x%X.\n",
                modname, hw->type, hw->port);

                hw->dpmsize = SDLA_WINDOWSIZE;
                switch (hw->type) {
                case SDLA_S502A:
                        hw->io_range    = S502A_IORANGE;
                        irq_opt         = s502a_irq_options;
                        dpmbase_opt     = s502a_dpmbase_options;
                        pclk_opt        = s502a_pclk_options;
                        break;

                case SDLA_S502E:
                        hw->io_range    = S502E_IORANGE;
                        irq_opt         = s502e_irq_options;
                        dpmbase_opt     = s508_dpmbase_options;
                        pclk_opt        = s502e_pclk_options;
                        break;

                case SDLA_S503:
                        hw->io_range    = S503_IORANGE;
                        irq_opt         = s503_irq_options;
                        dpmbase_opt     = s508_dpmbase_options;
                        pclk_opt        = s503_pclk_options;
                        break;

                case SDLA_S507:
                        hw->io_range    = S507_IORANGE;
                        irq_opt         = s508_irq_options;
                        dpmbase_opt     = s507_dpmbase_options;
                        pclk_opt        = s507_pclk_options;
                        break;

                case SDLA_S508:
                        hw->io_range    = S508_IORANGE;
                        irq_opt         = s508_irq_options;
                        dpmbase_opt     = s508_dpmbase_options;
                        pclk_opt        = s508_pclk_options;
                        break;
                }

                /* Verify IRQ configuration options */
                if (!get_option_index(irq_opt, hw->irq)) {
                        printk(KERN_INFO "%s: IRQ %d is invalid!\n",
                        	modname, hw->irq);
                      return -EINVAL;
                } 

                /* Verify CPU clock rate configuration options */
                if (hw->pclk == 0)
                        hw->pclk = pclk_opt[1];  /* use default */
        
                else if (!get_option_index(pclk_opt, hw->pclk)) {
                        printk(KERN_INFO "%s: CPU clock %u is invalid!\n",
				modname, hw->pclk);
                        return -EINVAL;
                } 
                printk(KERN_INFO "%s: assuming CPU clock rate of %u kHz.\n",
			modname, hw->pclk);

                /* Setup adapter dual-port memory window and test memory */
                if (hw->dpmbase == 0) {
                        err = sdla_autodpm(hw);
                        if (err) {
                                printk(KERN_INFO
				"%s: can't find available memory region!\n",
					modname);
                                return err;
                        }
                }
                else if (!get_option_index(dpmbase_opt,
			virt_to_phys(hw->dpmbase))) {
                        printk(KERN_INFO
				"%s: memory address 0x%lX is invalid!\n",
				modname, virt_to_phys(hw->dpmbase));
                        return -EINVAL;
                }               
                else if (sdla_setdpm(hw)) {
                        printk(KERN_INFO
			"%s: 8K memory region at 0x%lX is not available!\n",
				modname, virt_to_phys(hw->dpmbase));
                        return -EINVAL;
                } 
                printk(KERN_INFO
			"%s: dual-port memory window is set at 0x%lX.\n",
				modname, virt_to_phys(hw->dpmbase));


		/* If we find memory in 0xE**** Memory region, 
                 * warn the user to disable the SHADOW RAM.  
                 * Since memory corruption can occur if SHADOW is
                 * enabled. This can causes random crashes ! */
		if (virt_to_phys(hw->dpmbase) >= 0xE0000){
			printk(KERN_WARNING "\n%s: !!!!!!!!  WARNING !!!!!!!!\n",modname);
			printk(KERN_WARNING "%s: WANPIPE is using 0x%lX memory region !!!\n",
						modname, virt_to_phys(hw->dpmbase));
			printk(KERN_WARNING "         Please disable the SHADOW RAM, otherwise\n");
			printk(KERN_WARNING "         your system might crash randomly from time to time !\n");
			printk(KERN_WARNING "%s: !!!!!!!!  WARNING !!!!!!!!\n\n",modname);
		}
        }

	else {
		hw->memory = test_memregion((void*)hw->dpmbase, 
			MAX_SIZEOF_S514_MEMORY);
		if(hw->memory < (256 * 1024)) {
			printk(KERN_INFO
				"%s: error in testing S514 memory (0x%lX)\n",
				modname, hw->memory);
			sdla_down(hw);
			return -EINVAL;
		}
	}
    
	printk(KERN_INFO "%s: found %luK bytes of on-board memory\n",
		modname, hw->memory / 1024);

	/* Load firmware. If loader fails then shut down adapter */
	err = sdla_load(hw, sfm, len);
	if (err) sdla_down(hw);		/* shutdown adapter */

	return err;
} 

/*============================================================================
 * Shut down SDLA: disable shared memory access and interrupts, stop CPU, etc.
 */

EXPORT_SYMBOL(sdla_down);

int sdla_down (sdlahw_t* hw)
{
	unsigned port = hw->port;
	int i;
        unsigned char CPU_no;
        u32 int_config, int_status;

        if(!port && (hw->type != SDLA_S514))
                return -EFAULT;

	switch (hw->type) {
	case SDLA_S502A:
		_OUTB(port, 0x08);		/* halt CPU */
		_OUTB(port, 0x08);
		_OUTB(port, 0x08);
		hw->regs[0] = 0x08;
		_OUTB(port + 1, 0xFF);		/* close memory window */
		hw->regs[1] = 0xFF;
		break;

	case SDLA_S502E:
		_OUTB(port + 3, 0);		/* stop CPU */
		_OUTB(port, 0);			/* reset board */
		for (i = 0; i < S502E_IORANGE; ++i)
			hw->regs[i] = 0
		;
		break;

	case SDLA_S503:
	case SDLA_S507:
	case SDLA_S508:
		_OUTB(port, 0);			/* reset board logic */
		hw->regs[0] = 0;
		break;

	case SDLA_S514:
		/* halt the adapter */
                *(char *)hw->vector = S514_CPU_HALT;
        	CPU_no = hw->S514_cpu_no[0];

		/* disable the PCI IRQ and disable memory access */
                pci_read_config_dword(hw->pci_dev, PCI_INT_CONFIG, &int_config);
	        int_config &= (CPU_no == S514_CPU_A) ? ~PCI_DISABLE_IRQ_CPU_A :	~PCI_DISABLE_IRQ_CPU_B;
                pci_write_config_dword(hw->pci_dev, PCI_INT_CONFIG, int_config);
		read_S514_int_stat(hw, &int_status);
		S514_intack(hw, int_status);
		if(CPU_no == S514_CPU_A)
                        pci_write_config_dword(hw->pci_dev, PCI_MAP0_DWORD,
				PCI_CPU_A_MEM_DISABLE);
		else
                        pci_write_config_dword(hw->pci_dev, PCI_MAP1_DWORD,
				PCI_CPU_B_MEM_DISABLE);

		/* free up the allocated virtual memory */
 		iounmap((void *)hw->dpmbase);
        	iounmap((void *)hw->vector);
 		break;


	default:
		return -EINVAL;
	}
	return 0;
}

/*============================================================================
 * Map shared memory window into SDLA address space.
 */

EXPORT_SYMBOL(sdla_mapmem);

int sdla_mapmem (sdlahw_t* hw, unsigned long addr)
{
	unsigned port = hw->port;
	register int tmp;

	switch (hw->type) {
	case SDLA_S502A:
	case SDLA_S502E:
		if (addr < S502_MAXMEM)	{ /* verify parameter */
			tmp = addr >> 13;	/* convert to register mask */
			_OUTB(port + 2, tmp);
			hw->regs[2] = tmp;
		}
		else return -EINVAL;
		break;

	case SDLA_S503:
		if (addr < S503_MAXMEM)	{ /* verify parameter */
			tmp = (hw->regs[0] & 0x8F) | ((addr >> 9) & 0x70);
			_OUTB(port, tmp);
			hw->regs[0] = tmp;
		}
		else return -EINVAL;
		break;

	case SDLA_S507:
		if (addr < S507_MAXMEM) {
			if (!(_INB(port) & 0x02))
				return -EIO;
			tmp = addr >> 13;	/* convert to register mask */
			_OUTB(port + 2, tmp);
			hw->regs[2] = tmp;
		}
		else return -EINVAL;
		break;

	case SDLA_S508:
		if (addr < S508_MAXMEM) {
			tmp = addr >> 13;	/* convert to register mask */
			_OUTB(port + 2, tmp);
			hw->regs[2] = tmp;
		}
		else return -EINVAL;
		break;

	case SDLA_S514:
		return 0;

 	default:
		return -EINVAL;
	}
	hw->vector = addr & 0xFFFFE000L;
	return 0;
}

/*============================================================================
 * Enable interrupt generation.
 */

static int sdla_inten (sdlahw_t* hw)
{
	unsigned port = hw->port;
	int tmp, i;

	switch (hw->type) {
	case SDLA_S502E:
		/* Note thar interrupt control operations on S502E are allowed
		 * only if CPU is enabled (bit 0 of status register is set).
		 */
		if (_INB(port) & 0x01) {
			_OUTB(port, 0x02);	/* bit1 = 1, bit2 = 0 */
			_OUTB(port, 0x06);	/* bit1 = 1, bit2 = 1 */
			hw->regs[0] = 0x06;
		}
		else return -EIO;
		break;

	case SDLA_S503:
		tmp = hw->regs[0] | 0x04;
		_OUTB(port, tmp);
		hw->regs[0] = tmp;		/* update mirror */
		for (i = 0; i < SDLA_IODELAY; ++i);	/* delay */
		if (!(_INB(port) & 0x02))		/* verify */
			return -EIO;
		break;

	case SDLA_S508:
		tmp = hw->regs[0] | 0x10;
		_OUTB(port, tmp);
		hw->regs[0] = tmp;		/* update mirror */
		for (i = 0; i < SDLA_IODELAY; ++i);	/* delay */
		if (!(_INB(port + 1) & 0x10))		/* verify */
			return -EIO;
		break;

	case SDLA_S502A:
	case SDLA_S507:
		break;

        case SDLA_S514:
                break;

	default:
		return -EINVAL;

	}
	return 0;
}

/*============================================================================
 * Disable interrupt generation.
 */

#if 0
int sdla_intde (sdlahw_t* hw)
{
	unsigned port = hw->port;
	int tmp, i;

	switch (hw->type) {
	case SDLA_S502E:
		/* Notes:
		 *  1) interrupt control operations are allowed only if CPU is
		 *     enabled (bit 0 of status register is set).
		 *  2) disabling interrupts using bit 1 of control register
		 *     causes IRQ line go high, therefore we are going to use
		 *     0x04 instead: lower it to inhibit interrupts to PC.
		 */
		if (_INB(port) & 0x01) {
			_OUTB(port, hw->regs[0] & ~0x04);
			hw->regs[0] &= ~0x04;
		}
		else return -EIO;
		break;

	case SDLA_S503:
		tmp = hw->regs[0] & ~0x04;
		_OUTB(port, tmp);
		hw->regs[0] = tmp;			/* update mirror */
		for (i = 0; i < SDLA_IODELAY; ++i);	/* delay */
		if (_INB(port) & 0x02)			/* verify */
			return -EIO;
		break;

	case SDLA_S508:
		tmp = hw->regs[0] & ~0x10;
		_OUTB(port, tmp);
		hw->regs[0] = tmp;			/* update mirror */
		for (i = 0; i < SDLA_IODELAY; ++i);	/* delay */
		if (_INB(port) & 0x10)			/* verify */
			return -EIO;
		break;

	case SDLA_S502A:
	case SDLA_S507:
		break;

	default:
		return -EINVAL;
	}
	return 0;
}
#endif  /*  0  */

/*============================================================================
 * Acknowledge SDLA hardware interrupt.
 */

static int sdla_intack (sdlahw_t* hw)
{
	unsigned port = hw->port;
	int tmp;

	switch (hw->type) {
	case SDLA_S502E:
		/* To acknoledge hardware interrupt we have to toggle bit 3 of
		 * control register: \_/
		 * Note that interrupt control operations on S502E are allowed
		 * only if CPU is enabled (bit 1 of status register is set).
		 */
		if (_INB(port) & 0x01) {
			tmp = hw->regs[0] & ~0x04;
			_OUTB(port, tmp);
			tmp |= 0x04;
			_OUTB(port, tmp);
			hw->regs[0] = tmp;
		}
		else return -EIO;
		break;

	case SDLA_S503:
		if (_INB(port) & 0x04) {
			tmp = hw->regs[0] & ~0x08;
			_OUTB(port, tmp);
			tmp |= 0x08;
			_OUTB(port, tmp);
			hw->regs[0] = tmp;
		}
		break;

	case SDLA_S502A:
	case SDLA_S507:
	case SDLA_S508:
	break;

	default:
		return -EINVAL;
	}
	return 0;
}


/*============================================================================
 * Acknowledge S514 hardware interrupt.
 */

EXPORT_SYMBOL(S514_intack);

void S514_intack (sdlahw_t* hw, u32 int_status)
{
        pci_write_config_dword(hw->pci_dev, PCI_INT_STATUS, int_status);
}


/*============================================================================
 * Read the S514 hardware interrupt status.
 */

EXPORT_SYMBOL(read_S514_int_stat);

void read_S514_int_stat (sdlahw_t* hw, u32* int_status)
{
	pci_read_config_dword(hw->pci_dev, PCI_INT_STATUS, int_status);
}


/*============================================================================
 * Generate an interrupt to adapter's CPU.
 */

#if 0
int sdla_intr (sdlahw_t* hw)
{
	unsigned port = hw->port;

	switch (hw->type) {
	case SDLA_S502A:
		if (!(_INB(port) & 0x40)) {
			_OUTB(port, 0x10);		/* issue NMI to CPU */
			hw->regs[0] = 0x10;
		}
		else return -EIO;
		break;

	case SDLA_S507:
		if ((_INB(port) & 0x06) == 0x06) {
			_OUTB(port + 3, 0);
		}
		else return -EIO;
		break;

	case SDLA_S508:
		if (_INB(port + 1) & 0x02) {
			_OUTB(port, 0x08);
		}
		else return -EIO;
		break;

	case SDLA_S502E:
	case SDLA_S503:
	default:
		return -EINVAL;
	}
	return 0;
}
#endif  /*  0  */

/*============================================================================
 * Execute Adapter Command.
 * o Set exec flag.
 * o Busy-wait until flag is reset.
 * o Return number of loops made, or 0 if command timed out.
 */

EXPORT_SYMBOL(sdla_exec);

int sdla_exec (void* opflag)
{
	volatile unsigned char* flag = opflag;
	unsigned long tstop;
	int nloops;

	if(readb(flag) != 0x00) {
		printk(KERN_INFO
			"WANPIPE: opp flag set on entry to sdla_exec\n");
		return 0;
	}
	
	writeb(0x01, flag);

	tstop = SYSTEM_TICK + EXEC_TIMEOUT;

	for (nloops = 1; (readb(flag) == 0x01); ++ nloops) {
		unsigned delay = exec_idle;
		while (-- delay);			/* delay */
		if (SYSTEM_TICK > tstop) return 0;	/* time is up! */
	}
	return nloops;
}

/*============================================================================
 * Read absolute adapter memory.
 * Transfer data from adapter's memory to data buffer.
 *
 * Note:
 * Care should be taken when crossing dual-port memory window boundary.
 * This function is not atomic, so caller must disable interrupt if
 * interrupt routines are accessing adapter shared memory.
 */

EXPORT_SYMBOL(sdla_peek);

int sdla_peek (sdlahw_t* hw, unsigned long addr, void* buf, unsigned len)
{

	if (addr + len > hw->memory)	/* verify arguments */
		return -EINVAL;

        if(hw->type == SDLA_S514) {	/* copy data for the S514 adapter */
                peek_by_4 ((unsigned long)hw->dpmbase + addr, buf, len);
                return 0;
	}

        else {				/* copy data for the S508 adapter */
	        unsigned long oldvec = hw->vector;
        	unsigned winsize = hw->dpmsize;
	        unsigned curpos, curlen;   /* current offset and block size */
        	unsigned long curvec;      /* current DPM window vector */
	        int err = 0;

                while (len && !err) {
                        curpos = addr % winsize;  /* current window offset */
                        curvec = addr - curpos;   /* current window vector */
                        curlen = (len > (winsize - curpos)) ?
				(winsize - curpos) : len;
                        /* Relocate window and copy block of data */
                        err = sdla_mapmem(hw, curvec);
                        peek_by_4 ((unsigned long)hw->dpmbase + curpos, buf,
				curlen);
                        addr       += curlen;
                        buf         = (char*)buf + curlen;
                        len        -= curlen;
                }

                /* Restore DPM window position */
                sdla_mapmem(hw, oldvec);
                return err;
        }
}


/*============================================================================
 * Read data from adapter's memory to a data buffer in 4-byte chunks.
 * Note that we ensure that the SDLA memory address is on a 4-byte boundary
 * before we begin moving the data in 4-byte chunks.
*/

static void peek_by_4 (unsigned long src, void* buf, unsigned len)
{

        /* byte copy data until we get to a 4-byte boundary */
        while (len && (src & 0x03)) {
                *(char *)buf ++ = readb(src ++);
                len --;
        }

        /* copy data in 4-byte chunks */
        while (len >= 4) {
                *(unsigned long *)buf = readl(src);
                buf += 4;
                src += 4;
                len -= 4;
        }

        /* byte copy any remaining data */
        while (len) {
                *(char *)buf ++ = readb(src ++);
                len --;
        }
}


/*============================================================================
 * Write Absolute Adapter Memory.
 * Transfer data from data buffer to adapter's memory.
 *
 * Note:
 * Care should be taken when crossing dual-port memory window boundary.
 * This function is not atomic, so caller must disable interrupt if
 * interrupt routines are accessing adapter shared memory.
 */

EXPORT_SYMBOL(sdla_poke);
 
int sdla_poke (sdlahw_t* hw, unsigned long addr, void* buf, unsigned len)
{

	if (addr + len > hw->memory)	/* verify arguments */
		return -EINVAL;
   
        if(hw->type == SDLA_S514) {	/* copy data for the S514 adapter */
                poke_by_4 ((unsigned long)hw->dpmbase + addr, buf, len);
                return 0;
	}
	
	else {				/* copy data for the S508 adapter */
    		unsigned long oldvec = hw->vector;
	        unsigned winsize = hw->dpmsize;
        	unsigned curpos, curlen;     /* current offset and block size */
        	unsigned long curvec;        /* current DPM window vector */
        	int err = 0;

		while (len && !err) {
                        curpos = addr % winsize;    /* current window offset */
                        curvec = addr - curpos;     /* current window vector */
                        curlen = (len > (winsize - curpos)) ?
				(winsize - curpos) : len;
                        /* Relocate window and copy block of data */
                        sdla_mapmem(hw, curvec);
                        poke_by_4 ((unsigned long)hw->dpmbase + curpos, buf,
				curlen);
	                addr       += curlen;
                        buf         = (char*)buf + curlen;
                        len        -= curlen;
                }

                /* Restore DPM window position */
                sdla_mapmem(hw, oldvec);
                return err;
        }
}


/*============================================================================
 * Write from a data buffer to adapter's memory in 4-byte chunks.
 * Note that we ensure that the SDLA memory address is on a 4-byte boundary
 * before we begin moving the data in 4-byte chunks.
*/

static void poke_by_4 (unsigned long dest, void* buf, unsigned len)
{

        /* byte copy data until we get to a 4-byte boundary */
        while (len && (dest & 0x03)) {
                writeb (*(char *)buf ++, dest ++);
                len --;
        }

        /* copy data in 4-byte chunks */
        while (len >= 4) {
                writel (*(unsigned long *)buf, dest);
                dest += 4;
                buf += 4;
                len -= 4;
        }

        /* byte copy any remaining data */
        while (len) {
                writeb (*(char *)buf ++ , dest ++);
                len --;
        }
}


#ifdef	DONT_COMPIPLE_THIS
#endif	/* DONT_COMPIPLE_THIS */

/****** Hardware-Specific Functions *****************************************/

/*============================================================================
 * Detect adapter type.
 * o if adapter type is specified then call detection routine for that adapter
 *   type.  Otherwise call detection routines for every adapter types until
 *   adapter is detected.
 *
 * Notes:
 * 1) Detection tests are destructive! Adapter will be left in shutdown state
 *    after the test.
 */
static int sdla_detect (sdlahw_t* hw)
{
	unsigned port = hw->port;
	int err = 0;

	if (!port && (hw->type != SDLA_S514))
		return -EFAULT;

    	switch (hw->type) {
	case SDLA_S502A:
		if (!detect_s502a(port)) err = -ENODEV;
		break;

	case SDLA_S502E:
		if (!detect_s502e(port)) err = -ENODEV;
		break;

	case SDLA_S503:
		if (!detect_s503(port)) err = -ENODEV;
		break;

	case SDLA_S507:
		if (!detect_s507(port)) err = -ENODEV;
		break;

	case SDLA_S508:
		if (!detect_s508(port)) err = -ENODEV;
		break;

	case SDLA_S514:
                if (!detect_s514(hw)) err = -ENODEV;
		break;

	default:
		if (detect_s502a(port))
			hw->type = SDLA_S502A;
		else if (detect_s502e(port))
			hw->type = SDLA_S502E;
		else if (detect_s503(port))
			hw->type = SDLA_S503;
		else if (detect_s507(port))
			hw->type = SDLA_S507;
		else if (detect_s508(port))
			hw->type = SDLA_S508;
		else err = -ENODEV;
	}
	return err;
}

/*============================================================================
 * Autoselect memory region. 
 * o try all available DMP address options from the top down until success.
 */
static int sdla_autodpm (sdlahw_t* hw)
{
	int i, err = -EINVAL;
	unsigned* opt;

	switch (hw->type) {
	case SDLA_S502A:
		opt = s502a_dpmbase_options;
		break;

	case SDLA_S502E:
	case SDLA_S503:
	case SDLA_S508:
		opt = s508_dpmbase_options;
		break;

	case SDLA_S507:
		opt = s507_dpmbase_options;
		break;

	default:
		return -EINVAL;
	}

	/* Start testing from 8th position, address
         * 0xC8000 from the 508 address table. 
         * We don't want to test A**** addresses, since
         * they are usually used for Video */
	for (i = 8; i <= opt[0] && err; i++) {
		hw->dpmbase = phys_to_virt(opt[i]);
		err = sdla_setdpm(hw);
	}
	return err;
}

/*============================================================================
 * Set up adapter dual-port memory window. 
 * o shut down adapter
 * o make sure that no physical memory exists in this region, i.e entire
 *   region reads 0xFF and is not writable when adapter is shut down.
 * o initialize adapter hardware
 * o make sure that region is usable with SDLA card, i.e. we can write to it
 *   when adapter is configured.
 */
static int sdla_setdpm (sdlahw_t* hw)
{
	int err;

	/* Shut down card and verify memory region */
	sdla_down(hw);
	if (check_memregion(hw->dpmbase, hw->dpmsize))
		return -EINVAL;

	/* Initialize adapter and test on-board memory segment by segment.
	 * If memory size appears to be less than shared memory window size,
	 * assume that memory region is unusable.
	 */
	err = sdla_init(hw);
	if (err) return err;

	if (sdla_memtest(hw) < hw->dpmsize) {	/* less than window size */
		sdla_down(hw);
		return -EIO;
	}
	sdla_mapmem(hw, 0L);	/* set window vector at bottom */
	return 0;
}

/*============================================================================
 * Load adapter from the memory image of the SDLA firmware module. 
 * o verify firmware integrity and compatibility
 * o start adapter up
 */
static int sdla_load (sdlahw_t* hw, sfm_t* sfm, unsigned len)
{

	int i;

	/* Verify firmware signature */
	if (strcmp(sfm->signature, SFM_SIGNATURE)) {
		printk(KERN_INFO "%s: not SDLA firmware!\n",
			modname);
		return -EINVAL;
	}

	/* Verify firmware module format version */
	if (sfm->version != SFM_VERSION) {
		printk(KERN_INFO
			"%s: firmware format %u rejected! Expecting %u.\n",
			modname, sfm->version, SFM_VERSION);
		return -EINVAL;
	}

	/* Verify firmware module length and checksum */
	if ((len - offsetof(sfm_t, image) != sfm->info.codesize) ||
		(checksum((void*)&sfm->info,
		sizeof(sfm_info_t) + sfm->info.codesize) != sfm->checksum)) {
		printk(KERN_INFO "%s: firmware corrupted!\n", modname);
		return -EINVAL;
	}

	/* Announce */
	printk(KERN_INFO "%s: loading %s (ID=%u)...\n", modname,
		(sfm->descr[0] != '\0') ? sfm->descr : "unknown firmware",
		sfm->info.codeid);

	if(hw->type == SDLA_S514)
		printk(KERN_INFO "%s: loading S514 adapter, CPU %c\n",
			modname, hw->S514_cpu_no[0]);

	/* Scan through the list of compatible adapters and make sure our
	 * adapter type is listed.
	 */
	for (i = 0;
	     (i < SFM_MAX_SDLA) && (sfm->info.adapter[i] != hw->type);
	     ++i);
	
	if (i == SFM_MAX_SDLA) {
		printk(KERN_INFO "%s: firmware is not compatible with S%u!\n",
			modname, hw->type);
		return -EINVAL;
	}


	/* Make sure there is enough on-board memory */
	if (hw->memory < sfm->info.memsize) {
		printk(KERN_INFO
			"%s: firmware needs %lu bytes of on-board memory!\n",
			modname, sfm->info.memsize);
		return -EINVAL;
	}

	/* Move code onto adapter */
	if (sdla_poke(hw, sfm->info.codeoffs, sfm->image, sfm->info.codesize)) {
		printk(KERN_INFO "%s: failed to load code segment!\n",
			modname);
		return -EIO;
	}

	/* Prepare boot-time configuration data and kick-off CPU */
	sdla_bootcfg(hw, &sfm->info);
	if (sdla_start(hw, sfm->info.startoffs)) {
		printk(KERN_INFO "%s: Damn... Adapter won't start!\n",
			modname);
		return -EIO;
	}

	/* position DPM window over the mailbox and enable interrupts */
        if (sdla_mapmem(hw, sfm->info.winoffs) || sdla_inten(hw)) {
		printk(KERN_INFO "%s: adapter hardware failure!\n",
			modname);
		return -EIO;
	}
	hw->fwid = sfm->info.codeid;		/* set firmware ID */
	return 0;
}

/*============================================================================
 * Initialize SDLA hardware: setup memory window, IRQ, etc.
 */
static int sdla_init (sdlahw_t* hw)
{
	int i;

	for (i = 0; i < SDLA_MAXIORANGE; ++i)
		hw->regs[i] = 0;

	switch (hw->type) {
	case SDLA_S502A: return init_s502a(hw);
	case SDLA_S502E: return init_s502e(hw);
	case SDLA_S503:  return init_s503(hw);
	case SDLA_S507:  return init_s507(hw);
	case SDLA_S508:  return init_s508(hw);
	}
	return -EINVAL;
}

/*============================================================================
 * Test adapter on-board memory.
 * o slide DPM window from the bottom up and test adapter memory segment by
 *   segment.
 * Return adapter memory size.
 */
static unsigned long sdla_memtest (sdlahw_t* hw)
{
	unsigned long memsize;
	unsigned winsize;

	for (memsize = 0, winsize = hw->dpmsize;
	     !sdla_mapmem(hw, memsize) &&
		(test_memregion(hw->dpmbase, winsize) == winsize)
	     ;
	     memsize += winsize)
	;
	hw->memory = memsize;
	return memsize;
}

/*============================================================================
 * Prepare boot-time firmware configuration data.
 * o position DPM window
 * o initialize configuration data area
 */
static int sdla_bootcfg (sdlahw_t* hw, sfm_info_t* sfminfo)
{
	unsigned char* data;

	if (!sfminfo->datasize) return 0;	/* nothing to do */

	if (sdla_mapmem(hw, sfminfo->dataoffs) != 0)
		return -EIO;

	if(hw->type == SDLA_S514)
                data = (void*)(hw->dpmbase + sfminfo->dataoffs);
        else
                data = (void*)((u8 *)hw->dpmbase +
                        (sfminfo->dataoffs - hw->vector));

	memset_io (data, 0, sfminfo->datasize);

	writeb (make_config_byte(hw), &data[0x00]);

	switch (sfminfo->codeid) {
	case SFID_X25_502:
	case SFID_X25_508:
                writeb (3, &data[0x01]);        /* T1 timer */
                writeb (10, &data[0x03]);       /* N2 */
                writeb (7, &data[0x06]);        /* HDLC window size */
                writeb (1, &data[0x0B]);        /* DTE */
                writeb (2, &data[0x0C]);        /* X.25 packet window size */
                writew (128, &data[0x0D]);	/* default X.25 data size */
                writew (128, &data[0x0F]);	/* maximum X.25 data size */
		break;
	}
	return 0;
}

/*============================================================================
 * Prepare configuration byte identifying adapter type and CPU clock rate.
 */
static unsigned char make_config_byte (sdlahw_t* hw)
{
	unsigned char byte = 0;

	switch (hw->pclk) {
		case 5000:  byte = 0x01; break;
		case 7200:  byte = 0x02; break;
		case 8000:  byte = 0x03; break;
		case 10000: byte = 0x04; break;
		case 16000: byte = 0x05; break;
	}

	switch (hw->type) {
		case SDLA_S502E: byte |= 0x80; break;
		case SDLA_S503:  byte |= 0x40; break;
	}
	return byte;
}

/*============================================================================
 * Start adapter's CPU.
 * o calculate a pointer to adapter's cold boot entry point
 * o position DPM window
 * o place boot instruction (jp addr) at cold boot entry point
 * o start CPU
 */
static int sdla_start (sdlahw_t* hw, unsigned addr)
{
	unsigned port = hw->port;
	unsigned char *bootp;
	int err, tmp, i;

	if (!port && (hw->type != SDLA_S514)) return -EFAULT;

 	switch (hw->type) {
	case SDLA_S502A:
		bootp = hw->dpmbase;
		bootp += 0x66;
		break;

	case SDLA_S502E:
	case SDLA_S503:
	case SDLA_S507:
	case SDLA_S508:
	case SDLA_S514:
		bootp = hw->dpmbase;
		break;

	default:
		return -EINVAL;
	}

	err = sdla_mapmem(hw, 0);
	if (err) return err;

      	writeb (0xC3, bootp);   /* Z80: 'jp' opcode */
	bootp ++;
	writew (addr, bootp);

	switch (hw->type) {
	case SDLA_S502A:
		_OUTB(port, 0x10);		/* issue NMI to CPU */
		hw->regs[0] = 0x10;
		break;

	case SDLA_S502E:
		_OUTB(port + 3, 0x01);		/* start CPU */
		hw->regs[3] = 0x01;
		for (i = 0; i < SDLA_IODELAY; ++i);
		if (_INB(port) & 0x01) {	/* verify */
			/*
			 * Enabling CPU changes functionality of the
			 * control register, so we have to reset its
			 * mirror.
			 */
			_OUTB(port, 0);		/* disable interrupts */
			hw->regs[0] = 0;
		}
		else return -EIO;
		break;

	case SDLA_S503:
		tmp = hw->regs[0] | 0x09;	/* set bits 0 and 3 */
		_OUTB(port, tmp);
		hw->regs[0] = tmp;		/* update mirror */
		for (i = 0; i < SDLA_IODELAY; ++i);
		if (!(_INB(port) & 0x01))	/* verify */
			return -EIO;
		break;

	case SDLA_S507:
		tmp = hw->regs[0] | 0x02;
		_OUTB(port, tmp);
		hw->regs[0] = tmp;		/* update mirror */
		for (i = 0; i < SDLA_IODELAY; ++i);
		if (!(_INB(port) & 0x04))	/* verify */
			return -EIO;
		break;

	case SDLA_S508:
		tmp = hw->regs[0] | 0x02;
		_OUTB(port, tmp);
		hw->regs[0] = tmp;	/* update mirror */
		for (i = 0; i < SDLA_IODELAY; ++i);
		if (!(_INB(port + 1) & 0x02))	/* verify */
			return -EIO;
		break;

	case SDLA_S514:
		writeb (S514_CPU_START, hw->vector);
		break;

	default:
		return -EINVAL;
	}
	return 0;
}

/*============================================================================
 * Initialize S502A adapter.
 */
static int init_s502a (sdlahw_t* hw)
{
	unsigned port = hw->port;
	int tmp, i;

	if (!detect_s502a(port))
		return -ENODEV;

	hw->regs[0] = 0x08;
	hw->regs[1] = 0xFF;

	/* Verify configuration options */
	i = get_option_index(s502a_dpmbase_options, virt_to_phys(hw->dpmbase));
	if (i == 0)
		return -EINVAL;

	tmp = s502a_hmcr[i - 1];
	switch (hw->dpmsize) {
	case 0x2000:
		tmp |= 0x01;
		break;

	case 0x10000L:
		break;

	default:
		return -EINVAL;
	}

	/* Setup dual-port memory window (this also enables memory access) */
	_OUTB(port + 1, tmp);
	hw->regs[1] = tmp;
	hw->regs[0] = 0x08;
	return 0;
}

/*============================================================================
 * Initialize S502E adapter.
 */
static int init_s502e (sdlahw_t* hw)
{
	unsigned port = hw->port;
	int tmp, i;

	if (!detect_s502e(port))
		return -ENODEV;

	/* Verify configuration options */
	i = get_option_index(s508_dpmbase_options, virt_to_phys(hw->dpmbase));
	if (i == 0)
		return -EINVAL;

	tmp = s502e_hmcr[i - 1];
	switch (hw->dpmsize) {
	case 0x2000:
		tmp |= 0x01;
		break;

	case 0x10000L:
		break;

	default:
		return -EINVAL;
	}

	/* Setup dual-port memory window */
	_OUTB(port + 1, tmp);
	hw->regs[1] = tmp;

	/* Enable memory access */
	_OUTB(port, 0x02);
	hw->regs[0] = 0x02;
	for (i = 0; i < SDLA_IODELAY; ++i);	/* delay */
	return (_INB(port) & 0x02) ? 0 : -EIO;
}

/*============================================================================
 * Initialize S503 adapter.
 * ---------------------------------------------------------------------------
 */
static int init_s503 (sdlahw_t* hw)
{
	unsigned port = hw->port;
	int tmp, i;

	if (!detect_s503(port))
		return -ENODEV;

	/* Verify configuration options */
	i = get_option_index(s508_dpmbase_options, virt_to_phys(hw->dpmbase));
	if (i == 0)
		return -EINVAL;

	tmp = s502e_hmcr[i - 1];
	switch (hw->dpmsize) {
	case 0x2000:
		tmp |= 0x01;
		break;

	case 0x10000L:
		break;

	default:
		return -EINVAL;
	}

	/* Setup dual-port memory window */
	_OUTB(port + 1, tmp);
	hw->regs[1] = tmp;

	/* Enable memory access */
	_OUTB(port, 0x02);
	hw->regs[0] = 0x02;	/* update mirror */
	return 0;
}

/*============================================================================
 * Initialize S507 adapter.
 */
static int init_s507 (sdlahw_t* hw)
{
	unsigned port = hw->port;
	int tmp, i;

	if (!detect_s507(port))
		return -ENODEV;

	/* Verify configuration options */
	i = get_option_index(s507_dpmbase_options, virt_to_phys(hw->dpmbase));
	if (i == 0)
		return -EINVAL;

	tmp = s507_hmcr[i - 1];
	switch (hw->dpmsize) {
	case 0x2000:
		tmp |= 0x01;
		break;

	case 0x10000L:
		break;

	default:
		return -EINVAL;
	}

	/* Enable adapter's logic */
	_OUTB(port, 0x01);
	hw->regs[0] = 0x01;
	for (i = 0; i < SDLA_IODELAY; ++i);	/* delay */
	if (!(_INB(port) & 0x20))
		return -EIO;

	/* Setup dual-port memory window */
	_OUTB(port + 1, tmp);
	hw->regs[1] = tmp;

	/* Enable memory access */
	tmp = hw->regs[0] | 0x04;
	if (hw->irq) {
		i = get_option_index(s508_irq_options, hw->irq);
		if (i) tmp |= s507_irqmask[i - 1];
	}
	_OUTB(port, tmp);
	hw->regs[0] = tmp;		/* update mirror */
	for (i = 0; i < SDLA_IODELAY; ++i);	/* delay */
	return (_INB(port) & 0x08) ? 0 : -EIO;
}

/*============================================================================
 * Initialize S508 adapter.
 */
static int init_s508 (sdlahw_t* hw)
{
	unsigned port = hw->port;
	int tmp, i;

	if (!detect_s508(port))
		return -ENODEV;

	/* Verify configuration options */
	i = get_option_index(s508_dpmbase_options, virt_to_phys(hw->dpmbase));
	if (i == 0)
		return -EINVAL;

	/* Setup memory configuration */
	tmp = s508_hmcr[i - 1];
	_OUTB(port + 1, tmp);
	hw->regs[1] = tmp;

	/* Enable memory access */
	_OUTB(port, 0x04);
	hw->regs[0] = 0x04;		/* update mirror */
	for (i = 0; i < SDLA_IODELAY; ++i);	/* delay */
	return (_INB(port + 1) & 0x04) ? 0 : -EIO;
}

/*============================================================================
 * Detect S502A adapter.
 *	Following tests are used to detect S502A adapter:
 *	1. All registers other than status (BASE) should read 0xFF
 *	2. After writing 00001000b to control register, status register should
 *	   read 01000000b.
 *	3. After writing 0 to control register, status register should still
 *	   read  01000000b.
 *	4. After writing 00000100b to control register, status register should
 *	   read 01000100b.
 *	Return 1 if detected o.k. or 0 if failed.
 *	Note:	This test is destructive! Adapter will be left in shutdown
 *		state after the test.
 */
static int detect_s502a (int port)
{
	int i, j;

	if (!get_option_index(s502_port_options, port))
		return 0;
	
	for (j = 1; j < SDLA_MAXIORANGE; ++j) {
		if (_INB(port + j) != 0xFF)
			return 0;
		for (i = 0; i < SDLA_IODELAY; ++i);	/* delay */
	}

	_OUTB(port, 0x08);			/* halt CPU */
	_OUTB(port, 0x08);
	_OUTB(port, 0x08);
	for (i = 0; i < SDLA_IODELAY; ++i);	/* delay */
	if (_INB(port) != 0x40)
		return 0;
	_OUTB(port, 0x00);
	for (i = 0; i < SDLA_IODELAY; ++i);	/* delay */
	if (_INB(port) != 0x40)
		return 0;
	_OUTB(port, 0x04);
	for (i = 0; i < SDLA_IODELAY; ++i);	/* delay */
	if (_INB(port) != 0x44)
		return 0;

	/* Reset adapter */
	_OUTB(port, 0x08);
	_OUTB(port, 0x08);
	_OUTB(port, 0x08);
	_OUTB(port + 1, 0xFF);
	return 1;
}

/*============================================================================
 * Detect S502E adapter.
 *	Following tests are used to verify adapter presence:
 *	1. All registers other than status (BASE) should read 0xFF.
 *	2. After writing 0 to CPU control register (BASE+3), status register
 *	   (BASE) should read 11111000b.
 *	3. After writing 00000100b to port BASE (set bit 2), status register
 *	   (BASE) should read 11111100b.
 *	Return 1 if detected o.k. or 0 if failed.
 *	Note:	This test is destructive! Adapter will be left in shutdown
 *		state after the test.
 */
static int detect_s502e (int port)
{
	int i, j;

	if (!get_option_index(s502_port_options, port))
		return 0;
	for (j = 1; j < SDLA_MAXIORANGE; ++j) {
		if (_INB(port + j) != 0xFF)
			return 0;
		for (i = 0; i < SDLA_IODELAY; ++i);	/* delay */
	}

	_OUTB(port + 3, 0);			/* CPU control reg. */
	for (i = 0; i < SDLA_IODELAY; ++i);	/* delay */
	if (_INB(port) != 0xF8)			/* read status */
		return 0;
	_OUTB(port, 0x04);			/* set bit 2 */
	for (i = 0; i < SDLA_IODELAY; ++i);	/* delay */
	if (_INB(port) != 0xFC)			/* verify */
		return 0;

	/* Reset adapter */
	_OUTB(port, 0);
	return 1;
}

/*============================================================================
 * Detect s503 adapter.
 *	Following tests are used to verify adapter presence:
 *	1. All registers other than status (BASE) should read 0xFF.
 *	2. After writing 0 to control register (BASE), status register (BASE)
 *	   should read 11110000b.
 *	3. After writing 00000100b (set bit 2) to control register (BASE),
 *	   status register should read 11110010b.
 *	Return 1 if detected o.k. or 0 if failed.
 *	Note:	This test is destructive! Adapter will be left in shutdown
 *		state after the test.
 */
static int detect_s503 (int port)
{
	int i, j;

	if (!get_option_index(s503_port_options, port))
		return 0;
	for (j = 1; j < SDLA_MAXIORANGE; ++j) {
		if (_INB(port + j) != 0xFF)
			return 0;
		for (i = 0; i < SDLA_IODELAY; ++i);	/* delay */
	}

	_OUTB(port, 0);				/* reset control reg.*/
	for (i = 0; i < SDLA_IODELAY; ++i);	/* delay */
	if (_INB(port) != 0xF0)			/* read status */
		return 0;
	_OUTB(port, 0x04);			/* set bit 2 */
	for (i = 0; i < SDLA_IODELAY; ++i);	/* delay */
	if (_INB(port) != 0xF2)			/* verify */
		return 0;

	/* Reset adapter */
	_OUTB(port, 0);
	return 1;
}

/*============================================================================
 * Detect s507 adapter.
 *	Following tests are used to detect s507 adapter:
 *	1. All ports should read the same value.
 *	2. After writing 0x00 to control register, status register should read
 *	   ?011000?b.
 *	3. After writing 0x01 to control register, status register should read
 *	   ?011001?b.
 *	Return 1 if detected o.k. or 0 if failed.
 *	Note:	This test is destructive! Adapter will be left in shutdown
 *		state after the test.
 */
static int detect_s507 (int port)
{
	int tmp, i, j;

	if (!get_option_index(s508_port_options, port))
		return 0;
	tmp = _INB(port);
	for (j = 1; j < S507_IORANGE; ++j) {
		if (_INB(port + j) != tmp)
			return 0;
		for (i = 0; i < SDLA_IODELAY; ++i);	/* delay */
	}

	_OUTB(port, 0x00);
	for (i = 0; i < SDLA_IODELAY; ++i);	/* delay */
	if ((_INB(port) & 0x7E) != 0x30)
		return 0;
	_OUTB(port, 0x01);
	for (i = 0; i < SDLA_IODELAY; ++i);	/* delay */
	if ((_INB(port) & 0x7E) != 0x32)
		return 0;

	/* Reset adapter */
	_OUTB(port, 0x00);
	return 1;
}

/*============================================================================
 * Detect s508 adapter.
 *	Following tests are used to detect s508 adapter:
 *	1. After writing 0x00 to control register, status register should read
 *	   ??000000b.
 *	2. After writing 0x10 to control register, status register should read
 *	   ??010000b
 *	Return 1 if detected o.k. or 0 if failed.
 *	Note:	This test is destructive! Adapter will be left in shutdown
 *		state after the test.
 */
static int detect_s508 (int port)
{
	int i;

	if (!get_option_index(s508_port_options, port))
		return 0;
	_OUTB(port, 0x00);
	for (i = 0; i < SDLA_IODELAY; ++i);	/* delay */
	if ((_INB(port + 1) & 0x3F) != 0x00)
		return 0;
	_OUTB(port, 0x10);
	for (i = 0; i < SDLA_IODELAY; ++i);	/* delay */
	if ((_INB(port + 1) & 0x3F) != 0x10)
		return 0;

	/* Reset adapter */
	_OUTB(port, 0x00);
	return 1;
}

/*============================================================================
 * Detect s514 PCI adapter.
 *      Return 1 if detected o.k. or 0 if failed.
 *      Note:   This test is destructive! Adapter will be left in shutdown
 *              state after the test.
 */
static int detect_s514 (sdlahw_t* hw)
{
	unsigned char CPU_no, slot_no, auto_slot_cfg;
	int number_S514_cards = 0;
	u32 S514_mem_base_addr = 0;
	u32 ut_u32;
	struct pci_dev *pci_dev;


#ifndef CONFIG_PCI
        printk(KERN_INFO "%s: Linux not compiled for PCI usage!\n", modname);
        return 0;
#endif

	/*
	The 'setup()' procedure in 'sdlamain.c' passes the CPU number and the
	slot number defined in 'router.conf' via the 'port' definition.
	*/
	CPU_no = hw->S514_cpu_no[0];
	slot_no = hw->S514_slot_no;
	auto_slot_cfg = hw->auto_pci_cfg;

	if (auto_slot_cfg){
		printk(KERN_INFO "%s: srch... S514 card, CPU %c, Slot=Auto\n",
		modname, CPU_no);

	}else{
		printk(KERN_INFO "%s: srch... S514 card, CPU %c, Slot #%d\n",
		modname, CPU_no, slot_no);
	}
	
	/* check to see that CPU A or B has been selected in 'router.conf' */
	switch(CPU_no) {
		case S514_CPU_A:
		case S514_CPU_B:
			break;
	
		default:
			printk(KERN_INFO "%s: S514 CPU definition invalid.\n", 
				modname);
			printk(KERN_INFO "Must be 'A' or 'B'\n");
			return 0;
	}

	number_S514_cards = find_s514_adapter(hw, 0);
	if(!number_S514_cards)
		return 0;

	/* we are using a single S514 adapter with a slot of 0 so re-read the */	
	/* location of this adapter */
	if((number_S514_cards == 1) && auto_slot_cfg) {	
        	number_S514_cards = find_s514_adapter(hw, 1);
		if(!number_S514_cards) {
			printk(KERN_INFO "%s: Error finding PCI card\n",
				modname);
			return 0;
		}
	}

	pci_dev = hw->pci_dev;
	/* read the physical memory base address */
	S514_mem_base_addr = (CPU_no == S514_CPU_A) ? 
		(pci_dev->resource[1].start) :
		(pci_dev->resource[2].start);
	
	printk(KERN_INFO "%s: S514 PCI memory at 0x%X\n",
		modname, S514_mem_base_addr);
	if(!S514_mem_base_addr) {
		if(CPU_no == S514_CPU_B)
			printk(KERN_INFO "%s: CPU #B not present on the card\n", 				modname);
		else
			printk(KERN_INFO "%s: No PCI memory allocated to card\n",				modname);
		return 0;
	}

	/* enable the PCI memory */
	pci_read_config_dword(pci_dev, 
		(CPU_no == S514_CPU_A) ? PCI_MAP0_DWORD : PCI_MAP1_DWORD,
		&ut_u32);
	pci_write_config_dword(pci_dev,
		(CPU_no == S514_CPU_A) ? PCI_MAP0_DWORD : PCI_MAP1_DWORD,
		(ut_u32 | PCI_MEMORY_ENABLE));

	/* check the IRQ allocated and enable IRQ usage */
	if(!(hw->irq = pci_dev->irq)) {
		printk(KERN_INFO "%s: IRQ not allocated to S514 adapter\n",
			modname);
                return 0;
	}

	/* BUG FIX : Mar 6 2000
 	 * On a initial loading of the card, we must check
         * and clear PCI interrupt bits, due to a reset
         * problem on some other boards.  i.e. An interrupt
         * might be pending, even after system bootup, 
         * in which case, when starting wanrouter the machine
         * would crash. 
	 */
	if (init_pci_slot(hw))
		return 0;

        pci_read_config_dword(pci_dev, PCI_INT_CONFIG, &ut_u32);
        ut_u32 |= (CPU_no == S514_CPU_A) ?
                PCI_ENABLE_IRQ_CPU_A : PCI_ENABLE_IRQ_CPU_B;
        pci_write_config_dword(pci_dev, PCI_INT_CONFIG, ut_u32);

	printk(KERN_INFO "%s: IRQ %d allocated to the S514 card\n",
		modname, hw->irq);

	/* map the physical PCI memory to virtual memory */
	(void *)hw->dpmbase = ioremap((unsigned long)S514_mem_base_addr,
		(unsigned long)MAX_SIZEOF_S514_MEMORY);
    	/* map the physical control register memory to virtual memory */
	hw->vector = (unsigned long)ioremap(
		(unsigned long)(S514_mem_base_addr + S514_CTRL_REG_BYTE),
		(unsigned long)16);
     
        if(!hw->dpmbase || !hw->vector) {
		printk(KERN_INFO "%s: PCI virtual memory allocation failed\n",
			modname);
                return 0;
	}

	/* halt the adapter */
	writeb (S514_CPU_HALT, hw->vector);	

	return 1;
}

/*============================================================================
 * Find the S514 PCI adapter in the PCI bus.
 *      Return the number of S514 adapters found (0 if no adapter found).
 */
static int find_s514_adapter(sdlahw_t* hw, char find_first_S514_card)
{
        unsigned char slot_no;
        int number_S514_cards = 0;
	char S514_found_in_slot = 0;
        u16 PCI_subsys_vendor;

        struct pci_dev *pci_dev = NULL;
 
       slot_no = hw->S514_slot_no;
  
	while ((pci_dev = pci_find_device(V3_VENDOR_ID, V3_DEVICE_ID, pci_dev))
        	!= NULL) {
                
		pci_read_config_word(pci_dev, PCI_SUBSYS_VENDOR_WORD,
                        &PCI_subsys_vendor);
                
		if(PCI_subsys_vendor != SANGOMA_SUBSYS_VENDOR)
                	continue;
        	
		hw->pci_dev = pci_dev;
		
		if(find_first_S514_card)
			return(1);
		
                number_S514_cards ++;
                
		printk(KERN_INFO
			"%s: S514 card found, slot #%d (devfn 0x%X)\n",
                        modname, ((pci_dev->devfn >> 3) & PCI_DEV_SLOT_MASK),
			pci_dev->devfn);
		
		if (hw->auto_pci_cfg){
			hw->S514_slot_no = ((pci_dev->devfn >> 3) & PCI_DEV_SLOT_MASK);
			slot_no = hw->S514_slot_no;
			
		}else if (((pci_dev->devfn >> 3) & PCI_DEV_SLOT_MASK) == slot_no){
                        S514_found_in_slot = 1;
                        break;
                }
        }

	/* if no S514 adapter has been found, then exit */
        if (!number_S514_cards) {
                printk(KERN_INFO "%s: Error, no S514 adapters found\n", modname);
                return 0;
        }
        /* if more than one S514 card has been found, then the user must have */        /* defined a slot number so that the correct adapter is used */
        else if ((number_S514_cards > 1) && hw->auto_pci_cfg) {
                printk(KERN_INFO "%s: Error, PCI Slot autodetect Failed! \n"
				 "%s:        More than one S514 adapter found.\n"
				 "%s:        Disable the Autodetect feature and supply\n"
				 "%s:        the PCISLOT numbers for each card.\n",
                        modname,modname,modname,modname);
                return 0;
        }
        /* if the user has specified a slot number and the S514 adapter has */
        /* not been found in that slot, then exit */
        else if (!hw->auto_pci_cfg && !S514_found_in_slot) {
                printk(KERN_INFO
			"%s: Error, S514 card not found in specified slot #%d\n",
                        modname, slot_no);
                return 0;
        }

	return (number_S514_cards);
}



/******* Miscellaneous ******************************************************/

/*============================================================================
 * Calibrate SDLA memory access delay.
 * Count number of idle loops made within 1 second and then calculate the
 * number of loops that should be made to achive desired delay.
 */
static int calibrate_delay (int mks)
{
	unsigned int delay;
	unsigned long stop;

	for (delay = 0, stop = SYSTEM_TICK + HZ; SYSTEM_TICK < stop; ++delay);
	return (delay/(1000000L/mks) + 1);
}

/*============================================================================
 * Get option's index into the options list.
 *	Return option's index (1 .. N) or zero if option is invalid.
 */
static int get_option_index (unsigned* optlist, unsigned optval)
{
	int i;

	for (i = 1; i <= optlist[0]; ++i)
		if ( optlist[i] == optval)
			return i;
	return 0;
}

/*============================================================================
 * Check memory region to see if it's available. 
 * Return:	0	ok.
 */
static unsigned check_memregion (void* ptr, unsigned len)
{
	volatile unsigned char* p = ptr;

        for (; len && (readb (p) == 0xFF); --len, ++p) {
                writeb (0, p);          /* attempt to write 0 */
                if (readb(p) != 0xFF) { /* still has to read 0xFF */
                        writeb (0xFF, p);/* restore original value */
                        break;          /* not good */
                }
        }

	return len;
}

/*============================================================================
 * Test memory region.
 * Return:	size of the region that passed the test.
 * Note:	Region size must be multiple of 2 !
 */
static unsigned test_memregion (void* ptr, unsigned len)
{
	volatile unsigned short* w_ptr;
	unsigned len_w = len >> 1;	/* region len in words */
	unsigned i;

        for (i = 0, w_ptr = ptr; i < len_w; ++i, ++w_ptr)
                writew (0xAA55, w_ptr);
        
	for (i = 0, w_ptr = ptr; i < len_w; ++i, ++w_ptr)
                if (readw (w_ptr) != 0xAA55) {
                        len_w = i;
                        break;
                }

        for (i = 0, w_ptr = ptr; i < len_w; ++i, ++w_ptr)
                writew (0x55AA, w_ptr);
        
        for (i = 0, w_ptr = ptr; i < len_w; ++i, ++w_ptr)
                if (readw(w_ptr) != 0x55AA) {
                        len_w = i;
                        break;
                }
        
        for (i = 0, w_ptr = ptr; i < len_w; ++i, ++w_ptr)
		writew (0, w_ptr);

        return len_w << 1;
}

/*============================================================================
 * Calculate 16-bit CRC using CCITT polynomial.
 */
static unsigned short checksum (unsigned char* buf, unsigned len)
{
	unsigned short crc = 0;
	unsigned mask, flag;

	for (; len; --len, ++buf) {
		for (mask = 0x80; mask; mask >>= 1) {
			flag = (crc & 0x8000);
			crc <<= 1;
			crc |= ((*buf & mask) ? 1 : 0);
			if (flag) crc ^= 0x1021;
		}
	}
	return crc;
}

static int init_pci_slot(sdlahw_t *hw)
{

	u32 int_status;
	int volatile found=0;
	int i=0;

	/* Check if this is a very first load for a specific
         * pci card. If it is, clear the interrput bits, and
         * set the flag indicating that this card was initialized.
	 */
	
	for (i=0; (i<MAX_S514_CARDS) && !found; i++){
		if (pci_slot_ar[i] == hw->S514_slot_no){
			found=1;
			break;
		}
		if (pci_slot_ar[i] == 0xFF){
			break;
		}
	}

	if (!found){
		read_S514_int_stat(hw,&int_status);
		S514_intack(hw,int_status);
		if (i == MAX_S514_CARDS){
			printk(KERN_INFO "%s: Critical Error !!!\n",modname);
			printk(KERN_INFO 
				"%s: Number of Sangoma PCI cards exceeded maximum limit.\n",
					modname);
			printk(KERN_INFO "Please contact Sangoma Technologies\n");
			return 1;
		}
		pci_slot_ar[i] = hw->S514_slot_no;
	}
	return 0;
}

static int pci_probe(sdlahw_t *hw)
{

        unsigned char slot_no;
        int number_S514_cards = 0;
        u16 PCI_subsys_vendor;
	u16 PCI_card_type;

        struct pci_dev *pci_dev = NULL;
	struct pci_bus *bus = NULL;
 
       slot_no = 0;
  
	while ((pci_dev = pci_find_device(V3_VENDOR_ID, V3_DEVICE_ID, pci_dev))
        	!= NULL) {
		
                pci_read_config_word(pci_dev, PCI_SUBSYS_VENDOR_WORD,
                        &PCI_subsys_vendor);
		
                if(PCI_subsys_vendor != SANGOMA_SUBSYS_VENDOR)
                	continue;

		pci_read_config_word(pci_dev, PCI_CARD_TYPE,
                        &PCI_card_type);
	
		bus = pci_dev->bus;
		
		/* A dual cpu card can support up to 4 physical connections,
		 * where a single cpu card can support up to 2 physical
		 * connections.  The FT1 card can only support a single 
		 * connection, however we cannot distinguish between a Single
		 * CPU card and an FT1 card. */
		if (PCI_card_type == S514_DUAL_CPU){
                	number_S514_cards += 4;
			 printk(KERN_INFO
				"wanpipe: S514-PCI card found, cpu(s) 2, bus #%d, slot #%d, irq #%d\n",
                        	bus->number,((pci_dev->devfn >> 3) & PCI_DEV_SLOT_MASK),
				pci_dev->irq);
		}else{
			number_S514_cards += 2;
			printk(KERN_INFO
				"wanpipe: S514-PCI card found, cpu(s) 1, bus #%d, slot #%d, irq #%d\n",
                        	bus->number,((pci_dev->devfn >> 3) & PCI_DEV_SLOT_MASK),
				pci_dev->irq);
		}
        }

	return number_S514_cards;

}



EXPORT_SYMBOL(wanpipe_hw_probe);

unsigned wanpipe_hw_probe(void)
{
	sdlahw_t hw;
	unsigned* opt = s508_port_options; 
	unsigned cardno=0;
	int i;
	
	memset(&hw, 0, sizeof(hw));
	
	for (i = 1; i <= opt[0]; i++) {
		if (detect_s508(opt[i])){
			/* S508 card can support up to two physical links */
			cardno+=2;
			printk(KERN_INFO "wanpipe: S508-ISA card found, port 0x%x\n",opt[i]);
		}
	}

      #ifdef CONFIG_PCI
	hw.S514_slot_no = 0;
	cardno += pci_probe(&hw);
      #else
	printk(KERN_INFO "wanpipe: Warning, Kernel not compiled for PCI support!\n");
	printk(KERN_INFO "wanpipe: PCI Hardware Probe Failed!\n");
      #endif

	return cardno;
}

/****** End *****************************************************************/
