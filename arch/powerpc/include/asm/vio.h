/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * IBM PowerPC Virtual I/O Infrastructure Support.
 *
 * Copyright (c) 2003 IBM Corp.
 *  Dave Engebretsen engebret@us.ibm.com
 *  Santiago Leon santil@us.ibm.com
 */

#ifndef _ASM_POWERPC_VIO_H
#define _ASM_POWERPC_VIO_H
#ifdef __KERNEL__

#include <linux/errno.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/mod_devicetable.h>
#include <linux/scatterlist.h>

#include <asm/hvcall.h>

/*
 * Architecture-specific constants for drivers to
 * extract attributes of the device using vio_get_attribute()
 */
#define VETH_MAC_ADDR "local-mac-address"
#define VETH_MCAST_FILTER_SIZE "ibm,mac-address-filters"

/* End architecture-specific constants */

#define h_vio_signal(ua, mode) \
  plpar_hcall_norets(H_VIO_SIGNAL, ua, mode)

#define VIO_IRQ_DISABLE		0UL
#define VIO_IRQ_ENABLE		1UL

/*
 * VIO CMO minimum entitlement for all devices and spare entitlement
 */
#define VIO_CMO_MIN_ENT 1562624

extern const struct bus_type vio_bus_type;

struct iommu_table;

/*
 * Platform Facilities Option (PFO)-specific data
 */

/* Starting unit address for PFO devices on the VIO BUS */
#define VIO_BASE_PFO_UA	0x50000000

/**
 * vio_pfo_op - PFO operation parameters
 *
 * @flags: h_call subfunctions and modifiers
 * @in: Input data block logical real address
 * @inlen: If non-negative, the length of the input data block.  If negative,
 *	the length of the input data descriptor list in bytes.
 * @out: Output data block logical real address
 * @outlen: If non-negative, the length of the input data block.  If negative,
 *	the length of the input data descriptor list in bytes.
 * @csbcpb: Logical real address of the 4k naturally-aligned storage block
 *	containing the CSB & optional FC field specific CPB
 * @timeout: # of milliseconds to retry h_call, 0 for no timeout.
 * @hcall_err: pointer to return the h_call return value, else NULL
 */
struct vio_pfo_op {
	u64 flags;
	s64 in;
	s64 inlen;
	s64 out;
	s64 outlen;
	u64 csbcpb;
	void *done;
	unsigned long handle;
	unsigned int timeout;
	long hcall_err;
};

/* End PFO specific data */

enum vio_dev_family {
	VDEVICE,	/* The OF node is a child of /vdevice */
	PFO,		/* The OF node is a child of /ibm,platform-facilities */
};

/**
 * vio_dev - This structure is used to describe virtual I/O devices.
 *
 * @desired: set from return of driver's get_desired_dma() function
 * @entitled: bytes of IO data that has been reserved for this device.
 * @allocated: bytes of IO data currently in use by the device.
 * @allocs_failed: number of DMA failures due to insufficient entitlement.
 */
struct vio_dev {
	const char *name;
	const char *type;
	uint32_t unit_address;
	uint32_t resource_id;
	unsigned int irq;
	struct {
		size_t desired;
		size_t entitled;
		size_t allocated;
		atomic_t allocs_failed;
	} cmo;
	enum vio_dev_family family;
	struct device dev;
};

struct vio_driver {
	const char *name;
	const struct vio_device_id *id_table;
	int (*probe)(struct vio_dev *dev, const struct vio_device_id *id);
	void (*remove)(struct vio_dev *dev);
	void (*shutdown)(struct vio_dev *dev);
	/* A driver must have a get_desired_dma() function to
	 * be loaded in a CMO environment if it uses DMA.
	 */
	unsigned long (*get_desired_dma)(struct vio_dev *dev);
	const struct dev_pm_ops *pm;
	struct device_driver driver;
};

extern int __vio_register_driver(struct vio_driver *drv, struct module *owner,
				 const char *mod_name);
/*
 * vio_register_driver must be a macro so that KBUILD_MODNAME can be expanded
 */
#define vio_register_driver(driver)		\
	__vio_register_driver(driver, THIS_MODULE, KBUILD_MODNAME)
extern void vio_unregister_driver(struct vio_driver *drv);

extern int vio_cmo_entitlement_update(size_t);
extern void vio_cmo_set_dev_desired(struct vio_dev *viodev, size_t desired);

extern void vio_unregister_device(struct vio_dev *dev);

extern int vio_h_cop_sync(struct vio_dev *vdev, struct vio_pfo_op *op);

struct device_node;

extern struct vio_dev *vio_register_device_node(
		struct device_node *node_vdev);
extern const void *vio_get_attribute(struct vio_dev *vdev, char *which,
		int *length);
#ifdef CONFIG_PPC_PSERIES
extern struct vio_dev *vio_find_node(struct device_node *vnode);
extern int vio_enable_interrupts(struct vio_dev *dev);
extern int vio_disable_interrupts(struct vio_dev *dev);
#else
static inline int vio_enable_interrupts(struct vio_dev *dev)
{
	return 0;
}
#endif

#define to_vio_driver(__drv)	container_of_const(__drv, struct vio_driver, driver)
#define to_vio_dev(__dev)	container_of_const(__dev, struct vio_dev, dev)

#endif /* __KERNEL__ */
#endif /* _ASM_POWERPC_VIO_H */
