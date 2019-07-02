/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Intel MIC Platform Software Stack (MPSS)
 *
 * Copyright(c) 2015 Intel Corporation.
 *
 * Intel MIC COSM Bus Driver
 */
#ifndef _COSM_BUS_H_
#define _COSM_BUS_H_

#include <linux/scif.h>
#include <linux/mic_common.h>
#include "../common/mic_dev.h"

/**
 * cosm_device - representation of a cosm device
 *
 * @attr_group: Pointer to list of sysfs attribute groups.
 * @sdev: Device for sysfs entries.
 * @state: MIC state.
 * @prev_state: MIC state previous to MIC_RESETTING
 * @shutdown_status: MIC status reported by card for shutdown/crashes.
 * @shutdown_status_int: Internal shutdown status maintained by the driver
 * @cosm_mutex: Mutex for synchronizing access to data structures.
 * @reset_trigger_work: Work for triggering reset requests.
 * @scif_work: Work for handling per device SCIF connections
 * @cmdline: Kernel command line.
 * @firmware: Firmware file name.
 * @ramdisk: Ramdisk file name.
 * @bootmode: Boot mode i.e. "linux" or "elf" for flash updates.
 * @log_buf_addr: Log buffer address for MIC.
 * @log_buf_len: Log buffer length address for MIC.
 * @state_sysfs: Sysfs dirent for notifying ring 3 about MIC state changes.
 * @hw_ops: the hardware bus ops for this device.
 * @dev: underlying device.
 * @index: unique position on the cosm bus
 * @dbg_dir: debug fs directory
 * @newepd: new endpoint from scif accept to be assigned to this cdev
 * @epd: SCIF endpoint for this cdev
 * @heartbeat_watchdog_enable: if heartbeat watchdog is enabled for this cdev
 * @sysfs_heartbeat_enable: sysfs setting for disabling heartbeat notification
 */
struct cosm_device {
	const struct attribute_group **attr_group;
	struct device *sdev;
	u8 state;
	u8 prev_state;
	u8 shutdown_status;
	u8 shutdown_status_int;
	struct mutex cosm_mutex;
	struct work_struct reset_trigger_work;
	struct work_struct scif_work;
	char *cmdline;
	char *firmware;
	char *ramdisk;
	char *bootmode;
	void *log_buf_addr;
	int *log_buf_len;
	struct kernfs_node *state_sysfs;
	struct cosm_hw_ops *hw_ops;
	struct device dev;
	int index;
	struct dentry *dbg_dir;
	scif_epd_t newepd;
	scif_epd_t epd;
	bool heartbeat_watchdog_enable;
	bool sysfs_heartbeat_enable;
};

/**
 * cosm_driver - operations for a cosm driver
 *
 * @driver: underlying device driver (populate name and owner).
 * @probe: the function to call when a device is found.  Returns 0 or -errno.
 * @remove: the function to call when a device is removed.
 */
struct cosm_driver {
	struct device_driver driver;
	int (*probe)(struct cosm_device *dev);
	void (*remove)(struct cosm_device *dev);
};

/**
 * cosm_hw_ops - cosm bus ops
 *
 * @reset: trigger MIC reset
 * @force_reset: force MIC reset
 * @post_reset: inform MIC reset is complete
 * @ready: is MIC ready for OS download
 * @start: boot MIC
 * @stop: prepare MIC for reset
 * @family: return MIC HW family string
 * @stepping: return MIC HW stepping string
 * @aper: return MIC PCIe aperture
 */
struct cosm_hw_ops {
	void (*reset)(struct cosm_device *cdev);
	void (*force_reset)(struct cosm_device *cdev);
	void (*post_reset)(struct cosm_device *cdev, enum mic_states state);
	bool (*ready)(struct cosm_device *cdev);
	int (*start)(struct cosm_device *cdev, int id);
	void (*stop)(struct cosm_device *cdev, bool force);
	ssize_t (*family)(struct cosm_device *cdev, char *buf);
	ssize_t (*stepping)(struct cosm_device *cdev, char *buf);
	struct mic_mw *(*aper)(struct cosm_device *cdev);
};

struct cosm_device *
cosm_register_device(struct device *pdev, struct cosm_hw_ops *hw_ops);
void cosm_unregister_device(struct cosm_device *dev);
int cosm_register_driver(struct cosm_driver *drv);
void cosm_unregister_driver(struct cosm_driver *drv);
struct cosm_device *cosm_find_cdev_by_id(int id);

static inline struct cosm_device *dev_to_cosm(struct device *dev)
{
	return container_of(dev, struct cosm_device, dev);
}

static inline struct cosm_driver *drv_to_cosm(struct device_driver *drv)
{
	return container_of(drv, struct cosm_driver, driver);
}
#endif /* _COSM_BUS_H */
