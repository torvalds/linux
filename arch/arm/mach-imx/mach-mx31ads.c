/*
 *  Copyright (C) 2000 Deep Blue Solutions Ltd
 *  Copyright (C) 2002 Shane Nay (shane@minirl.com)
 *  Copyright 2005-2007 Freescale Semiconductor, Inc. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/types.h>
#include <linux/init.h>
#include <linux/clk.h>
#include <linux/serial_8250.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>
#include <asm/memory.h>
#include <asm/mach/map.h>
#include <mach/common.h>
#include <mach/iomux-mx3.h>

#ifdef CONFIG_MACH_MX31ADS_WM1133_EV1
#include <linux/mfd/wm8350/audio.h>
#include <linux/mfd/wm8350/core.h>
#include <linux/mfd/wm8350/pmic.h>
#endif

#include "devices-imx31.h"

/* Base address of PBC controller */
#define PBC_BASE_ADDRESS	MX31_CS4_BASE_ADDR_VIRT

/* PBC Board interrupt status register */
#define PBC_INTSTATUS           0x000016

/* PBC Board interrupt current status register */
#define PBC_INTCURR_STATUS      0x000018

/* PBC Interrupt mask register set address */
#define PBC_INTMASK_SET         0x00001A

/* PBC Interrupt mask register clear address */
#define PBC_INTMASK_CLEAR       0x00001C

/* External UART A */
#define PBC_SC16C652_UARTA      0x010000

/* External UART B */
#define PBC_SC16C652_UARTB      0x010010

#define PBC_INTSTATUS_REG	(PBC_INTSTATUS + PBC_BASE_ADDRESS)
#define PBC_INTMASK_SET_REG	(PBC_INTMASK_SET + PBC_BASE_ADDRESS)
#define PBC_INTMASK_CLEAR_REG	(PBC_INTMASK_CLEAR + PBC_BASE_ADDRESS)

#define EXPIO_INT_XUART_INTA	10
#define EXPIO_INT_XUART_INTB	11

#define MXC_MAX_EXP_IO_LINES	16

/* CS8900 */
#define EXPIO_INT_ENET_INT	8
#define CS4_CS8900_MMIO_START	0x20000

static struct irq_domain *domain;

/*
 * The serial port definition structure.
 */
static struct plat_serial8250_port serial_platform_data[] = {
	{
		.membase  = (void *)(PBC_BASE_ADDRESS + PBC_SC16C652_UARTA),
		.mapbase  = (unsigned long)(MX31_CS4_BASE_ADDR + PBC_SC16C652_UARTA),
		.uartclk  = 14745600,
		.regshift = 0,
		.iotype   = UPIO_MEM,
		.flags    = UPF_BOOT_AUTOCONF | UPF_SKIP_TEST | UPF_AUTO_IRQ,
	}, {
		.membase  = (void *)(PBC_BASE_ADDRESS + PBC_SC16C652_UARTB),
		.mapbase  = (unsigned long)(MX31_CS4_BASE_ADDR + PBC_SC16C652_UARTB),
		.uartclk  = 14745600,
		.regshift = 0,
		.iotype   = UPIO_MEM,
		.flags    = UPF_BOOT_AUTOCONF | UPF_SKIP_TEST | UPF_AUTO_IRQ,
	},
	{},
};

static struct platform_device serial_device = {
	.name	= "serial8250",
	.id	= 0,
	.dev	= {
		.platform_data = serial_platform_data,
	},
};

static struct resource mx31ads_cs8900_resources[] __initdata = {
	DEFINE_RES_MEM(MX31_CS4_BASE_ADDR + CS4_CS8900_MMIO_START, SZ_64K),
	DEFINE_RES_IRQ(-1),
};

static const struct platform_device_info mx31ads_cs8900_devinfo __initconst = {
	.name = "cs89x0",
	.id = 0,
	.res = mx31ads_cs8900_resources,
	.num_res = ARRAY_SIZE(mx31ads_cs8900_resources),
};

static int __init mxc_init_extuart(void)
{
	serial_platform_data[0].irq = irq_find_mapping(domain,
						       EXPIO_INT_XUART_INTA);
	serial_platform_data[1].irq = irq_find_mapping(domain,
						       EXPIO_INT_XUART_INTB);
	return platform_device_register(&serial_device);
}

static void __init mxc_init_ext_ethernet(void)
{
	mx31ads_cs8900_resources[1].start =
			irq_find_mapping(domain, EXPIO_INT_ENET_INT);
	mx31ads_cs8900_resources[1].end =
			irq_find_mapping(domain, EXPIO_INT_ENET_INT);
	platform_device_register_full(
		(struct platform_device_info *)&mx31ads_cs8900_devinfo);
}

