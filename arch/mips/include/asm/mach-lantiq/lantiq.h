/*
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License version 2 as published
 *  by the Free Software Foundation.
 *
 *  Copyright (C) 2010 John Crispin <blogic@openwrt.org>
 */
#ifndef _LANTIQ_H__
#define _LANTIQ_H__

#include <linux/irq.h>
#include <linux/device.h>
#include <linux/clk.h>

/* generic reg access functions */
#define ltq_r32(reg)		__raw_readl(reg)
#define ltq_w32(val, reg)	__raw_writel(val, reg)
#define ltq_w32_mask(clear, set, reg)	\
	ltq_w32((ltq_r32(reg) & ~(clear)) | (set), reg)
#define ltq_r8(reg)		__raw_readb(reg)
#define ltq_w8(val, reg)	__raw_writeb(val, reg)

/* register access macros for EBU and CGU */
#define ltq_ebu_w32(x, y)	ltq_w32((x), ltq_ebu_membase + (y))
#define ltq_ebu_r32(x)		ltq_r32(ltq_ebu_membase + (x))
#define ltq_ebu_w32_mask(x, y, z) \
	ltq_w32_mask(x, y, ltq_ebu_membase + (z))
extern __iomem void *ltq_ebu_membase;

extern unsigned int ltq_get_cpu_ver(void);
extern unsigned int ltq_get_soc_type(void);

/* spinlock all ebu i/o */
extern spinlock_t ebu_lock;

/* some irq helpers */
extern void ltq_disable_irq(struct irq_data *data);
extern void ltq_mask_and_ack_irq(struct irq_data *data);
extern void ltq_enable_irq(struct irq_data *data);

/* clock handling */
extern int clk_activate(struct clk *clk);
extern void clk_deactivate(struct clk *clk);
extern struct clk *clk_get_cpu(void);
extern struct clk *clk_get_fpi(void);
extern struct clk *clk_get_io(void);

/* find out what bootsource we have */
extern unsigned char ltq_boot_select(void);
/* find out what caused the last cpu reset */
extern int ltq_reset_cause(void);

#define IOPORT_RESOURCE_START	0x10000000
#define IOPORT_RESOURCE_END	0xffffffff
#define IOMEM_RESOURCE_START	0x10000000
#define IOMEM_RESOURCE_END	0xffffffff
#define LTQ_FLASH_START		0x10000000
#define LTQ_FLASH_MAX		0x04000000

#endif
