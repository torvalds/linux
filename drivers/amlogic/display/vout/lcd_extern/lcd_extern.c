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
#include <linux/amlogic/vout/aml_lcd_extern.h>

//#define LCD_EXT_DEBUG_INFO
#ifdef LCD_EXT_DEBUG_INFO
#define DBG_PRINT(...)		printk(__VA_ARGS__)
#else
#define DBG_PRINT(...)
#endif

static struct aml_lcd_extern_driver_t lcd_ext_driver = {
	.type = LCD_EXTERN_MAX,
	.name = NULL,
	.reg_read = NULL,
	.reg_write = NULL,
	.power_on = NULL,
	.power_off = NULL,
	.init_on_cmd_8 = NULL,
	.init_off_cmd_8 = NULL,
};

struct aml_lcd_extern_driver_t* aml_lcd_extern_get_driver(void)
{
    return &lcd_ext_driver;
}

int lcd_extern_driver_check(void)
{
	struct aml_lcd_extern_driver_t* lcd_ext;

	lcd_ext = aml_lcd_extern_get_driver();
	if (lcd_ext) {
		if (lcd_ext->type < LCD_EXTERN_MAX) {
			printk("[warning]: lcd_extern has already exist (%s)\n", lcd_ext->name);
			return -1;
		}
	}
	else {
		printk("get lcd_extern_driver failed\n");
	}
	
	return 0;
}

#define BL_EXT_NAME_LEN_MAX		50
int get_lcd_extern_dt_data(struct device_node* of_node, struct lcd_extern_config_t *pdata)
{
	int err;
	int val;
	const char *str;
	
	err = of_property_read_string(of_node, "dev_name", &str);
	if (err) {
		str = "aml_lcd_extern";
		printk("warning: get dev_name failed\n");
	}
	pdata->name = (char *)kmalloc(sizeof(char)*BL_EXT_NAME_LEN_MAX, GFP_KERNEL);
	if (pdata->name == NULL) {
		printk("[get_lcd_extern_dt_data]: Not enough memory\n");
	}
	else {
		memset(pdata->name, 0, BL_EXT_NAME_LEN_MAX);
		strcpy(pdata->name, str);
		printk("load bl_extern in dtb: %s\n", pdata->name);
	}
	err = of_property_read_u32(of_node, "type", &pdata->type);
	if (err) {
		pdata->type = LCD_EXTERN_MAX;
		printk("warning: get type failed, exit\n");
		return -1;
	}
	switch (pdata->type) {
		case LCD_EXTERN_I2C:
			err = of_property_read_u32(of_node,"i2c_address",&pdata->i2c_addr);
			if (err) {
				printk("%s warning: get i2c_address failed\n", pdata->name);
				pdata->i2c_addr = 0;
			}
			DBG_PRINT("%s: i2c_address=0x%02x\n", pdata->name, pdata->i2c_addr);
		  
			err = of_property_read_string(of_node, "i2c_bus", &str);
			if (err) {
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
		case LCD_EXTERN_SPI:
			err = of_property_read_string(of_node,"gpio_spi_cs", &str);
			if (err) {
				printk("%s warning: get spi gpio_spi_cs failed\n", pdata->name);
				pdata->spi_cs = -1;
			}
			else {
			    val = amlogic_gpio_name_map_num(str);
				if (val > 0) {
					err = lcd_extern_gpio_request(val);
					if (err) {
					  printk("faild to alloc spi_cs gpio (%s)!\n", str);
					}
					pdata->spi_cs = val;
					DBG_PRINT("spi_cs gpio = %s(%d)\n", str, pdata->spi_cs);
				}
				else {
					pdata->spi_cs = -1;
				}
			}
			err = of_property_read_string(of_node,"gpio_spi_clk", &str);
			if (err) {
				printk("%s warning: get spi gpio_spi_clk failed\n", pdata->name);
				pdata->spi_clk = -1;
			}
			else {
			    val = amlogic_gpio_name_map_num(str);
				if (val > 0) {
					err = lcd_extern_gpio_request(val);
					if (err) {
					  printk("faild to alloc spi_clk gpio (%s)!\n", str);
					}
					pdata->spi_clk = val;
					DBG_PRINT("spi_clk gpio = %s(%d)\n", str, pdata->spi_clk);
				}
				else {
					pdata->spi_clk = -1;
				}
			}
			err = of_property_read_string(of_node,"gpio_spi_data", &str);
			if (err) {
				printk("%s warning: get spi gpio_spi_data failed\n", pdata->name);
				pdata->spi_data = -1;
			}
			else {
			    val = amlogic_gpio_name_map_num(str);
				if (val > 0) {
					err = lcd_extern_gpio_request(val);
					if (err) {
					  printk("faild to alloc spi_data gpio (%s)!\n", str);
					}
					pdata->spi_data = val;
					DBG_PRINT("spi_data gpio = %s(%d)\n", str, pdata->spi_data);
				}
				else {
					pdata->spi_data = -1;
				}
			}
			break;
		case LCD_EXTERN_MIPI:
			break;
		default:
			break;
	}
	
	return 0;
}

int remove_lcd_extern(struct lcd_extern_config_t *pdata)
{
	if (pdata->name)
		kfree(pdata->name);
		
	return 0;
}

