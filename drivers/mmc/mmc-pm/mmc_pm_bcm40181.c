/*
 * bcm40181 sdio wifi power management API
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <mach/sys_config.h>

#include "mmc_pm.h"

#define bcm40181_msg(...)    do {printk("[bcm40181]: "__VA_ARGS__);} while(0)

static int bcm40181_powerup = 0;
static int bcm40181_suspend = 0;

static int bcm40181_gpio_ctrl(char* name, int level)
{
	int i = 0, ret = 0;
	struct mmc_pm_ops *ops = &mmc_card_pm_ops;
	char* gpio_name[4] = {"bcm40181_wakeup",
						"bcm40181_shdn",
						"bcm40181_vcc_en",
						"bcm40181_vdd_en"
						};

    for (i=0; i<4; i++) {
        if (strcmp(name, gpio_name[i])==0)
            break;
    }
    if (i==4) {
        bcm40181_msg("No gpio %s for bcm40181-wifi module\n", name);
        return -1;
    }

    ret = gpio_write_one_pin_value(ops->pio_hdle, level, name);
    if (ret) {
        bcm40181_msg("Failed to set gpio %s to %d !\n", name, level);
        return -1;
    } else
		bcm40181_msg("Succeed to set gpio %s to %d !\n", name, level);

    if (strcmp(name, "bcm40181_vdd_en") == 0) {
        bcm40181_powerup = level;
        bcm40181_msg("BCM40181 SDIO Wifi Power %s !!\n", level ? "UP" : "Off");
    }

    return 0;
}

static int bcm40181_get_io_value(char* name)
{
	int ret = -1;
	struct mmc_pm_ops *ops = &mmc_card_pm_ops;
	
    if (strcmp(name, "bcm40181_wakeup")) {
        bcm40181_msg("No gpio %s for BCM40181\n", name);
        return -1;
    }
	ret = gpio_read_one_pin_value(ops->pio_hdle, name);
	bcm40181_msg("Succeed to get gpio %s value: %d !\n", name, ret);

	return ret;
}

void bcm40181_wifi_gpio_init(void)
{
	struct mmc_pm_ops *ops = &mmc_card_pm_ops;

	bcm40181_msg("exec bcm40181_wifi_gpio_init...\n");
	bcm40181_powerup = 0;
	bcm40181_suspend = 0;
	ops->gpio_ctrl = bcm40181_gpio_ctrl;
	ops->get_io_val = bcm40181_get_io_value;
}
