/* SPDX-License-Identifier: GPL-2.0 */
static struct resource iop32x_gpio_res[] = {
	DEFINE_RES_MEM((IOP3XX_PERIPHERAL_PHYS_BASE + 0x07c4), 0x10),
};

static inline void register_iop32x_gpio(void)
{
	platform_device_register_simple("gpio-iop", 0,
					iop32x_gpio_res,
					ARRAY_SIZE(iop32x_gpio_res));
}
