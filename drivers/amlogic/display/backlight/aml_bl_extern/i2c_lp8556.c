/*
 * AMLOGIC backlight external driver.
 *
 * Communication protocol:
 * I2C 
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

static struct bl_extern_config_t *bl_ext_config = NULL;

static struct i2c_client *aml_lp8556_i2c_client;

//#define BL_EXT_DEBUG_INFO
#ifdef BL_EXT_DEBUG_INFO
#define DBG_PRINT(...)		printk(__VA_ARGS__)
#else
#define DBG_PRINT(...)
#endif

#define BL_EXTERN_NAME			"bl_i2c_lp8556"
static unsigned int bl_status = 1;
static unsigned int bl_level = 0;

static unsigned char i2c_init_table[][2] = {
    {0xa1, 0x76}, //hight bit(8~11)(0~0X66e set backlight)
    {0xa0, 0x66},  //low bit(0~7)  20mA
    {0x16, 0x1F}, // 5channel LED enable 0x1F
    {0xa9, 0xA0}, //VBOOST_MAX 25V
    {0x9e, 0x12},
    {0xa2, 0x23}, //23
    {0x01, 0x05}, //0x03 pwm+I2c set brightness,0x5 I2c set brightness
    {0xff, 0xff},//ending flag
};

static int aml_i2c_write(struct i2c_client *i2client,unsigned char *buff, unsigned len)
{
    int res = 0;
    struct i2c_msg msg[] = {
        {
        .addr = i2client->addr,
        .flags = 0,
        .len = len,
        .buf = buff,
        }
    };
    
    res = i2c_transfer(i2client->adapter, msg, 1);
    if (res < 0) {
        printk("%s: i2c transfer failed [addr 0x%02x]\n", __FUNCTION__, i2client->addr);
    }
    
    return res;
}
#if 0
static int aml_i2c_read(struct i2c_client *i2client,unsigned char *buff, unsigned len)
{
    int res = 0;
    struct i2c_msg msgs[] = {
        {
            .addr = i2client->addr,
            .flags = 0,
            .len = 1,
            .buf = buff,
        },
        {
            .addr = i2client->addr,
            .flags = I2C_M_RD,
            .len = len,
            .buf = buff,
        }
    };
    res = i2c_transfer(i2client->adapter, msgs, 2);
    if (res < 0) {
        printk("%s: i2c transfer failed [addr 0x%02x]\n", __FUNCTION__, i2client->addr);
    }

    return res;
}
#endif
static int bl_extern_set_level(unsigned int level)
{
    unsigned char tData[3];
    int ret = 0;

    bl_level = level;

    if (bl_ext_config == NULL) {
        printk("no %s driver\n", BL_EXTERN_NAME);
        return -1;
    }
    get_bl_ext_level(bl_ext_config);
    level = bl_ext_config->dim_min - ((level - bl_ext_config->level_min) * (bl_ext_config->dim_min - bl_ext_config->dim_max)) / (bl_ext_config->level_max - bl_ext_config->level_min);
    level &= 0xff;

    if (bl_status) {
        tData[0] = 0x0;
        tData[1] = level;
        ret = aml_i2c_write(aml_lp8556_i2c_client, tData, 2);
    }

    return ret;
}

static int bl_extern_power_on(void)
{
    unsigned char tData[3];
    int i=0, ending_flag=0;
    int ret=0;

    if (bl_ext_config->gpio_used > 0) {
        bl_extern_gpio_direction_output(bl_ext_config->gpio, 1);
    }

    while (ending_flag == 0) {
        if (i2c_init_table[i][0] == 0xff) {    //special mark
            if (i2c_init_table[i][1] == 0xff) { //ending flag
                ending_flag = 1;
            }
            else { //delay flag
                mdelay(i2c_init_table[i][1]);
            }
        }
        else {
            tData[0]=i2c_init_table[i][0];
            tData[1]=i2c_init_table[i][1];
            ret = aml_i2c_write(aml_lp8556_i2c_client, tData, 2);
        }
        i++;
    }
    bl_status = 1;
    bl_extern_set_level(bl_level);//recover bl level

    printk("%s\n", __FUNCTION__);
    return ret;
}

static int bl_extern_power_off(void)
{
    bl_status = 0;
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

static int aml_lp8556_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id)
{

    if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
        printk("[error] %s: functionality check failed\n", __FUNCTION__);
        return -ENODEV;
    }
    else {
        aml_lp8556_i2c_client = client;
        bl_extern_driver_update();
    }

    printk("%s OK\n", __FUNCTION__);
    return 0;
}

static int aml_lp8556_i2c_remove(struct i2c_client *client)
{
    return 0;
}

static const struct i2c_device_id aml_lp8556_i2c_id[] = {
    {BL_EXTERN_NAME, 0},
    { }
};
// MODULE_DEVICE_TABLE(i2c, aml_lp8556_id);

static struct i2c_driver aml_lp8556_i2c_driver = {
    .probe    = aml_lp8556_i2c_probe,
    .remove   = aml_lp8556_i2c_remove,
    .id_table = aml_lp8556_i2c_id,
    .driver = {
        .name = BL_EXTERN_NAME,
        .owner =THIS_MODULE,
    },
};

static int aml_lp8556_probe(struct platform_device *pdev)
{
    struct i2c_board_info i2c_info;
    struct i2c_adapter *adapter;
    struct i2c_client *i2c_client;
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

    memset(&i2c_info, 0, sizeof(i2c_info));

    adapter = i2c_get_adapter(bl_ext_config->i2c_bus);
    if (!adapter) {
        printk("[error] %s£ºfailed to get i2c adapter\n", BL_EXTERN_NAME);
        goto bl_extern_probe_failed;
    }

    strncpy(i2c_info.type, bl_ext_config->name, I2C_NAME_SIZE);
    i2c_info.addr = bl_ext_config->i2c_addr;
    i2c_info.platform_data = bl_ext_config;
    i2c_info.flags=0;
    if(i2c_info.addr>0x7f)
        i2c_info.flags=0x10;
    i2c_client = i2c_new_device(adapter, &i2c_info);
    if (!i2c_client) {
        printk("[error] %s :failed to new i2c device\n", BL_EXTERN_NAME);
        goto bl_extern_probe_failed;
    }
    else{
        DBG_PRINT("[error] %s: new i2c device succeed\n",((struct bl_extern_data_t *)(i2c_client->dev.platform_data))->name);
    }

    if (!aml_lp8556_i2c_client) {
        ret = i2c_add_driver(&aml_lp8556_i2c_driver);
        if (ret) {
            printk("[error] %s probe: add i2c_driver failed\n", BL_EXTERN_NAME);
            goto bl_extern_probe_failed;
        }
    }

    printk("%s ok\n", __FUNCTION__);
    return ret;

bl_extern_probe_failed:
    if (bl_ext_config)
        kfree(bl_ext_config);
    return -1;
}

static int aml_lp8556_remove(struct platform_device *pdev)
{
    if (pdev->dev.platform_data)
        kfree (pdev->dev.platform_data);
    return 0;
}

#ifdef CONFIG_USE_OF
static const struct of_device_id aml_lp8556_dt_match[]={
    {
        .compatible = "amlogic,bl_i2c_lp8556",
    },
    {},
};
#else
#define aml_lp8556_dt_match NULL
#endif

static struct platform_driver aml_lp8556_driver = {
    .probe  = aml_lp8556_probe,
    .remove = aml_lp8556_remove,
    .driver = {
        .name  = BL_EXTERN_NAME,
        .owner = THIS_MODULE,
#ifdef CONFIG_USE_OF
        .of_match_table = aml_lp8556_dt_match,
#endif
    },
};

static int __init aml_lp8556_init(void)
{
    int ret;
    DBG_PRINT("%s\n", __FUNCTION__);

    ret = platform_driver_register(&aml_lp8556_driver);
    if (ret) {
        printk("[error] %s failed to register bl extern driver module\n", __FUNCTION__);
        return -ENODEV;
    }
    return ret;
}

static void __exit aml_lp8556_exit(void)
{
    platform_driver_unregister(&aml_lp8556_driver);
}

//late_initcall(aml_lp8556_init);
module_init(aml_lp8556_init);
module_exit(aml_lp8556_exit);

MODULE_AUTHOR("AMLOGIC");
MODULE_DESCRIPTION("BL Extern driver for LP8556");
MODULE_LICENSE("GPL");