static const struct imxuart_platform_data uart_pdata __initconst = {
	.flags = IMXUART_HAVE_RTSCTS,
};

static unsigned int uart_pins[] = {
	MX31_PIN_CTS1__CTS1,
	MX31_PIN_RTS1__RTS1,
	MX31_PIN_TXD1__TXD1,
	MX31_PIN_RXD1__RXD1
};

static inline void mxc_init_imx_uart(void)
{
	mxc_iomux_setup_multiple_pins(uart_pins, ARRAY_SIZE(uart_pins), "uart-0");
	imx31_add_imx_uart0(&uart_pdata);
}

static void mx31ads_expio_irq_handler(u32 irq, struct irq_desc *desc)
{
	u32 imr_val;
	u32 int_valid;
	u32 expio_irq;

	imr_val = __raw_readw(PBC_INTMASK_SET_REG);
	int_valid = __raw_readw(PBC_INTSTATUS_REG) & imr_val;

	expio_irq = 0;
	for (; int_valid != 0; int_valid >>= 1, expio_irq++) {
		if ((int_valid & 1) == 0)
			continue;

		generic_handle_irq(irq_find_mapping(domain, expio_irq));
	}
}

/*
 * Disable an expio pin's interrupt by setting the bit in the imr.
 * @param d	an expio virtual irq description
 */
static void expio_mask_irq(struct irq_data *d)
{
	u32 expio = d->hwirq;
	/* mask the interrupt */
	__raw_writew(1 << expio, PBC_INTMASK_CLEAR_REG);
	__raw_readw(PBC_INTMASK_CLEAR_REG);
}

/*
 * Acknowledge an expanded io pin's interrupt by clearing the bit in the isr.
 * @param d	an expio virtual irq description
 */
static void expio_ack_irq(struct irq_data *d)
{
	u32 expio = d->hwirq;
	/* clear the interrupt status */
	__raw_writew(1 << expio, PBC_INTSTATUS_REG);
}

/*
 * Enable a expio pin's interrupt by clearing the bit in the imr.
 * @param d	an expio virtual irq description
 */
static void expio_unmask_irq(struct irq_data *d)
{
	u32 expio = d->hwirq;
	/* unmask the interrupt */
	__raw_writew(1 << expio, PBC_INTMASK_SET_REG);
}

static struct irq_chip expio_irq_chip = {
	.name = "EXPIO(CPLD)",
	.irq_ack = expio_ack_irq,
	.irq_mask = expio_mask_irq,
	.irq_unmask = expio_unmask_irq,
};

static void __init mx31ads_init_expio(void)
{
	int irq_base;
	int i, irq;

	printk(KERN_INFO "MX31ADS EXPIO(CPLD) hardware\n");

	/*
	 * Configure INT line as GPIO input
	 */
	mxc_iomux_alloc_pin(IOMUX_MODE(MX31_PIN_GPIO1_4, IOMUX_CONFIG_GPIO), "expio");

	/* disable the interrupt and clear the status */
	__raw_writew(0xFFFF, PBC_INTMASK_CLEAR_REG);
	__raw_writew(0xFFFF, PBC_INTSTATUS_REG);

	irq_base = irq_alloc_descs(-1, 0, MXC_MAX_EXP_IO_LINES, numa_node_id());
	WARN_ON(irq_base < 0);

	domain = irq_domain_add_legacy(NULL, MXC_MAX_EXP_IO_LINES, irq_base, 0,
				       &irq_domain_simple_ops, NULL);
	WARN_ON(!domain);

	for (i = irq_base; i < irq_base + MXC_MAX_EXP_IO_LINES; i++) {
		irq_set_chip_and_handler(i, &expio_irq_chip, handle_level_irq);
		set_irq_flags(i, IRQF_VALID);
	}
	irq = gpio_to_irq(IOMUX_TO_GPIO(MX31_PIN_GPIO1_4));
	irq_set_irq_type(irq, IRQ_TYPE_LEVEL_HIGH);
	irq_set_chained_handler(irq, mx31ads_expio_irq_handler);
}

#ifdef CONFIG_MACH_MX31ADS_WM1133_EV1
/* This section defines setup for the Wolfson Microelectronics
 * 1133-EV1 PMU/audio board.  When other PMU boards are supported the
 * regulator definitions may be shared with them, but for now they can
 * only be used with this board so would generate warnings about
 * unused statics and some of the configuration is specific to this
 * module.
 */

