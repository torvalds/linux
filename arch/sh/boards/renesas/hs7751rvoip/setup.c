/*
 * linux/arch/sh/kernel/setup_hs7751rvoip.c
 *
 * Copyright (C) 2000  Kazumoto Kojima
 *
 * Renesas Technology Sales HS7751RVoIP Support.
 *
 * Modified for HS7751RVoIP by
 * Atom Create Engineering Co., Ltd. 2002.
 * Lineo uSolutions, Inc. 2003.
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/irq.h>

#include <linux/hdreg.h>
#include <linux/ide.h>
#include <asm/io.h>
#include <asm/hs7751rvoip/hs7751rvoip.h>

#include <linux/mm.h>
#include <linux/vmalloc.h>

/* defined in mm/ioremap.c */
extern void * p3_ioremap(unsigned long phys_addr, unsigned long size, unsigned long flags);

unsigned int debug_counter;

const char *get_system_type(void)
{
	return "HS7751RVoIP";
}

/*
 * Initialize the board
 */
void __init platform_setup(void)
{
	printk(KERN_INFO "Renesas Technology Sales HS7751RVoIP-2 support.\n");
	ctrl_outb(0xf0, PA_OUTPORTR);
	debug_counter = 0;
}

void *area5_io8_base;
void *area6_io8_base;
void *area5_io16_base;
void *area6_io16_base;

int __init cf_init(void)
{
	pgprot_t prot;
	unsigned long paddrbase, psize;

	/* open I/O area window */
	paddrbase = virt_to_phys((void *)(PA_AREA5_IO+0x00000800));
	psize = PAGE_SIZE;
	prot = PAGE_KERNEL_PCC(1, _PAGE_PCC_COM16);
	area5_io16_base = p3_ioremap(paddrbase, psize, prot.pgprot);
	if (!area5_io16_base) {
		printk("allocate_cf_area : can't open CF I/O window!\n");
		return -ENOMEM;
	}

	/* XXX : do we need attribute and common-memory area also? */

	paddrbase = virt_to_phys((void *)PA_AREA6_IO);
	psize = PAGE_SIZE;
#if defined(CONFIG_HS7751RVOIP_CODEC)
	prot = PAGE_KERNEL_PCC(0, _PAGE_PCC_COM8);
#else
	prot = PAGE_KERNEL_PCC(0, _PAGE_PCC_IO8);
#endif
	area6_io8_base = p3_ioremap(paddrbase, psize, prot.pgprot);
	if (!area6_io8_base) {
		printk("allocate_cf_area : can't open CODEC I/O 8bit window!\n");
		return -ENOMEM;
	}
	prot = PAGE_KERNEL_PCC(0, _PAGE_PCC_IO16);
	area6_io16_base = p3_ioremap(paddrbase, psize, prot.pgprot);
	if (!area6_io16_base) {
		printk("allocate_cf_area : can't open CODEC I/O 16bit window!\n");
		return -ENOMEM;
	}

	return 0;
}

__initcall (cf_init);
