/*
 * cyttsp4_core.h
 * Cypress TrueTouch(TM) Standard Product V4 Core driver module.
 * For use with Cypress Txx4xx parts.
 * Supported parts include:
 * TMA4XX
 * TMA1036
 *
 * Copyright (C) 2012 Cypress Semiconductor
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
 * Contact Cypress Semiconductor at www.cypress.com <ttdrivers@cypress.com>
 *
 */

#ifndef _LINUX_CYTTSP4_CORE_H
#define _LINUX_CYTTSP4_CORE_H

#include <linux/device.h>
#include <linux/err.h>
#include <linux/input.h>
#include <linux/kernel.h>
#include <linux/limits.h>
#include <linux/module.h>
#include <linux/stringify.h>
#include <linux/types.h>
#include <linux/platform_data/cyttsp4.h>

#define CY_REG_BASE			0x00

#define CY_POST_CODEL_WDG_RST		0x01
#define CY_POST_CODEL_CFG_DATA_CRC_FAIL	0x02
#define CY_POST_CODEL_PANEL_TEST_FAIL	0x04

#define CY_NUM_BTN_PER_REG		4

/* touch record system information offset masks and shifts */
#define CY_BYTE_OFS_MASK		0x1F
#define CY_BOFS_MASK			0xE0
#define CY_BOFS_SHIFT			5

#define CY_TMA1036_TCH_REC_SIZE		6
#define CY_TMA4XX_TCH_REC_SIZE		9
#define CY_TMA1036_MAX_TCH		0x0E
#define CY_TMA4XX_MAX_TCH		0x1E

#define CY_NORMAL_ORIGIN		0	/* upper, left corner */
#define CY_INVERT_ORIGIN		1	/* lower, right corner */

/* helpers */
#define GET_NUM_TOUCHES(x)		((x) & 0x1F)
#define IS_LARGE_AREA(x)		((x) & 0x20)
#define IS_BAD_PKT(x)			((x) & 0x20)
#define IS_BOOTLOADER(hst_mode, reset_detect)	\
		((hst_mode) & 0x01 || (reset_detect) != 0)
#define IS_TMO(t)			((t) == 0)


enum cyttsp_cmd_bits {
	CY_CMD_COMPLETE = (1 << 6),
};

/* Timeout in ms. */
#define CY_WATCHDOG_TIMEOUT		1000

#define CY_MAX_PRINT_SIZE		512
#ifdef VERBOSE_DEBUG
#define CY_MAX_PRBUF_SIZE		PIPE_BUF
#define CY_PR_TRUNCATED			" truncated..."
#endif

enum cyttsp4_ic_grpnum {
	CY_IC_GRPNUM_RESERVED,
	CY_IC_GRPNUM_CMD_REGS,
	CY_IC_GRPNUM_TCH_REP,
	CY_IC_GRPNUM_DATA_REC,
	CY_IC_GRPNUM_TEST_REC,
	CY_IC_GRPNUM_PCFG_REC,
	CY_IC_GRPNUM_TCH_PARM_VAL,
	CY_IC_GRPNUM_TCH_PARM_SIZE,
	CY_IC_GRPNUM_RESERVED1,
	CY_IC_GRPNUM_RESERVED2,
	CY_IC_GRPNUM_OPCFG_REC,
	CY_IC_GRPNUM_DDATA_REC,
	CY_IC_GRPNUM_MDATA_REC,
	CY_IC_GRPNUM_TEST_REGS,
	CY_IC_GRPNUM_BTN_KEYS,
	CY_IC_GRPNUM_TTHE_REGS,
	CY_IC_GRPNUM_NUM
};

enum cyttsp4_int_state {
	CY_INT_NONE,
	CY_INT_IGNORE      = (1 << 0),
	CY_INT_MODE_CHANGE = (1 << 1),
	CY_INT_EXEC_CMD    = (1 << 2),
	CY_INT_AWAKE       = (1 << 3),
};

enum cyttsp4_mode {
	CY_MODE_UNKNOWN,
	CY_MODE_BOOTLOADER   = (1 << 1),
	CY_MODE_OPERATIONAL  = (1 << 2),
	CY_MODE_SYSINFO      = (1 << 3),
	CY_MODE_CAT          = (1 << 4),
	CY_MODE_STARTUP      = (1 << 5),
	CY_MODE_LOADER       = (1 << 6),
	CY_MODE_CHANGE_MODE  = (1 << 7),
	CY_MODE_CHANGED      = (1 << 8),
	CY_MODE_CMD_COMPLETE = (1 << 9),
};

