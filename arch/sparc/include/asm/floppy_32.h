/* SPDX-License-Identifier: GPL-2.0 */
/* asm/floppy.h: Sparc specific parts of the Floppy driver.
 *
 * Copyright (C) 1995 David S. Miller (davem@davemloft.net)
 */

#ifndef __ASM_SPARC_FLOPPY_H
#define __ASM_SPARC_FLOPPY_H

#include <linux/of.h>
#include <linux/of_device.h>

#include <asm/pgtable.h>
#include <asm/idprom.h>
#include <asm/oplib.h>
#include <asm/auxio.h>
#include <asm/setup.h>
#include <asm/page.h>
#include <asm/irq.h>

/* We don't need no stinkin' I/O port allocation crap. */
#undef release_region
#undef request_region
#define release_region(X, Y)	do { } while(0)
#define request_region(X, Y, Z)	(1)

/* References:
 * 1) Netbsd Sun floppy driver.
 * 2) NCR 82077 controller manual
 * 3) Intel 82077 controller manual
 */
struct sun_flpy_controller {
	volatile unsigned char status_82072;  /* Main Status reg. */
#define dcr_82072              status_82072   /* Digital Control reg. */
#define status1_82077          status_82072   /* Auxiliary Status reg. 1 */

	volatile unsigned char data_82072;    /* Data fifo. */
#define status2_82077          data_82072     /* Auxiliary Status reg. 2 */

	volatile unsigned char dor_82077;     /* Digital Output reg. */
	volatile unsigned char tapectl_82077; /* What the? Tape control reg? */

	volatile unsigned char status_82077;  /* Main Status Register. */
#define drs_82077              status_82077   /* Digital Rate Select reg. */

	volatile unsigned char data_82077;    /* Data fifo. */
	volatile unsigned char ___unused;
	volatile unsigned char dir_82077;     /* Digital Input reg. */
#define dcr_82077              dir_82077      /* Config Control reg. */
};

/* You'll only ever find one controller on a SparcStation anyways. */
static struct sun_flpy_controller *sun_fdc = NULL;

struct sun_floppy_ops {
	unsigned char (*fd_inb)(int port);
	void (*fd_outb)(unsigned char value, int port);
};

static struct sun_floppy_ops sun_fdops;

#define fd_inb(port)              sun_fdops.fd_inb(port)
#define fd_outb(value,port)       sun_fdops.fd_outb(value,port)
#define fd_enable_dma()           sun_fd_enable_dma()
#define fd_disable_dma()          sun_fd_disable_dma()
#define fd_request_dma()          (0) /* nothing... */
#define fd_free_dma()             /* nothing... */
#define fd_clear_dma_ff()         /* nothing... */
#define fd_set_dma_mode(mode)     sun_fd_set_dma_mode(mode)
#define fd_set_dma_addr(addr)     sun_fd_set_dma_addr(addr)
#define fd_set_dma_count(count)   sun_fd_set_dma_count(count)
#define fd_enable_irq()           /* nothing... */
#define fd_disable_irq()          /* nothing... */
#define fd_request_irq()          sun_fd_request_irq()
#define fd_free_irq()             /* nothing... */
#if 0  /* P3: added by Alain, these cause a MMU corruption. 19960524 XXX */
#define fd_dma_mem_alloc(size)    ((unsigned long) vmalloc(size))
#define fd_dma_mem_free(addr,size) (vfree((void *)(addr)))
#endif

/* XXX This isn't really correct. XXX */
#define get_dma_residue(x)        (0)

#define FLOPPY0_TYPE  4
#define FLOPPY1_TYPE  0

/* Super paranoid... */
#undef HAVE_DISABLE_HLT

/* Here is where we catch the floppy driver trying to initialize,
 * therefore this is where we call the PROM device tree probing
 * routine etc. on the Sparc.
 */
#define FDC1                      sun_floppy_init()

#define N_FDC    1
#define N_DRIVE  8

/* No 64k boundary crossing problems on the Sparc. */
#define CROSS_64KB(a,s) (0)

/* Routines unique to each controller type on a Sun. */
static void sun_set_dor(unsigned char value, int fdc_82077)
{
	if (fdc_82077)
		sun_fdc->dor_82077 = value;
}

