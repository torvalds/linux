/*
 * cyttsp5_core.h
 * Parade TrueTouch(TM) Standard Product V5 Core Module.
 * For use with Parade touchscreen controllers.
 * Supported parts include:
 * CYTMA5XX
 * CYTMA448
 * CYTMA445A
 * CYTT21XXX
 * CYTT31XXX
 *
 * Copyright (C) 2015 Parade Semiconductor
 * Copyright (C) 2012-2015 Cypress Semiconductor
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2, and only version 2, as published by the
 * Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Contact Parade Semiconductor at www.parade.com <ttdrivers@paradetech.com>
 *
 */

#ifndef _LINUX_CYTTSP5_CORE_H
#define _LINUX_CYTTSP5_CORE_H

#include <linux/stringify.h>

#define CYTTSP5_I2C_NAME "cyttsp5_i2c_adapter"
#define CYTTSP5_SPI_NAME "cyttsp5_spi_adapter"

#define CYTTSP5_CORE_NAME "cyttsp5_core"
#define CYTTSP5_MT_NAME "cyttsp5_mt"
#define CYTTSP5_BTN_NAME "cyttsp5_btn"
#define CYTTSP5_PROXIMITY_NAME "cyttsp5_proximity"

#define CY_DRIVER_NAME TTDA
#define CY_DRIVER_MAJOR 03
#define CY_DRIVER_MINOR 08

#define CY_DRIVER_REVCTRL 874312

#define CY_DRIVER_VERSION		    \
__stringify(CY_DRIVER_NAME)		    \
"." __stringify(CY_DRIVER_MAJOR)	    \
"." __stringify(CY_DRIVER_MINOR)	    \
"." __stringify(CY_DRIVER_REVCTRL)

#define CY_DRIVER_DATE "20170125"

/* abs settings */
#define CY_IGNORE_VALUE             -1

enum cyttsp5_core_platform_flags {
	CY_CORE_FLAG_NONE,
	CY_CORE_FLAG_POWEROFF_ON_SLEEP = 0x02,
	CY_CORE_FLAG_RESTORE_PARAMETERS = 0x04,
};

enum cyttsp5_core_platform_easy_wakeup_gesture {
	CY_CORE_EWG_NONE,
	CY_CORE_EWG_TAP_TAP,
	CY_CORE_EWG_TWO_FINGER_SLIDE,
	CY_CORE_EWG_RESERVED,
	CY_CORE_EWG_WAKE_ON_INT_FROM_HOST = 0xFF,
};

enum cyttsp5_loader_platform_flags {
	CY_LOADER_FLAG_NONE,
	CY_LOADER_FLAG_CALIBRATE_AFTER_FW_UPGRADE,
	/* Use CONFIG_VER field in TT_CFG to decide TT_CFG update */
	CY_LOADER_FLAG_CHECK_TTCONFIG_VERSION,
	CY_LOADER_FLAG_CALIBRATE_AFTER_TTCONFIG_UPGRADE,
};

struct touch_settings {
	const uint8_t   *data;
	uint32_t         size;
	uint8_t         tag;
};

struct cyttsp5_touch_firmware {
	const uint8_t *img;
	uint32_t size;
	const uint8_t *ver;
	uint8_t vsize;
	uint8_t panel_id;
};

struct cyttsp5_touch_config {
	struct touch_settings *param_regs;
	struct touch_settings *param_size;
	const uint8_t *fw_ver;
	uint8_t fw_vsize;
	uint8_t panel_id;
};

struct cyttsp5_loader_platform_data {
	struct cyttsp5_touch_firmware *fw;
	struct cyttsp5_touch_config *ttconfig;
	struct cyttsp5_touch_firmware **fws;
	struct cyttsp5_touch_config **ttconfigs;
	u32 flags;
};

typedef int (*cyttsp5_platform_read) (struct device *dev, void *buf, int size);

#define CY_TOUCH_SETTINGS_MAX 32

struct cyttsp5_core_platform_data {
	int irq_gpio;
	int rst_gpio;
	int level_irq_udelay;
	u16 hid_desc_register;
	u16 vendor_id;
	u16 product_id;

	int (*xres)(struct cyttsp5_core_platform_data *pdata,
		struct device *dev);
	int (*init)(struct cyttsp5_core_platform_data *pdata,
		int on, struct device *dev);
	int (*power)(struct cyttsp5_core_platform_data *pdata,
		int on, struct device *dev, atomic_t *ignore_irq);
	int (*detect)(struct cyttsp5_core_platform_data *pdata,
		struct device *dev, cyttsp5_platform_read read);
	int (*irq_stat)(struct cyttsp5_core_platform_data *pdata,
		struct device *dev);
	struct touch_settings *sett[CY_TOUCH_SETTINGS_MAX];
	u32 flags;
	u8 easy_wakeup_gesture;
};

struct touch_framework {
	const int16_t  *abs;
	uint8_t         size;
	uint8_t         enable_vkeys;
} __packed;

enum cyttsp5_mt_platform_flags {
	CY_MT_FLAG_NONE,
	CY_MT_FLAG_HOVER = 0x04,
	CY_MT_FLAG_FLIP = 0x08,
	CY_MT_FLAG_INV_X = 0x10,
	CY_MT_FLAG_INV_Y = 0x20,
	CY_MT_FLAG_VKEYS = 0x40,
	CY_MT_FLAG_NO_TOUCH_ON_LO = 0x80,
};

struct cyttsp5_mt_platform_data {
	struct touch_framework *frmwrk;
	unsigned short flags;
	char const *inp_dev_name;
	int vkeys_x;
	int vkeys_y;
	int swap_x;
	int swap_y;
	int xy_exchange;
};

struct cyttsp5_btn_platform_data {
	char const *inp_dev_name;
};

struct cyttsp5_proximity_platform_data {
	struct touch_framework *frmwrk;
	char const *inp_dev_name;
};

struct cyttsp5_platform_data {
	struct cyttsp5_core_platform_data *core_pdata;
	struct cyttsp5_mt_platform_data *mt_pdata;
	struct cyttsp5_btn_platform_data *btn_pdata;
	struct cyttsp5_proximity_platform_data *prox_pdata;
	struct cyttsp5_loader_platform_data *loader_pdata;
};

#endif /* _LINUX_CYTTSP5_CORE_H */
