/*
 * Samsung Exynos5 SoC series FIMC-IS driver
 *
 * exynos5 fimc-is video functions
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <mach/videonode.h>
#include <media/exynos_mc.h>
#include <linux/cma.h>
#include <asm/cacheflush.h>
#include <asm/pgtable.h>
#include <linux/firmware.h>
#include <linux/dma-mapping.h>
#include <linux/scatterlist.h>
#include <linux/videodev2.h>
#include <linux/videodev2_exynos_camera.h>
#include <linux/videodev2_exynos_media.h>
#include <linux/v4l2-mediabus.h>
#include <linux/bug.h>

#include <mach/map.h>
#include <mach/regs-clock.h>

#include "fimc-is-core.h"
#include "fimc-is-param.h"
#include "fimc-is-cmd.h"
#include "fimc-is-regs.h"
#include "fimc-is-err.h"
#include "fimc-is-video.h"

#include "fimc-is-device-sensor.h"

/* PMU for FIMC-IS*/
#define MIPICSI0_REG_BASE	(S5P_VA_MIPICSI0)   /* phy : 0x13c2_0000 */
#define MIPICSI1_REG_BASE	(S5P_VA_MIPICSI1)   /* phy : 0x13c3_0000 */
#define MIPICSI2_REG_BASE	(S5P_VA_MIPICSI2)   /* phy : 0x13d1_0000 */

/*MIPI*/
/* CSIS global control */
#define S5PCSIS_CTRL					(0x00)
#define S5PCSIS_CTRL_DPDN_DEFAULT			(0 << 31)
#define S5PCSIS_CTRL_DPDN_SWAP				(1 << 31)
#define S5PCSIS_CTRL_ALIGN_32BIT			(1 << 20)
#define S5PCSIS_CTRL_UPDATE_SHADOW			(1 << 16)
#define S5PCSIS_CTRL_WCLK_EXTCLK			(1 << 8)
#define S5PCSIS_CTRL_RESET				(1 << 4)
#define S5PCSIS_CTRL_ENABLE				(1 << 0)

/* D-PHY control */
#define S5PCSIS_DPHYCTRL				(0x04)
#define S5PCSIS_DPHYCTRL_HSS_MASK			(soc_is_exynos5250() ? \
							0x1f << 27 : \
							0xff << 24)
#define S5PCSIS_DPHYCTRL_ENABLE				(soc_is_exynos5250() ? \
							0x7 << 0 : \
							0x1f << 0)

#define S5PCSIS_CONFIG					(0x08)
#define S5PCSIS_CFG_FMT_YCBCR422_8BIT			(0x1e << 2)
#define S5PCSIS_CFG_FMT_RAW8				(0x2a << 2)
#define S5PCSIS_CFG_FMT_RAW10				(0x2b << 2)
#define S5PCSIS_CFG_FMT_RAW12				(0x2c << 2)
/* User defined formats, x = 1...4 */
#define S5PCSIS_CFG_FMT_USER(x)				((0x30 + x - 1) << 2)
#define S5PCSIS_CFG_FMT_MASK				(0x3f << 2)
#define S5PCSIS_CFG_NR_LANE_MASK			(3)

/* Interrupt mask. */
#define S5PCSIS_INTMSK					(0x10)
#define S5PCSIS_INTMSK_EN_ALL				(0xfc00103f)
#define S5PCSIS_INTSRC					(0x14)

/* Pixel resolution */
#define S5PCSIS_RESOL					(0x2c)
#define CSIS_MAX_PIX_WIDTH				(0xffff)
#define CSIS_MAX_PIX_HEIGHT				(0xffff)

struct _csis_hsync_settle {
	int channel;
	int width;
	int height;
	int framerate;
	int settle;
};

static struct _csis_hsync_settle csis_hsync_settle[] = {
	/* IMX135, 1936x1090@24fps */
	[0] = {
		.channel	= CSI_ID_A,
		.width		= 1936,
		.height		= 1090,
		.framerate	= 24,
		.settle		= 4,
	},
	/* IMX135, 1936x1090@30fps */
	[1] = {
		.channel	= CSI_ID_A,
		.width		= 1936,
		.height		= 1090,
		.framerate	= 30,
		.settle		= 5,
	},
	/* IMX135, 1936x1450@24fps */
	[2] = {
		.channel	= CSI_ID_A,
		.width		= 1936,
		.height		= 1450,
		.framerate	= 24,
		.settle		= 5,
	},
	/* IMX135, 2064x1162@24fps */
	[3] = {
		.channel	= CSI_ID_A,
		.width		= 2064,
		.height		= 1162,
		.framerate	= 24,
		.settle		= 8,
	},
	/* IMX135, 1936x1090@60fps */
	[4] = {
		.channel	= CSI_ID_A,
		.width		= 1936,
		.height		= 1090,
		.framerate	= 60,
		.settle		= 9,
	},
	/* IMX135, 816x460@60fps */
	[5] = {
		.channel	= CSI_ID_A,
		.width		= 816,
		.height		= 460,
		.framerate	= 60,
		.settle		= 9,
	},
	/* IMX135, 736x490@120fps */
	[6] = {
		.channel	= CSI_ID_A,
		.width		= 736,
		.height		= 490,
		.framerate	= 120,
		.settle		= 11,
	},
	/* IMX135, 1296x730@120fps */
	[7] = {
		.channel	= CSI_ID_A,
		.width		= 1296,
		.height		= 730,
		.framerate	= 120,
		.settle		= 11,
	},
	/* IMX135, 4112x2314@24ps */
	[8] = {
		.channel	= CSI_ID_A,
		.width		= 4112,
		.height		= 2314,
		.framerate	= 24,
		.settle		= 14,
	},
	/* IMX135, 816x460@120fps */
	[9] = {
		.channel	= CSI_ID_A,
		.width		= 816,
		.height		= 460,
		.framerate	= 120,
		.settle		= 18,
	},
	/* IMX135, 4144x2332@24fps */
	[10] = {
		.channel	= CSI_ID_A,
		.width		= 4144,
		.height		= 2332,
		.framerate	= 24,
		.settle		= 19,
	},
	/* IMX135, 4144x2332@30fps */
	[11] = {
		.channel	= CSI_ID_A,
		.width		= 4144,
		.height		= 2332,
		.framerate	= 30,
		.settle		= 18,
	},
	/* IMX135, 4112x3082@24fps */
	[12] = {
		.channel	= CSI_ID_A,
		.width		= 4112,
		.height		= 3082,
		.framerate	= 24,
		.settle		= 19,
	},
	/* IMX135, 4144x3106@24fps */
	[13] = {
		.channel	= CSI_ID_A,
		.width		= 4144,
		.height		= 3106,
		.framerate	= 24,
		.settle		= 23,
	},
	/* IMX135, 4144x3106@30fps */
	[14] = {
		.channel	= CSI_ID_A,
		.width		= 4144,
		.height		= 3106,
		.framerate	= 30,
		.settle		= 23,
	},
	/* IMX135, 1024x576@120fps */
	[15] = {
		.channel	= CSI_ID_A,
		.width		= 1024,
		.height		= 576,
		.framerate	= 120,
		.settle		= 9,
	},
	/* IMX135, 2048x1152@60fps */
	[16] = {
		.channel	= CSI_ID_A,
		.width		= 2048,
		.height		= 1152,
		.framerate	= 60,
		.settle		= 9,
	},