static unsigned char sun_read_dir(void)
{
	return sun_fdc->dir_82077;
}

static unsigned char sun_82072_fd_inb(int port)
{
	udelay(5);
	switch(port & 7) {
	default:
		printk("floppy: Asked to read unknown port %d\n", port);
		panic("floppy: Port bolixed.");
	case 4: /* FD_STATUS */
		return sun_fdc->status_82072 & ~STATUS_DMA;
	case 5: /* FD_DATA */
		return sun_fdc->data_82072;
	case 7: /* FD_DIR */
		return sun_read_dir();
	}
	panic("sun_82072_fd_inb: How did I get here?");
}

static void sun_82072_fd_outb(unsigned char value, int port)
{
	udelay(5);
	switch(port & 7) {
	default:
		printk("floppy: Asked to write to unknown port %d\n", port);
		panic("floppy: Port bolixed.");
	case 2: /* FD_DOR */
		sun_set_dor(value, 0);
		break;
	case 5: /* FD_DATA */
		sun_fdc->data_82072 = value;
		break;
	case 7: /* FD_DCR */
		sun_fdc->dcr_82072 = value;
		break;
	case 4: /* FD_STATUS */
		sun_fdc->status_82072 = value;
		break;
	}
	return;
}

static unsigned char sun_82077_fd_inb(int port)
{
	udelay(5);
	switch(port & 7) {
	default:
		printk("floppy: Asked to read unknown port %d\n", port);
		panic("floppy: Port bolixed.");
	case 0: /* FD_STATUS_0 */
		return sun_fdc->status1_82077;
	case 1: /* FD_STATUS_1 */
		return sun_fdc->status2_82077;
	case 2: /* FD_DOR */
		return sun_fdc->dor_82077;
	case 3: /* FD_TDR */
		return sun_fdc->tapectl_82077;
	case 4: /* FD_STATUS */
		return sun_fdc->status_82077 & ~STATUS_DMA;
	case 5: /* FD_DATA */
		return sun_fdc->data_82077;
	case 7: /* FD_DIR */
		return sun_read_dir();
	}
	panic("sun_82077_fd_inb: How did I get here?");
}

static void sun_82077_fd_outb(unsigned char value, int port)
{
	udelay(5);
	switch(port & 7) {
	default:
		printk("floppy: Asked to write to unknown port %d\n", port);
		panic("floppy: Port bolixed.");
	case 2: /* FD_DOR */
		sun_set_dor(value, 1);
		break;
	case 5: /* FD_DATA */
		sun_fdc->data_82077 = value;
		break;
	case 7: /* FD_DCR */
		sun_fdc->dcr_82077 = value;
		break;
	case 4: /* FD_STATUS */
		sun_fdc->status_82077 = value;
		break;
	case 3: /* FD_TDR */
		sun_fdc->tapectl_82077 = value;
		break;
	}
	return;
}

/* For pseudo-dma (Sun floppy drives have no real DMA available to
 * them so we must eat the data fifo bytes directly ourselves) we have
 * three state variables.  doing_pdma tells our inline low-level
 * assembly floppy interrupt entry point whether it should sit and eat
 * bytes from the fifo or just transfer control up to the higher level
 * floppy interrupt c-code.  I tried very hard but I could not get the
 * pseudo-dma to work in c-code without getting many overruns and
 * underruns.  If non-zero, doing_pdma encodes the direction of
 * the transfer for debugging.  1=read 2=write
 */

/* Common routines to all controller types on the Sparc. */
static inline void virtual_dma_init(void)
{
	/* nothing... */
}

static inline void sun_fd_disable_dma(void)
{
	doing_pdma = 0;
	pdma_base = NULL;
}

static inline void sun_fd_set_dma_mode(int mode)
{
	switch(mode) {
	case DMA_MODE_READ:
		doing_pdma = 1;
		break;
	case DMA_MODE_WRITE:
		doing_pdma = 2;
		break;
	default:
		printk("Unknown dma mode %d\n", mode);
		panic("floppy: Giving up...");
	}
}

static inline void sun_fd_set_dma_addr(char *buffer)
{
	pdma_vaddr = buffer;
}

static inline void sun_fd_set_dma_count(int length)
{
	pdma_size = length;
}

