/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_MACH_PXA910_H
#define __ASM_MACH_PXA910_H

extern void pxa910_timer_init(void);
extern void __init pxa910_init_irq(void);

#include <linux/i2c.h>
#include <linux/platform_data/i2c-pxa.h>
#include <linux/platform_data/mtd-nand-pxa3xx.h>
#include <video/mmp_disp.h>
#include <linux/irqchip/mmp.h>

#include "devices.h"

extern struct mmp_device_desc pxa910_device_uart1;
extern struct mmp_device_desc pxa910_device_uart2;
extern struct mmp_device_desc pxa910_device_twsi0;
extern struct mmp_device_desc pxa910_device_twsi1;
extern struct mmp_device_desc pxa910_device_pwm1;
extern struct mmp_device_desc pxa910_device_pwm2;
extern struct mmp_device_desc pxa910_device_pwm3;
extern struct mmp_device_desc pxa910_device_pwm4;
extern struct mmp_device_desc pxa910_device_nand;
extern struct platform_device pxa168_device_usb_phy;
extern struct platform_device pxa168_device_u2o;
extern struct platform_device pxa168_device_u2ootg;
extern struct platform_device pxa168_device_u2oehci;
extern struct mmp_device_desc pxa910_device_disp;
extern struct mmp_device_desc pxa910_device_fb;
extern struct mmp_device_desc pxa910_device_panel;
extern struct platform_device pxa910_device_gpio;
extern struct platform_device pxa910_device_rtc;

static inline int pxa910_add_uart(int id)
{
	struct mmp_device_desc *d = NULL;

	switch (id) {
	case 1: d = &pxa910_device_uart1; break;
	case 2: d = &pxa910_device_uart2; break;
	}

	if (d == NULL)
		return -EINVAL;

	return mmp_register_device(d, NULL, 0);
}

static inline int pxa910_add_twsi(int id, struct i2c_pxa_platform_data *data,
				  struct i2c_board_info *info, unsigned size)
{
	struct mmp_device_desc *d = NULL;
	int ret;

	switch (id) {
	case 0: d = &pxa910_device_twsi0; break;
	case 1: d = &pxa910_device_twsi1; break;
	default:
		return -EINVAL;
	}

	ret = i2c_register_board_info(id, info, size);
	if (ret)
		return ret;

	return mmp_register_device(d, data, sizeof(*data));
}

static inline int pxa910_add_pwm(int id)
{
	struct mmp_device_desc *d = NULL;

	switch (id) {
	case 1: d = &pxa910_device_pwm1; break;
	case 2: d = &pxa910_device_pwm2; break;
	case 3: d = &pxa910_device_pwm3; break;
	case 4: d = &pxa910_device_pwm4; break;
	default:
		return -EINVAL;
	}

	return mmp_register_device(d, NULL, 0);
}

static inline int pxa910_add_nand(struct pxa3xx_nand_platform_data *info)
{
	return mmp_register_device(&pxa910_device_nand, info, sizeof(*info));
}
#endif /* __ASM_MACH_PXA910_H */