enum cyttsp4_sleep_state {
	SS_SLEEP_OFF,
	SS_SLEEP_ON,
	SS_SLEEPING,
	SS_WAKING,
};

enum cyttsp4_startup_state {
	STARTUP_NONE,
	STARTUP_QUEUED,
	STARTUP_RUNNING,
};

#define CY_NUM_REVCTRL			8
struct cyttsp4_cydata {
	u8 ttpidh;
	u8 ttpidl;
	u8 fw_ver_major;
	u8 fw_ver_minor;
	u8 revctrl[CY_NUM_REVCTRL];
	u8 blver_major;
	u8 blver_minor;
	u8 jtag_si_id3;
	u8 jtag_si_id2;
	u8 jtag_si_id1;
	u8 jtag_si_id0;
	u8 mfgid_sz;
	u8 cyito_idh;
	u8 cyito_idl;
	u8 cyito_verh;
	u8 cyito_verl;
	u8 ttsp_ver_major;
	u8 ttsp_ver_minor;
	u8 device_info;
	u8 mfg_id[];
} __packed;

struct cyttsp4_test {
	u8 post_codeh;
	u8 post_codel;
} __packed;

struct cyttsp4_pcfg {
	u8 electrodes_x;
	u8 electrodes_y;
	u8 len_xh;
	u8 len_xl;
	u8 len_yh;
	u8 len_yl;
	u8 res_xh;
	u8 res_xl;
	u8 res_yh;
	u8 res_yl;
	u8 max_zh;
	u8 max_zl;
	u8 panel_info0;
} __packed;

struct cyttsp4_tch_rec_params {
	u8 loc;
	u8 size;
} __packed;

#define CY_NUM_TCH_FIELDS		7
#define CY_NUM_EXT_TCH_FIELDS		3
struct cyttsp4_opcfg {
	u8 cmd_ofs;
	u8 rep_ofs;
	u8 rep_szh;
	u8 rep_szl;
	u8 num_btns;
	u8 tt_stat_ofs;
	u8 obj_cfg0;
	u8 max_tchs;
	u8 tch_rec_size;
	struct cyttsp4_tch_rec_params tch_rec_old[CY_NUM_TCH_FIELDS];
	u8 btn_rec_size;	/* btn record size (in bytes) */
	u8 btn_diff_ofs;	/* btn data loc, diff counts  */
	u8 btn_diff_size;	/* btn size of diff counts (in bits) */
	struct cyttsp4_tch_rec_params tch_rec_new[CY_NUM_EXT_TCH_FIELDS];
} __packed;

struct cyttsp4_sysinfo_ptr {
	struct cyttsp4_cydata *cydata;
	struct cyttsp4_test *test;
	struct cyttsp4_pcfg *pcfg;
	struct cyttsp4_opcfg *opcfg;
	struct cyttsp4_ddata *ddata;
	struct cyttsp4_mdata *mdata;
} __packed;

struct cyttsp4_sysinfo_data {
	u8 hst_mode;
	u8 reserved;
	u8 map_szh;
	u8 map_szl;
	u8 cydata_ofsh;
	u8 cydata_ofsl;
	u8 test_ofsh;
	u8 test_ofsl;
	u8 pcfg_ofsh;
	u8 pcfg_ofsl;
	u8 opcfg_ofsh;
	u8 opcfg_ofsl;
	u8 ddata_ofsh;
	u8 ddata_ofsl;
	u8 mdata_ofsh;
	u8 mdata_ofsl;
} __packed;

enum cyttsp4_tch_abs {	/* for ordering within the extracted touch data array */
	CY_TCH_X,	/* X */
	CY_TCH_Y,	/* Y */
	CY_TCH_P,	/* P (Z) */
	CY_TCH_T,	/* TOUCH ID */
	CY_TCH_E,	/* EVENT ID */
	CY_TCH_O,	/* OBJECT ID */
	CY_TCH_W,	/* SIZE */
	CY_TCH_MAJ,	/* TOUCH_MAJOR */
	CY_TCH_MIN,	/* TOUCH_MINOR */
	CY_TCH_OR,	/* ORIENTATION */
	CY_TCH_NUM_ABS
};

