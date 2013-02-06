/* linux/arm/arch/mach-exynos/include/mach/mipi_ddi.h
 *
 * definitions for DDI based MIPI-DSI.
 *
 * Copyright (c) 2009 Samsung Electronics
 * InKi Dae <inki.dae@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef _MIPI_DDI_H
#define _MIPI_DDI_H

enum mipi_ddi_interface {
	RGB_IF = 0x4000,
	I80_IF = 0x8000,
	YUV_601 = 0x10000,
	YUV_656 = 0x20000,
	MIPI_VIDEO = 0x1000,
	MIPI_COMMAND = 0x2000,
};

enum mipi_ddi_panel_select {
	DDI_MAIN_LCD = 0,
	DDI_SUB_LCD = 1,
};

enum mipi_ddi_model {
	S6DR117 = 0,
};

enum mipi_ddi_parameter {
	/* DSIM video interface parameter */
	DSI_VIRTUAL_CH_ID = 0,
	DSI_FORMAT = 1,
	DSI_VIDEO_MODE_SEL = 2,
};

struct mipi_ddi_spec {
	unsigned int parameter[3];
};

struct mipi_ddi_platform_data {
	unsigned int dsim_base;
	unsigned int te_irq;
	unsigned int resume_complete;

	int (*lcd_reset) (void);
	int (*lcd_power_on) (void *pdev, int enable);
	int (*backlight_on) (int enable);

	unsigned char (*cmd_write) (unsigned int dsim_base, unsigned int data0,
		unsigned int data1, unsigned int data2);
	int (*cmd_read) (unsigned int reg_base, u8 addr, u16 count, u8 *buf);

	unsigned int reset_delay;
	unsigned int power_on_delay;
	unsigned int power_off_delay;
#if defined(CONFIG_S5P_DSIM_SWITCHABLE_DUAL_LCD)
	unsigned int lcd_sel_pin;
#endif	/* CONFIG_S5P_DSIM_SWITCHABLE_DUAL_LCD */
};

#endif /* _MIPI_DDI_H */
