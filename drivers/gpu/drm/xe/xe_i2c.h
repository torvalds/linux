/* SPDX-License-Identifier: MIT */
#ifndef _XE_I2C_H_
#define _XE_I2C_H_

#include <linux/bits.h>
#include <linux/notifier.h>
#include <linux/types.h>
#include <linux/workqueue.h>

struct device;
struct fwnode_handle;
struct i2c_adapter;
struct i2c_client;
struct platform_device;
struct xe_amc;
struct xe_device;
struct xe_mmio;

#define XE_I2C_EP_COOKIE_DEVICE		0xde

/* Endpoint Capabilities */
#define XE_I2C_EP_CAP_IRQ		BIT(0)

enum XE_I2C_CLIENT {
	XE_I2C_CLIENT_AMC = 1,
	XE_I2C_MAX_CLIENTS = 3,
};

struct xe_i2c_endpoint {
	u8 cookie;
	u8 capabilities;
	u16 addr[XE_I2C_MAX_CLIENTS];
};

struct xe_i2c {
	struct platform_device *pdev;
	struct i2c_adapter *adapter;
	struct i2c_client *client[XE_I2C_MAX_CLIENTS];
	unsigned int ic_enable;

	struct notifier_block bus_notifier;
	struct work_struct work;

	struct xe_i2c_endpoint ep;
	struct device *drm_dev;

	struct xe_mmio *mmio;
	struct xe_amc *amc;
};

#if IS_ENABLED(CONFIG_I2C)
int xe_i2c_probe(struct xe_device *xe);
bool xe_i2c_present(struct xe_device *xe);
void xe_i2c_irq_handler(struct xe_device *xe, u32 master_ctl);
void xe_i2c_irq_postinstall(struct xe_device *xe);
void xe_i2c_irq_reset(struct xe_device *xe);
void xe_i2c_pm_suspend(struct xe_device *xe);
void xe_i2c_pm_resume(struct xe_device *xe, bool d3cold);
#else
static inline int xe_i2c_probe(struct xe_device *xe) { return 0; }
static inline bool xe_i2c_present(struct xe_device *xe) { return false; }
static inline void xe_i2c_irq_handler(struct xe_device *xe, u32 master_ctl) { }
static inline void xe_i2c_irq_postinstall(struct xe_device *xe) { }
static inline void xe_i2c_irq_reset(struct xe_device *xe) { }
static inline void xe_i2c_pm_suspend(struct xe_device *xe) { }
static inline void xe_i2c_pm_resume(struct xe_device *xe, bool d3cold) { }
#endif

#endif