	/* S5K6B2, 1456x1090@24fps */
	[17] = {
		.channel	= CSI_ID_C,
		.width		= 1456,
		.height		= 1090,
		.framerate	= 24,
		.settle		= 13,
	},
	/* S5K6B2, 1936x1090@24fps */
	[18] = {
		.channel	= CSI_ID_C,
		.width		= 1936,
		.height		= 1090,
		.framerate	= 24,
		.settle		= 13,
	},
	/* S5K6B2, 1456x1090@30fps */
	[19] = {
		.channel	= CSI_ID_C,
		.width		= 1456,
		.height		= 1090,
		.framerate	= 30,
		.settle		= 16,
	},
	/* S5K6B2, 1936x1090@30fps */
	[20] = {
		.channel	= CSI_ID_C,
		.width		= 1936,
		.height		= 1090,
		.framerate	= 30,
		.settle		= 16,
	},
};

static unsigned int num_of_settle = ARRAY_SIZE(csis_hsync_settle);

static int get_hsync_settle(int channel, int width,
				int height, int framerate)
{
	int i;

	for (i = 0; i < num_of_settle; i++) {
		if ((csis_hsync_settle[i].channel == channel) &&
		    (csis_hsync_settle[i].width == width) &&
		    (csis_hsync_settle[i].height == height) &&
		    (csis_hsync_settle[i].framerate == framerate)) {
			dbg_front("settle time: ch%d, %dx%d@%dfps -> %d",
				channel, width, height, framerate,
				csis_hsync_settle[i].settle);

			return csis_hsync_settle[i].settle;
		}
	}

	warn("could not find proper settle time: ch%d, %dx%d@%dfps",
		channel, width, height, framerate);

	/*
	 * return a max settle time value in above table
	 * as a default depending on the channel
	 */
	if (channel == CSI_ID_A)
		return 23;

	return 16;
}

static void s5pcsis_enable_interrupts(unsigned long mipi_reg_base, bool on)
{
	u32 val = readl(mipi_reg_base + S5PCSIS_INTMSK);

	val = on ? val | S5PCSIS_INTMSK_EN_ALL :
		   val & ~S5PCSIS_INTMSK_EN_ALL;
	writel(val, mipi_reg_base + S5PCSIS_INTMSK);
}

static void s5pcsis_reset(unsigned long mipi_reg_base)
{
	u32 val = readl(mipi_reg_base + S5PCSIS_CTRL);

	writel(val | S5PCSIS_CTRL_RESET, mipi_reg_base + S5PCSIS_CTRL);
	udelay(10);
}

static void s5pcsis_system_enable(unsigned long mipi_reg_base, int on)
{
	u32 val;

	val = readl(mipi_reg_base + S5PCSIS_CTRL);
	if (on) {
		val |= S5PCSIS_CTRL_ENABLE;
		val |= S5PCSIS_CTRL_WCLK_EXTCLK;
	} else
		val &= ~S5PCSIS_CTRL_ENABLE;
	writel(val, mipi_reg_base + S5PCSIS_CTRL);

	val = readl(mipi_reg_base + S5PCSIS_DPHYCTRL);
	if (on)
		val |= S5PCSIS_DPHYCTRL_ENABLE;
	else
		val &= ~S5PCSIS_DPHYCTRL_ENABLE;
	writel(val, mipi_reg_base + S5PCSIS_DPHYCTRL);
}

/* Called with the state.lock mutex held */
static void __s5pcsis_set_format(unsigned long mipi_reg_base,
				struct fimc_is_frame_info *f_frame)
{
	u32 val;

	/* Color format */
	val = readl(mipi_reg_base + S5PCSIS_CONFIG);
	val = (val & ~S5PCSIS_CFG_FMT_MASK) | S5PCSIS_CFG_FMT_RAW10;
	writel(val, mipi_reg_base + S5PCSIS_CONFIG);

	/* Pixel resolution */
	val = (f_frame->o_width << 16) | f_frame->o_height;
	writel(val, mipi_reg_base + S5PCSIS_RESOL);
}

static void s5pcsis_set_hsync_settle(unsigned long mipi_reg_base, int settle)
{
	u32 val = readl(mipi_reg_base + S5PCSIS_DPHYCTRL);

	if (soc_is_exynos5250())
		val = (val & ~S5PCSIS_DPHYCTRL_HSS_MASK) | (0x6 << 28);
	else
		val = (val & ~S5PCSIS_DPHYCTRL_HSS_MASK) | (settle << 24);

	writel(val, mipi_reg_base + S5PCSIS_DPHYCTRL);
}

static void s5pcsis_set_params(unsigned long mipi_reg_base,
				struct fimc_is_frame_info *f_frame)
{
	u32 val;

	val = readl(mipi_reg_base + S5PCSIS_CONFIG);
	if (soc_is_exynos5250())
		val = (val & ~S5PCSIS_CFG_NR_LANE_MASK) | (2 - 1);
	else
		val = (val & ~S5PCSIS_CFG_NR_LANE_MASK) | (4 - 1);
	writel(val, mipi_reg_base + S5PCSIS_CONFIG);

