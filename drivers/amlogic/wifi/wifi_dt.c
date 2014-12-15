#include <linux/amlogic/wifi_dt.h>

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/pinctrl/consumer.h>
#include <linux/amlogic/aml_gpio_consumer.h>
#include <plat/io.h>
#include <mach/io.h>
#include <mach/register.h>

#define OWNER_NAME "sdio_wifi"


int wifi_power_gpio = 0;
int wifi_power_gpio2 = 0;

struct wifi_plat_info {
	int interrupt_pin;
	int irq_num;
	int irq_trigger_type;

	int power_on_pin;
	int power_on_pin_level;
	int power_on_pin2;

	int clock_32k_pin;

	int plat_info_valid;
};

static struct wifi_plat_info wifi_info;

#ifdef CONFIG_OF
static const struct of_device_id wifi_match[]={
	{	.compatible = "amlogic,aml_broadcm_wifi",
		.data		= (void *)&wifi_info
	},
	{},
};

static struct wifi_plat_info * wifi_get_driver_data(struct platform_device *pdev)
{
	const struct of_device_id *match;
	match = of_match_node(wifi_match, pdev->dev.of_node);
	return (struct wifi_plat_info *)match->data;
}
#else
#define wifi_match NULL
#endif

#define CHECK_PROP(ret, msg, value)	\
{	\
	if (ret) { \
		printk("wifi_dt : no prop for %s\n", msg); 	\
		return -1;	\
	} else {	\
		printk("wifi_dt : %s=%s\n", msg, value);	\
	}	\
}	\

#define CHECK_RET(ret) \
	if (ret) \
		printk("wifi_dt : gpio op failed(%d) at line %d\n", ret, __LINE__)

extern const char * amlogic_cat_gpio_owner(unsigned int pin);

#define SHOW_PIN_OWN(pin_str, pin_num) 	\
	printk("%s(%d) : %s\n", pin_str, pin_num, amlogic_cat_gpio_owner(pin_num))

static int wifi_dev_probe(struct platform_device *pdev)
{
    int ret;
	
#ifdef CONFIG_OF
	struct wifi_plat_info *plat;
	const char *value;
#else
    struct wifi_plat_info *plat = (struct wifi_plat_info *)(pdev->dev.platform_data);
#endif

    printk("wifi_dev_probe\n");

#ifdef CONFIG_OF
	if (pdev->dev.of_node) {
		plat = wifi_get_driver_data(pdev);
		plat->plat_info_valid = 0;
		
		ret = of_property_read_string(pdev->dev.of_node, "interrupt_pin", &value);
		CHECK_PROP(ret, "interrupt_pin", value);
		plat->interrupt_pin = amlogic_gpio_name_map_num(value);
		
		ret = of_property_read_u32(pdev->dev.of_node, "irq_num", &plat->irq_num);
		CHECK_PROP(ret, "irq_num", "null");

		ret = of_property_read_string(pdev->dev.of_node, "irq_trigger_type", &value);
		CHECK_PROP(ret, "irq_trigger_type", value);
		if (strcmp(value, "GPIO_IRQ_HIGH") == 0)
			plat->irq_trigger_type = GPIO_IRQ_HIGH;
		else if (strcmp(value, "GPIO_IRQ_LOW") == 0)
			plat->irq_trigger_type = GPIO_IRQ_LOW;
		else if (strcmp(value, "GPIO_IRQ_RISING") == 0)
			plat->irq_trigger_type = GPIO_IRQ_RISING;
		else if (strcmp(value, "GPIO_IRQ_FALLING") == 0)
			plat->irq_trigger_type = GPIO_IRQ_FALLING;
		else {
			printk("wifi_dt : unknown irq trigger type - %s\n", value);
			return -1;
		}
		
		ret = of_property_read_string(pdev->dev.of_node, "power_on_pin", &value);
		if(!ret){
			CHECK_PROP(ret, "power_on_pin", value);
			wifi_power_gpio = 1;
			plat->power_on_pin = amlogic_gpio_name_map_num(value);
		}
	
		ret = of_property_read_u32(pdev->dev.of_node, "power_on_pin_level", &plat->power_on_pin_level);
		
		ret = of_property_read_string(pdev->dev.of_node, "power_on_pin2", &value);
		if(!ret){
			CHECK_PROP(ret, "power_on_pin2", value);
			wifi_power_gpio2 = 1;
			plat->power_on_pin2 = amlogic_gpio_name_map_num(value);
		}

		ret = of_property_read_string(pdev->dev.of_node, "clock_32k_pin", &value);
		CHECK_PROP(ret, "clock_32k_pin", value);
		plat->clock_32k_pin = amlogic_gpio_name_map_num(value);

		plat->plat_info_valid = 1;
		
		printk("interrupt_pin=%d, irq_num=%d, irq_trigger_type=%d, "
				"power_on_pin=%d,"
				"clock_32k_pin=%d\n", 
				plat->interrupt_pin, plat->irq_num, plat->irq_trigger_type, 
				plat->power_on_pin,
				plat->clock_32k_pin);
	}
#endif

    return 0;
}

