/*
 * Intel MIC Platform Software Stack (MPSS)
 *
 * Copyright(c) 2016 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Intel Virtio over PCIe Bus driver.
 */
#ifndef _VOP_BUS_H_
#define _VOP_BUS_H_
/*
 * Everything a vop driver needs to work with any particular vop
 * implementation.
 */
#include <linux/dmaengine.h>
#include <linux/interrupt.h>

#include "../common/mic_dev.h"

struct vop_device_id {
	u32 device;
	u32 vendor;
};

#define VOP_DEV_TRNSP 1
#define VOP_DEV_ANY_ID 0xffffffff
/*
 * Size of the internal buffer used during DMA's as an intermediate buffer
 * for copy to/from user. Must be an integral number of pages.
 */
#define VOP_INT_DMA_BUF_SIZE PAGE_ALIGN(64 * 1024ULL)

/**
 * vop_device - representation of a device using vop
 * @hw_ops: the hardware ops supported by this device.
 * @id: the device type identification (used to match it with a driver).
 * @dev: underlying device.
 * @dnode - The destination node which this device will communicate with.
 * @aper: Aperture memory window
 * @dma_ch - DMA channel
 * @index: unique position on the vop bus
 */
struct vop_device {
	struct vop_hw_ops *hw_ops;
	struct vop_device_id id;
	struct device dev;
	u8 dnode;
	struct mic_mw *aper;
	struct dma_chan *dma_ch;
	int index;
};

/**
 * vop_driver - operations for a vop I/O driver
 * @driver: underlying device driver (populate name and owner).
 * @id_table: the ids serviced by this driver.
 * @probe: the function to call when a device is found.  Returns 0 or -errno.
 * @remove: the function to call when a device is removed.
 */
struct vop_driver {
	struct device_driver driver;
	const struct vop_device_id *id_table;
	int (*probe)(struct vop_device *dev);
	void (*remove)(struct vop_device *dev);
};

/**
 * vop_hw_ops - Hardware operations for accessing a VOP device on the VOP bus.
 *
 * @next_db: Obtain the next available doorbell.
 * @request_irq: Request an interrupt on a particular doorbell.
 * @free_irq: Free an interrupt requested previously.
 * @ack_interrupt: acknowledge an interrupt in the ISR.
 * @get_remote_dp: Get access to the virtio device page used by the remote
 *                 node to add/remove/configure virtio devices.
 * @get_dp: Get access to the virtio device page used by the self
 *          node to add/remove/configure virtio devices.
 * @send_intr: Send an interrupt to the peer node on a specified doorbell.
 * @remap: Map a buffer with the specified DMA address and length.
 * @unmap: Unmap a buffer previously mapped.
 * @dma_filter: The DMA filter function to use for obtaining access to
 *		a DMA channel on the peer node.
 */
struct vop_hw_ops {
	int (*next_db)(struct vop_device *vpdev);
	struct mic_irq *(*request_irq)(struct vop_device *vpdev,
				       irqreturn_t (*func)(int irq, void *data),
				       const char *name, void *data,
				       int intr_src);
	void (*free_irq)(struct vop_device *vpdev,
			 struct mic_irq *cookie, void *data);
	void (*ack_interrupt)(struct vop_device *vpdev, int num);
	void __iomem * (*get_remote_dp)(struct vop_device *vpdev);
	void * (*get_dp)(struct vop_device *vpdev);
	void (*send_intr)(struct vop_device *vpdev, int db);
	void __iomem * (*remap)(struct vop_device *vpdev,
				  dma_addr_t pa, size_t len);
	void (*unmap)(struct vop_device *vpdev, void __iomem *va);
};

struct vop_device *
vop_register_device(struct device *pdev, int id,
		    const struct dma_map_ops *dma_ops,
		    struct vop_hw_ops *hw_ops, u8 dnode, struct mic_mw *aper,
		    struct dma_chan *chan);
void vop_unregister_device(struct vop_device *dev);
int vop_register_driver(struct vop_driver *drv);
void vop_unregister_driver(struct vop_driver *drv);

/*
 * module_vop_driver() - Helper macro for drivers that don't do
 * anything special in module init/exit.  This eliminates a lot of
 * boilerplate.  Each module may only use this macro once, and
 * calling it replaces module_init() and module_exit()
 */
#define module_vop_driver(__vop_driver) \
	module_driver(__vop_driver, vop_register_driver, \
			vop_unregister_driver)

static inline struct vop_device *dev_to_vop(struct device *dev)
{
	return container_of(dev, struct vop_device, dev);
}

static inline struct vop_driver *drv_to_vop(struct device_driver *drv)
{
	return container_of(drv, struct vop_driver, driver);
}
#endif /* _VOP_BUS_H */
