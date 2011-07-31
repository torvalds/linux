/*
 *  lis3lv02d.h - ST LIS3LV02DL accelerometer driver
 *
 *  Copyright (C) 2007-2008 Yan Burman
 *  Copyright (C) 2008-2009 Eric Piel
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <linux/platform_device.h>
#include <linux/input-polldev.h>

/*
 * This driver tries to support the "digital" accelerometer chips from
 * STMicroelectronics such as LIS3LV02DL, LIS302DL, LIS3L02DQ, LIS331DL,
 * LIS35DE, or LIS202DL. They are very similar in terms of programming, with
 * almost the same registers. In addition to differing on physical properties,
 * they differ on the number of axes (2/3), precision (8/12 bits), and special
 * features (freefall detection, click...). Unfortunately, not all the
 * differences can be probed via a register.
 * They can be connected either via I²C or SPI.
 */

#include <linux/lis3lv02d.h>

enum lis3_reg {
	WHO_AM_I	= 0x0F,
	OFFSET_X	= 0x16,
	OFFSET_Y	= 0x17,
	OFFSET_Z	= 0x18,
	GAIN_X		= 0x19,
	GAIN_Y		= 0x1A,
	GAIN_Z		= 0x1B,
	CTRL_REG1	= 0x20,
	CTRL_REG2	= 0x21,
	CTRL_REG3	= 0x22,
	HP_FILTER_RESET	= 0x23,
	STATUS_REG	= 0x27,
	OUTX_L		= 0x28,
	OUTX_H		= 0x29,
	OUTX		= 0x29,
	OUTY_L		= 0x2A,
	OUTY_H		= 0x2B,
	OUTY		= 0x2B,
	OUTZ_L		= 0x2C,
	OUTZ_H		= 0x2D,
	OUTZ		= 0x2D,
};

enum lis302d_reg {
	FF_WU_CFG_1	= 0x30,
	FF_WU_SRC_1	= 0x31,
	FF_WU_THS_1	= 0x32,
	FF_WU_DURATION_1 = 0x33,
	FF_WU_CFG_2	= 0x34,
	FF_WU_SRC_2	= 0x35,
	FF_WU_THS_2	= 0x36,
	FF_WU_DURATION_2 = 0x37,
	CLICK_CFG	= 0x38,
	CLICK_SRC	= 0x39,
	CLICK_THSY_X	= 0x3B,
	CLICK_THSZ	= 0x3C,
	CLICK_TIMELIMIT	= 0x3D,
	CLICK_LATENCY	= 0x3E,
	CLICK_WINDOW	= 0x3F,
};

enum lis3lv02d_reg {
	FF_WU_CFG	= 0x30,
	FF_WU_SRC	= 0x31,
	FF_WU_ACK	= 0x32,
	FF_WU_THS_L	= 0x34,
	FF_WU_THS_H	= 0x35,
	FF_WU_DURATION	= 0x36,
	DD_CFG		= 0x38,
	DD_SRC		= 0x39,
	DD_ACK		= 0x3A,
	DD_THSI_L	= 0x3C,
	DD_THSI_H	= 0x3D,
	DD_THSE_L	= 0x3E,
	DD_THSE_H	= 0x3F,
};

enum lis3_who_am_i {
	WAI_12B		= 0x3A, /* 12 bits: LIS3LV02D[LQ]... */
	WAI_8B		= 0x3B, /* 8 bits: LIS[23]02D[LQ]... */
	WAI_6B		= 0x52, /* 6 bits: LIS331DLF - not supported */
};

enum lis3lv02d_ctrl1_12b {
	CTRL1_Xen	= 0x01,
	CTRL1_Yen	= 0x02,
	CTRL1_Zen	= 0x04,
	CTRL1_ST	= 0x08,
	CTRL1_DF0	= 0x10,
	CTRL1_DF1	= 0x20,
	CTRL1_PD0	= 0x40,
	CTRL1_PD1	= 0x80,
};

/* Delta to ctrl1_12b version */
enum lis3lv02d_ctrl1_8b {
	CTRL1_STM	= 0x08,
	CTRL1_STP	= 0x10,
	CTRL1_FS	= 0x20,
	CTRL1_PD	= 0x40,
	CTRL1_DR	= 0x80,
};

enum lis3lv02d_ctrl2 {
	CTRL2_DAS	= 0x01,
	CTRL2_SIM	= 0x02,
	CTRL2_DRDY	= 0x04,
	CTRL2_IEN	= 0x08,
	CTRL2_BOOT	= 0x10,
	CTRL2_BLE	= 0x20,
	CTRL2_BDU	= 0x40, /* Block Data Update */
	CTRL2_FS	= 0x80, /* Full Scale selection */
};

enum lis302d_ctrl2 {
	HP_FF_WU2	= 0x08,
	HP_FF_WU1	= 0x04,
};

