/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2018-2021 Intel Corporation */

#ifndef __PECI_INTERNAL_H
#define __PECI_INTERNAL_H

#include <linux/device.h>
#include <linux/types.h>

struct peci_controller;
struct attribute_group;
struct peci_device;
struct peci_request;

/* PECI CPU address range 0x30-0x37 */
#define PECI_BASE_ADDR		0x30
#define PECI_DEVICE_NUM_MAX	8

struct peci_request *peci_request_alloc(struct peci_device *device, u8 tx_len, u8 rx_len);
void peci_request_free(struct peci_request *req);

int peci_request_status(struct peci_request *req);

u64 peci_request_dib_read(struct peci_request *req);
s16 peci_request_temp_read(struct peci_request *req);

u8 peci_request_data_readb(struct peci_request *req);
u16 peci_request_data_readw(struct peci_request *req);
u32 peci_request_data_readl(struct peci_request *req);
u64 peci_request_data_readq(struct peci_request *req);

struct peci_request *peci_xfer_get_dib(struct peci_device *device);
struct peci_request *peci_xfer_get_temp(struct peci_device *device);

struct peci_request *peci_xfer_pkg_cfg_readb(struct peci_device *device, u8 index, u16 param);
struct peci_request *peci_xfer_pkg_cfg_readw(struct peci_device *device, u8 index, u16 param);
struct peci_request *peci_xfer_pkg_cfg_readl(struct peci_device *device, u8 index, u16 param);
struct peci_request *peci_xfer_pkg_cfg_readq(struct peci_device *device, u8 index, u16 param);

struct peci_request *peci_xfer_pci_cfg_local_readb(struct peci_device *device,
						   u8 bus, u8 dev, u8 func, u16 reg);
struct peci_request *peci_xfer_pci_cfg_local_readw(struct peci_device *device,
						   u8 bus, u8 dev, u8 func, u16 reg);
struct peci_request *peci_xfer_pci_cfg_local_readl(struct peci_device *device,
						   u8 bus, u8 dev, u8 func, u16 reg);

struct peci_request *peci_xfer_ep_pci_cfg_local_readb(struct peci_device *device, u8 seg,
						      u8 bus, u8 dev, u8 func, u16 reg);
struct peci_request *peci_xfer_ep_pci_cfg_local_readw(struct peci_device *device, u8 seg,
						      u8 bus, u8 dev, u8 func, u16 reg);
struct peci_request *peci_xfer_ep_pci_cfg_local_readl(struct peci_device *device, u8 seg,
						      u8 bus, u8 dev, u8 func, u16 reg);

struct peci_request *peci_xfer_ep_pci_cfg_readb(struct peci_device *device, u8 seg,
						u8 bus, u8 dev, u8 func, u16 reg);
struct peci_request *peci_xfer_ep_pci_cfg_readw(struct peci_device *device, u8 seg,
						u8 bus, u8 dev, u8 func, u16 reg);
struct peci_request *peci_xfer_ep_pci_cfg_readl(struct peci_device *device, u8 seg,
						u8 bus, u8 dev, u8 func, u16 reg);

struct peci_request *peci_xfer_ep_mmio32_readl(struct peci_device *device, u8 bar, u8 seg,
					       u8 bus, u8 dev, u8 func, u64 offset);

struct peci_request *peci_xfer_ep_mmio64_readl(struct peci_device *device, u8 bar, u8 seg,
					       u8 bus, u8 dev, u8 func, u64 offset);
/**
 * struct peci_device_id - PECI device data to match
 * @data: pointer to driver private data specific to device
 * @family: device family
 * @model: device model
 */
struct peci_device_id {
	const void *data;
	u16 family;
	u8 model;
};

extern struct device_type peci_device_type;
extern const struct attribute_group *peci_device_groups[];

int peci_device_create(struct peci_controller *controller, u8 addr);
void peci_device_destroy(struct peci_device *device);

extern struct bus_type peci_bus_type;
extern const struct attribute_group *peci_bus_groups[];

/**
 * struct peci_driver - PECI driver
 * @driver: inherit device driver
 * @probe: probe callback
 * @remove: remove callback
 * @id_table: PECI device match table to decide which device to bind
 */
struct peci_driver {
	struct device_driver driver;
	int (*probe)(struct peci_device *device, const struct peci_device_id *id);
	void (*remove)(struct peci_device *device);
	const struct peci_device_id *id_table;
};

static inline struct peci_driver *to_peci_driver(struct device_driver *d)
{
	return container_of(d, struct peci_driver, driver);
}

int __peci_driver_register(struct peci_driver *driver, struct module *owner,
			   const char *mod_name);
/**
 * peci_driver_register() - register PECI driver
 * @driver: the driver to be registered
 *
 * PECI drivers that don't need to do anything special in module init should
 * use the convenience "module_peci_driver" macro instead
 *
 * Return: zero on success, else a negative error code.
 */
#define peci_driver_register(driver) \
	__peci_driver_register(driver, THIS_MODULE, KBUILD_MODNAME)
void peci_driver_unregister(struct peci_driver *driver);

/**
 * module_peci_driver() - helper macro for registering a modular PECI driver
 * @__peci_driver: peci_driver struct
 *
 * Helper macro for PECI drivers which do not do anything special in module
 * init/exit. This eliminates a lot of boilerplate. Each module may only
 * use this macro once, and calling it replaces module_init() and module_exit()
 */
#define module_peci_driver(__peci_driver) \
	module_driver(__peci_driver, peci_driver_register, peci_driver_unregister)

extern struct device_type peci_controller_type;

int peci_controller_scan_devices(struct peci_controller *controller);

#endif /* __PECI_INTERNAL_H */
