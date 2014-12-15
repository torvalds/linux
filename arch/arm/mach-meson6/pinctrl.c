/*
 * Driver for the amlogic pin controller
 *
 *
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/pinctrl/machine.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/pinctrl/pinconf.h>

#include <plat/io.h>
#include <mach/am_regs.h>
#include <linux/amlogic/pinctrl-amlogic.h>
#include <linux/amlogic/aml_gpio_consumer.h>
DEFINE_MUTEX(spi_nand_mutex);
unsigned p_pull_up_addr[]={
	P_PAD_PULL_UP_REG0,
	P_PAD_PULL_UP_REG1,
	P_PAD_PULL_UP_REG2,
	P_PAD_PULL_UP_REG3,
	P_PAD_PULL_UP_REG4,
	P_PAD_PULL_UP_REG5,
	P_PAD_PULL_UP_REG6,
};
unsigned int p_pin_mux_reg_addr[]=
{
	P_PERIPHS_PIN_MUX_0,
	P_PERIPHS_PIN_MUX_1,
	P_PERIPHS_PIN_MUX_2,
	P_PERIPHS_PIN_MUX_3,
	P_PERIPHS_PIN_MUX_4,
	P_PERIPHS_PIN_MUX_5,
	P_PERIPHS_PIN_MUX_6,
	P_PERIPHS_PIN_MUX_7,
	P_PERIPHS_PIN_MUX_8,
	P_PERIPHS_PIN_MUX_9,
	P_AO_RTI_PIN_MUX_REG,
};
/* Pad names for the pinmux subsystem */
const static struct pinctrl_pin_desc amlogic_pads[] = {
	PINCTRL_PIN(GPIOA_0,"GPIOA_0"),
	PINCTRL_PIN(GPIOA_1,"GPIOA_1"),
	PINCTRL_PIN(GPIOA_2,"GPIOA_2"),
	PINCTRL_PIN(GPIOA_3,"GPIOA_3"),
	PINCTRL_PIN(GPIOA_4,"GPIOA_4"),
	PINCTRL_PIN(GPIOA_5,"GPIOA_5"),
	PINCTRL_PIN(GPIOA_6,"GPIOA_6"),
	PINCTRL_PIN(GPIOA_7,"GPIOA_7"),
	PINCTRL_PIN(GPIOA_8,"GPIOA_8"),
	PINCTRL_PIN(GPIOA_9,"GPIOA_9"),
	PINCTRL_PIN(GPIOA_10,"GPIOA_10"),
	PINCTRL_PIN(GPIOA_11,"GPIOA_11"),
	PINCTRL_PIN(GPIOA_12,"GPIOA_12"),
	PINCTRL_PIN(GPIOA_13,"GPIOA_13"),
	PINCTRL_PIN(GPIOA_14,"GPIOA_14"),
	PINCTRL_PIN(GPIOA_15,"GPIOA_15"),
	PINCTRL_PIN(GPIOA_16,"GPIOA_16"),
	PINCTRL_PIN(GPIOA_17,"GPIOA_17"),
	PINCTRL_PIN(GPIOA_18,"GPIOA_18"),
	PINCTRL_PIN(GPIOA_19,"GPIOA_19"),
	PINCTRL_PIN(GPIOA_20,"GPIOA_20"),
	PINCTRL_PIN(GPIOA_21,"GPIOA_21"),
	PINCTRL_PIN(GPIOA_22,"GPIOA_22"),
	PINCTRL_PIN(GPIOA_23,"GPIOA_23"),
	PINCTRL_PIN(GPIOA_24,"GPIOA_24"),
	PINCTRL_PIN(GPIOA_25,"GPIOA_25"),
	PINCTRL_PIN(GPIOA_26,"GPIOA_26"),
	PINCTRL_PIN(GPIOA_27,"GPIOA_27"),
	PINCTRL_PIN(GPIOB_0,"GPIOB_0"),
	PINCTRL_PIN(GPIOB_1,"GPIOB_1"),
	PINCTRL_PIN(GPIOB_2,"GPIOB_2"),
	PINCTRL_PIN(GPIOB_3,"GPIOB_3"),
	PINCTRL_PIN(GPIOB_4,"GPIOB_4"),
	PINCTRL_PIN(GPIOB_5,"GPIOB_5"),
	PINCTRL_PIN(GPIOB_6,"GPIOB_6"),
	PINCTRL_PIN(GPIOB_7,"GPIOB_7"),
	PINCTRL_PIN(GPIOB_8,"GPIOB_8"),
	PINCTRL_PIN(GPIOB_9,"GPIOB_9"),
	PINCTRL_PIN(GPIOB_10,"GPIOB_10"),
	PINCTRL_PIN(GPIOB_11,"GPIOB_11"),
	PINCTRL_PIN(GPIOB_12,"GPIOB_12"),
	PINCTRL_PIN(GPIOB_13,"GPIOB_13"),
	PINCTRL_PIN(GPIOB_14,"GPIOB_14"),
	PINCTRL_PIN(GPIOB_15,"GPIOB_15"),
	PINCTRL_PIN(GPIOB_16,"GPIOB_16"),
	PINCTRL_PIN(GPIOB_17,"GPIOB_17"),
	PINCTRL_PIN(GPIOB_18,"GPIOB_18"),
	PINCTRL_PIN(GPIOB_19,"GPIOB_19"),
	PINCTRL_PIN(GPIOB_20,"GPIOB_20"),
	PINCTRL_PIN(GPIOB_21,"GPIOB_21"),
	PINCTRL_PIN(GPIOB_22,"GPIOB_22"),
	PINCTRL_PIN(GPIOB_23,"GPIOB_23"),
	PINCTRL_PIN(GPIOC_0,"GPIOC_0"),
	PINCTRL_PIN(GPIOC_1,"GPIOC_1"),
	PINCTRL_PIN(GPIOC_2,"GPIOC_2"),
	PINCTRL_PIN(GPIOC_3,"GPIOC_3"),
	PINCTRL_PIN(GPIOC_4,"GPIOC_4"),
	PINCTRL_PIN(GPIOC_5,"GPIOC_5"),
	PINCTRL_PIN(GPIOC_6,"GPIOC_6"),
	PINCTRL_PIN(GPIOC_7,"GPIOC_7"),
	PINCTRL_PIN(GPIOC_8,"GPIOC_8"),
	PINCTRL_PIN(GPIOC_9,"GPIOC_9"),
	PINCTRL_PIN(GPIOC_10,"GPIOC_10"),
	PINCTRL_PIN(GPIOC_11,"GPIOC_11"),
	PINCTRL_PIN(GPIOC_12,"GPIOC_12"),
	PINCTRL_PIN(GPIOC_13,"GPIOC_13"),
	PINCTRL_PIN(GPIOC_14,"GPIOC_14"),
	PINCTRL_PIN(GPIOC_15,"GPIOC_15"),
	PINCTRL_PIN(GPIOD_0,"GPIOD_0"),
	PINCTRL_PIN(GPIOD_1,"GPIOD_1"),
	PINCTRL_PIN(GPIOD_2,"GPIOD_2"),
	PINCTRL_PIN(GPIOD_3,"GPIOD_3"),
	PINCTRL_PIN(GPIOD_4,"GPIOD_4"),
	PINCTRL_PIN(GPIOD_5,"GPIOD_5"),
	PINCTRL_PIN(GPIOD_6,"GPIOD_6"),
	PINCTRL_PIN(GPIOD_7,"GPIOD_7"),
	PINCTRL_PIN(GPIOD_8,"GPIOD_8"),
	PINCTRL_PIN(GPIOD_9,"GPIOD_9"),
	PINCTRL_PIN(GPIOE_0,"GPIOE_0"),
	PINCTRL_PIN(GPIOE_1,"GPIOE_1"),
	PINCTRL_PIN(GPIOE_2,"GPIOE_2"),
	PINCTRL_PIN(GPIOE_3,"GPIOE_3"),
	PINCTRL_PIN(GPIOE_4,"GPIOE_4"),
	PINCTRL_PIN(GPIOE_5,"GPIOE_5"),
	PINCTRL_PIN(GPIOE_6,"GPIOE_6"),
	PINCTRL_PIN(GPIOE_7,"GPIOE_7"),
	PINCTRL_PIN(GPIOE_8,"GPIOE_8"),
	PINCTRL_PIN(GPIOE_9,"GPIOE_9"),
	PINCTRL_PIN(GPIOE_10,"GPIOE_10"),
	PINCTRL_PIN(GPIOE_11,"GPIOE_11"),
	PINCTRL_PIN(CARD_0,"CARD_0"),
	PINCTRL_PIN(CARD_1,"CARD_1"),
	PINCTRL_PIN(CARD_2,"CARD_2"),
	PINCTRL_PIN(CARD_3,"CARD_3"),
	PINCTRL_PIN(CARD_4,"CARD_4"),
	PINCTRL_PIN(CARD_5,"CARD_5"),
	PINCTRL_PIN(CARD_6,"CARD_6"),
	PINCTRL_PIN(CARD_7,"CARD_7"),
	PINCTRL_PIN(CARD_8,"CARD_8"),
	PINCTRL_PIN(BOOT_0,"BOOT_0"),
	PINCTRL_PIN(BOOT_1,"BOOT_1"),
	PINCTRL_PIN(BOOT_2,"BOOT_2"),
	PINCTRL_PIN(BOOT_3,"BOOT_3"),
	PINCTRL_PIN(BOOT_4,"BOOT_4"),
	PINCTRL_PIN(BOOT_5,"BOOT_5"),
	PINCTRL_PIN(BOOT_6,"BOOT_6"),
	PINCTRL_PIN(BOOT_7,"BOOT_7"),
	PINCTRL_PIN(BOOT_8,"BOOT_8"),
	PINCTRL_PIN(BOOT_9,"BOOT_9"),
	PINCTRL_PIN(BOOT_10,"BOOT_10"),
	PINCTRL_PIN(BOOT_11,"BOOT_11"),
	PINCTRL_PIN(BOOT_12,"BOOT_12"),
	PINCTRL_PIN(BOOT_13,"BOOT_13"),
	PINCTRL_PIN(BOOT_14,"BOOT_14"),
	PINCTRL_PIN(BOOT_15,"BOOT_15"),
	PINCTRL_PIN(BOOT_16,"BOOT_16"),
	PINCTRL_PIN(BOOT_17,"BOOT_17"),
	PINCTRL_PIN(GPIOX_0,"GPIOX_0"),
	PINCTRL_PIN(GPIOX_1,"GPIOX_1"),
	PINCTRL_PIN(GPIOX_2,"GPIOX_2"),
	PINCTRL_PIN(GPIOX_3,"GPIOX_3"),
	PINCTRL_PIN(GPIOX_4,"GPIOX_4"),
	PINCTRL_PIN(GPIOX_5,"GPIOX_5"),
	PINCTRL_PIN(GPIOX_6,"GPIOX_6"),
	PINCTRL_PIN(GPIOX_7,"GPIOX_7"),
	PINCTRL_PIN(GPIOX_8,"GPIOX_8"),
	PINCTRL_PIN(GPIOX_9,"GPIOX_9"),
	PINCTRL_PIN(GPIOX_10,"GPIOX_10"),
	PINCTRL_PIN(GPIOX_11,"GPIOX_11"),
	PINCTRL_PIN(GPIOX_12,"GPIOX_12"),
	PINCTRL_PIN(GPIOX_13,"GPIOX_13"),
	PINCTRL_PIN(GPIOX_14,"GPIOX_14"),
	PINCTRL_PIN(GPIOX_15,"GPIOX_15"),
	PINCTRL_PIN(GPIOX_16,"GPIOX_16"),
	PINCTRL_PIN(GPIOX_17,"GPIOX_17"),
	PINCTRL_PIN(GPIOX_18,"GPIOX_18"),
	PINCTRL_PIN(GPIOX_19,"GPIOX_19"),
	PINCTRL_PIN(GPIOX_20,"GPIOX_20"),
	PINCTRL_PIN(GPIOX_21,"GPIOX_21"),
	PINCTRL_PIN(GPIOX_22,"GPIOX_22"),
	PINCTRL_PIN(GPIOX_23,"GPIOX_23"),
	PINCTRL_PIN(GPIOX_24,"GPIOX_24"),
	PINCTRL_PIN(GPIOX_25,"GPIOX_25"),
	PINCTRL_PIN(GPIOX_26,"GPIOX_26"),
	PINCTRL_PIN(GPIOX_27,"GPIOX_27"),
	PINCTRL_PIN(GPIOX_28,"GPIOX_28"),
	PINCTRL_PIN(GPIOX_29,"GPIOX_29"),
	PINCTRL_PIN(GPIOX_30,"GPIOX_30"),
	PINCTRL_PIN(GPIOX_31,"GPIOX_31"),
	PINCTRL_PIN(GPIOX_32,"GPIOX_32"),
	PINCTRL_PIN(GPIOX_33,"GPIOX_33"),
	PINCTRL_PIN(GPIOX_34,"GPIOX_34"),
	PINCTRL_PIN(GPIOX_35,"GPIOX_35"),
	PINCTRL_PIN(GPIOY_0,"GPIOY_0"),
	PINCTRL_PIN(GPIOY_1,"GPIOY_1"),
	PINCTRL_PIN(GPIOY_2,"GPIOY_2"),
	PINCTRL_PIN(GPIOY_3,"GPIOY_3"),
	PINCTRL_PIN(GPIOY_4,"GPIOY_4"),
	PINCTRL_PIN(GPIOY_5,"GPIOY_5"),
	PINCTRL_PIN(GPIOY_6,"GPIOY_6"),
	PINCTRL_PIN(GPIOY_7,"GPIOY_7"),
	PINCTRL_PIN(GPIOY_8,"GPIOY_8"),
	PINCTRL_PIN(GPIOY_9,"GPIOY_9"),
	PINCTRL_PIN(GPIOY_10,"GPIOY_10"),
	PINCTRL_PIN(GPIOY_11,"GPIOY_11"),
	PINCTRL_PIN(GPIOY_12,"GPIOY_12"),
	PINCTRL_PIN(GPIOY_13,"GPIOY_13"),
	PINCTRL_PIN(GPIOY_14,"GPIOY_14"),
	PINCTRL_PIN(GPIOY_15,"GPIOY_15"),
	PINCTRL_PIN(GPIOZ_0,"GPIOZ_0"),
	PINCTRL_PIN(GPIOZ_1,"GPIOZ_1"),
	PINCTRL_PIN(GPIOZ_2,"GPIOZ_2"),
	PINCTRL_PIN(GPIOZ_3,"GPIOZ_3"),
	PINCTRL_PIN(GPIOZ_4,"GPIOZ_4"),
	PINCTRL_PIN(GPIOZ_5,"GPIOZ_5"),
	PINCTRL_PIN(GPIOZ_6,"GPIOZ_6"),
	PINCTRL_PIN(GPIOZ_7,"GPIOZ_7"),
	PINCTRL_PIN(GPIOZ_8,"GPIOZ_8"),
	PINCTRL_PIN(GPIOZ_9,"GPIOZ_9"),
	PINCTRL_PIN(GPIOZ_10,"GPIOZ_10"),
	PINCTRL_PIN(GPIOZ_11,"GPIOZ_11"),
	PINCTRL_PIN(GPIOZ_12,"GPIOZ_12"),
	PINCTRL_PIN(GPIOAO_0,"GPIOAO_0"),
	PINCTRL_PIN(GPIOAO_1,"GPIOAO_1"),
	PINCTRL_PIN(GPIOAO_2,"GPIOAO_2"),
	PINCTRL_PIN(GPIOAO_3,"GPIOAO_3"),
	PINCTRL_PIN(GPIOAO_4,"GPIOAO_4"),
	PINCTRL_PIN(GPIOAO_5,"GPIOAO_5"),
	PINCTRL_PIN(GPIOAO_6,"GPIOAO_6"),
	PINCTRL_PIN(GPIOAO_7,"GPIOAO_7"),
	PINCTRL_PIN(GPIOAO_8,"GPIOAO_8"),
	PINCTRL_PIN(GPIOAO_9,"GPIOAO_9"),
	PINCTRL_PIN(GPIOAO_10,"GPIOAO_10"),
	PINCTRL_PIN(GPIOAO_11,"GPIOAO_11"),
};
static int amlogic_pin_to_pullup(unsigned int pin ,unsigned int *reg,unsigned int *bit)
{
	if(pin<=GPIOZ_12)
	{
		*reg=6;
		*bit=pin;
	}
	else if (pin<=GPIOE_11)
	{
		*reg=6;
		*bit=pin-GPIOE_0+16;
	}
	else if(pin<=GPIOY_15)
	{
		*reg=5;
		*bit=pin-GPIOY_0+4;
	}
	else if(pin<=GPIOX_31)
	{
		*reg=4;
		*bit=pin-GPIOX_0;
	}
	else if(pin<=GPIOX_35)
	{
		*reg=5;
		*bit=pin-GPIOX_32;	
	}
	else if(pin<=BOOT_17)
	{
		*reg=3;
		*bit=pin-BOOT_0;
	}
	else if(pin<=GPIOD_9)
	{
		*reg=2;
		*bit=pin-GPIOD_0+16;
	}
	else if(pin<=GPIOC_15)
	{
		*reg=2;
		*bit=pin-GPIOC_0;
	}
	else if(pin<=CARD_8)
	{
		*reg=3;
		*bit=pin-CARD_0+20;
	}
	else if(pin<=GPIOB_23)
	{
		*reg=1;
		*bit=pin-GPIOB_0;
	}
	else if(pin<=GPIOA_27)
	{
		*reg=0;
		*bit=pin-GPIOA_0;
	}
	else
		return -1;
	return 0;
}
static int amlogic_pin_map_to_direction(unsigned int pin,unsigned int *reg,unsigned int *bit)
{
	if(pin<=GPIOZ_12)
	{
		*reg=6;
		*bit=pin-GPIOZ_0+16;
	}
	else if (pin<=GPIOE_11)
	{
		*reg=6;
		*bit=pin-GPIOE_0;
	}
	else if(pin<=GPIOY_15)
	{
		*reg=5;
		*bit=pin-GPIOY_0;
	}
	else if(pin<=GPIOX_31)
	{
		*reg=4;
		*bit=pin-GPIOX_0;
	}
	else if(pin<=GPIOX_35)
	{
		*reg=3;
		*bit=pin-GPIOX_32+20;	
	}
	else if(pin<=BOOT_17)
	{
		*reg=3;
		*bit=pin-BOOT_0;
	}
	else if(pin<=GPIOD_9)
	{
		*reg=2;
		*bit=pin-GPIOD_0+16;
	}
	else if(pin<=GPIOC_15)
	{
		*reg=2;
		*bit=pin-GPIOC_0;
	}
	else if(pin<=CARD_8)
	{
		*reg=5;
		*bit=pin-CARD_0+23;
	}
	else if(pin<=GPIOB_23)
	{
		*reg=1;
		*bit=pin-GPIOB_0;
	}
	else if(pin<=GPIOA_27)
	{
		*reg=0;
		*bit=pin-GPIOA_0;
	}
	else if(pin<=GPIOAO_11)
	{
		*reg=7;
		*bit=pin-GPIOA_0;	
	}
	else
		return -1;
	return 0;
}
static int m6_set_pullup(unsigned int pin,unsigned int config)
{
	unsigned int reg=0,bit=0,ret;
	u16 pullarg = AML_PINCONF_UNPACK_PULL_ARG(config);
	ret=amlogic_pin_to_pullup(pin,&reg,&bit);
	if(!ret)
	{
		if(pullarg)
			aml_set_reg32_mask(p_pull_up_addr[reg],1<<bit);
		else
			aml_clr_reg32_mask(p_pull_up_addr[reg],1<<bit);
	}
	return ret;
}
static struct amlogic_pinctrl_soc_data m6_pinctrl = {
	.pins = amlogic_pads,
	.npins = ARRAY_SIZE(amlogic_pads),
	.meson_set_pullup=m6_set_pullup,
	.pin_map_to_direction=amlogic_pin_map_to_direction,
};
static struct of_device_id amlogic_pinctrl_of_table[]=
{
	{
		.compatible="amlogic,pinmux-m6",
	},
	{},
};

static int  m6_pmx_probe(struct platform_device *pdev)
{
	return amlogic_pmx_probe(pdev,&m6_pinctrl);
}

static int  m6_pmx_remove(struct platform_device *pdev)
{
	return amlogic_pmx_remove(pdev);
}

static struct platform_driver amlogic_pmx_driver = {
	.driver = {
		.name = "pinmux-m6",
		.owner = THIS_MODULE,
		.of_match_table=of_match_ptr(amlogic_pinctrl_of_table),
	},
	.probe = m6_pmx_probe,
	.remove = m6_pmx_remove,
};

static int __init amlogic_pmx_init(void)
{
	return platform_driver_register(&amlogic_pmx_driver);
}
arch_initcall(amlogic_pmx_init);

static void __exit amlogic_pmx_exit(void)
{
	platform_driver_unregister(&amlogic_pmx_driver);
}
module_exit(amlogic_pmx_exit);
MODULE_DESCRIPTION("amlogic pin control driver");
MODULE_LICENSE("GPL v2");