static int wifi_dev_remove(struct platform_device *pdev)
{
    printk("wifi_dev_remove\n");
    return 0;
}

static struct platform_driver wifi_plat_driver = {
    .probe = wifi_dev_probe,
    .remove = wifi_dev_remove,
    .driver = {
        .name = "aml_broadcm_wifi",
        .owner = THIS_MODULE,
        .of_match_table = wifi_match
    },
};

static int __init wifi_dt_init(void)
{
	int ret;
	ret = platform_driver_register(&wifi_plat_driver);
	return ret;
}
// module_init(wifi_dt_init);
fs_initcall_sync(wifi_dt_init);

static void __exit wifi_dt_exit(void)
{
	platform_driver_unregister(&wifi_plat_driver);
}
module_exit(wifi_dt_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("AMLOGIC");
MODULE_DESCRIPTION("wifi device tree driver");

#ifdef CONFIG_OF

int wifi_setup_dt()
{
	int ret;
	uint flag;

	printk("wifi_setup_dt\n");
	if (!wifi_info.plat_info_valid) {
		printk("wifi_setup_dt : invalid device tree setting\n");
		return -1;
	}
	
	//setup 32k clock
	wifi_request_32k_clk(1, OWNER_NAME);
	
#if ((!(defined CONFIG_ARCH_MESON8)) && (!(defined CONFIG_ARCH_MESON8B)))
	//setup sdio pullup
	aml_clr_reg32_mask(P_PAD_PULL_UP_REG4,0xf|1<<8|1<<9|1<<11|1<<12);		
	aml_clr_reg32_mask(P_PAD_PULL_UP_REG2,1<<7|1<<8|1<<9);	
#endif
	
	//setup irq
	SHOW_PIN_OWN("interrupt_pin", wifi_info.interrupt_pin);
	ret = amlogic_gpio_request(wifi_info.interrupt_pin, OWNER_NAME);
	CHECK_RET(ret);
	ret = amlogic_disable_pullup(wifi_info.interrupt_pin, OWNER_NAME);
	CHECK_RET(ret);
	ret = amlogic_gpio_direction_input(wifi_info.interrupt_pin, OWNER_NAME);
	CHECK_RET(ret);
	if (wifi_info.irq_num == 4) {
		flag = AML_GPIO_IRQ(GPIO_IRQ4, FILTER_NUM4, wifi_info.irq_trigger_type);
	} else if (wifi_info.irq_num == 5) {
		flag = AML_GPIO_IRQ(GPIO_IRQ5, FILTER_NUM5, wifi_info.irq_trigger_type);
	}
	else {
		printk("wifi_dt : unsupported irq number - %d\n", wifi_info.irq_num);
		return -1;
	}
	ret = amlogic_gpio_to_irq(wifi_info.interrupt_pin, OWNER_NAME, flag);
	CHECK_RET(ret);
	SHOW_PIN_OWN("interrupt_pin", wifi_info.interrupt_pin);
	
	//setup power
	if(wifi_power_gpio){
		SHOW_PIN_OWN("power_on_pin", wifi_info.power_on_pin);
		ret = amlogic_gpio_request(wifi_info.power_on_pin, OWNER_NAME);
		CHECK_RET(ret);
		if(wifi_info.power_on_pin_level)
			ret = amlogic_gpio_direction_output(wifi_info.power_on_pin, 1, OWNER_NAME);
		else
			ret = amlogic_gpio_direction_output(wifi_info.power_on_pin, 0, OWNER_NAME);
		CHECK_RET(ret);
		SHOW_PIN_OWN("power_on_pin", wifi_info.power_on_pin);
	}	

	if(wifi_power_gpio2){
		SHOW_PIN_OWN("power_on_pin2", wifi_info.power_on_pin2);
		ret = amlogic_gpio_request(wifi_info.power_on_pin2, OWNER_NAME);
		CHECK_RET(ret);
		ret = amlogic_gpio_direction_output(wifi_info.power_on_pin2, 0, OWNER_NAME);
		CHECK_RET(ret);
		SHOW_PIN_OWN("power_on_pin2", wifi_info.power_on_pin2);
	}

	return 0;
}
EXPORT_SYMBOL(wifi_setup_dt);

void wifi_teardown_dt()
{
	int ret = 0;
	
	printk("wifi_teardown_dt\n");
	if (!wifi_info.plat_info_valid) {
		printk("wifi_teardown_dt : invalid device tree setting\n");
		return;
	}
	
	if(wifi_power_gpio){
		ret = amlogic_gpio_free(wifi_info.power_on_pin, OWNER_NAME);
		CHECK_RET(ret);
	}
	
	if(wifi_power_gpio2)
	{
		ret = amlogic_gpio_free(wifi_info.power_on_pin2, OWNER_NAME);
		CHECK_RET(ret);
	}
	ret = amlogic_gpio_free(wifi_info.interrupt_pin, OWNER_NAME);
	CHECK_RET(ret);
	wifi_request_32k_clk(0, OWNER_NAME);
}
EXPORT_SYMBOL(wifi_teardown_dt);

static int clk_32k_on = 0;

void wifi_request_32k_clk(int is_on, const char *requestor)
{
	int ret;
	
//	if (!wifi_info.plat_info_valid) {
//		printk("wifi_request_32k_clk : invalid device tree setting\n");
//		return;
//	}
	printk("wifi_request_32k_clk : %s-->%s for %s\n", 
		clk_32k_on > 0 ? "ON" : "OFF", is_on ? "ON" : "OFF", requestor);
	
	if (is_on) {
		if (clk_32k_on == 0) {			
			SHOW_PIN_OWN("clock_32k_pin", wifi_info.clock_32k_pin);
			ret = amlogic_gpio_request(wifi_info.clock_32k_pin, OWNER_NAME);
			CHECK_RET(ret);
			amlogic_gpio_direction_output(wifi_info.clock_32k_pin, 0, OWNER_NAME);
			CHECK_RET(ret);
			SHOW_PIN_OWN("clock_32k_pin", wifi_info.clock_32k_pin);
#if ((defined CONFIG_ARCH_MESON8) || (defined CONFIG_ARCH_MESON8B))
			aml_set_reg32_mask(P_PERIPHS_PIN_MUX_3,0x1<<22);//set mode GPIOX_10-->CLK_OUT3
#else
            if(wifi_info.clock_32k_pin == 96) { // GPIOD_1, as PWM_D output
                aml_write_reg32(P_PERIPHS_PIN_MUX_2, aml_read_reg32(P_PERIPHS_PIN_MUX_2) | (1<<3)); //pwm_D pinmux£¬D13£¬GPIOD_1
                aml_write_reg32(P_PWM_MISC_REG_CD, (aml_read_reg32(P_PWM_MISC_REG_CD) & ~(0x7f<<16)) | ((1 << 23) | (0<<16) | (3<<6) | (1<<1)));//
                aml_set_reg32_bits(P_PWM_PWM_D, 0x27bc-1, 0, 16);   //pwm low
                aml_set_reg32_bits(P_PWM_PWM_D, 0x27bd-1, 16, 16); //pwm high
            } if(wifi_info.clock_32k_pin == 188) { //GPIOAO_6, as CLK_OUT2
                aml_set_reg32_mask(P_HHI_GEN_CLK_CNTL2,1<<9);//set clk source
                aml_clr_reg32_mask(P_HHI_GEN_CLK_CNTL2,0x3f<<0);//set div ==1
                aml_set_reg32_mask(P_HHI_GEN_CLK_CNTL2,1<<8);//set enable clk
                aml_set_reg32_mask(P_AO_RTI_PIN_MUX_REG,0x1<<22);//set mode GPIOAO_6-->CLK_OUT2
            } else {
                aml_set_reg32_mask(P_HHI_GEN_CLK_CNTL2,1<<22);//set clk source
                aml_clr_reg32_mask(P_HHI_GEN_CLK_CNTL2,0x3f<<13);//set div ==1
                aml_set_reg32_mask(P_HHI_GEN_CLK_CNTL2,1<<21);//set enable clk
                aml_set_reg32_mask(P_PERIPHS_PIN_MUX_3,0x1<<21);//set mode GPIOX_12-->CLK_OUT3
            }
#endif
			udelay(200);
		}
		++clk_32k_on;
	} else {
		--clk_32k_on;
        if(clk_32k_on < 0)
            clk_32k_on = 0; 
		if (clk_32k_on == 0) {
#if ((defined CONFIG_ARCH_MESON8) || (defined CONFIG_ARCH_MESON8B))
                        aml_clr_reg32_mask(P_PERIPHS_PIN_MUX_3,0x1<<22);
#else
			aml_clr_reg32_mask(P_PERIPHS_PIN_MUX_3,0x1<<21);
#endif
			ret = amlogic_gpio_free(wifi_info.clock_32k_pin, OWNER_NAME);
			CHECK_RET(ret);
		}
	}
}
EXPORT_SYMBOL(wifi_request_32k_clk);

void extern_wifi_set_enable(int is_on)
{
	int ret = 0;
	if (is_on) {
		if(wifi_power_gpio){
			if(wifi_info.power_on_pin_level)
				ret = amlogic_gpio_direction_output(wifi_info.power_on_pin, 0, OWNER_NAME);
			else
				ret = amlogic_gpio_direction_output(wifi_info.power_on_pin, 1, OWNER_NAME);
			CHECK_RET(ret);
		}	
		if(wifi_power_gpio2){
			ret = amlogic_gpio_direction_output(wifi_info.power_on_pin2, 1, OWNER_NAME);
			CHECK_RET(ret);
		}
		printk("WIFI  Enable! %d\n", wifi_info.power_on_pin);
	} else {
		if(wifi_power_gpio){
			if(wifi_info.power_on_pin_level)
				ret = amlogic_gpio_direction_output(wifi_info.power_on_pin, 1, OWNER_NAME);
			else
				ret = amlogic_gpio_direction_output(wifi_info.power_on_pin, 0, OWNER_NAME);
			CHECK_RET(ret);
		}
		if(wifi_power_gpio2){
			ret = amlogic_gpio_direction_output(wifi_info.power_on_pin2, 0, OWNER_NAME);
			CHECK_RET(ret);
		}
		
		printk("WIFI  Disable! %d\n", wifi_info.power_on_pin);
	
	}
}
EXPORT_SYMBOL(extern_wifi_set_enable);
#else

int wifi_setup_dt()
{
	return 0;
}
EXPORT_SYMBOL(wifi_setup_dt);

void wifi_teardown_dt(void)
{
}
EXPORT_SYMBOL(wifi_teardown_dt);

void wifi_request_32k_clk(int is_on, const char *requestor)
{
}
EXPORT_SYMBOL(wifi_request_32k_clk);

#endif