/* CPU */
static struct regulator_consumer_supply sw1a_consumers[] = {
	{
		.supply = "cpu_vcc",
	}
};

static struct regulator_init_data sw1a_data = {
	.constraints = {
		.name = "SW1A",
		.min_uV = 1275000,
		.max_uV = 1600000,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE |
				  REGULATOR_CHANGE_MODE,
		.valid_modes_mask = REGULATOR_MODE_NORMAL |
				    REGULATOR_MODE_FAST,
		.state_mem = {
			 .uV = 1400000,
			 .mode = REGULATOR_MODE_NORMAL,
			 .enabled = 1,
		 },
		.initial_state = PM_SUSPEND_MEM,
		.always_on = 1,
		.boot_on = 1,
	},
	.num_consumer_supplies = ARRAY_SIZE(sw1a_consumers),
	.consumer_supplies = sw1a_consumers,
};

/* System IO - High */
static struct regulator_init_data viohi_data = {
	.constraints = {
		.name = "VIOHO",
		.min_uV = 2800000,
		.max_uV = 2800000,
		.state_mem = {
			 .uV = 2800000,
			 .mode = REGULATOR_MODE_NORMAL,
			 .enabled = 1,
		 },
		.initial_state = PM_SUSPEND_MEM,
		.always_on = 1,
		.boot_on = 1,
	},
};

/* System IO - Low */
static struct regulator_init_data violo_data = {
	.constraints = {
		.name = "VIOLO",
		.min_uV = 1800000,
		.max_uV = 1800000,
		.state_mem = {
			 .uV = 1800000,
			 .mode = REGULATOR_MODE_NORMAL,
			 .enabled = 1,
		 },
		.initial_state = PM_SUSPEND_MEM,
		.always_on = 1,
		.boot_on = 1,
	},
};

/* DDR RAM */
static struct regulator_init_data sw2a_data = {
	.constraints = {
		.name = "SW2A",
		.min_uV = 1800000,
		.max_uV = 1800000,
		.valid_modes_mask = REGULATOR_MODE_NORMAL,
		.state_mem = {
			 .uV = 1800000,
			 .mode = REGULATOR_MODE_NORMAL,
			 .enabled = 1,
		 },
		.state_disk = {
			 .mode = REGULATOR_MODE_NORMAL,
			 .enabled = 0,
		 },
		.always_on = 1,
		.boot_on = 1,
		.initial_state = PM_SUSPEND_MEM,
	},
};

static struct regulator_init_data ldo1_data = {
	.constraints = {
		.name = "VCAM/VMMC1/VMMC2",
		.min_uV = 2800000,
		.max_uV = 2800000,
		.valid_modes_mask = REGULATOR_MODE_NORMAL,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		.apply_uV = 1,
	},
};

static struct regulator_consumer_supply ldo2_consumers[] = {
	{ .supply = "AVDD", .dev_name = "1-001a" },
	{ .supply = "HPVDD", .dev_name = "1-001a" },
};

/* CODEC and SIM */
static struct regulator_init_data ldo2_data = {
	.constraints = {
		.name = "VESIM/VSIM/AVDD",
		.min_uV = 3300000,
		.max_uV = 3300000,
		.valid_modes_mask = REGULATOR_MODE_NORMAL,
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
		.apply_uV = 1,
	},
	.num_consumer_supplies = ARRAY_SIZE(ldo2_consumers),
	.consumer_supplies = ldo2_consumers,
};

/* General */
static struct regulator_init_data vdig_data = {
	.constraints = {
		.name = "VDIG",
		.min_uV = 1500000,
		.max_uV = 1500000,
		.valid_modes_mask = REGULATOR_MODE_NORMAL,
		.apply_uV = 1,
		.always_on = 1,
		.boot_on = 1,
	},
};

/* Tranceivers */
static struct regulator_init_data ldo4_data = {
	.constraints = {
		.name = "VRF1/CVDD_2.775",
		.min_uV = 2500000,
		.max_uV = 2500000,
		.valid_modes_mask = REGULATOR_MODE_NORMAL,
		.apply_uV = 1,
		.always_on = 1,
		.boot_on = 1,
	},
};

static struct wm8350_led_platform_data wm8350_led_data = {
	.name            = "wm8350:white",
	.default_trigger = "heartbeat",
	.max_uA          = 27899,
};