	__s5pcsis_set_format(mipi_reg_base, f_frame);

	val = readl(mipi_reg_base + S5PCSIS_CTRL);
	val &= ~S5PCSIS_CTRL_ALIGN_32BIT;

	/* Not using external clock. */
	val &= ~S5PCSIS_CTRL_WCLK_EXTCLK;

	writel(val, mipi_reg_base + S5PCSIS_CTRL);

	/* Update the shadow register. */
	val = readl(mipi_reg_base + S5PCSIS_CTRL);
	writel(val | S5PCSIS_CTRL_UPDATE_SHADOW, mipi_reg_base + S5PCSIS_CTRL);
}

int enable_mipi(void)
{
	void __iomem *addr;
	u32 cfg;

	addr = S5P_MIPI_DPHY_CONTROL(0);

	cfg = __raw_readl(addr);
	cfg = (cfg | S5P_MIPI_DPHY_SRESETN);
	__raw_writel(cfg, addr);

	if (1) {
		cfg |= S5P_MIPI_DPHY_ENABLE;
	} else if (!(cfg & (S5P_MIPI_DPHY_SRESETN | S5P_MIPI_DPHY_MRESETN)
			& (~S5P_MIPI_DPHY_SRESETN))) {
		cfg &= ~S5P_MIPI_DPHY_ENABLE;
	}

	__raw_writel(cfg, addr);


	addr = S5P_MIPI_DPHY_CONTROL(1);

	cfg = __raw_readl(addr);
	cfg = (cfg | S5P_MIPI_DPHY_SRESETN);
	__raw_writel(cfg, addr);

	if (1) {
		cfg |= S5P_MIPI_DPHY_ENABLE;
	} else if (!(cfg & (S5P_MIPI_DPHY_SRESETN | S5P_MIPI_DPHY_MRESETN)
			& (~S5P_MIPI_DPHY_SRESETN))) {
		cfg &= ~S5P_MIPI_DPHY_ENABLE;
	}

	__raw_writel(cfg, addr);

	addr = S5P_MIPI_DPHY_CONTROL(2);

	cfg = __raw_readl(addr);
	cfg = (cfg | S5P_MIPI_DPHY_SRESETN);
	__raw_writel(cfg, addr);

	cfg |= S5P_MIPI_DPHY_ENABLE;
	if (!(cfg & (S5P_MIPI_DPHY_SRESETN | S5P_MIPI_DPHY_MRESETN)
			& (~S5P_MIPI_DPHY_SRESETN)))
		cfg &= ~S5P_MIPI_DPHY_ENABLE;

	__raw_writel(cfg, addr);
	return 0;

}

int start_mipi_csi(int channel, struct fimc_is_frame_info *f_frame,
			int framerate)
{
	unsigned long base_reg = (unsigned long)MIPICSI0_REG_BASE;

	if (channel == CSI_ID_A)
		base_reg = (unsigned long)MIPICSI0_REG_BASE;
	else if (channel == CSI_ID_B)
		base_reg = (unsigned long)MIPICSI1_REG_BASE;
	else if (channel == CSI_ID_C)
		base_reg = (unsigned long)MIPICSI2_REG_BASE;

	s5pcsis_reset(base_reg);
	s5pcsis_set_hsync_settle(base_reg,
			get_hsync_settle(channel, f_frame->width,
				f_frame->height, framerate));
	s5pcsis_set_params(base_reg, f_frame);
	s5pcsis_system_enable(base_reg, true);
	s5pcsis_enable_interrupts(base_reg, true);

	return 0;
}

int stop_mipi_csi(int channel)
{
	unsigned long base_reg = (unsigned long)MIPICSI0_REG_BASE;

	if (channel == CSI_ID_A)
		base_reg = (unsigned long)MIPICSI0_REG_BASE;
	else if (channel == CSI_ID_B)
		base_reg = (unsigned long)MIPICSI1_REG_BASE;
	else if (channel == CSI_ID_C)
		base_reg = (unsigned long)MIPICSI2_REG_BASE;

	s5pcsis_enable_interrupts(base_reg, false);
	s5pcsis_system_enable(base_reg, false);

	return 0;
}

static int testnset_state(struct fimc_is_device_sensor *device,
	unsigned long state)
{
	int ret = 0;

	spin_lock(&device->slock_state);

	if (test_bit(state, &device->state)) {
		ret = -EINVAL;
		goto exit;
	}
	set_bit(state, &device->state);

exit:
	spin_unlock(&device->slock_state);
	return ret;
}

static int testnclr_state(struct fimc_is_device_sensor *device,
	unsigned long state)
{
	int ret = 0;

	spin_lock(&device->slock_state);

	if (!test_bit(state, &device->state)) {
		ret = -EINVAL;
		goto exit;
	}
	clear_bit(state, &device->state);

exit:
	spin_unlock(&device->slock_state);
	return ret;
}

