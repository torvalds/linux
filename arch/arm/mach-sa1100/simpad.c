/*
 * linux/arch/arm/mach-sa1100/simpad.c
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/tty.h>
#include <linux/proc_fs.h>
#include <linux/string.h> 
#include <linux/pm.h>
#include <linux/platform_device.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/io.h>
#include <linux/gpio.h>

#include <asm/irq.h>
#include <mach/hardware.h>
#include <asm/setup.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/flash.h>
#include <asm/mach/map.h>
#include <asm/mach/serial_sa1100.h>
#include <mach/mcp.h>
#include <mach/simpad.h>

#include <linux/serial_core.h>
#include <linux/ioport.h>
#include <linux/input.h>
#include <linux/gpio_keys.h>
#include <linux/leds.h>
#include <linux/i2c-gpio.h>

#include "generic.h"

/*
 * CS3 support
 */

static long cs3_shadow;
static spinlock_t cs3_lock;
static struct gpio_chip cs3_gpio;

long simpad_get_cs3_ro(void)
{
	return readl(CS3_BASE);
}
EXPORT_SYMBOL(simpad_get_cs3_ro);

long simpad_get_cs3_shadow(void)
{
	return cs3_shadow;
}
EXPORT_SYMBOL(simpad_get_cs3_shadow);

static void __simpad_write_cs3(void)
{
	writel(cs3_shadow, CS3_BASE);
}

void simpad_set_cs3_bit(int value)
{
	unsigned long flags;

	spin_lock_irqsave(&cs3_lock, flags);
	cs3_shadow |= value;
	__simpad_write_cs3();
	spin_unlock_irqrestore(&cs3_lock, flags);
}
EXPORT_SYMBOL(simpad_set_cs3_bit);

void simpad_clear_cs3_bit(int value)
{
	unsigned long flags;

	spin_lock_irqsave(&cs3_lock, flags);
	cs3_shadow &= ~value;
	__simpad_write_cs3();
	spin_unlock_irqrestore(&cs3_lock, flags);
}
EXPORT_SYMBOL(simpad_clear_cs3_bit);

static void cs3_gpio_set(struct gpio_chip *chip, unsigned offset, int value)
{
	if (offset > 15)
		return;
	if (value)
		simpad_set_cs3_bit(1 << offset);
	else
		simpad_clear_cs3_bit(1 << offset);
};

static int cs3_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	if (offset > 15)
		return simpad_get_cs3_ro() & (1 << (offset - 16));
	return simpad_get_cs3_shadow() & (1 << offset);
};

static int cs3_gpio_direction_input(struct gpio_chip *chip, unsigned offset)
{
	if (offset > 15)
		return 0;
	return -EINVAL;
};

static int cs3_gpio_direction_output(struct gpio_chip *chip, unsigned offset,
	int value)
{
	if (offset > 15)
		return -EINVAL;
	cs3_gpio_set(chip, offset, value);
	return 0;
};

static struct map_desc simpad_io_desc[] __initdata = {
	{	/* MQ200 */
		.virtual	=  0xf2800000,
		.pfn		= __phys_to_pfn(0x4b800000),
		.length		= 0x00800000,
		.type		= MT_DEVICE
	}, {	/* Simpad CS3 */
		.virtual	= CS3_BASE,
		.pfn		= __phys_to_pfn(SA1100_CS3_PHYS),
		.length		= 0x00100000,
		.type		= MT_DEVICE
	},
};


static void simpad_uart_pm(struct uart_port *port, u_int state, u_int oldstate)
{
	if (port->mapbase == (u_int)&Ser1UTCR0) {
		if (state)
		{
			simpad_clear_cs3_bit(RS232_ON);
			simpad_clear_cs3_bit(DECT_POWER_ON);
		}else
		{
			simpad_set_cs3_bit(RS232_ON);
			simpad_set_cs3_bit(DECT_POWER_ON);
		}
	}
}

static struct sa1100_port_fns simpad_port_fns __initdata = {
	.pm	   = simpad_uart_pm,
};