static struct wm8350_audio_platform_data imx32ads_wm8350_setup = {
	.vmid_discharge_msecs = 1000,
	.drain_msecs = 30,
	.cap_discharge_msecs = 700,
	.vmid_charge_msecs = 700,
	.vmid_s_curve = WM8350_S_CURVE_SLOW,
	.dis_out4 = WM8350_DISCHARGE_SLOW,
	.dis_out3 = WM8350_DISCHARGE_SLOW,
	.dis_out2 = WM8350_DISCHARGE_SLOW,
	.dis_out1 = WM8350_DISCHARGE_SLOW,
	.vroi_out4 = WM8350_TIE_OFF_500R,
	.vroi_out3 = WM8350_TIE_OFF_500R,
	.vroi_out2 = WM8350_TIE_OFF_500R,
	.vroi_out1 = WM8350_TIE_OFF_500R,
	.vroi_enable = 0,
	.codec_current_on = WM8350_CODEC_ISEL_1_0,
	.codec_current_standby = WM8350_CODEC_ISEL_0_5,
	.codec_current_charge = WM8350_CODEC_ISEL_1_5,
};

static int mx31_wm8350_init(struct wm8350 *wm8350)
{
	wm8350_gpio_config(wm8350, 0, WM8350_GPIO_DIR_IN,
			   WM8350_GPIO0_PWR_ON_IN, WM8350_GPIO_ACTIVE_LOW,
			   WM8350_GPIO_PULL_UP, WM8350_GPIO_INVERT_OFF,
			   WM8350_GPIO_DEBOUNCE_ON);

	wm8350_gpio_config(wm8350, 3, WM8350_GPIO_DIR_IN,
			   WM8350_GPIO3_PWR_OFF_IN, WM8350_GPIO_ACTIVE_HIGH,
			   WM8350_GPIO_PULL_DOWN, WM8350_GPIO_INVERT_OFF,
			   WM8350_GPIO_DEBOUNCE_ON);

	wm8350_gpio_config(wm8350, 4, WM8350_GPIO_DIR_IN,
			   WM8350_GPIO4_MR_IN, WM8350_GPIO_ACTIVE_HIGH,
			   WM8350_GPIO_PULL_DOWN, WM8350_GPIO_INVERT_OFF,
			   WM8350_GPIO_DEBOUNCE_OFF);

	wm8350_gpio_config(wm8350, 7, WM8350_GPIO_DIR_IN,
			   WM8350_GPIO7_HIBERNATE_IN, WM8350_GPIO_ACTIVE_HIGH,
			   WM8350_GPIO_PULL_DOWN, WM8350_GPIO_INVERT_OFF,
			   WM8350_GPIO_DEBOUNCE_OFF);

	wm8350_gpio_config(wm8350, 6, WM8350_GPIO_DIR_OUT,
			   WM8350_GPIO6_SDOUT_OUT, WM8350_GPIO_ACTIVE_HIGH,
			   WM8350_GPIO_PULL_NONE, WM8350_GPIO_INVERT_OFF,
			   WM8350_GPIO_DEBOUNCE_OFF);

	wm8350_gpio_config(wm8350, 8, WM8350_GPIO_DIR_OUT,
			   WM8350_GPIO8_VCC_FAULT_OUT, WM8350_GPIO_ACTIVE_LOW,
			   WM8350_GPIO_PULL_NONE, WM8350_GPIO_INVERT_OFF,
			   WM8350_GPIO_DEBOUNCE_OFF);

	wm8350_gpio_config(wm8350, 9, WM8350_GPIO_DIR_OUT,
			   WM8350_GPIO9_BATT_FAULT_OUT, WM8350_GPIO_ACTIVE_LOW,
			   WM8350_GPIO_PULL_NONE, WM8350_GPIO_INVERT_OFF,
			   WM8350_GPIO_DEBOUNCE_OFF);

	wm8350_register_regulator(wm8350, WM8350_DCDC_1, &sw1a_data);
	wm8350_register_regulator(wm8350, WM8350_DCDC_3, &viohi_data);
	wm8350_register_regulator(wm8350, WM8350_DCDC_4, &violo_data);
	wm8350_register_regulator(wm8350, WM8350_DCDC_6, &sw2a_data);
	wm8350_register_regulator(wm8350, WM8350_LDO_1, &ldo1_data);
	wm8350_register_regulator(wm8350, WM8350_LDO_2, &ldo2_data);
	wm8350_register_regulator(wm8350, WM8350_LDO_3, &vdig_data);
	wm8350_register_regulator(wm8350, WM8350_LDO_4, &ldo4_data);

	/* LEDs */
	wm8350_dcdc_set_slot(wm8350, WM8350_DCDC_5, 1, 1,
			     WM8350_DC5_ERRACT_SHUTDOWN_CONV);
	wm8350_isink_set_flash(wm8350, WM8350_ISINK_A,
			       WM8350_ISINK_FLASH_DISABLE,
			       WM8350_ISINK_FLASH_TRIG_BIT,
			       WM8350_ISINK_FLASH_DUR_32MS,
			       WM8350_ISINK_FLASH_ON_INSTANT,
			       WM8350_ISINK_FLASH_OFF_INSTANT,
			       WM8350_ISINK_FLASH_MODE_EN);
	wm8350_dcdc25_set_mode(wm8350, WM8350_DCDC_5,
			       WM8350_ISINK_MODE_BOOST,
			       WM8350_ISINK_ILIM_NORMAL,
			       WM8350_DC5_RMP_20V,
			       WM8350_DC5_FBSRC_ISINKA);
	wm8350_register_led(wm8350, 0, WM8350_DCDC_5, WM8350_ISINK_A,
			    &wm8350_led_data);

	wm8350->codec.platform_data = &imx32ads_wm8350_setup;

	regulator_has_full_constraints();

	return 0;
}