int fimc_is_sensor_probe(struct fimc_is_device_sensor *device, u32 channel)
{
	int ret = 0;
	struct sensor_open_extended *ext;
	struct fimc_is_enum_sensor *enum_sensor;

	BUG_ON(!device);

	enum_sensor = device->enum_sensor;
	device->flite.channel = channel;

	/*sensor init*/
	clear_bit(FIMC_IS_SENSOR_OPEN, &device->state);
	clear_bit(FIMC_IS_SENSOR_FRONT_START, &device->state);
	clear_bit(FIMC_IS_SENSOR_BACK_START, &device->state);
	spin_lock_init(&device->slock_state);

	ret = fimc_is_flite_probe(&device->flite, (u32)device);
	if (ret) {
		merr("fimc_is_flite%d_probe is fail", device,
			device->flite.channel);
		goto p_err;
	}

	/* S5K3H2 */
	enum_sensor[SENSOR_NAME_S5K3H2].sensor = SENSOR_NAME_S5K3H2;
	enum_sensor[SENSOR_NAME_S5K3H2].pixel_width = 0;
	enum_sensor[SENSOR_NAME_S5K3H2].pixel_height = 0;
	enum_sensor[SENSOR_NAME_S5K3H2].active_width = 0;
	enum_sensor[SENSOR_NAME_S5K3H2].active_height = 0;
	enum_sensor[SENSOR_NAME_S5K3H2].max_framerate = 0;
	enum_sensor[SENSOR_NAME_S5K3H2].csi_ch = 0;
	enum_sensor[SENSOR_NAME_S5K3H2].flite_ch = 0;
	enum_sensor[SENSOR_NAME_S5K3H2].i2c_ch = 0;
	enum_sensor[SENSOR_NAME_S5K3H2].setfile_name =
			"setfile_3h2.bin";

	ext = &enum_sensor[SENSOR_NAME_S5K3H2].ext;
	memset(ext, 0x0, sizeof(struct sensor_open_extended));

	/* S5K6A3 */
	enum_sensor[SENSOR_NAME_S5K6A3].sensor = SENSOR_NAME_S5K6A3;
	enum_sensor[SENSOR_NAME_S5K6A3].pixel_width = 1392 + 16;
	enum_sensor[SENSOR_NAME_S5K6A3].pixel_height = 1392 + 10;
	enum_sensor[SENSOR_NAME_S5K6A3].active_width = 1392;
	enum_sensor[SENSOR_NAME_S5K6A3].active_height = 1392;
	enum_sensor[SENSOR_NAME_S5K6A3].max_framerate = 30;
	if (soc_is_exynos5250()) {
		enum_sensor[SENSOR_NAME_S5K6A3].csi_ch = 1;
		enum_sensor[SENSOR_NAME_S5K6A3].flite_ch = FLITE_ID_B;
		enum_sensor[SENSOR_NAME_S5K6A3].i2c_ch = 1;
	} else {
		enum_sensor[SENSOR_NAME_S5K6A3].csi_ch = 2;
		enum_sensor[SENSOR_NAME_S5K6A3].flite_ch = FLITE_ID_C;
		enum_sensor[SENSOR_NAME_S5K6A3].i2c_ch = 2;
	}
	enum_sensor[SENSOR_NAME_S5K6A3].setfile_name =
			"setfile_6a3.bin";

	ext = &enum_sensor[SENSOR_NAME_S5K6A3].ext;
	memset(ext, 0x0, sizeof(struct sensor_open_extended));

	/* S5K4E5 */
	enum_sensor[SENSOR_NAME_S5K4E5].sensor = SENSOR_NAME_S5K4E5;
	enum_sensor[SENSOR_NAME_S5K4E5].pixel_width = 2560 + 16;
	enum_sensor[SENSOR_NAME_S5K4E5].pixel_height = 1920 + 10;
	enum_sensor[SENSOR_NAME_S5K4E5].active_width = 2560;
	enum_sensor[SENSOR_NAME_S5K4E5].active_height = 1920;
	enum_sensor[SENSOR_NAME_S5K4E5].max_framerate = 30;
	enum_sensor[SENSOR_NAME_S5K4E5].csi_ch = 0;
	enum_sensor[SENSOR_NAME_S5K4E5].flite_ch = FLITE_ID_A;
	enum_sensor[SENSOR_NAME_S5K4E5].i2c_ch = 0;
	enum_sensor[SENSOR_NAME_S5K4E5].setfile_name =
			"setfile_4e5.bin";

	ext = &enum_sensor[SENSOR_NAME_S5K4E5].ext;
	ext->actuator_con.product_name = ACTUATOR_NAME_DWXXXX;
	ext->actuator_con.peri_type = SE_I2C;
	ext->actuator_con.peri_setting.i2c.channel
		= SENSOR_CONTROL_I2C1;

	if (soc_is_exynos5250()) {
		ext->flash_con.product_name = FLADRV_NAME_KTD267;
		ext->flash_con.peri_type = SE_GPIO;
		ext->flash_con.peri_setting.gpio.first_gpio_port_no = 17;
		ext->flash_con.peri_setting.gpio.second_gpio_port_no = 16;
	} else if (soc_is_exynos5410()) {
		ext->flash_con.product_name = FLADRV_NAME_MAX77693;
		ext->flash_con.peri_type = SE_GPIO;
		ext->flash_con.peri_setting.gpio.first_gpio_port_no = 0;
		ext->flash_con.peri_setting.gpio.second_gpio_port_no = 1;
	}

	ext->from_con.product_name = FROMDRV_NAME_NOTHING;
	ext->mclk = 0;
	ext->mipi_lane_num = 0;
	ext->mipi_speed = 0;
	ext->fast_open_sensor = 0;
	ext->self_calibration_mode = 0;
	ext->I2CSclk = I2C_L0;

	/* S5K3H5 */
	enum_sensor[SENSOR_NAME_S5K3H5].sensor = SENSOR_NAME_S5K3H5;
	enum_sensor[SENSOR_NAME_S5K3H5].pixel_width = 3248 + 16;
	enum_sensor[SENSOR_NAME_S5K3H5].pixel_height = 2438 + 10;
	enum_sensor[SENSOR_NAME_S5K3H5].active_width = 3248;
	enum_sensor[SENSOR_NAME_S5K3H5].active_height = 2438;
	enum_sensor[SENSOR_NAME_S5K3H5].max_framerate = 30;
	enum_sensor[SENSOR_NAME_S5K3H5].csi_ch = 0;
	enum_sensor[SENSOR_NAME_S5K3H5].flite_ch = FLITE_ID_A;
	enum_sensor[SENSOR_NAME_S5K3H5].i2c_ch = 0;
	enum_sensor[SENSOR_NAME_S5K3H5].setfile_name =
			"setfile_3h5.bin";

	ext = &enum_sensor[SENSOR_NAME_S5K3H5].ext;

	ext->actuator_con.product_name = ACTUATOR_NAME_AK7343;
	ext->actuator_con.peri_type = SE_I2C;
	ext->actuator_con.peri_setting.i2c.channel
		= SENSOR_CONTROL_I2C0;

	if (soc_is_exynos5250()) {
		ext->flash_con.product_name = FLADRV_NAME_AAT1290A;
		ext->flash_con.peri_type = SE_GPIO;
		ext->flash_con.peri_setting.gpio.first_gpio_port_no = 17;
		ext->flash_con.peri_setting.gpio.second_gpio_port_no = 16;
	} else if (soc_is_exynos5410()) {
		ext->flash_con.product_name = FLADRV_NAME_KTD267;
		ext->flash_con.peri_type = SE_GPIO;
		ext->flash_con.peri_setting.gpio.first_gpio_port_no = 14;
		ext->flash_con.peri_setting.gpio.second_gpio_port_no = 15;
	}

	ext->from_con.product_name = FROMDRV_NAME_NOTHING;
	ext->mclk = 0;
	ext->mipi_lane_num = 0;
	ext->mipi_speed = 0;
	ext->fast_open_sensor = 0;
	ext->self_calibration_mode = 0;
	ext->I2CSclk = I2C_L0;

	/* S5K3H7 */
	enum_sensor[SENSOR_NAME_S5K3H7].sensor = SENSOR_NAME_S5K3H7;
	enum_sensor[SENSOR_NAME_S5K3H7].pixel_width = 3248 + 16;
	enum_sensor[SENSOR_NAME_S5K3H7].pixel_height = 2438 + 10;
	enum_sensor[SENSOR_NAME_S5K3H7].active_width = 3248;
	enum_sensor[SENSOR_NAME_S5K3H7].active_height = 2438;
	enum_sensor[SENSOR_NAME_S5K3H7].max_framerate = 30;
	enum_sensor[SENSOR_NAME_S5K3H7].csi_ch = 1;
	enum_sensor[SENSOR_NAME_S5K3H7].flite_ch = FLITE_ID_B;
	enum_sensor[SENSOR_NAME_S5K3H7].i2c_ch = 1;
	enum_sensor[SENSOR_NAME_S5K3H7].setfile_name =
			"setfile_3h7.bin";

	ext = &enum_sensor[SENSOR_NAME_S5K3H7].ext;

	ext->actuator_con.product_name = ACTUATOR_NAME_AK7343;
	ext->actuator_con.peri_type = SE_I2C;
	ext->actuator_con.peri_setting.i2c.channel
		= SENSOR_CONTROL_I2C0;

	if (soc_is_exynos5250()) {
		ext->flash_con.product_name = FLADRV_NAME_KTD267;
		ext->flash_con.peri_type = SE_GPIO;
		ext->flash_con.peri_setting.gpio.first_gpio_port_no = 17;
		ext->flash_con.peri_setting.gpio.second_gpio_port_no = 16;
	} else if (soc_is_exynos5410()) {
		ext->flash_con.product_name = FLADRV_NAME_KTD267;
		ext->flash_con.peri_type = SE_GPIO;
		ext->flash_con.peri_setting.gpio.first_gpio_port_no = 14;
		ext->flash_con.peri_setting.gpio.second_gpio_port_no = 15;
	}

	ext->from_con.product_name = FROMDRV_NAME_NOTHING;
	ext->mclk = 0;
	ext->mipi_lane_num = 0;
	ext->mipi_speed = 0;
	ext->fast_open_sensor = 0;
	ext->self_calibration_mode = 0;
	ext->I2CSclk = I2C_L0;

	/* IMX135 */
	enum_sensor[SENSOR_NAME_IMX135].sensor = SENSOR_NAME_IMX135;
	enum_sensor[SENSOR_NAME_IMX135].pixel_width = 4128 + 16;
	enum_sensor[SENSOR_NAME_IMX135].pixel_height = 3096 + 10;
	enum_sensor[SENSOR_NAME_IMX135].active_width = 4128;
	enum_sensor[SENSOR_NAME_IMX135].active_height = 3096;
	enum_sensor[SENSOR_NAME_IMX135].max_framerate = 30;
	enum_sensor[SENSOR_NAME_IMX135].csi_ch = 0;
	enum_sensor[SENSOR_NAME_IMX135].flite_ch = FLITE_ID_A;
	enum_sensor[SENSOR_NAME_IMX135].i2c_ch = 0;
	enum_sensor[SENSOR_NAME_IMX135].setfile_name =
			"setfile_imx135.bin";

	ext = &enum_sensor[SENSOR_NAME_IMX135].ext;

	ext->actuator_con.product_name = ACTUATOR_NAME_AK7345;
	ext->actuator_con.peri_type = SE_I2C;
	ext->actuator_con.peri_setting.i2c.channel
		= SENSOR_CONTROL_I2C0;

	if (soc_is_exynos5250()) {
		ext->flash_con.product_name = FLADRV_NAME_AAT1290A;
		ext->flash_con.peri_type = SE_GPIO;
		ext->flash_con.peri_setting.gpio.first_gpio_port_no = 17;
		ext->flash_con.peri_setting.gpio.second_gpio_port_no = 16;
	} else if (soc_is_exynos5410()) {
		ext->flash_con.product_name = FLADRV_NAME_AAT1290A;
		ext->flash_con.peri_type = SE_GPIO;
		ext->flash_con.peri_setting.gpio.first_gpio_port_no = 14;
		ext->flash_con.peri_setting.gpio.second_gpio_port_no = 15;
	}

	ext->from_con.product_name = FROMDRV_NAME_NOTHING;
	ext->mclk = 0;
	ext->mipi_lane_num = 0;
	ext->mipi_speed = 0;
	ext->fast_open_sensor = 0;
	ext->self_calibration_mode = 0;
	ext->I2CChannel = 0;
	ext->I2CSclk = I2C_L0;

	/* S5K6B2 */
	enum_sensor[SENSOR_NAME_S5K6B2].sensor = SENSOR_NAME_S5K6B2;
	enum_sensor[SENSOR_NAME_S5K6B2].pixel_width = 1920 + 16;
	enum_sensor[SENSOR_NAME_S5K6B2].pixel_height = 1080 + 10;
	enum_sensor[SENSOR_NAME_S5K6B2].active_width = 1920;
	enum_sensor[SENSOR_NAME_S5K6B2].active_height = 1080;
	enum_sensor[SENSOR_NAME_S5K6B2].max_framerate = 30;
	if (soc_is_exynos5250()) {
		enum_sensor[SENSOR_NAME_S5K6B2].csi_ch = 1;
		enum_sensor[SENSOR_NAME_S5K6B2].flite_ch = FLITE_ID_B;
		enum_sensor[SENSOR_NAME_S5K6B2].i2c_ch = 1;
	} else {
		enum_sensor[SENSOR_NAME_S5K6B2].csi_ch = 2;
		enum_sensor[SENSOR_NAME_S5K6B2].flite_ch = FLITE_ID_C;
		enum_sensor[SENSOR_NAME_S5K6B2].i2c_ch = 2;
	}
	enum_sensor[SENSOR_NAME_S5K6B2].setfile_name =
			"setfile_6b2.bin";

	ext = &enum_sensor[SENSOR_NAME_S5K6B2].ext;
	memset(ext, 0x0, sizeof(struct sensor_open_extended));

	ext->I2CChannel = 2;
	ext->I2CSclk = I2C_L0;

	/* IMX135_FHD60 */
	enum_sensor[SENSOR_NAME_IMX135_FHD60].sensor = SENSOR_NAME_IMX135_FHD60;
	enum_sensor[SENSOR_NAME_IMX135_FHD60].pixel_width = 1920 + 16;
	enum_sensor[SENSOR_NAME_IMX135_FHD60].pixel_height = 1080 + 10;
	enum_sensor[SENSOR_NAME_IMX135_FHD60].active_width = 1920;
	enum_sensor[SENSOR_NAME_IMX135_FHD60].active_height = 1080;
	enum_sensor[SENSOR_NAME_IMX135_FHD60].max_framerate = 60;
	enum_sensor[SENSOR_NAME_IMX135_FHD60].csi_ch = 0;
	enum_sensor[SENSOR_NAME_IMX135_FHD60].flite_ch = FLITE_ID_A;
	enum_sensor[SENSOR_NAME_IMX135_FHD60].i2c_ch = 0;
	enum_sensor[SENSOR_NAME_IMX135_FHD60].setfile_name =
			"setfile_imx135.bin";

	ext = &enum_sensor[SENSOR_NAME_IMX135_FHD60].ext;

	ext->actuator_con.product_name = ACTUATOR_NAME_AK7345;
	ext->actuator_con.peri_type = SE_I2C;
	ext->actuator_con.peri_setting.i2c.channel
		= SENSOR_CONTROL_I2C0;

	if (soc_is_exynos5250()) {
		ext->flash_con.product_name = FLADRV_NAME_AAT1290A;
		ext->flash_con.peri_type = SE_GPIO;
		ext->flash_con.peri_setting.gpio.first_gpio_port_no = 17;
		ext->flash_con.peri_setting.gpio.second_gpio_port_no = 16;
	} else if (soc_is_exynos5410()) {
		ext->flash_con.product_name = FLADRV_NAME_KTD267;
		ext->flash_con.peri_type = SE_GPIO;
		ext->flash_con.peri_setting.gpio.first_gpio_port_no = 14;
		ext->flash_con.peri_setting.gpio.second_gpio_port_no = 15;
	}

	ext->from_con.product_name = FROMDRV_NAME_NOTHING;
	ext->mclk = 0;
	ext->mipi_lane_num = 0;
	ext->mipi_speed = 0;
	ext->fast_open_sensor = 0;
	ext->self_calibration_mode = 0;
	ext->I2CSclk = I2C_L0;

	/* IMX135_HD120 */
	enum_sensor[SENSOR_NAME_IMX135_HD120].sensor = SENSOR_NAME_IMX135_HD120;
	enum_sensor[SENSOR_NAME_IMX135_HD120].pixel_width = 1280 + 16;
	enum_sensor[SENSOR_NAME_IMX135_HD120].pixel_height = 720 + 10;
	enum_sensor[SENSOR_NAME_IMX135_HD120].active_width = 1280;
	enum_sensor[SENSOR_NAME_IMX135_HD120].active_height = 720;
	enum_sensor[SENSOR_NAME_IMX135_HD120].max_framerate = 120;
	enum_sensor[SENSOR_NAME_IMX135_HD120].csi_ch = 0;
	enum_sensor[SENSOR_NAME_IMX135_HD120].flite_ch = FLITE_ID_A;
	enum_sensor[SENSOR_NAME_IMX135_HD120].i2c_ch = 0;
	enum_sensor[SENSOR_NAME_IMX135_HD120].setfile_name =
			"setfile_imx135.bin";

	ext = &enum_sensor[SENSOR_NAME_IMX135_HD120].ext;

	ext->actuator_con.product_name = ACTUATOR_NAME_AK7345;
	ext->actuator_con.peri_type = SE_I2C;
	ext->actuator_con.peri_setting.i2c.channel
		= SENSOR_CONTROL_I2C0;

	if (soc_is_exynos5250()) {
		ext->flash_con.product_name = FLADRV_NAME_AAT1290A;
		ext->flash_con.peri_type = SE_GPIO;
		ext->flash_con.peri_setting.gpio.first_gpio_port_no = 17;
		ext->flash_con.peri_setting.gpio.second_gpio_port_no = 16;
	} else if (soc_is_exynos5410()) {
		ext->flash_con.product_name = FLADRV_NAME_AAT1290A;
		ext->flash_con.peri_type = SE_GPIO;
		ext->flash_con.peri_setting.gpio.first_gpio_port_no = 14;
		ext->flash_con.peri_setting.gpio.second_gpio_port_no = 15;
	}

	ext->from_con.product_name = FROMDRV_NAME_NOTHING;
	ext->mclk = 0;
	ext->mipi_lane_num = 0;
	ext->mipi_speed = 0;
	ext->fast_open_sensor = 0;
	ext->self_calibration_mode = 0;
	ext->I2CSclk = I2C_L0;

p_err:
	return ret;
}

