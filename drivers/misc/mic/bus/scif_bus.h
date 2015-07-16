/*
 * Intel MIC Platform Software Stack (MPSS)
 *
 * Copyright(c) 2014 Intel Corporation.
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
 * Intel Symmetric Communications Interface Bus driver.
 */
#ifndef _SCIF_BUS_H_
#define _SCIF_BUS_H_
/*
 * Everything a scif driver needs to work with any particular scif
 * hardware abstraction layer.
 */
#include <linux/dma-mapping.h>

#include <linux/mic_common.h>
#include "../common/mic_dev.h"

struct scif_hw_dev_id {
	u32 device;
	u32 vendor;
};

#define MIC_SCIF_DEV 1
#define SCIF_DEV_ANY_ID 0xffffffff

/**
 * scif_hw_dev - representation of a hardware device abstracted for scif
 * @hw_ops: the hardware ops supported by this device
 * @id: the device type identification (used to match it with a driver)
 * @mmio: MMIO memory window
 * @aper: Aperture memory window
 * @dev: underlying device
 * @dnode - The destination node which this device will communicate with.
 * @snode - The source node for this device.
 * @dp - Self device page
 * @rdp - Remote device page
 * @dma_ch - Array of DMA channels
 * @num_dma_ch - Number of DMA channels available
 */
struct scif_hw_dev {
	struct scif_hw_ops *hw_ops;
	struct scif_hw_dev_id id;
	struct mic_mw *mmio;
	struct mic_mw *aper;
	struct device dev;
	u8 dnode;
	u8 snode;
	void *dp;
	void __iomem *rdp;
	struct dma_chan **dma_ch;
	int num_dma_ch;
};

/**
 * scif_driver - operations for a scif I/O driver
 * @driver: underlying device driver (populate name and owner).
 * @id_table: the ids serviced by this driver.
 * @probe: the function to call when a device is found.  Returns 0 or -errno.
 * @remove: the function to call when a device is removed.
 */
struct scif_driver {
	struct device_driver driver;
	const struct scif_hw_dev_id *id_table;
	int (*probe)(struct scif_hw_dev *dev);
	void (*remove)(struct scif_hw_dev *dev);
};

/**
 * scif_hw_ops - Hardware operations for accessing a SCIF device on the SCIF bus.
 *
 * @next_db: Obtain the next available doorbell.
 * @request_irq: Request an interrupt on a particular doorbell.
 * @free_irq: Free an interrupt requested previously.
 * @ack_interrupt: acknowledge an interrupt in the ISR.
 * @send_intr: Send an interrupt to the remote node on a specified doorbell.
 * @send_p2p_intr: Send an interrupt to the peer node on a specified doorbell
 * which is specifically targeted for a peer to peer node.
 * @ioremap: Map a buffer with the specified physical address and length.
 * @iounmap: Unmap a buffer previously mapped.
 */
struct scif_hw_ops {
	int (*next_db)(struct scif_hw_dev *sdev);
	struct mic_irq * (*request_irq)(struct scif_hw_dev *sdev,
					irqreturn_t (*func)(int irq,
							    void *data),
					const char *name, void *data,
					int db);
	void (*free_irq)(struct scif_hw_dev *sdev,
			 struct mic_irq *cookie, void *data);
	void (*ack_interrupt)(struct scif_hw_dev *sdev, int num);
	void (*send_intr)(struct scif_hw_dev *sdev, int db);
	void (*send_p2p_intr)(struct scif_hw_dev *sdev, int db,
			      struct mic_mw *mw);
	void __iomem * (*ioremap)(struct scif_hw_dev *sdev,
				  phys_addr_t pa, size_t len);
	void (*iounmap)(struct scif_hw_dev *sdev, void __iomem *va);
};

int scif_register_driver(struct scif_driver *driver);
void scif_unregister_driver(struct scif_driver *driver);
struct scif_hw_dev *
scif_register_device(struct device *pdev, int id,
		     struct dma_map_ops *dma_ops,
		     struct scif_hw_ops *hw_ops, u8 dnode, u8 snode,
		     struct mic_mw *mmio, struct mic_mw *aper,
		     void *dp, void __iomem *rdp,
		     struct dma_chan **chan, int num_chan);
void scif_unregister_device(struct scif_hw_dev *sdev);

static inline struct scif_hw_dev *dev_to_scif(struct device *dev)
{
	return container_of(dev, struct scif_hw_dev, dev);
}

static inline struct scif_driver *drv_to_scif(struct device_driver *drv)
{
	return container_of(drv, struct scif_driver, driver);
}
#endif /* _SCIF_BUS_H */
