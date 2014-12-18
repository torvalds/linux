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
	P_AO_RTI_PULL_UP_REG,
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
unsigned p_pull_upen_addr[]={
	P_PAD_PULL_UP_EN_REG0,
	P_PAD_PULL_UP_EN_REG1,
	P_PAD_PULL_UP_EN_REG2,
	P_PAD_PULL_UP_EN_REG3,
	P_PAD_PULL_UP_EN_REG4,
	P_AO_RTI_PULL_UP_REG,
};

/* Pad names for the pinmux subsystem */
const static struct pinctrl_pin_desc m8b_pads[] = {
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
	PINCTRL_PIN(GPIOAO_12,"GPIOAO_12"),
	PINCTRL_PIN(GPIOAO_13,"GPIOAO_13"),
	PINCTRL_PIN(GPIOH_0,"GPIOH_0"),
	PINCTRL_PIN(GPIOH_1,"GPIOH_1"),
	PINCTRL_PIN(GPIOH_2,"GPIOH_2"),
	PINCTRL_PIN(GPIOH_3,"GPIOH_3"),
	PINCTRL_PIN(GPIOH_4,"GPIOH_4"),
	PINCTRL_PIN(GPIOH_5,"GPIOH_5"),
	PINCTRL_PIN(GPIOH_6,"GPIOH_6"),
	PINCTRL_PIN(GPIOH_7,"GPIOH_7"),
	PINCTRL_PIN(GPIOH_8,"GPIOH_8"),
	PINCTRL_PIN(GPIOH_9,"GPIOH_9"),
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
	PINCTRL_PIN(BOOT_18,"BOOT_18"),
	PINCTRL_PIN(CARD_0,"CARD_0"),
	PINCTRL_PIN(CARD_1,"CARD_1"),
	PINCTRL_PIN(CARD_2,"CARD_2"),
	PINCTRL_PIN(CARD_3,"CARD_3"),
	PINCTRL_PIN(CARD_4,"CARD_4"),
	PINCTRL_PIN(CARD_5,"CARD_5"),
	PINCTRL_PIN(CARD_6,"CARD_6"),
	PINCTRL_PIN(GPIODV_0,"GPIODV_0"),
	PINCTRL_PIN(GPIODV_1,"GPIODV_1"),
	PINCTRL_PIN(GPIODV_2,"GPIODV_2"),
	PINCTRL_PIN(GPIODV_3,"GPIODV_3"),
	PINCTRL_PIN(GPIODV_4,"GPIODV_4"),
	PINCTRL_PIN(GPIODV_5,"GPIODV_5"),
	PINCTRL_PIN(GPIODV_6,"GPIODV_6"),
	PINCTRL_PIN(GPIODV_7,"GPIODV_7"),
	PINCTRL_PIN(GPIODV_8,"GPIODV_8"),
	PINCTRL_PIN(GPIODV_9,"GPIODV_9"),
	PINCTRL_PIN(GPIODV_10,"GPIODV_10"),
	PINCTRL_PIN(GPIODV_11,"GPIODV_11"),
	PINCTRL_PIN(GPIODV_12,"GPIODV_12"),
	PINCTRL_PIN(GPIODV_13,"GPIODV_13"),
	PINCTRL_PIN(GPIODV_14,"GPIODV_14"),
	PINCTRL_PIN(GPIODV_15,"GPIODV_15"),
	PINCTRL_PIN(GPIODV_16,"GPIODV_16"),
	PINCTRL_PIN(GPIODV_17,"GPIODV_17"),
	PINCTRL_PIN(GPIODV_18,"GPIODV_18"),
	PINCTRL_PIN(GPIODV_19,"GPIODV_19"),
	PINCTRL_PIN(GPIODV_20,"GPIODV_20"),
	PINCTRL_PIN(GPIODV_21,"GPIODV_21"),
	PINCTRL_PIN(GPIODV_22,"GPIODV_22"),
	PINCTRL_PIN(GPIODV_23,"GPIODV_23"),
	PINCTRL_PIN(GPIODV_24,"GPIODV_24"),
	PINCTRL_PIN(GPIODV_25,"GPIODV_25"),
	PINCTRL_PIN(GPIODV_26,"GPIODV_26"),
	PINCTRL_PIN(GPIODV_27,"GPIODV_27"),
	PINCTRL_PIN(GPIODV_28,"GPIODV_28"),
	PINCTRL_PIN(GPIODV_29,"GPIODV_29"),
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
	PINCTRL_PIN(GPIOY_16,"GPIOY_16"),
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
	PINCTRL_PIN(DIF_TTL_0_P,"DIF_TTL_0_P"),
	PINCTRL_PIN(DIF_TTL_0_N,"DIF_TTL_0_N"),
	PINCTRL_PIN(DIF_TTL_1_P,"DIF_TTL_1_P"),
	PINCTRL_PIN(DIF_TTL_1_N,"DIF_TTL_1_N"),
	PINCTRL_PIN(DIF_TTL_2_P,"DIF_TTL_2_P"),
	PINCTRL_PIN(DIF_TTL_2_N,"DIF_TTL_2_N"),
	PINCTRL_PIN(DIF_TTL_3_P,"DIF_TTL_3_P"),
	PINCTRL_PIN(DIF_TTL_3_N,"DIF_TTL_3_N"),
	PINCTRL_PIN(DIF_TTL_4_P,"DIF_TTL_4_P"),
	PINCTRL_PIN(DIF_TTL_4_N,"DIF_TTL_4_N"),
	PINCTRL_PIN(HDMI_TTL_0_P,"HDMI_TTL_0_P"),
	PINCTRL_PIN(HDMI_TTL_0_N,"HDMI_TTL_0_N"),
	PINCTRL_PIN(HDMI_TTL_1_P,"HDMI_TTL_1_P"),
	PINCTRL_PIN(HDMI_TTL_1_N,"HDMI_TTL_1_N"),
	PINCTRL_PIN(HDMI_TTL_2_P,"HDMI_TTL_2_P"),
	PINCTRL_PIN(HDMI_TTL_2_N,"HDMI_TTL_2_N"),
	PINCTRL_PIN(HDMI_TTL_CK_P,"HDMI_TTL_CK_P"),
	PINCTRL_PIN(HDMI_TTL_CK_N,"HDMI_TTL_CK_N"),
	PINCTRL_PIN(GPIO_BSD_EN,"GPIO_BSD_EN"),
	PINCTRL_PIN(GPIO_TEST_N,"GPIO_TEST_N"),

};
int m8b_pin_to_pullup(unsigned int pin ,unsigned int *reg,unsigned int *bit,unsigned int *bit_en)
{
	if(pin<=GPIOAO_13)
	{
		*reg=5;
		*bit=pin+16;
		*bit_en=pin;
	}
	else if(pin<=GPIOH_9)
	{
		*reg=1;
		*bit=pin-GPIOH_0+16;
		*bit_en=*bit;
	}
	else if(pin<=BOOT_18)
	{
		*reg=2;
		*bit=pin-BOOT_0;
		*bit_en=*bit;
	}
	else if(pin<=CARD_6)
	{
		*reg=2;
		*bit=pin-CARD_0+20;
		*bit_en=*bit;
	}
	else if(pin<=GPIODV_29)
	{
		*reg=0;
		*bit=pin-GPIODV_0;
		*bit_en=*bit;
	}
	else if(pin<=GPIOY_16)
	{
		*reg=3;
		*bit=pin-GPIOY_0;
		*bit_en=*bit;
	}
	else if(pin<=GPIOX_21)
	{
		*reg=4;
		*bit=pin-GPIOX_0;
		*bit_en=*bit;
	}
	else if(pin == GPIO_TEST_N)
	{
		*reg=5;
		*bit=pin-GPIO_TEST_N+14;
		*bit_en=pin-GPIO_TEST_N+30;
	}
	else
		return -1;
	return 0;

}
int m8b_pin_map_to_direction(unsigned int pin,unsigned int *reg,unsigned int *bit)
{
	if(pin<=GPIOAO_13)
	{
		*reg=6;
		*bit=pin;
	}
	else if(pin<=GPIOH_9)
	{
		*reg=3;
		*bit=pin-GPIOH_0+19;
	}
	else if(pin<=BOOT_18)
	{
		*reg=3;
		*bit=pin-BOOT_0;
	}
	else if(pin<=CARD_6)
	{
		*reg=0;
		*bit=pin-CARD_0+22;
	}
	else if(pin<=GPIODV_29)
	{
		*reg=2;
		*bit=pin-GPIODV_0;
	}
	else if(pin<=GPIOY_16)
	{
		*reg=1;
		*bit=pin-GPIOY_0;
	}
	else if(pin<=GPIOX_21)
	{
		*reg=0;
		*bit=pin-GPIOX_0;
	}
	else if(pin<=HDMI_TTL_CK_N)
	{
		*reg=4;
		*bit=pin-DIF_TTL_0_P+12;
	}
	//else if(pin<=GPIO_TEST_N)
	//{
		//*reg=5;
		//*bit=pin-GPIO_TEST_N+14;
	//}
	else
		return -1;
	return 0;
}
static int m8b_set_pullup(unsigned int pin,unsigned int config)
{
	unsigned int reg=0,bit=0,bit_en=0,ret;
	u16 pullarg = AML_PINCONF_UNPACK_PULL_ARG(config);
	u16 pullen = AML_PINCONF_UNPACK_PULL_EN(config);
	ret=m8b_pin_to_pullup(pin,&reg,&bit,&bit_en);
	if(!ret)
	{
		if(pullen){
			if(!ret)
			{
				if(pullarg)
					aml_set_reg32_mask(p_pull_up_addr[reg],1<<bit);
				else
					aml_clr_reg32_mask(p_pull_up_addr[reg],1<<bit);
			}
			aml_set_reg32_mask(p_pull_upen_addr[reg],1<<bit_en);
		}
		else
			aml_clr_reg32_mask(p_pull_upen_addr[reg],1<<bit_en);
	}
	return ret;
}

