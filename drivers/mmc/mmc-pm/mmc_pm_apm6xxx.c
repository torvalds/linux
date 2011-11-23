
/*
 * USI wm-bn-bm-01-5(bcm4329) sdio wifi power management API
 * gpio define
 * apm_6981_vcc_en         = port:PA09<1><default><default><0>
 * apm_6981_vdd_en         = port:PA10<1><default><default><0>
 * apm_6981_wakeup         = port:PA11<1><default><default><0>
 * apm_6981_rst_n          = port:PA12<1><default><default><0>
 * apm_6981_pwd_n          = port:PA13<1><default><default><0>
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <mach/sys_config.h>

#include "mmc_pm.h"

#define apm_msg(...)    do {printk("[apm_wifi]: "__VA_ARGS__);} while(0)

static int apm_6xxx_gpio_ctrl(char* name, int level)
{
    struct mmc_pm_ops *ops = &mmc_card_pm_ops;
    char* gpio_cmd[5] = {"apm_6981_vcc_en", "apm_6981_vdd_en", "apm_6981_wakeup", 
                               "apm_6981_rst_n", "apm_6981_pwd_n"};
    int i = 0;
    int ret = 0;
    
    for (i=0; i<5; i++) {
        if (strcmp(name, gpio_cmd[i])==0)
            break;
    }
    if (i==5) {
        apm_msg("No gpio %s for APM 6XXX module\n", name);
        return -1;
    }
    
    ret = gpio_write_one_pin_value(ops->pio_hdle, level, name);
    if (ret) {
        apm_msg("Failed to set gpio %s to %d !\n", name, level);
        return -1;
    }
    apm_msg("Set gpio %s to %d !\n", name, level);
    return 0;
}

int apm_6xxx_get_gpio_value(char* name)
{
    return -1;
}

void apm_6xxx_gpio_init(void)
{
    struct mmc_pm_ops *ops = &mmc_card_pm_ops;
    apm_6xxx_gpio_ctrl("apm_6981_wakeup", 1);
    apm_6xxx_gpio_ctrl("apm_6981_pwd_n", 0);
    apm_6xxx_gpio_ctrl("apm_6981_rst_n", 0);
    ops->gpio_ctrl = apm_6xxx_gpio_ctrl;
    ops->get_io_val = apm_6xxx_get_gpio_value;
}