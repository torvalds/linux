#ifndef __ASM_MACH_PXA910_H
#define __ASM_MACH_PXA910_H

#include <linux/i2c.h>
#include <mach/devices.h>
#include <plat/i2c.h>

extern struct pxa_device_desc pxa910_device_uart1;
extern struct pxa_device_desc pxa910_device_uart2;
extern struct pxa_device_desc pxa910_device_twsi0;
extern struct pxa_device_desc pxa910_device_twsi1;
extern struct pxa_device_desc pxa910_device_pwm1;
extern struct pxa_device_desc pxa910_device_pwm2;
extern struct pxa_device_desc pxa910_device_pwm3;
extern struct pxa_device_desc pxa910_device_pwm4;

static inline int pxa910_add_uart(int id)
{
	struct pxa_device_desc *d = NULL;

	switch (id) {
	case 1: d = &pxa910_device_uart1; break;
	case 2: d = &pxa910_device_uart2; break;
	}

	if (d == NULL)
		return -EINVAL;

	return pxa_register_device(d, NULL, 0);
}

static inline int pxa910_add_twsi(int id, struct i2c_pxa_platform_data *data,
				  struct i2c_board_info *info, unsigned size)
{
	struct pxa_device_desc *d = NULL;
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

	return pxa_register_device(d, data, sizeof(*data));
}

static inline int pxa910_add_pwm(int id)
{
	struct pxa_device_desc *d = NULL;

	switch (id) {
	case 1: d = &pxa910_device_pwm1; break;
	case 2: d = &pxa910_device_pwm2; break;
	case 3: d = &pxa910_device_pwm3; break;
	case 4: d = &pxa910_device_pwm4; break;
	default:
		return -EINVAL;
	}

	return pxa_register_device(d, NULL, 0);
}
#endif /* __ASM_MACH_PXA910_H */