static const char * const cyttsp4_tch_abs_string[] = {
	[CY_TCH_X]	= "X",
	[CY_TCH_Y]	= "Y",
	[CY_TCH_P]	= "P",
	[CY_TCH_T]	= "T",
	[CY_TCH_E]	= "E",
	[CY_TCH_O]	= "O",
	[CY_TCH_W]	= "W",
	[CY_TCH_MAJ]	= "MAJ",
	[CY_TCH_MIN]	= "MIN",
	[CY_TCH_OR]	= "OR",
	[CY_TCH_NUM_ABS] = "INVALID"
};

struct cyttsp4_touch {
	int abs[CY_TCH_NUM_ABS];
};

struct cyttsp4_tch_abs_params {
	size_t ofs;	/* abs byte offset */
	size_t size;	/* size in bits */
	size_t max;	/* max value */
	size_t bofs;	/* bit offset */
};

struct cyttsp4_sysinfo_ofs {
	size_t chip_type;
	size_t cmd_ofs;
	size_t rep_ofs;
	size_t rep_sz;
	size_t num_btns;
	size_t num_btn_regs;	/* ceil(num_btns/4) */
	size_t tt_stat_ofs;
	size_t tch_rec_size;
	size_t obj_cfg0;
	size_t max_tchs;
	size_t mode_size;
	size_t data_size;
	size_t map_sz;
	size_t max_x;
	size_t x_origin;	/* left or right corner */
	size_t max_y;
	size_t y_origin;	/* upper or lower corner */
	size_t max_p;
	size_t cydata_ofs;
	size_t test_ofs;
	size_t pcfg_ofs;
	size_t opcfg_ofs;
	size_t ddata_ofs;
	size_t mdata_ofs;
	size_t cydata_size;
	size_t test_size;
	size_t pcfg_size;
	size_t opcfg_size;
	size_t ddata_size;
	size_t mdata_size;
	size_t btn_keys_size;
	struct cyttsp4_tch_abs_params tch_abs[CY_TCH_NUM_ABS];
	size_t btn_rec_size; /* btn record size (in bytes) */
	size_t btn_diff_ofs;/* btn data loc ,diff counts, (Op-Mode byte ofs) */
	size_t btn_diff_size;/* btn size of diff counts (in bits) */
};

enum cyttsp4_btn_state {
	CY_BTN_RELEASED,
	CY_BTN_PRESSED,
	CY_BTN_NUM_STATE
};

struct cyttsp4_btn {
	bool enabled;
	int state;	/* CY_BTN_PRESSED, CY_BTN_RELEASED */
	int key_code;
};

struct cyttsp4_sysinfo {
	bool ready;
	struct cyttsp4_sysinfo_data si_data;
	struct cyttsp4_sysinfo_ptr si_ptrs;
	struct cyttsp4_sysinfo_ofs si_ofs;
	struct cyttsp4_btn *btn;	/* button states */
	u8 *btn_rec_data;		/* button diff count data */
	u8 *xy_mode;			/* operational mode and status regs */
	u8 *xy_data;			/* operational touch regs */
};

struct cyttsp4_mt_data {
	struct cyttsp4_mt_platform_data *pdata;
	struct cyttsp4_sysinfo *si;
	struct input_dev *input;
	struct mutex report_lock;
	bool is_suspended;
	char phys[NAME_MAX];
	int num_prv_tch;
};

struct cyttsp4 {
	struct device *dev;
	struct mutex system_lock;
	struct mutex adap_lock;
	enum cyttsp4_mode mode;
	enum cyttsp4_sleep_state sleep_state;
	enum cyttsp4_startup_state startup_state;
	int int_status;
	wait_queue_head_t wait_q;
	int irq;
	struct work_struct startup_work;
	struct work_struct watchdog_work;
	struct timer_list watchdog_timer;
	struct cyttsp4_sysinfo sysinfo;
	void *exclusive_dev;
	int exclusive_waits;
	atomic_t ignore_irq;
	bool invalid_touch_app;
	struct cyttsp4_mt_data md;
	struct cyttsp4_platform_data *pdata;
	struct cyttsp4_core_platform_data *cpdata;
	const struct cyttsp4_bus_ops *bus_ops;
	u8 *xfer_buf;
#ifdef VERBOSE_DEBUG
	u8 pr_buf[CY_MAX_PRBUF_SIZE];
#endif
};

