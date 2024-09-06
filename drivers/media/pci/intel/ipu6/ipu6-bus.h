/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2013 - 2024 Intel Corporation */

#ifndef IPU6_BUS_H
#define IPU6_BUS_H

#include <linux/auxiliary_bus.h>
#include <linux/container_of.h>
#include <linux/device.h>
#include <linux/irqreturn.h>
#include <linux/list.h>
#include <linux/scatterlist.h>
#include <linux/types.h>

struct firmware;
struct pci_dev;

#define IPU6_BUS_NAME	IPU6_NAME "-bus"

struct ipu6_buttress_ctrl;

struct ipu6_bus_device {
	struct auxiliary_device auxdev;
	const struct auxiliary_driver *auxdrv;
	const struct ipu6_auxdrv_data *auxdrv_data;
	struct list_head list;
	void *pdata;
	struct ipu6_mmu *mmu;
	struct ipu6_device *isp;
	struct ipu6_buttress_ctrl *ctrl;
	u64 dma_mask;
	const struct firmware *fw;
	struct sg_table fw_sgt;
	u64 *pkg_dir;
	dma_addr_t pkg_dir_dma_addr;
	unsigned int pkg_dir_size;
};

struct ipu6_auxdrv_data {
	irqreturn_t (*isr)(struct ipu6_bus_device *adev);
	irqreturn_t (*isr_threaded)(struct ipu6_bus_device *adev);
	bool wake_isr_thread;
};

#define to_ipu6_bus_device(_dev) \
	container_of(to_auxiliary_dev(_dev), struct ipu6_bus_device, auxdev)
#define auxdev_to_adev(_auxdev) \
	container_of(_auxdev, struct ipu6_bus_device, auxdev)
#define ipu6_bus_get_drvdata(adev) dev_get_drvdata(&(adev)->auxdev.dev)

struct ipu6_bus_device *
ipu6_bus_initialize_device(struct pci_dev *pdev, struct device *parent,
			   void *pdata, struct ipu6_buttress_ctrl *ctrl,
			   char *name);
int ipu6_bus_add_device(struct ipu6_bus_device *adev);
void ipu6_bus_del_devices(struct pci_dev *pdev);

#endif