int fimc_is_sensor_open(struct fimc_is_device_sensor *device,
	struct fimc_is_video_ctx *vctx)
{
	int ret = 0;
	struct fimc_is_core *core;

	if (testnset_state(device, FIMC_IS_SENSOR_OPEN)) {
		merr("already open", device);
		ret = -EMFILE;
		goto p_err;
	}

	clear_bit(FIMC_IS_SENSOR_FRONT_START, &device->state);
	clear_bit(FIMC_IS_SENSOR_BACK_START, &device->state);
	device->vctx = vctx;
	device->active_sensor = NULL;
	device->ischain = NULL;

	core = (struct fimc_is_core *)vctx->video->core;

	/* for mediaserver force close */
	ret = fimc_is_resource_get(core);
	if (ret) {
		merr("fimc_is_resource_get is fail", device);
		goto p_err;
	}

	/* Sensor clock on */
	ret = fimc_is_flite_set_clk(device->flite.channel,
		core, &device->flite);
	if (ret != 0) {
		merr("fimc_is_flite_set_clk(%d) fail",
			device, device->flite.channel);
		goto p_err;
	}

	ret = fimc_is_flite_open(&device->flite, vctx);
	if (ret != 0) {
		merr("fimc_is_flite_open(%d) fail",
			device, device->flite.channel);
		goto p_err;
	}