struct cyttsp4_bus_ops {
	u16 bustype;
	int (*write)(struct device *dev, u8 *xfer_buf, u8 addr, u8 length,
			const void *values);
	int (*read)(struct device *dev, u8 *xfer_buf, u8 addr, u8 length,
			void *values);
};

enum cyttsp4_hst_mode_bits {
	CY_HST_TOGGLE      = (1 << 7),
	CY_HST_MODE_CHANGE = (1 << 3),
	CY_HST_MODE        = (7 << 4),
	CY_HST_OPERATE     = (0 << 4),
	CY_HST_SYSINFO     = (1 << 4),
	CY_HST_CAT         = (2 << 4),
	CY_HST_LOWPOW      = (1 << 2),
	CY_HST_SLEEP       = (1 << 1),
	CY_HST_RESET       = (1 << 0),
};

/* abs settings */
#define CY_IGNORE_VALUE			0xFFFF

/* abs signal capabilities offsets in the frameworks array */
enum cyttsp4_sig_caps {
	CY_SIGNAL_OST,
	CY_MIN_OST,
	CY_MAX_OST,
	CY_FUZZ_OST,
	CY_FLAT_OST,
	CY_NUM_ABS_SET	/* number of signal capability fields */
};

/* abs axis signal offsets in the framworks array  */
enum cyttsp4_sig_ost {
	CY_ABS_X_OST,
	CY_ABS_Y_OST,
	CY_ABS_P_OST,
	CY_ABS_W_OST,
	CY_ABS_ID_OST,
	CY_ABS_MAJ_OST,
	CY_ABS_MIN_OST,
	CY_ABS_OR_OST,
	CY_NUM_ABS_OST	/* number of abs signals */
};

enum cyttsp4_flags {
	CY_FLAG_NONE = 0x00,
	CY_FLAG_HOVER = 0x04,
	CY_FLAG_FLIP = 0x08,
	CY_FLAG_INV_X = 0x10,
	CY_FLAG_INV_Y = 0x20,
	CY_FLAG_VKEYS = 0x40,
};

enum cyttsp4_object_id {
	CY_OBJ_STANDARD_FINGER,
	CY_OBJ_LARGE_OBJECT,
	CY_OBJ_STYLUS,
	CY_OBJ_HOVER,
};

enum cyttsp4_event_id {
	CY_EV_NO_EVENT,
	CY_EV_TOUCHDOWN,
	CY_EV_MOVE,		/* significant displacement (> act dist) */
	CY_EV_LIFTOFF,		/* record reports last position */
};

/* x-axis resolution of panel in pixels */
#define CY_PCFG_RESOLUTION_X_MASK	0x7F

/* y-axis resolution of panel in pixels */
#define CY_PCFG_RESOLUTION_Y_MASK	0x7F

/* x-axis, 0:origin is on left side of panel, 1: right */
#define CY_PCFG_ORIGIN_X_MASK		0x80

/* y-axis, 0:origin is on top side of panel, 1: bottom */
#define CY_PCFG_ORIGIN_Y_MASK		0x80

static inline int cyttsp4_adap_read(struct cyttsp4 *ts, u8 addr, int size,
		void *buf)
{
	return ts->bus_ops->read(ts->dev, ts->xfer_buf, addr, size, buf);
}

static inline int cyttsp4_adap_write(struct cyttsp4 *ts, u8 addr, int size,
		const void *buf)
{
	return ts->bus_ops->write(ts->dev, ts->xfer_buf, addr, size, buf);
}

extern struct cyttsp4 *cyttsp4_probe(const struct cyttsp4_bus_ops *ops,
		struct device *dev, u16 irq, size_t xfer_buf_size);
extern int cyttsp4_remove(struct cyttsp4 *ts);
int cyttsp_i2c_write_block_data(struct device *dev, u8 *xfer_buf, u8 addr,
		u8 length, const void *values);
int cyttsp_i2c_read_block_data(struct device *dev, u8 *xfer_buf, u8 addr,
		u8 length, void *values);
extern const struct dev_pm_ops cyttsp4_pm_ops;

#endif /* _LINUX_CYTTSP4_CORE_H */