static inline void sun_fd_enable_dma(void)
{
	pdma_base = pdma_vaddr;
	pdma_areasize = pdma_size;
}

int sparc_floppy_request_irq(unsigned int irq, irq_handler_t irq_handler);

static int sun_fd_request_irq(void)
{
	static int once = 0;

	if (!once) {
		once = 1;
		return sparc_floppy_request_irq(FLOPPY_IRQ, floppy_interrupt);
	} else {
		return 0;
	}
}

static struct linux_prom_registers fd_regs[2];

static int sun_floppy_init(void)
{
	struct platform_device *op;
	struct device_node *dp;
	struct resource r;
	char state[128];
	phandle fd_node;
	phandle tnode;
	int num_regs;

	use_virtual_dma = 1;

	/* Forget it if we aren't on a machine that could possibly
	 * ever have a floppy drive.
	 */
	if (sparc_cpu_model != sun4m) {
		/* We certainly don't have a floppy controller. */
		goto no_sun_fdc;
	}
	/* Well, try to find one. */
	tnode = prom_getchild(prom_root_node);
	fd_node = prom_searchsiblings(tnode, "obio");
	if (fd_node != 0) {
		tnode = prom_getchild(fd_node);
		fd_node = prom_searchsiblings(tnode, "SUNW,fdtwo");
	} else {
		fd_node = prom_searchsiblings(tnode, "fd");
	}
	if (fd_node == 0) {
		goto no_sun_fdc;
	}

	/* The sun4m lets us know if the controller is actually usable. */
	if (prom_getproperty(fd_node, "status", state, sizeof(state)) != -1) {
		if(!strcmp(state, "disabled")) {
			goto no_sun_fdc;
		}
	}
	num_regs = prom_getproperty(fd_node, "reg", (char *) fd_regs, sizeof(fd_regs));
	num_regs = (num_regs / sizeof(fd_regs[0]));
	prom_apply_obio_ranges(fd_regs, num_regs);
	memset(&r, 0, sizeof(r));
	r.flags = fd_regs[0].which_io;
	r.start = fd_regs[0].phys_addr;
	sun_fdc = of_ioremap(&r, 0, fd_regs[0].reg_size, "floppy");

	/* Look up irq in platform_device.
	 * We try "SUNW,fdtwo" and "fd"
	 */
	op = NULL;
	for_each_node_by_name(dp, "SUNW,fdtwo") {
		op = of_find_device_by_node(dp);
		if (op)
			break;
	}
	if (!op) {
		for_each_node_by_name(dp, "fd") {
			op = of_find_device_by_node(dp);
			if (op)
				break;
		}
	}
	if (!op)
		goto no_sun_fdc;

	FLOPPY_IRQ = op->archdata.irqs[0];

	/* Last minute sanity check... */
	if (sun_fdc->status_82072 == 0xff) {
		sun_fdc = NULL;
		goto no_sun_fdc;
	}

	sun_fdops.fd_inb = sun_82077_fd_inb;
	sun_fdops.fd_outb = sun_82077_fd_outb;
	fdc_status = &sun_fdc->status_82077;

	if (sun_fdc->dor_82077 == 0x80) {
		sun_fdc->dor_82077 = 0x02;
		if (sun_fdc->dor_82077 == 0x80) {
			sun_fdops.fd_inb = sun_82072_fd_inb;
			sun_fdops.fd_outb = sun_82072_fd_outb;
			fdc_status = &sun_fdc->status_82072;
		}
	}

	/* Success... */
	allowed_drive_mask = 0x01;
	return (int) sun_fdc;

no_sun_fdc:
	return -1;
}

static int sparc_eject(void)
{
	set_dor(0x00, 0xff, 0x90);
	udelay(500);
	set_dor(0x00, 0x6f, 0x00);
	udelay(500);
	return 0;
}

#define fd_eject(drive) sparc_eject()

#define EXTRA_FLOPPY_PARAMS

static DEFINE_SPINLOCK(dma_spin_lock);

#define claim_dma_lock() \
({	unsigned long flags; \
	spin_lock_irqsave(&dma_spin_lock, flags); \
	flags; \
})

#define release_dma_lock(__flags) \
	spin_unlock_irqrestore(&dma_spin_lock, __flags);

#endif /* !(__ASM_SPARC_FLOPPY_H) */