	/* Sensor power on */
	if (core->pdata->cfg_gpio) {
		core->pdata->cfg_gpio(core->pdev,
					device->flite.channel,
					true);
	} else {
		err("failed to sensor_power_on\n");
		ret = -EINVAL;
		goto p_err;
	}

p_err:
	pr_info("[SEN:D:%d] %s(%d)\n", device->instance, __func__, ret);

	return ret;
}

int fimc_is_sensor_close(struct fimc_is_device_sensor *device)
{
	int ret = 0;
	struct fimc_is_core *core;
	struct fimc_is_device_ischain *ischain;
	struct fimc_is_group *group_3ax;

	BUG_ON(!device);

	core = (struct fimc_is_core *)device->vctx->video->core;

	if (testnclr_state(device, FIMC_IS_SENSOR_OPEN)) {
		merr("already close", device);
		ret = -EMFILE;
		goto exit;
	}

	/* for mediaserver force close */
	ischain = device->ischain;
	if (ischain) {
		group_3ax = &ischain->group_3ax;
		if (test_bit(FIMC_IS_GROUP_READY, &group_3ax->state)) {
			pr_info("media server is dead, 3ax forcely done\n");
			set_bit(FIMC_IS_GROUP_REQUEST_FSTOP, &group_3ax->state);
		}
	}

	fimc_is_sensor_back_stop(device);
	fimc_is_sensor_front_stop(device);

	ret = fimc_is_flite_close(&device->flite);
	if (ret != 0) {
		merr("fimc_is_flite_close(%d) fail",
			device, device->flite.channel);
		goto exit_rsc;
	}

	/* Sensor clock off */
	ret = fimc_is_flite_put_clk(device->flite.channel,
		core, &device->flite);
	if (ret != 0) {
		merr("fimc_is_flite_put_clk(%d) fail",
			device, device->flite.channel);
		goto exit_rsc;
	}

exit_rsc:
	/* for mediaserver force close */
	ret = fimc_is_resource_put(core);
	if (ret) {
		merr("fimc_is_resource_put is fail", device);
		goto exit;
	}

exit:
	pr_info("[SEN:D:%d] %s(%d)\n", device->instance, __func__, ret);
	return ret;
}

