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
#include <linux/i2c.h>
#include <linux/i2c-aml.h>
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
#include <linux/amlogic/aml_bl_extern.h>
#ifdef CONFIG_AMLOGIC_BOARD_HAS_PMU
#include <linux/amlogic/aml_pmu_common.h>
#endif

static struct bl_extern_config_t *bl_ext_config = NULL;

//#define BL_EXT_DEBUG_INFO
#ifdef BL_EXT_DEBUG_INFO
#define DBG_PRINT(...)		printk(__VA_ARGS__)
#else
#define DBG_PRINT(...)
#endif

#define BL_EXTERN_NAME			"bl_pmu_aml1218"

static int bl_extern_set_level(unsigned int level)
{
#ifdef CONFIG_AMLOGIC_BOARD_HAS_PMU
    struct aml_pmu_driver *pmu_driver;
    unsigned char temp;
#endif
    int ret = 0;

    if (bl_ext_config == NULL) {
        printk("no %s driver\n", BL_EXTERN_NAME);
        return -1;
    }
    get_bl_ext_level(bl_ext_config);
    level = bl_ext_config->dim_min - ((level - bl_ext_config->level_min) * (bl_ext_config->dim_min - bl_ext_config->dim_max)) / (bl_ext_config->level_max - bl_ext_config->level_min);
    level &= 0x1f;

#ifdef CONFIG_AMLOGIC_BOARD_HAS_PMU
    pmu_driver = aml_pmu_get_driver();
    if (pmu_driver == NULL) {
        printk("no pmu driver\n");
        return -1;
    }
    else {
        if ((pmu_driver->pmu_reg_write) && (pmu_driver->pmu_reg_read)) {
            ret = pmu_driver->pmu_reg_read(0x005f, &temp);
            temp &= ~(0x3f << 2);
            temp |= (level << 2);
            ret = pmu_driver->pmu_reg_write(0x005f, temp);
        }
        else {
            printk("no pmu_reg_read/write\n");
            return -1;
        }
    }
#endif
    return ret;
}

static int bl_extern_power_on(void)
{
#ifdef CONFIG_AMLOGIC_BOARD_HAS_PMU
    struct aml_pmu_driver *pmu_driver;
    unsigned char temp;
#endif
    int ret = 0;

#ifdef CONFIG_AMLOGIC_BOARD_HAS_PMU
    pmu_driver = aml_pmu_get_driver();
    if (pmu_driver == NULL) {
        printk("no pmu driver\n");
        return -1;
    }
    else {
        if ((pmu_driver->pmu_reg_write) && (pmu_driver->pmu_reg_read)) {
            ret = pmu_driver->pmu_reg_read(0x005e, &temp);
            temp |= (1 << 7);
            ret = pmu_driver->pmu_reg_write(0x005e, temp);//DCEXT_IREF_ADJLV2_EN
        }
        else {
            printk("no pmu_reg_read/write\n");
            return -1;
        }
    }
#endif
    if (bl_ext_config->gpio_used > 0) {
        bl_extern_gpio_direction_output(bl_ext_config->gpio, 1);
    }

    printk("%s\n", __FUNCTION__);
    return ret;
}