static struct wm8350_platform_data __initdata mx31_wm8350_pdata = {
	.init = mx31_wm8350_init,
};
#endif

static struct i2c_board_info __initdata mx31ads_i2c1_devices[] = {
#ifdef CONFIG_MACH_MX31ADS_WM1133_EV1
	{
		I2C_BOARD_INFO("wm8350", 0x1a),
		.platform_data = &mx31_wm8350_pdata,
		/* irq number is run-time assigned */
	},
#endif
};

static void __init mxc_init_i2c(void)
{
#ifdef CONFIG_MACH_MX31ADS_WM1133_EV1
	mx31ads_i2c1_devices[0].irq =
			gpio_to_irq(IOMUX_TO_GPIO(MX31_PIN_GPIO1_3));
#endif
	i2c_register_board_info(1, mx31ads_i2c1_devices,
				ARRAY_SIZE(mx31ads_i2c1_devices));

	mxc_iomux_mode(IOMUX_MODE(MX31_PIN_CSPI2_MOSI, IOMUX_CONFIG_ALT1));
	mxc_iomux_mode(IOMUX_MODE(MX31_PIN_CSPI2_MISO, IOMUX_CONFIG_ALT1));

	imx31_add_imx_i2c1(NULL);
}

static unsigned int ssi_pins[] = {
	MX31_PIN_SFS5__SFS5,
	MX31_PIN_SCK5__SCK5,
	MX31_PIN_SRXD5__SRXD5,
	MX31_PIN_STXD5__STXD5,
};

static void __init mxc_init_audio(void)
{
	imx31_add_imx_ssi(0, NULL);
	mxc_iomux_setup_multiple_pins(ssi_pins, ARRAY_SIZE(ssi_pins), "ssi");
}

/*
 * Static mappings, starting from the CS4 start address up to the start address
 * of the CS8900.
 */
static struct map_desc mx31ads_io_desc[] __initdata = {
	{
		.virtual	= (unsigned long)MX31_CS4_BASE_ADDR_VIRT,
		.pfn		= __phys_to_pfn(MX31_CS4_BASE_ADDR),
		.length		= CS4_CS8900_MMIO_START,
		.type		= MT_DEVICE
	},
};

static void __init mx31ads_map_io(void)
{
	mx31_map_io();
	iotable_init(mx31ads_io_desc, ARRAY_SIZE(mx31ads_io_desc));
}

static void __init mx31ads_init_irq(void)
{
	mx31_init_irq();
	mx31ads_init_expio();
}

static void __init mx31ads_init(void)
{
	imx31_soc_init();

	mxc_init_extuart();
	mxc_init_imx_uart();
	mxc_init_i2c();
	mxc_init_audio();
	mxc_init_ext_ethernet();
}

static void __init mx31ads_timer_init(void)
{
	mx31_clocks_init(26000000);
}

static struct sys_timer mx31ads_timer = {
	.init	= mx31ads_timer_init,
};

MACHINE_START(MX31ADS, "Freescale MX31ADS")
	/* Maintainer: Freescale Semiconductor, Inc. */
	.atag_offset = 0x100,
	.map_io = mx31ads_map_io,
	.init_early = imx31_init_early,
	.init_irq = mx31ads_init_irq,
	.handle_irq = imx31_handle_irq,
	.timer = &mx31ads_timer,
	.init_machine = mx31ads_init,
	.restart	= mxc_restart,
MACHINE_END
