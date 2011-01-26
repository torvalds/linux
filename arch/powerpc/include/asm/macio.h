#ifndef __MACIO_ASIC_H__
#define __MACIO_ASIC_H__
#ifdef __KERNEL__

#include <linux/of_device.h>

extern struct bus_type macio_bus_type;

/* MacIO device driver is defined later */
struct macio_driver;
struct macio_chip;

#define MACIO_DEV_COUNT_RESOURCES	8
#define MACIO_DEV_COUNT_IRQS		8

/*
 * the macio_bus structure is used to describe a "virtual" bus
 * within a MacIO ASIC. It's typically provided by a macio_pci_asic
 * PCI device, but could be provided differently as well (nubus
 * machines using a fake OF tree).
 *
 * The pdev field can be NULL on non-PCI machines
 */
struct macio_bus
{
	struct macio_chip	*chip;		/* macio_chip (private use) */
	int			index;		/* macio chip index in system */
#ifdef CONFIG_PCI
	struct pci_dev		*pdev;		/* PCI device hosting this bus */
#endif
};

/*
 * the macio_dev structure is used to describe a device
 * within an Apple MacIO ASIC.
 */
struct macio_dev
{
	struct macio_bus	*bus;		/* macio bus this device is on */
	struct macio_dev	*media_bay;	/* Device is part of a media bay */
	struct platform_device	ofdev;
	struct device_dma_parameters dma_parms; /* ide needs that */
	int			n_resources;
	struct resource		resource[MACIO_DEV_COUNT_RESOURCES];
	int			n_interrupts;
	struct resource		interrupt[MACIO_DEV_COUNT_IRQS];
};
#define	to_macio_device(d) container_of(d, struct macio_dev, ofdev.dev)
#define	of_to_macio_device(d) container_of(d, struct macio_dev, ofdev)

extern struct macio_dev *macio_dev_get(struct macio_dev *dev);
extern void macio_dev_put(struct macio_dev *dev);

/*
 * Accessors to resources & interrupts and other device
 * fields
 */

static inline int macio_resource_count(struct macio_dev *dev)
{
	return dev->n_resources;
}

static inline unsigned long macio_resource_start(struct macio_dev *dev, int resource_no)
{
	return dev->resource[resource_no].start;
}

static inline unsigned long macio_resource_end(struct macio_dev *dev, int resource_no)
{
	return dev->resource[resource_no].end;
}

static inline unsigned long macio_resource_len(struct macio_dev *dev, int resource_no)
{
	struct resource *res = &dev->resource[resource_no];
	if (res->start == 0 || res->end == 0 || res->end < res->start)
		return 0;
	return res->end - res->start + 1;
}

extern int macio_enable_devres(struct macio_dev *dev);

extern int macio_request_resource(struct macio_dev *dev, int resource_no, const char *name);
extern void macio_release_resource(struct macio_dev *dev, int resource_no);
extern int macio_request_resources(struct macio_dev *dev, const char *name);
extern void macio_release_resources(struct macio_dev *dev);

static inline int macio_irq_count(struct macio_dev *dev)
{
	return dev->n_interrupts;
}

static inline int macio_irq(struct macio_dev *dev, int irq_no)
{
	return dev->interrupt[irq_no].start;
}

static inline void macio_set_drvdata(struct macio_dev *dev, void *data)
{
	dev_set_drvdata(&dev->ofdev.dev, data);
}

static inline void* macio_get_drvdata(struct macio_dev *dev)
{
	return dev_get_drvdata(&dev->ofdev.dev);
}

static inline struct device_node *macio_get_of_node(struct macio_dev *mdev)
{
	return mdev->ofdev.dev.of_node;
}

#ifdef CONFIG_PCI
static inline struct pci_dev *macio_get_pci_dev(struct macio_dev *mdev)
{
	return mdev->bus->pdev;
}
#endif

/*
 * A driver for a mac-io chip based device
 */
struct macio_driver
{
	int	(*probe)(struct macio_dev* dev, const struct of_device_id *match);
	int	(*remove)(struct macio_dev* dev);

	int	(*suspend)(struct macio_dev* dev, pm_message_t state);
	int	(*resume)(struct macio_dev* dev);
	int	(*shutdown)(struct macio_dev* dev);

#ifdef CONFIG_PMAC_MEDIABAY
	void	(*mediabay_event)(struct macio_dev* dev, int mb_state);
#endif
	struct device_driver	driver;
};
#define	to_macio_driver(drv) container_of(drv,struct macio_driver, driver)

extern int macio_register_driver(struct macio_driver *);
extern void macio_unregister_driver(struct macio_driver *);

#endif /* __KERNEL__ */
#endif /* __MACIO_ASIC_H__ */