static int bl_extern_power_off(void)
{
#ifdef CONFIG_AMLOGIC_BOARD_HAS_PMU
    struct aml_pmu_driver *pmu_driver;
    unsigned char temp;
#endif
    int ret = 0;

    if (bl_ext_config->gpio_used > 0) {
        bl_extern_gpio_direction_output(bl_ext_config->gpio, 0);
    }
#ifdef CONFIG_AMLOGIC_BOARD_HAS_PMU
    pmu_driver = aml_pmu_get_driver();
    if (pmu_driver == NULL) {
        printk("no pmu driver\n");
        return -1;
    }
    else {
        if ((pmu_driver->pmu_reg_write) && (pmu_driver->pmu_reg_read)) {
            ret = pmu_driver->pmu_reg_read(0x005e, &temp);
            temp &= ~(1 << 7);
            ret = pmu_driver->pmu_reg_write(0x005e, temp);//DCEXT_IREF_ADJLV2_EN
        }
        else {
            printk("no pmu_reg_read/write\n");
            return -1;
        }
    }
#endif

    printk("%s\n", __FUNCTION__);
    return ret;
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

//***********************************************//
static ssize_t bl_extern_debug(struct class *class, struct class_attribute *attr, const char *buf, size_t count)
{
#ifdef CONFIG_AMLOGIC_BOARD_HAS_PMU
    struct aml_pmu_driver *pmu_driver;
    unsigned char temp;
    unsigned int t[2];
#endif
    int ret = 0;

#ifdef CONFIG_AMLOGIC_BOARD_HAS_PMU
    pmu_driver = aml_pmu_get_driver();
    if (pmu_driver == NULL) {
        printk("no pmu driver\n");
        return -EINVAL;
    }

    switch (buf[0]) {
        case 'r': //read
            ret = sscanf(buf, "r %x", &t[0]);
            if ((pmu_driver->pmu_reg_write) && (pmu_driver->pmu_reg_read)) {
                ret = pmu_driver->pmu_reg_read(t[0], &temp);
                printk("read pmu reg: 0x%x=0x%x\n", t[0], temp);
            }
            break;
        case 'w': //write
            ret = sscanf(buf, "w %x %x", &t[0], &t[1]);
            if ((pmu_driver->pmu_reg_write) && (pmu_driver->pmu_reg_read)) {
                ret = pmu_driver->pmu_reg_write(t[0], t[1]);
                ret = pmu_driver->pmu_reg_read(t[0], &temp);
                printk("write pmu reg 0x%x: 0x%x, readback: 0x%x\n", t[0], t[1], temp);
            }
            break;
        default:
            printk("wrong format of command.\n");
            break;
    }
#endif
    if (ret != 1 || ret !=2)
        return -EINVAL;

    return count;
    //return 0;
}

static struct class_attribute bl_extern_debug_class_attrs[] = {
    __ATTR(debug, S_IRUGO | S_IWUSR, NULL, bl_extern_debug),
    __ATTR_NULL
};

static struct class bl_extern_debug_class = {
    .name = "bl_ext",
    .class_attrs = bl_extern_debug_class_attrs,
};
//*********************************************************//

static int aml_aml1218_probe(struct platform_device *pdev)
{
    int ret = 0;

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

    ret = class_register(&bl_extern_debug_class);
    if(ret){
        printk("class register bl_extern_debug_class fail!\n");
    }

    printk("%s ok\n", __FUNCTION__);
    return ret;

bl_extern_probe_failed:
    if (bl_ext_config)
        kfree(bl_ext_config);
    return -1;
}

static int aml_aml1218_remove(struct platform_device *pdev)
{
    if (pdev->dev.platform_data)
        kfree (pdev->dev.platform_data);
    return 0;
}

#ifdef CONFIG_USE_OF
static const struct of_device_id aml_aml1218_dt_match[]={
    {
        .compatible = "amlogic,bl_pmu_aml1218",
    },
    {},
};
#else
#define aml_aml1218_dt_match NULL
#endif

static struct platform_driver aml_aml1218_driver = {
    .probe  = aml_aml1218_probe,
    .remove = aml_aml1218_remove,
    .driver = {
        .name  = BL_EXTERN_NAME,
        .owner = THIS_MODULE,
#ifdef CONFIG_USE_OF
        .of_match_table = aml_aml1218_dt_match,
#endif
    },
};

static int __init aml_aml1218_init(void)
{
    int ret;
    DBG_PRINT("%s\n", __FUNCTION__);

    ret = platform_driver_register(&aml_aml1218_driver);
    if (ret) {
        printk("[error] %s failed to register bl extern driver module\n", __FUNCTION__);
        return -ENODEV;
    }
    return ret;
}

static void __exit aml_aml1218_exit(void)
{
    platform_driver_unregister(&aml_aml1218_driver);
}

rootfs_initcall(aml_aml1218_init);
module_exit(aml_aml1218_exit);

MODULE_AUTHOR("AMLOGIC");
MODULE_DESCRIPTION("BL Extern driver for aml1218");
MODULE_LICENSE("GPL");
