#include <linux/types.h>

#define MAX_RESOURCE_DMA	2

/* structure for describing the on-chip devices */
struct pxa_device_desc {
	const char	*dev_name;
	const char	*drv_name;
	int		id;
	int		irq;
	unsigned long	start;
	unsigned long	size;
	int		dma[MAX_RESOURCE_DMA];
};

#define PXA168_DEVICE(_name, _drv, _id, _irq, _start, _size, _dma...)	\
struct pxa_device_desc pxa168_device_##_name __initdata = {		\
	.dev_name	= "pxa168-" #_name,				\
	.drv_name	= _drv,						\
	.id		= _id,						\
	.irq		= IRQ_PXA168_##_irq,				\
	.start		= _start,					\
	.size		= _size,					\
	.dma		= { _dma },					\
};

#define PXA910_DEVICE(_name, _drv, _id, _irq, _start, _size, _dma...)	\
struct pxa_device_desc pxa910_device_##_name __initdata = {		\
	.dev_name	= "pxa910-" #_name,				\
	.drv_name	= _drv,						\
	.id		= _id,						\
	.irq		= IRQ_PXA910_##_irq,				\
	.start		= _start,					\
	.size		= _size,					\
	.dma		= { _dma },					\
};
extern int pxa_register_device(struct pxa_device_desc *, void *, size_t);