static struct mtd_partition simpad_partitions[] = {
	{
		.name       = "SIMpad boot firmware",
		.size       = 0x00080000,
		.offset     = 0,
		.mask_flags = MTD_WRITEABLE,
	}, {
		.name       = "SIMpad kernel",
		.size       = 0x0010000,
		.offset     = MTDPART_OFS_APPEND,
	}, {
		.name       = "SIMpad root jffs2",
		.size       = MTDPART_SIZ_FULL,
		.offset     = MTDPART_OFS_APPEND,
	}
};

static struct flash_platform_data simpad_flash_data = {
	.map_name    = "cfi_probe",
	.parts       = simpad_partitions,
	.nr_parts    = ARRAY_SIZE(simpad_partitions),
};


static struct resource simpad_flash_resources [] = {
	{
		.start     = SA1100_CS0_PHYS,
		.end       = SA1100_CS0_PHYS + SZ_16M -1,
		.flags     = IORESOURCE_MEM,
	}, {
		.start     = SA1100_CS1_PHYS,
		.end       = SA1100_CS1_PHYS + SZ_16M -1,
		.flags     = IORESOURCE_MEM,
	}
};

static struct mcp_plat_data simpad_mcp_data = {
	.mccr0		= MCCR0_ADM,
	.sclk_rate	= 11981000,
	.gpio_base	= SIMPAD_UCB1X00_GPIO_BASE,
};



static void __init simpad_map_io(void)
{
	sa1100_map_io();

	iotable_init(simpad_io_desc, ARRAY_SIZE(simpad_io_desc));

	/* Initialize CS3 */
	cs3_shadow = (EN1 | EN0 | LED2_ON | DISPLAY_ON |
		RS232_ON | ENABLE_5V | RESET_SIMCARD | DECT_POWER_ON);
	__simpad_write_cs3(); /* Spinlocks not yet initialized */

        sa1100_register_uart_fns(&simpad_port_fns);
	sa1100_register_uart(0, 3);  /* serial interface */
	sa1100_register_uart(1, 1);  /* DECT             */

	// Reassign UART 1 pins
	GAFR |= GPIO_UART_TXD | GPIO_UART_RXD;
	GPDR |= GPIO_UART_TXD | GPIO_LDD13 | GPIO_LDD15;
	GPDR &= ~GPIO_UART_RXD;
	PPAR |= PPAR_UPR;

	/*
	 * Set up registers for sleep mode.
	 */


	PWER = PWER_GPIO0| PWER_RTC;
	PGSR = 0x818;
	PCFR = 0;
	PSDR = 0;

}

static void simpad_power_off(void)
{
	local_irq_disable();
	cs3_shadow = SD_MEDIAQ;
	__simpad_write_cs3(); /* Bypass spinlock here */

	/* disable internal oscillator, float CS lines */
	PCFR = (PCFR_OPDE | PCFR_FP | PCFR_FS);
	/* enable wake-up on GPIO0 */
	PWER = GFER = GRER = PWER_GPIO0;
	/*
	 * set scratchpad to zero, just in case it is used as a
	 * restart address by the bootloader.
	 */
	PSPR = 0;
	PGSR = 0;
	/* enter sleep mode */
	PMCR = PMCR_SF;
	while(1);

	local_irq_enable(); /* we won't ever call it */


}

/*
 * gpio_keys
*/

static struct gpio_keys_button simpad_button_table[] = {
	{ KEY_POWER, IRQ_GPIO_POWER_BUTTON, 1, "power button" },
};

static struct gpio_keys_platform_data simpad_keys_data = {
	.buttons = simpad_button_table,
	.nbuttons = ARRAY_SIZE(simpad_button_table),
};

static struct platform_device simpad_keys = {
	.name = "gpio-keys",
	.dev = {
		.platform_data = &simpad_keys_data,
	},
};

