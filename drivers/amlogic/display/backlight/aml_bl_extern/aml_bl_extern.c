#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/i2c-aml.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <mach/am_regs.h>
#include <mach/gpio.h>
#include <linux/amlogic/aml_bl_extern.h>

//#define BL_EXT_DEBUG_INFO
#ifdef BL_EXT_DEBUG_INFO
#define DBG_PRINT(...)		printk(__VA_ARGS__)
#else
#define DBG_PRINT(...)
#endif

static struct aml_bl_extern_driver_t bl_ext_driver = {
    .type = BL_EXTERN_MAX,
    .name = NULL,
    .power_on = NULL,
    .power_off = NULL,
    .set_level = NULL,

};

struct aml_bl_extern_driver_t* aml_bl_extern_get_driver(void)
{
    return &bl_ext_driver;
}

int bl_extern_driver_check(void)
{
    struct aml_bl_extern_driver_t* bl_ext;

    bl_ext = aml_bl_extern_get_driver();
    if (bl_ext) {
        if (bl_ext->type < BL_EXTERN_MAX) {
            printk("[warning]: bl_extern has already exist (%s)\n", bl_ext->name);
            return -1;
        }
    }
    else {
        printk("get bl_extern_driver failed\n");
    }
    
    return 0;
}

int get_bl_extern_dt_data(struct device_node* of_node, struct bl_extern_config_t *pdata)
{
    int ret;
    int val;
    unsigned int bl_para[2];
    const char *str;

    ret = of_property_read_string(of_node, "dev_name", (const char **)&pdata->name);
    if (ret) {
        pdata->name = "aml_bl_extern";
        printk("warning: get dev_name failed\n");
    }

    ret = of_property_read_u32(of_node, "type", &pdata->type);
    if (ret) {
        pdata->type = BL_EXTERN_MAX;
        printk("%s warning: get type failed, exit\n", pdata->name);
        return -1;
    }
    pdata->gpio_used = 0;
    pdata->gpio = GPIO_MAX;
    ret = of_property_read_string(of_node, "gpio_enable", &str);
    if (ret) {
        printk("%s warning: get gpio_enable failed\n", pdata->name);
    }
    else {
        if (strncmp(str, "G", 1) == 0) {//"GPIO_xx"
                pdata->gpio_used = 1;
                val = amlogic_gpio_name_map_num(str);
                ret = bl_extern_gpio_request(val);
                if (ret) {
                    printk("%s warning: faild to alloc gpio (%s)\n", pdata->name, str);
                }
                pdata->gpio = val;
        }
        DBG_PRINT("%s: gpio_enable %s\n", pdata->name, ((pdata->gpio_used) ? str:"none"));
    }
    switch (pdata->type) {
        case BL_EXTERN_I2C:
            ret = of_property_read_u32(of_node,"i2c_address",&pdata->i2c_addr);
            if (ret) {
                printk("%s warning: get i2c_address failed\n", pdata->name);
                pdata->i2c_addr = 0;
            }
            DBG_PRINT("%s: i2c_address=0x%02x\n", pdata->name, pdata->i2c_addr);
          
            ret = of_property_read_string(of_node, "i2c_bus", &str);
            if (ret) {
                printk("%s warning: get i2c_bus failed, use default i2c bus\n", pdata->name);
                pdata->i2c_bus = AML_I2C_MASTER_A;
            }
            else {
                if (strncmp(str, "i2c_bus_a", 9) == 0)
                    pdata->i2c_bus = AML_I2C_MASTER_A;
                else if (strncmp(str, "i2c_bus_b", 9) == 0)
                    pdata->i2c_bus = AML_I2C_MASTER_B;
                else if (strncmp(str, "i2c_bus_c", 9) == 0)
                    pdata->i2c_bus = AML_I2C_MASTER_C;
                else if (strncmp(str, "i2c_bus_d", 9) == 0)
                    pdata->i2c_bus = AML_I2C_MASTER_D;
                else if (strncmp(str, "i2c_bus_ao", 10) == 0)
                    pdata->i2c_bus = AML_I2C_MASTER_AO;
                else
                    pdata->i2c_bus = AML_I2C_MASTER_A; 
            }
            DBG_PRINT("%s: i2c_bus=%s[%d]\n", pdata->name, str, pdata->i2c_bus);
            break;
        case BL_EXTERN_SPI:
            ret = of_property_read_string(of_node,"gpio_spi_cs", &str);
            if (ret) {
                printk("%s warning: get spi gpio_spi_cs failed\n", pdata->name);
                pdata->spi_cs = -1;
            }
            else {
                val = amlogic_gpio_name_map_num(str);
                if (val > 0) {
                    ret = bl_extern_gpio_request(val);
                    if (ret) {
                        printk("faild to alloc spi_cs gpio (%s)!\n", str);
                    }
                    pdata->spi_cs = val;
                    DBG_PRINT("spi_cs gpio = %s(%d)\n", str, pdata->spi_cs);
                }
                else {
                    pdata->spi_cs = -1;
                }
            }
            ret = of_property_read_string(of_node,"gpio_spi_clk", &str);
            if (ret) {
                printk("%s warning: get spi gpio_spi_clk failed\n", pdata->name);
                pdata->spi_clk = -1;
            }
            else {
                val = amlogic_gpio_name_map_num(str);
                if (val > 0) {
                    ret = bl_extern_gpio_request(val);
                    if (ret) {
                        printk("%s: faild to alloc spi_clk gpio (%s)!\n", pdata->name, str);
                    }
                    pdata->spi_clk = val;
                    DBG_PRINT("%s: spi_clk gpio = %s(%d)\n", pdata->name, str, pdata->spi_clk);
                }
                else {
                    pdata->spi_clk = -1;
                }
            }
            ret = of_property_read_string(of_node,"gpio_spi_data", &str);
            if (ret) {
                printk("%s warning: get spi gpio_spi_data failed\n", pdata->name);
                pdata->spi_data = -1;
            }
            else {
                val = amlogic_gpio_name_map_num(str);
                if (val > 0) {
                    ret = bl_extern_gpio_request(val);
                    if (ret) {
                        printk("%s: faild to alloc spi_data gpio (%s)!\n", pdata->name, str);
                    }
                    pdata->spi_data = val;
                    DBG_PRINT("%s: spi_data gpio = %s(%d)\n", pdata->name, str, pdata->spi_data);
                }
                else {
                    pdata->spi_data = -1;
                }
            }
            break;
        case BL_EXTERN_OTHER:
            break;
        default:
            break;
    }
    ret = of_property_read_u32_array(of_node,"dim_max_min", &bl_para[0], 2);
    if(ret){
            printk("%s warning: get dim_max_min failed\n", pdata->name);
            pdata->dim_max = 0;
            pdata->dim_min = 0;
        }
        else {
            pdata->dim_max = bl_para[0];
            pdata->dim_min = bl_para[1];
        }
        DBG_PRINT("%s dim_min = %d, dim_max = %d\n", pdata->name, pdata->dim_min, pdata->dim_max);

    return 0;
}
