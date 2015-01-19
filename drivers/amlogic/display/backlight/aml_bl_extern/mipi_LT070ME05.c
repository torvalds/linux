/*
 * AMLOGIC backlight external driver.
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/jiffies.h> 
#include <linux/miscdevice.h>
#include <linux/mutex.h>
#include <linux/mm.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/delay.h>
#include <linux/sysctl.h>
#include <asm/uaccess.h>
#include <mach/pinmux.h>
#include <mach/gpio.h>
#include <mach/lcdoutc.h>
#include <linux/amlogic/aml_bl_extern.h>

#ifdef CONFIG_LCD_IF_MIPI_VALID
static struct bl_extern_config_t *bl_ext_config = NULL;

//#define BL_EXT_DEBUG_INFO
#ifdef BL_EXT_DEBUG_INFO
#define DBG_PRINT(...)		printk(__VA_ARGS__)
#else
#define DBG_PRINT(...)
#endif

#define BL_EXTERN_NAME			"bl_mipi_LT070ME05"
static unsigned int bl_status = 1;
static unsigned int bl_level = 0;

static int bl_extern_set_level(unsigned int level)
{
    unsigned char payload[]={0x15,0x51,1,0xe6,0xff,0xff};

    bl_level = level;

    if (bl_ext_config == NULL) {
        printk("no %s driver\n", BL_EXTERN_NAME);
        return -1;
    }
    get_bl_ext_level(bl_ext_config);
    level = bl_ext_config->dim_min - ((level - bl_ext_config->level_min) * (bl_ext_config->dim_min - bl_ext_config->dim_max)) / (bl_ext_config->level_max - bl_ext_config->level_min);
    level &= 0xff;

    if (bl_status) {
        payload[3] = level;
        dsi_write_cmd(payload);
    }

    return 0;
}

static int bl_extern_power_on(void)
{
    if (bl_ext_config->gpio_used > 0) {
        bl_extern_gpio_direction_output(bl_ext_config->gpio, 1);
    }

    bl_status = 1;
    bl_extern_set_level(bl_level);//recover bl level

    printk("%s\n", __FUNCTION__);
    return 0;
}

static int bl_extern_power_off(void)
{
    if (bl_ext_config->gpio_used > 0) {
        bl_extern_gpio_direction_output(bl_ext_config->gpio, 0);
    }

    printk("%s\n", __FUNCTION__);
    return 0;
}

static int bl_extern_driver_update(void)
{
    struct aml_bl_extern_driver_t* bl_ext;

    bl_ext = aml_bl_extern_get_driver();
    if (bl_ext) {
        bl_ext->type      = bl_ext_config->type;
        bl_ext->name      = bl_ext_config->name;
        bl_ext->power_on  = bl_extern_power_on;
        bl_ext->power_off = bl_extern_power_off;
        bl_ext->set_level = bl_extern_set_level;
    }
    else {
        printk("[error] %s get bl_extern_driver failed\n", bl_ext_config->name);
    }

    return 0;
}

static int aml_LT070ME05_probe(struct platform_device *pdev)
{
    if (bl_extern_driver_check()) {
        return -1;
    }
    if (bl_ext_config == NULL)
        bl_ext_config = kzalloc(sizeof(*bl_ext_config), GFP_KERNEL);
    if (bl_ext_config == NULL) {
        printk("[error] %s probe: failed to alloc data\n", BL_EXTERN_NAME);
        return -1;
    }

    pdev->dev.platform_data = bl_ext_config;

    if (get_bl_extern_dt_data(pdev->dev.of_node, bl_ext_config) != 0) {
        printk("[error] %s probe: failed to get dt data\n", BL_EXTERN_NAME);
        goto bl_extern_probe_failed;
    }

    bl_extern_driver_update();

    printk("%s ok\n", __FUNCTION__);
    return 0;

bl_extern_probe_failed:
    if (bl_ext_config)
        kfree(bl_ext_config);
    return -1;
}

static int aml_LT070ME05_remove(struct platform_device *pdev)
{
    if (pdev->dev.platform_data)
        kfree (pdev->dev.platform_data);
    return 0;
}

#ifdef CONFIG_USE_OF
static const struct of_device_id aml_LT070ME05_dt_match[]={
    {
        .compatible = "amlogic,bl_mipi_LT070ME05",
    },
    {},
};
#else
#define aml_LT070ME05_dt_match NULL
#endif

static struct platform_driver aml_LT070ME05_driver = {
    .probe  = aml_LT070ME05_probe,
    .remove = aml_LT070ME05_remove,
    .driver = {
        .name  = BL_EXTERN_NAME,
        .owner = THIS_MODULE,
#ifdef CONFIG_USE_OF
        .of_match_table = aml_LT070ME05_dt_match,
#endif
    },
};

static int __init aml_LT070ME05_init(void)
{
    int ret;
    DBG_PRINT("%s\n", __FUNCTION__);

    ret = platform_driver_register(&aml_LT070ME05_driver);
    if (ret) {
        printk("[error] %s failed to register bl extern driver module\n", __FUNCTION__);
        return -ENODEV;
    }
    return ret;
}

static void __exit aml_LT070ME05_exit(void)
{
    platform_driver_unregister(&aml_LT070ME05_driver);
}

rootfs_initcall(aml_LT070ME05_init);
module_exit(aml_LT070ME05_exit);

MODULE_AUTHOR("AMLOGIC");
MODULE_DESCRIPTION("BL Extern driver for LT070ME05");
MODULE_LICENSE("GPL");

#endif
