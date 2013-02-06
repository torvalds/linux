#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/nfc/pn65n.h>

#include <plat/gpio-cfg.h>
#include <mach/regs-gpio.h>
#include <mach/gpio.h>
#include "midas.h"

/* GPIO_LEVEL_NONE = 2, GPIO_LEVEL_LOW = 0 */
static unsigned int nfc_gpio_table[][4] = {
	{GPIO_NFC_IRQ, S3C_GPIO_INPUT, 2, S3C_GPIO_PULL_DOWN},
	{GPIO_NFC_EN, S3C_GPIO_OUTPUT, 0, S3C_GPIO_PULL_NONE},
	{GPIO_NFC_FIRMWARE, S3C_GPIO_OUTPUT, 0, S3C_GPIO_PULL_NONE},
};

static inline void nfc_setup_gpio(void)
{
	int err = 0;
	int array_size = ARRAY_SIZE(nfc_gpio_table);
	u32 i, gpio;
	for (i = 0; i < array_size; i++) {
		gpio = nfc_gpio_table[i][0];

		err = s3c_gpio_cfgpin(gpio, S3C_GPIO_SFN(nfc_gpio_table[i][1]));
		if (err < 0)
			pr_err("%s, s3c_gpio_cfgpin gpio(%d) fail(err = %d)\n",
				__func__, i, err);

		err = s3c_gpio_setpull(gpio, nfc_gpio_table[i][3]);
		if (err < 0)
			pr_err("%s, s3c_gpio_setpull gpio(%d) fail(err = %d)\n",
				__func__, i, err);

		if (nfc_gpio_table[i][2] != 2)
			gpio_set_value(gpio, nfc_gpio_table[i][2]);
	}
}

static struct pn65n_i2c_platform_data pn65n_pdata = {
	.irq_gpio = GPIO_NFC_IRQ,
	.ven_gpio = GPIO_NFC_EN,
	.firm_gpio = GPIO_NFC_FIRMWARE,
};

static struct i2c_board_info i2c_dev_pn65n __initdata = {
	I2C_BOARD_INFO("pn65n", 0x2b),
	.irq = IRQ_EINT(15),
	.platform_data = &pn65n_pdata,
};

#ifdef CONFIG_SLP
/* In SLP Kernel, i2c_bus number is decided at board file. */
int __init midas_nfc_init(int i2c_bus)
{
	nfc_setup_gpio();

	i2c_register_board_info(i2c_bus, &i2c_dev_pn65n, 1);

	return 0;
}
#else
static int __init midas_nfc_init(void)
{
	int ret = 0;
#if defined(CONFIG_MACH_C1) || \
	defined(CONFIG_MACH_M0_CTC)
#define I2C_BUSNUM_PN65N	(system_rev == 3 ? 0 : 5)
#elif defined(CONFIG_MACH_M3)
#define I2C_BUSNUM_PN65N	12
#elif defined(CONFIG_MACH_M0)
#define I2C_BUSNUM_PN65N	(system_rev == 3 ? 12 : 5)
#elif defined(CONFIG_MACH_T0)
#define I2C_BUSNUM_PN65N	(system_rev == 0 ? 5 : 12)
#elif defined(CONFIG_MACH_BAFFIN)
#define I2C_BUSNUM_PN65N	5
#else
#define I2C_BUSNUM_PN65N	12
#endif

	nfc_setup_gpio();

	ret = i2c_add_devices(I2C_BUSNUM_PN65N, &i2c_dev_pn65n, 1);
	if (ret < 0) {
		pr_err("%s, i2c%d adding i2c fail(err=%d)\n",
			__func__, I2C_BUSNUM_PN65N, ret);
		return ret;
	}

	return ret;
}
module_init(midas_nfc_init);
#endif