int fimc_is_sensor_s_format(struct fimc_is_device_sensor *device,
	u32 width, u32 height)
{
	device->width = width;
	device->height = height;

	return 0;
}

int fimc_is_sensor_s_active_sensor(struct fimc_is_device_sensor *device,
	struct fimc_is_video_ctx *vctx,
	struct fimc_is_framemgr *framemgr,
	u32 input)
{
	int ret = 0;

	mdbgd_sensor("%s(%d)\n", device, __func__, input);

	device->active_sensor = &device->enum_sensor[input];
	device->framerate = min_t(unsigned int, SENSOR_DEFAULT_FRAMERATE,
				device->active_sensor->max_framerate);
	device->width = device->active_sensor->pixel_width;
	device->height = device->active_sensor->pixel_height;

	return ret;
}

int fimc_is_sensor_buffer_queue(struct fimc_is_device_sensor *device,
	u32 index)
{
	int ret = 0;
	unsigned long flags;
	struct fimc_is_frame *frame;
	struct fimc_is_framemgr *framemgr;

	if (index >= FRAMEMGR_MAX_REQUEST) {
		err("index(%d) is invalid", index);
		ret = -EINVAL;
		goto exit;
	}

	framemgr = &device->vctx->q_dst.framemgr;
	if (framemgr == NULL) {
		err("framemgr is null\n");
		ret = EINVAL;
		goto exit;
	}

	frame = &framemgr->frame[index];
	if (frame == NULL) {
		err("frame is null\n");
		ret = EINVAL;
		goto exit;
	}

	if (frame->init == FRAME_UNI_MEM) {
		err("frame %d is NOT init", index);
		ret = EINVAL;
		goto exit;
	}

	framemgr_e_barrier_irqs(framemgr, FMGR_IDX_2 + index, flags);

	if (frame->state == FIMC_IS_FRAME_STATE_FREE) {
		fimc_is_frame_trans_fre_to_req(framemgr, frame);
	} else {
		err("frame(%d) is not free state(%d)", index, frame->state);
		fimc_is_frame_print_all(framemgr);
	}

	framemgr_x_barrier_irqr(framemgr, FMGR_IDX_2 + index, flags);

exit:
	return ret;
}

int fimc_is_sensor_buffer_finish(struct fimc_is_device_sensor *device,
	u32 index)
{
	int ret = 0;
	unsigned long flags;
	struct fimc_is_frame *frame;
	struct fimc_is_framemgr *framemgr;

	if (index >= FRAMEMGR_MAX_REQUEST) {
		err("index(%d) is invalid", index);
		ret = -EINVAL;
		goto exit;
	}

	framemgr = &device->vctx->q_dst.framemgr;
	frame = &framemgr->frame[index];

	framemgr_e_barrier_irqs(framemgr, FMGR_IDX_3 + index, flags);

	if (frame->state == FIMC_IS_FRAME_STATE_COMPLETE) {
		if (!frame->shot->dm.request.frameCount)
			err("request.frameCount is 0\n");
		fimc_is_frame_trans_com_to_fre(framemgr, frame);

		frame->shot_ext->free_cnt = framemgr->frame_fre_cnt;
		frame->shot_ext->request_cnt = framemgr->frame_req_cnt;
		frame->shot_ext->process_cnt = framemgr->frame_pro_cnt;
		frame->shot_ext->complete_cnt = framemgr->frame_com_cnt;
	} else {
		err("frame(%d) is not com state(%d)", index, frame->state);
		fimc_is_frame_print_all(framemgr);
	}

	framemgr_x_barrier_irqr(framemgr, FMGR_IDX_3 + index, flags);

exit:
	return ret;
}

