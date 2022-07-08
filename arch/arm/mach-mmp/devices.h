/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __MACH_DEVICE_H
#define __MACH_DEVICE_H

#include <linux/types.h>

#define MAX_RESOURCE_DMA	2

/* structure for describing the on-chip devices */
struct mmp_device_desc {
	const char	*dev_name;
	const char	*drv_name;
	int		id;
	int		irq;
	unsigned long	start;
	unsigned long	size;
	int		dma[MAX_RESOURCE_DMA];
};

#define PXA168_DEVICE(_name, _drv, _id, _irq, _start, _size, _dma...)	\
struct mmp_device_desc pxa168_device_##_name __initdata = {		\
	.dev_name	= "pxa168-" #_name,				\
	.drv_name	= _drv,						\
	.id		= _id,						\
	.irq		= IRQ_PXA168_##_irq,				\
	.start		= _start,					\
	.size		= _size,					\
	.dma		= { _dma },					\
};

#define PXA910_DEVICE(_name, _drv, _id, _irq, _start, _size, _dma...)	\
struct mmp_device_desc pxa910_device_##_name __initdata = {		\
	.dev_name	= "pxa910-" #_name,				\
	.drv_name	= _drv,						\
	.id		= _id,						\
	.irq		= IRQ_PXA910_##_irq,				\
	.start		= _start,					\
	.size		= _size,					\
	.dma		= { _dma },					\
};

#define MMP2_DEVICE(_name, _drv, _id, _irq, _start, _size, _dma...)	\
struct mmp_device_desc mmp2_device_##_name __initdata = {		\
	.dev_name	= "mmp2-" #_name,				\
	.drv_name	= _drv,						\
	.id		= _id,						\
	.irq		= IRQ_MMP2_##_irq,				\
	.start		= _start,					\
	.size		= _size,					\
	.dma		= { _dma },					\
}

extern int mmp_register_device(struct mmp_device_desc *, void *, size_t);
extern int pxa_usb_phy_init(void __iomem *phy_reg);
extern void pxa_usb_phy_deinit(void __iomem *phy_reg);

#endif /* __MACH_DEVICE_H */
