/*
 * AMLOGIC lcd external driver.
 *
 * Communication protocol:
 * SPI 
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
#include <linux/amlogic/vout/aml_lcd_extern.h>

static struct lcd_extern_config_t *lcd_ext_config = NULL;

//#define LCD_EXT_DEBUG_INFO
#ifdef LCD_EXT_DEBUG_INFO
#define DBG_PRINT(...)		printk(__VA_ARGS__)
#else
#define DBG_PRINT(...)
#endif

#define LCD_EXTERN_NAME			"lcd_spi_LD070WS2"

#define SPI_DELAY		30 //unit: us

static unsigned char spi_init_table[][2] = {
    {0x00,0x21},  //reset
    {0x00,0xa5},  //standby
    {0x01,0x30},  //enable FRC/Dither
    {0x02,0x40},  //enable normally black
    {0x0e,0x5f},  //enable test mode1
    {0x0f,0xa4},  //enable test mode2
    {0x0d,0x00},  //enable SDRRS, enlarge OE width
    {0x02,0x43},  //adjust charge sharing time
    {0x0a,0x28},  //trigger bias reduction
    {0x10,0x41},  //adopt 2 line/1 dot
    {0xff,50},    //delay 50ms
    {0x00,0xad},  //display on
    {0xff,0xff},  //ending flag
};

static unsigned char spi_off_table[][2] = {
    {0x00,0xa5},  //standby
    {0xff,0xff},
};

static void set_lcd_csb(unsigned v)
{
    lcd_extern_gpio_direction_output(lcd_ext_config->spi_cs, v);
    udelay(SPI_DELAY);
}

static void set_lcd_scl(unsigned v)
{
    lcd_extern_gpio_direction_output(lcd_ext_config->spi_clk, v);
    udelay(SPI_DELAY);
}
    
static void set_lcd_sda(unsigned v)
{
    lcd_extern_gpio_direction_output(lcd_ext_config->spi_data, v);
    udelay(SPI_DELAY);
}

static void spi_gpio_init(void)
{
    set_lcd_csb(1);
    set_lcd_scl(1);
    set_lcd_sda(1);
}

static void spi_gpio_off(void)
{
    set_lcd_sda(0);
    set_lcd_scl(0);
    set_lcd_csb(0);
}

static void spi_write_8(unsigned char addr, unsigned char data)
{
    int i;
    unsigned int sdata;

    sdata = (unsigned int)(addr & 0x3f);
    sdata <<= 10;
    sdata |= (data & 0xff);
    sdata &= ~(1<<9); //write flag

    set_lcd_csb(1);
    set_lcd_scl(1);
    set_lcd_sda(1);

    set_lcd_csb(0);
    for (i = 0; i < 16; i++) {
        set_lcd_scl(0);
        if (sdata & 0x8000)
            set_lcd_sda(1);
        else
            set_lcd_sda(0);
        sdata <<= 1;
        set_lcd_scl(1);
    }

    set_lcd_csb(1);
    set_lcd_scl(1);
    set_lcd_sda(1);
    udelay(SPI_DELAY);
}

static int lcd_extern_spi_init(void)
{
    int ending_flag = 0;
    int i=0;

    spi_gpio_init();

    while(ending_flag == 0) {
        if (spi_init_table[i][0] == 0xff) {
            if (spi_init_table[i][1] == 0xff)
                ending_flag = 1;
            else
                mdelay(spi_init_table[i][1]);
        }
        else {
            spi_write_8(spi_init_table[i][0], spi_init_table[i][1]);
        }
        i++;
    }
    printk("%s\n", __FUNCTION__);

    return 0;
}

static int lcd_extern_spi_off(void)
{
    int ending_flag = 0;
    int i=0;

    spi_gpio_init();

    while(ending_flag == 0) {
        if (spi_off_table[i][0] == 0xff) {
            if (spi_off_table[i][1] == 0xff)
                ending_flag = 1;
            else
                mdelay(spi_off_table[i][1]);
        }
        else {
            spi_write_8(spi_off_table[i][0], spi_off_table[i][1]);
        }
        i++;
    }
    printk("%s\n", __FUNCTION__);
    mdelay(10);
    spi_gpio_off();

    return 0;
}

static int lcd_extern_driver_update(void)
{
    struct aml_lcd_extern_driver_t* lcd_ext;

    lcd_ext = aml_lcd_extern_get_driver();
    if (lcd_ext) {
        lcd_ext->type       = lcd_ext_config->type;
        lcd_ext->name       = lcd_ext_config->name;
        lcd_ext->power_on   = lcd_extern_spi_init;
        lcd_ext->power_off  = lcd_extern_spi_off;
    }
    else {
        printk("[error] %s get lcd_extern_driver failed\n", lcd_ext_config->name);
    }

    return 0;
}

static int aml_LD070WS2_probe(struct platform_device *pdev)
{
    //int i = 0;

    if (lcd_extern_driver_check()) {
        return -1;
    }
    if (lcd_ext_config == NULL)
        lcd_ext_config = kzalloc(sizeof(*lcd_ext_config), GFP_KERNEL);
    if (lcd_ext_config == NULL) {
        printk("[error] %s probe: failed to alloc data\n", LCD_EXTERN_NAME);
        return -1;
    }

    pdev->dev.platform_data = lcd_ext_config;

    if (get_lcd_extern_dt_data(pdev->dev.of_node, lcd_ext_config) != 0) {
        printk("[error] %s probe: failed to get dt data\n", LCD_EXTERN_NAME);
        goto lcd_extern_probe_failed;
    }
    lcd_extern_driver_update();

    printk("%s probe ok\n", LCD_EXTERN_NAME);
    return 0;

lcd_extern_probe_failed:
    if (lcd_ext_config)
        kfree(lcd_ext_config);
    return -1;
}

static int aml_LD070WS2_remove(struct platform_device *pdev)
{
    remove_lcd_extern(lcd_ext_config);
    if (pdev->dev.platform_data)
        kfree (pdev->dev.platform_data);
    return 0;
}

#ifdef CONFIG_USE_OF
static const struct of_device_id aml_LD070WS2_dt_match[]={
    {
        .compatible = "amlogic,lcd_spi_LD070WS2",
    },
    {},
};
#else
#define aml_LD070WS2_dt_match NULL
#endif

static struct platform_driver aml_LD070WS2_driver = {
    .probe  = aml_LD070WS2_probe,
    .remove = aml_LD070WS2_remove,
    .driver = {
        .name  = LCD_EXTERN_NAME,
        .owner = THIS_MODULE,
#ifdef CONFIG_USE_OF
        .of_match_table = aml_LD070WS2_dt_match,
#endif
    },
};

static int __init aml_LD070WS2_init(void)
{
    int ret;
    DBG_PRINT("%s\n", __FUNCTION__);

    ret = platform_driver_register(&aml_LD070WS2_driver);
    if (ret) {
        printk("[error] %s failed to register lcd extern driver module\n", __FUNCTION__);
        return -ENODEV;
    }
    return ret;
}

static void __exit aml_LD070WS2_exit(void)
{
    platform_driver_unregister(&aml_LD070WS2_driver);
}

//late_initcall(aml_LD070WS2_init);
module_init(aml_LD070WS2_init);
module_exit(aml_LD070WS2_exit);

MODULE_AUTHOR("AMLOGIC");
MODULE_DESCRIPTION("LCD Extern driver for LD070WS2");
MODULE_LICENSE("GPL");