int fimc_is_sensor_back_start(struct fimc_is_device_sensor *device,
	struct fimc_is_video_ctx *vctx)
{
	int ret = 0;
	struct fimc_is_frame_info frame;

	dbg_back("%s\n", __func__);

	if (testnset_state(device, FIMC_IS_SENSOR_BACK_START)) {
		err("already back start");
		ret = -EINVAL;
		goto exit;
	}

	frame.o_width = device->width;
	frame.o_height = device->height;
	frame.offs_h = 0;
	frame.offs_v = 0;
	frame.width = device->width;
	frame.height = device->height;

	/*start flite*/
	fimc_is_flite_start(&device->flite, &frame, vctx);

	pr_info("[BAK:D:%d] start(%dx%d)\n", device->instance,
		frame.width, frame.height);

exit:
	return ret;
}

int fimc_is_sensor_back_stop(struct fimc_is_device_sensor *device)
{
	int ret = 0;
	bool wait = true;
	unsigned long flags;
	struct fimc_is_frame *frame;
	struct fimc_is_framemgr *framemgr;
	struct fimc_is_device_flite *flite;

	dbg_back("%s\n", __func__);

	if (testnclr_state(device, FIMC_IS_SENSOR_BACK_START)) {
		warn("already back stop");
		goto exit;
	}

	framemgr = GET_DST_FRAMEMGR(device->vctx);
	flite = &device->flite;

	if (test_bit(FIMC_IS_SENSOR_FRONT_START, &device->state)) {
		wait = true;
	} else {
		warn("front already stop, no waiting...");
		wait = false;
	}

	ret = fimc_is_flite_stop(flite, wait);
	if (ret)
		err("fimc_is_flite_stop is fail");

	framemgr_e_barrier_irqs(framemgr, FMGR_IDX_3, flags);

	fimc_is_frame_complete_head(framemgr, &frame);
	while (frame) {
		fimc_is_frame_trans_com_to_fre(framemgr, frame);
		fimc_is_frame_complete_head(framemgr, &frame);
	}

	fimc_is_frame_process_head(framemgr, &frame);
	while (frame) {
		fimc_is_frame_trans_pro_to_fre(framemgr, frame);
		fimc_is_frame_process_head(framemgr, &frame);
	}

	fimc_is_frame_request_head(framemgr, &frame);
	while (frame) {
		fimc_is_frame_trans_req_to_fre(framemgr, frame);
		fimc_is_frame_request_head(framemgr, &frame);
	}

	framemgr_x_barrier_irqr(framemgr, FMGR_IDX_3, flags);

exit:
	pr_info("[BAK:D:%d] %s(%d)\n", device->instance, __func__, ret);
	return ret;
}

int fimc_is_sensor_back_pause(struct fimc_is_device_sensor *device)
{
	int ret = 0;
#if (defined(DEBUG) && defined(DBG_DEVICE))
	struct fimc_is_framemgr *framemgr = GET_DST_FRAMEMGR(device->vctx);
#endif

	dbg_back("%s\n", __func__);

	ret = fimc_is_flite_stop(&device->flite, false);
	if (ret)
		err("fimc_is_flite_stop is fail");

	dbg_back("framemgr cnt: %d, fre: %d, req: %d, pro: %d, com: %d\n",
			framemgr->frame_cnt, framemgr->frame_fre_cnt,
			framemgr->frame_req_cnt, framemgr->frame_pro_cnt,
			framemgr->frame_com_cnt);

	/* need to re-arrange frames in manager */

	return ret;
}

void fimc_is_sensor_back_restart(struct fimc_is_device_sensor *device)
{
	struct fimc_is_frame_info frame;

	dbg_back("%s\n", __func__);

	frame.o_width = device->width;
	frame.o_height = device->height;
	frame.offs_h = 0;
	frame.offs_v = 0;
	frame.width = device->width;
	frame.height = device->height;

	fimc_is_flite_restart(&device->flite, &frame, device->vctx);

	dbg_back("restart flite (pos:%d) (port:%d) : %d x %d\n",
		device->active_sensor->sensor,
		device->active_sensor->flite_ch,
		frame.width, frame.height);
}

int fimc_is_sensor_front_start(struct fimc_is_device_sensor *device)
{
	int ret = 0;
	struct fimc_is_frame_info frame;

	dbg_front("%s\n", __func__);

	if (testnset_state(device, FIMC_IS_SENSOR_FRONT_START)) {
		err("already front start");
		ret = -EINVAL;
		goto exit;
	}

	frame.o_width = device->width;
	frame.o_height = device->height;
	frame.offs_h = 0;
	frame.offs_v = 0;
	frame.width = device->width;
	frame.height = device->height;

	start_mipi_csi(device->active_sensor->csi_ch, &frame, device->framerate);

	/*start mipi*/
	dbg_front("start mipi (snesor id:%d) (port:%d) : %d x %d\n",
		device->active_sensor->sensor,
		device->active_sensor->csi_ch,
		frame.width, frame.height);

	if (!device->ischain) {
		mwarn("ischain is NULL", device);
		goto exit;
	}

	ret = fimc_is_itf_stream_on(device->ischain);
	if (ret)
		err("sensor stream on is failed(error %d)\n", ret);
	else
		mdbgd_sensor("sensor stream on\n", device);

exit:
	return ret;
}

int fimc_is_sensor_front_stop(struct fimc_is_device_sensor *device)
{
	int ret = 0;

	dbg_front("%s\n", __func__);

	if (testnclr_state(device, FIMC_IS_SENSOR_FRONT_START)) {
		warn("already front stop");
		goto exit;
	}

	if (!device->ischain) {
		mwarn("ischain is NULL", device);
		goto exit;
	}

	ret = fimc_is_itf_stream_off(device->ischain);
	if (ret)
		err("sensor stream off is failed(error %d)\n", ret);
	else
		dbg_front("sensor stream off\n");

	if (!device->active_sensor) {
		mwarn("active_sensor is NULL", device);
		goto exit;
	}

	stop_mipi_csi(device->active_sensor->csi_ch);

exit:
	pr_info("[FRT:D:%d] %s(%d)\n", device->instance, __func__, ret);
	return ret;
}