static struct amlogic_pinctrl_soc_data m8b_pinctrl = {
	.pins = m8b_pads,
	.npins = ARRAY_SIZE(m8b_pads),
	.meson_set_pullup=m8b_set_pullup,
	.pin_map_to_direction=m8b_pin_map_to_direction,
};
static struct of_device_id m8b_pinctrl_of_table[]=
{
	{
		.compatible="amlogic,pinmux-m8b",
	},
	{},
};

static int  m8b_pmx_probe(struct platform_device *pdev)
{
	return amlogic_pmx_probe(pdev,&m8b_pinctrl);
}

static int  m8b_pmx_remove(struct platform_device *pdev)
{
	return amlogic_pmx_remove(pdev);
}

static struct platform_driver m8b_pmx_driver = {
	.driver = {
		.name = "pinmux-m8b",
		.owner = THIS_MODULE,
		.of_match_table=of_match_ptr(m8b_pinctrl_of_table),
	},
	.probe = m8b_pmx_probe,
	.remove = m8b_pmx_remove,
};

static int __init m8b_pmx_init(void)
{
	return platform_driver_register(&m8b_pmx_driver);
}
arch_initcall(m8b_pmx_init);

static void __exit m8b_pmx_exit(void)
{
	platform_driver_unregister(&m8b_pmx_driver);
}
module_exit(m8b_pmx_exit);
MODULE_DESCRIPTION("m8b pin control driver");
MODULE_LICENSE("GPL v2");