static struct gpio_keys_button simpad_polled_button_table[] = {
	{ KEY_PROG1, SIMPAD_UCB1X00_GPIO_PROG1, 1, "prog1 button" },
	{ KEY_PROG2, SIMPAD_UCB1X00_GPIO_PROG2, 1, "prog2 button" },
	{ KEY_UP,    SIMPAD_UCB1X00_GPIO_UP,    1, "up button" },
	{ KEY_DOWN,  SIMPAD_UCB1X00_GPIO_DOWN,  1, "down button" },
	{ KEY_LEFT,  SIMPAD_UCB1X00_GPIO_LEFT,  1, "left button" },
	{ KEY_RIGHT, SIMPAD_UCB1X00_GPIO_RIGHT, 1, "right button" },
};

static struct gpio_keys_platform_data simpad_polled_keys_data = {
	.buttons = simpad_polled_button_table,
	.nbuttons = ARRAY_SIZE(simpad_polled_button_table),
	.poll_interval = 50,
};

static struct platform_device simpad_polled_keys = {
	.name = "gpio-keys-polled",
	.dev = {
		.platform_data = &simpad_polled_keys_data,
	},
};

/*
 * GPIO LEDs
 */

static struct gpio_led simpad_leds[] = {
	{
		.name = "simpad:power",
		.gpio = SIMPAD_CS3_LED2_ON,
		.active_low = 0,
		.default_trigger = "default-on",
	},
};

static struct gpio_led_platform_data simpad_led_data = {
	.num_leds = ARRAY_SIZE(simpad_leds),
	.leds = simpad_leds,
};

static struct platform_device simpad_gpio_leds = {
	.name = "leds-gpio",
	.id = 0,
	.dev = {
		.platform_data = &simpad_led_data,
	},
};

/*
 * i2c
 */
static struct i2c_gpio_platform_data simpad_i2c_data = {
	.sda_pin = GPIO_GPIO21,
	.scl_pin = GPIO_GPIO25,
	.udelay = 10,
	.timeout = HZ,
};

static struct platform_device simpad_i2c = {
	.name = "i2c-gpio",
	.id = 0,
	.dev = {
		.platform_data = &simpad_i2c_data,
	},
};

/*
 * MediaQ Video Device
 */
static struct platform_device simpad_mq200fb = {
	.name = "simpad-mq200",
	.id   = 0,
};

static struct platform_device *devices[] __initdata = {
	&simpad_keys,
	&simpad_polled_keys,
	&simpad_mq200fb,
	&simpad_gpio_leds,
	&simpad_i2c,
};



static int __init simpad_init(void)
{
	int ret;

	spin_lock_init(&cs3_lock);

	cs3_gpio.label = "simpad_cs3";
	cs3_gpio.base = SIMPAD_CS3_GPIO_BASE;
	cs3_gpio.ngpio = 24;
	cs3_gpio.set = cs3_gpio_set;
	cs3_gpio.get = cs3_gpio_get;
	cs3_gpio.direction_input = cs3_gpio_direction_input;
	cs3_gpio.direction_output = cs3_gpio_direction_output;
	ret = gpiochip_add(&cs3_gpio);
	if (ret)
		printk(KERN_WARNING "simpad: Unable to register cs3 GPIO device");

	pm_power_off = simpad_power_off;

	sa11x0_register_mtd(&simpad_flash_data, simpad_flash_resources,
			      ARRAY_SIZE(simpad_flash_resources));
	sa11x0_register_mcp(&simpad_mcp_data);

	ret = platform_add_devices(devices, ARRAY_SIZE(devices));
	if(ret)
		printk(KERN_WARNING "simpad: Unable to register mq200 framebuffer device");

	return 0;
}

arch_initcall(simpad_init);


MACHINE_START(SIMPAD, "Simpad")
	/* Maintainer: Holger Freyther */
	.atag_offset	= 0x100,
	.map_io		= simpad_map_io,
	.init_irq	= sa1100_init_irq,
	.timer		= &sa1100_timer,
	.restart	= sa11x0_restart,
MACHINE_END
