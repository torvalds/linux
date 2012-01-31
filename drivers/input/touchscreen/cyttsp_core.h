/*
 * Header file for:
 * Cypress TrueTouch(TM) Standard Product (TTSP) touchscreen drivers.
 * For use with Cypress Txx3xx parts.
 * Supported parts include:
 * CY8CTST341
 * CY8CTMA340
 *
 * Copyright (C) 2009, 2010, 2011 Cypress Semiconductor, Inc.
 * Copyright (C) 2012 Javier Martinez Canillas <javier@dowhile0.org>
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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * Contact Cypress Semiconductor at www.cypress.com <kev@cypress.com>
 *
 */


#ifndef __CYTTSP_CORE_H__
#define __CYTTSP_CORE_H__

#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/device.h>
#include <linux/input/cyttsp.h>

#define CY_NUM_RETRY		16 /* max number of retries for read ops */

struct cyttsp_tch {
	__be16 x, y;
	u8 z;
} __packed;

/* TrueTouch Standard Product Gen3 interface definition */
struct cyttsp_xydata {
	u8 hst_mode;
	u8 tt_mode;
	u8 tt_stat;
	struct cyttsp_tch tch1;
	u8 touch12_id;
	struct cyttsp_tch tch2;
	u8 gest_cnt;
	u8 gest_id;
	struct cyttsp_tch tch3;
	u8 touch34_id;
	struct cyttsp_tch tch4;
	u8 tt_undef[3];
	u8 act_dist;
	u8 tt_reserved;
} __packed;


/* TTSP System Information interface definition */
struct cyttsp_sysinfo_data {
	u8 hst_mode;
	u8 mfg_cmd;
	u8 mfg_stat;
	u8 cid[3];
	u8 tt_undef1;
	u8 uid[8];
	u8 bl_verh;
	u8 bl_verl;
	u8 tts_verh;
	u8 tts_verl;
	u8 app_idh;
	u8 app_idl;
	u8 app_verh;
	u8 app_verl;
	u8 tt_undef[5];
	u8 scn_typ;
	u8 act_intrvl;
	u8 tch_tmout;
	u8 lp_intrvl;
};

/* TTSP Bootloader Register Map interface definition */
#define CY_BL_CHKSUM_OK 0x01
struct cyttsp_bootloader_data {
	u8 bl_file;
	u8 bl_status;
	u8 bl_error;
	u8 blver_hi;
	u8 blver_lo;
	u8 bld_blver_hi;
	u8 bld_blver_lo;
	u8 ttspver_hi;
	u8 ttspver_lo;
	u8 appid_hi;
	u8 appid_lo;
	u8 appver_hi;
	u8 appver_lo;
	u8 cid_0;
	u8 cid_1;
	u8 cid_2;
};

struct cyttsp;

struct cyttsp_bus_ops {
	u16 bustype;
	int (*write)(struct cyttsp *ts,
		     u8 addr, u8 length, const void *values);
	int (*read)(struct cyttsp *ts, u8 addr, u8 length, void *values);
};

enum cyttsp_state {
	CY_IDLE_STATE,
	CY_ACTIVE_STATE,
	CY_BL_STATE,
};

struct cyttsp {
	struct device *dev;
	int irq;
	struct input_dev *input;
	char phys[32];
	const struct cyttsp_platform_data *pdata;
	const struct cyttsp_bus_ops *bus_ops;
	struct cyttsp_bootloader_data bl_data;
	struct cyttsp_sysinfo_data sysinfo_data;
	struct cyttsp_xydata xy_data;
	struct completion bl_ready;
	enum cyttsp_state state;
	bool suspended;

	u8 xfer_buf[] ____cacheline_aligned;
};

struct cyttsp *cyttsp_probe(const struct cyttsp_bus_ops *bus_ops,
			    struct device *dev, int irq, size_t xfer_buf_size);
void cyttsp_remove(struct cyttsp *ts);

extern const struct dev_pm_ops cyttsp_pm_ops;

#endif /* __CYTTSP_CORE_H__ */
