/*
 *  lis3lv02d.h - ST LIS3LV02DL accelerometer driver
 *
 *  Copyright (C) 2007-2008 Yan Burman
 *  Copyright (C) 2008 Eric Piel
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

/*
 * The actual chip is STMicroelectronics LIS3LV02DL or LIS3LV02DQ that seems to
 * be connected via SPI. There exists also several similar chips (such as LIS302DL or
 * LIS3L02DQ) and they have slightly different registers, but we can provide a
 * common interface for all of them.
 * They can also be connected via I²C.
 */

/* 2-byte registers */
#define LIS_DOUBLE_ID	0x3A /* LIS3LV02D[LQ] */
/* 1-byte registers */
#define LIS_SINGLE_ID	0x3B /* LIS[32]02DL and others */

enum lis3lv02d_reg {
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

enum lis3lv02d_ctrl1 {
	CTRL1_Xen	= 0x01,
	CTRL1_Yen	= 0x02,
	CTRL1_Zen	= 0x04,
	CTRL1_ST	= 0x08,
	CTRL1_DF0	= 0x10,
	CTRL1_DF1	= 0x20,
	CTRL1_PD0	= 0x40,
	CTRL1_PD1	= 0x80,
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

struct axis_conversion {
	s8	x;
	s8	y;
	s8	z;
};

struct acpi_lis3lv02d {
	struct acpi_device	*device;   /* The ACPI device */
	acpi_status (*init) (acpi_handle handle);
	acpi_status (*write) (acpi_handle handle, int reg, u8 val);
	acpi_status (*read) (acpi_handle handle, int reg, u8 *ret);

	u8			whoami;    /* 3Ah: 2-byte registries, 3Bh: 1-byte registries */
	s16 (*read_data) (acpi_handle handle, int reg);
	int			mdps_max_val;

	struct input_dev	*idev;     /* input device */
	struct task_struct	*kthread;  /* kthread for input */
	struct mutex            lock;
	struct platform_device	*pdev;     /* platform device */
	atomic_t		count;     /* interrupt count after last read */
	int			xcalib;    /* calibrated null value for x */
	int			ycalib;    /* calibrated null value for y */
	int			zcalib;    /* calibrated null value for z */
	unsigned char		is_on;     /* whether the device is on or off */
	unsigned char		usage;     /* usage counter */
	struct axis_conversion	ac;        /* hw -> logical axis */

	u32			irq;       /* IRQ number */
	struct fasync_struct	*async_queue; /* queue for the misc device */
	wait_queue_head_t	misc_wait; /* Wait queue for the misc device */
	unsigned long		misc_opened; /* bit0: whether the device is open */
};

int lis3lv02d_init_device(struct acpi_lis3lv02d *dev);
int lis3lv02d_joystick_enable(void);
void lis3lv02d_joystick_disable(void);
void lis3lv02d_poweroff(acpi_handle handle);
void lis3lv02d_poweron(acpi_handle handle);
int lis3lv02d_remove_fs(void);

extern struct acpi_lis3lv02d adev;