enum lis3lv02d_ctrl3 {
	CTRL3_CFS0	= 0x01,
	CTRL3_CFS1	= 0x02,
	CTRL3_FDS	= 0x10,
	CTRL3_HPFF	= 0x20,
	CTRL3_HPDD	= 0x40,
	CTRL3_ECK	= 0x80,
};

enum lis3lv02d_status_reg {
	STATUS_XDA	= 0x01,
	STATUS_YDA	= 0x02,
	STATUS_ZDA	= 0x04,
	STATUS_XYZDA	= 0x08,
	STATUS_XOR	= 0x10,
	STATUS_YOR	= 0x20,
	STATUS_ZOR	= 0x40,
	STATUS_XYZOR	= 0x80,
};

enum lis3lv02d_ff_wu_cfg {
	FF_WU_CFG_XLIE	= 0x01,
	FF_WU_CFG_XHIE	= 0x02,
	FF_WU_CFG_YLIE	= 0x04,
	FF_WU_CFG_YHIE	= 0x08,
	FF_WU_CFG_ZLIE	= 0x10,
	FF_WU_CFG_ZHIE	= 0x20,
	FF_WU_CFG_LIR	= 0x40,
	FF_WU_CFG_AOI	= 0x80,
};

enum lis3lv02d_ff_wu_src {
	FF_WU_SRC_XL	= 0x01,
	FF_WU_SRC_XH	= 0x02,
	FF_WU_SRC_YL	= 0x04,
	FF_WU_SRC_YH	= 0x08,
	FF_WU_SRC_ZL	= 0x10,
	FF_WU_SRC_ZH	= 0x20,
	FF_WU_SRC_IA	= 0x40,
};

enum lis3lv02d_dd_cfg {
	DD_CFG_XLIE	= 0x01,
	DD_CFG_XHIE	= 0x02,
	DD_CFG_YLIE	= 0x04,
	DD_CFG_YHIE	= 0x08,
	DD_CFG_ZLIE	= 0x10,
	DD_CFG_ZHIE	= 0x20,
	DD_CFG_LIR	= 0x40,
	DD_CFG_IEND	= 0x80,
};

enum lis3lv02d_dd_src {
	DD_SRC_XL	= 0x01,
	DD_SRC_XH	= 0x02,
	DD_SRC_YL	= 0x04,
	DD_SRC_YH	= 0x08,
	DD_SRC_ZL	= 0x10,
	DD_SRC_ZH	= 0x20,
	DD_SRC_IA	= 0x40,
};

enum lis3lv02d_click_src_8b {
	CLICK_SINGLE_X	= 0x01,
	CLICK_DOUBLE_X	= 0x02,
	CLICK_SINGLE_Y	= 0x04,
	CLICK_DOUBLE_Y	= 0x08,
	CLICK_SINGLE_Z	= 0x10,
	CLICK_DOUBLE_Z	= 0x20,
	CLICK_IA	= 0x40,
};

struct axis_conversion {
	s8	x;
	s8	y;
	s8	z;
};

struct lis3lv02d {
	void			*bus_priv; /* used by the bus layer only */
	int (*init) (struct lis3lv02d *lis3);
	int (*write) (struct lis3lv02d *lis3, int reg, u8 val);
	int (*read) (struct lis3lv02d *lis3, int reg, u8 *ret);

	int                     *odrs;     /* Supported output data rates */
	u8                      odr_mask;  /* ODR bit mask */
	u8			whoami;    /* indicates measurement precision */
	s16 (*read_data) (struct lis3lv02d *lis3, int reg);
	int			mdps_max_val;
	int			pwron_delay;
	int                     scale; /*
					* relationship between 1 LBS and mG
					* (1/1000th of earth gravity)
					*/

	struct input_polled_dev	*idev;     /* input device */
	struct platform_device	*pdev;     /* platform device */
	atomic_t		count;     /* interrupt count after last read */
	struct axis_conversion	ac;        /* hw -> logical axis */
	int			mapped_btns[3];

	u32			irq;       /* IRQ number */
	struct fasync_struct	*async_queue; /* queue for the misc device */
	wait_queue_head_t	misc_wait; /* Wait queue for the misc device */
	unsigned long		misc_opened; /* bit0: whether the device is open */

	struct lis3lv02d_platform_data *pdata;	/* for passing board config */
	struct mutex		mutex;     /* Serialize poll and selftest */
};

int lis3lv02d_init_device(struct lis3lv02d *lis3);
int lis3lv02d_joystick_enable(void);
void lis3lv02d_joystick_disable(void);
void lis3lv02d_poweroff(struct lis3lv02d *lis3);
void lis3lv02d_poweron(struct lis3lv02d *lis3);
int lis3lv02d_remove_fs(struct lis3lv02d *lis3);

extern struct lis3lv02d lis3_dev;
