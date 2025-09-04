// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2020 Intel Corporation */

#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/gpio/driver.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>

#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>

#include <linux/platform_device.h>

#include "core.h"
#include "pinmux.h"

/* GPIO data registers' offsets */
#define KEEMBAY_GPIO_DATA_OUT		0x000
#define KEEMBAY_GPIO_DATA_IN		0x020
#define KEEMBAY_GPIO_DATA_IN_RAW	0x040
#define KEEMBAY_GPIO_DATA_HIGH		0x060
#define KEEMBAY_GPIO_DATA_LOW		0x080

/* GPIO Interrupt and mode registers' offsets */
#define KEEMBAY_GPIO_INT_CFG		0x000
#define KEEMBAY_GPIO_MODE		0x070

/* GPIO mode register bit fields */
#define KEEMBAY_GPIO_MODE_PULLUP_MASK	GENMASK(13, 12)
#define KEEMBAY_GPIO_MODE_DRIVE_MASK	GENMASK(8, 7)
#define KEEMBAY_GPIO_MODE_INV_MASK	GENMASK(5, 4)
#define KEEMBAY_GPIO_MODE_SELECT_MASK	GENMASK(2, 0)
#define KEEMBAY_GPIO_MODE_DIR_OVR	BIT(15)
#define KEEMBAY_GPIO_MODE_REN		BIT(11)
#define KEEMBAY_GPIO_MODE_SCHMITT_EN	BIT(10)
#define KEEMBAY_GPIO_MODE_SLEW_RATE	BIT(9)
#define KEEMBAY_GPIO_IRQ_ENABLE		BIT(7)
#define KEEMBAY_GPIO_MODE_DIR		BIT(3)
#define KEEMBAY_GPIO_MODE_DEFAULT	0x7
#define KEEMBAY_GPIO_MODE_INV_VAL	0x3

#define KEEMBAY_GPIO_DISABLE		0
#define KEEMBAY_GPIO_PULL_UP		1
#define KEEMBAY_GPIO_PULL_DOWN		2
#define KEEMBAY_GPIO_BUS_HOLD		3
#define KEEMBAY_GPIO_NUM_IRQ		8
#define KEEMBAY_GPIO_MAX_PER_IRQ	4
#define KEEMBAY_GPIO_MAX_PER_REG	32
#define KEEMBAY_GPIO_MIN_STRENGTH	2
#define KEEMBAY_GPIO_MAX_STRENGTH	12
#define KEEMBAY_GPIO_SENSE_LOW		(IRQ_TYPE_LEVEL_LOW | IRQ_TYPE_EDGE_FALLING)

/* GPIO reg address calculation */
#define KEEMBAY_GPIO_REG_OFFSET(pin)	((pin) * 4)

/**
 * struct keembay_mux_desc - Mux properties of each GPIO pin
 * @mode: Pin mode when operating in this function
 * @name: Pin function name
 */
struct keembay_mux_desc {
	u8 mode;
	const char *name;
};

#define KEEMBAY_PIN_DESC(pin_number, pin_name, ...) {	\
	.number = pin_number,				\
	.name = pin_name,				\
	.drv_data = &(struct keembay_mux_desc[]) {	\
		    __VA_ARGS__, { } },			\
}							\

#define KEEMBAY_MUX(pin_mode, pin_function) {		\
	.mode = pin_mode,				\
	.name = pin_function,				\
}							\

/**
 * struct keembay_gpio_irq - Config of each GPIO Interrupt sources
 * @source: Interrupt source number (0 - 7)
 * @line: Actual Interrupt line number
 * @pins: Array of GPIO pins using this Interrupt line
 * @trigger: Interrupt trigger type for this line
 * @num_share: Number of pins currently using this Interrupt line
 */
struct keembay_gpio_irq {
	unsigned int source;
	unsigned int line;
	unsigned int pins[KEEMBAY_GPIO_MAX_PER_IRQ];
	unsigned int trigger;
	unsigned int num_share;
};

/**
 * struct keembay_pinctrl - Intel Keembay pinctrl structure
 * @pctrl: Pointer to the pin controller device
 * @base0: First register base address
 * @base1: Second register base address
 * @dev: Pointer to the device structure
 * @chip: GPIO chip used by this pin controller
 * @soc: Pin control configuration data based on SoC
 * @lock: Spinlock to protect various gpio config register access
 * @ngroups: Number of pin groups available
 * @nfuncs: Number of pin functions available
 * @npins: Number of GPIO pins available
 * @irq: Store Interrupt source
 * @max_gpios_level_type: Store max level trigger type
 * @max_gpios_edge_type: Store max edge trigger type
 */
struct keembay_pinctrl {
	struct pinctrl_dev *pctrl;
	void __iomem *base0;
	void __iomem *base1;
	struct device *dev;
	struct gpio_chip chip;
	const struct keembay_pin_soc *soc;
	raw_spinlock_t lock;
	unsigned int ngroups;
	unsigned int nfuncs;
	unsigned int npins;
	struct keembay_gpio_irq irq[KEEMBAY_GPIO_NUM_IRQ];
	int max_gpios_level_type;
	int max_gpios_edge_type;
};

/**
 * struct keembay_pin_soc - Pin control config data based on SoC
 * @pins: Pin description structure
 */
struct keembay_pin_soc {
	const struct pinctrl_pin_desc *pins;
};

struct keembay_pinfunction {
	struct pinfunction func;
	u8 mux_mode;
};

static const struct pinctrl_pin_desc keembay_pins[] = {
	KEEMBAY_PIN_DESC(0, "GPIO0",
			 KEEMBAY_MUX(0x0, "I2S0_M0"),
			 KEEMBAY_MUX(0x1, "SD0_M1"),
			 KEEMBAY_MUX(0x2, "SLVDS0_M2"),
			 KEEMBAY_MUX(0x3, "I2C0_M3"),
			 KEEMBAY_MUX(0x4, "CAM_M4"),
			 KEEMBAY_MUX(0x5, "ETH_M5"),
			 KEEMBAY_MUX(0x6, "LCD_M6"),
			 KEEMBAY_MUX(0x7, "GPIO_M7")),
	KEEMBAY_PIN_DESC(1, "GPIO1",
			 KEEMBAY_MUX(0x0, "I2S0_M0"),
			 KEEMBAY_MUX(0x1, "SD0_M1"),
			 KEEMBAY_MUX(0x2, "SLVDS0_M2"),
			 KEEMBAY_MUX(0x3, "I2C0_M3"),
			 KEEMBAY_MUX(0x4, "CAM_M4"),
			 KEEMBAY_MUX(0x5, "ETH_M5"),
			 KEEMBAY_MUX(0x6, "LCD_M6"),
			 KEEMBAY_MUX(0x7, "GPIO_M7")),
	KEEMBAY_PIN_DESC(2, "GPIO2",
			 KEEMBAY_MUX(0x0, "I2S0_M0"),
			 KEEMBAY_MUX(0x1, "I2S0_M1"),
			 KEEMBAY_MUX(0x2, "SLVDS0_M2"),
			 KEEMBAY_MUX(0x3, "I2C1_M3"),
			 KEEMBAY_MUX(0x4, "CAM_M4"),
			 KEEMBAY_MUX(0x5, "ETH_M5"),
			 KEEMBAY_MUX(0x6, "LCD_M6"),
			 KEEMBAY_MUX(0x7, "GPIO_M7")),
	KEEMBAY_PIN_DESC(3, "GPIO3",
			 KEEMBAY_MUX(0x0, "I2S0_M0"),
			 KEEMBAY_MUX(0x1, "I2S0_M1"),
			 KEEMBAY_MUX(0x2, "SLVDS0_M2"),
			 KEEMBAY_MUX(0x3, "I2C1_M3"),
			 KEEMBAY_MUX(0x4, "CAM_M4"),
			 KEEMBAY_MUX(0x5, "ETH_M5"),
			 KEEMBAY_MUX(0x6, "LCD_M6"),
			 KEEMBAY_MUX(0x7, "GPIO_M7")),
	KEEMBAY_PIN_DESC(4, "GPIO4",
			 KEEMBAY_MUX(0x0, "I2S0_M0"),
			 KEEMBAY_MUX(0x1, "I2S0_M1"),
			 KEEMBAY_MUX(0x2, "SLVDS0_M2"),
			 KEEMBAY_MUX(0x3, "I2C2_M3"),
			 KEEMBAY_MUX(0x4, "CAM_M4"),
			 KEEMBAY_MUX(0x5, "ETH_M5"),
			 KEEMBAY_MUX(0x6, "LCD_M6"),
			 KEEMBAY_MUX(0x7, "GPIO_M7")),
	KEEMBAY_PIN_DESC(5, "GPIO5",
			 KEEMBAY_MUX(0x0, "I2S0_M0"),
			 KEEMBAY_MUX(0x1, "I2S0_M1"),
			 KEEMBAY_MUX(0x2, "SLVDS0_M2"),
			 KEEMBAY_MUX(0x3, "I2C2_M3"),
			 KEEMBAY_MUX(0x4, "CAM_M4"),
			 KEEMBAY_MUX(0x5, "ETH_M5"),
			 KEEMBAY_MUX(0x6, "LCD_M6"),
			 KEEMBAY_MUX(0x7, "GPIO_M7")),
	KEEMBAY_PIN_DESC(6, "GPIO6",
			 KEEMBAY_MUX(0x0, "I2S1_M0"),
			 KEEMBAY_MUX(0x1, "SD0_M1"),
			 KEEMBAY_MUX(0x2, "SLVDS0_M2"),
			 KEEMBAY_MUX(0x3, "I2C3_M3"),
			 KEEMBAY_MUX(0x4, "CAM_M4"),
			 KEEMBAY_MUX(0x5, "ETH_M5"),
			 KEEMBAY_MUX(0x6, "LCD_M6"),
			 KEEMBAY_MUX(0x7, "GPIO_M7")),
	KEEMBAY_PIN_DESC(7, "GPIO7",
			 KEEMBAY_MUX(0x0, "I2S1_M0"),
			 KEEMBAY_MUX(0x1, "SD0_M1"),
			 KEEMBAY_MUX(0x2, "SLVDS0_M2"),
			 KEEMBAY_MUX(0x3, "I2C3_M3"),
			 KEEMBAY_MUX(0x4, "CAM_M4"),
			 KEEMBAY_MUX(0x5, "ETH_M5"),
			 KEEMBAY_MUX(0x6, "LCD_M6"),
			 KEEMBAY_MUX(0x7, "GPIO_M7")),
	KEEMBAY_PIN_DESC(8, "GPIO8",
			 KEEMBAY_MUX(0x0, "I2S1_M0"),
			 KEEMBAY_MUX(0x1, "I2S1_M1"),
			 KEEMBAY_MUX(0x2, "SLVDS0_M2"),
			 KEEMBAY_MUX(0x3, "UART0_M3"),
			 KEEMBAY_MUX(0x4, "CAM_M4"),
			 KEEMBAY_MUX(0x5, "ETH_M5"),
			 KEEMBAY_MUX(0x6, "LCD_M6"),
			 KEEMBAY_MUX(0x7, "GPIO_M7")),
	KEEMBAY_PIN_DESC(9, "GPIO9",
			 KEEMBAY_MUX(0x0, "I2S1_M0"),
			 KEEMBAY_MUX(0x1, "I2S1_M1"),
			 KEEMBAY_MUX(0x2, "PWM_M2"),
			 KEEMBAY_MUX(0x3, "UART0_M3"),
			 KEEMBAY_MUX(0x4, "CAM_M4"),
			 KEEMBAY_MUX(0x5, "ETH_M5"),
			 KEEMBAY_MUX(0x6, "LCD_M6"),
			 KEEMBAY_MUX(0x7, "GPIO_M7")),
	KEEMBAY_PIN_DESC(10, "GPIO10",
			 KEEMBAY_MUX(0x0, "I2S2_M0"),
			 KEEMBAY_MUX(0x1, "SD0_M1"),
			 KEEMBAY_MUX(0x2, "PWM_M2"),
			 KEEMBAY_MUX(0x3, "UART0_M3"),
			 KEEMBAY_MUX(0x4, "CAM_M4"),
			 KEEMBAY_MUX(0x5, "ETH_M5"),
			 KEEMBAY_MUX(0x6, "LCD_M6"),
			 KEEMBAY_MUX(0x7, "GPIO_M7")),
	KEEMBAY_PIN_DESC(11, "GPIO11",
			 KEEMBAY_MUX(0x0, "I2S2_M0"),
			 KEEMBAY_MUX(0x1, "SD0_M1"),
			 KEEMBAY_MUX(0x2, "PWM_M2"),
			 KEEMBAY_MUX(0x3, "UART0_M3"),
			 KEEMBAY_MUX(0x4, "CAM_M4"),
			 KEEMBAY_MUX(0x5, "ETH_M5"),
			 KEEMBAY_MUX(0x6, "LCD_M6"),
			 KEEMBAY_MUX(0x7, "GPIO_M7")),
	KEEMBAY_PIN_DESC(12, "GPIO12",
			 KEEMBAY_MUX(0x0, "I2S2_M0"),
			 KEEMBAY_MUX(0x1, "I2S2_M1"),
			 KEEMBAY_MUX(0x2, "PWM_M2"),
			 KEEMBAY_MUX(0x3, "SPI0_M3"),
			 KEEMBAY_MUX(0x4, "CAM_M4"),
			 KEEMBAY_MUX(0x5, "ETH_M5"),
			 KEEMBAY_MUX(0x6, "LCD_M6"),
			 KEEMBAY_MUX(0x7, "GPIO_M7")),
	KEEMBAY_PIN_DESC(13, "GPIO13",
			 KEEMBAY_MUX(0x0, "I2S2_M0"),
			 KEEMBAY_MUX(0x1, "I2S2_M1"),
			 KEEMBAY_MUX(0x2, "PWM_M2"),
			 KEEMBAY_MUX(0x3, "SPI0_M3"),
			 KEEMBAY_MUX(0x4, "CAM_M4"),
			 KEEMBAY_MUX(0x5, "ETH_M5"),
			 KEEMBAY_MUX(0x6, "LCD_M6"),
			 KEEMBAY_MUX(0x7, "GPIO_M7")),
	KEEMBAY_PIN_DESC(14, "GPIO14",
			 KEEMBAY_MUX(0x0, "UART0_M0"),
			 KEEMBAY_MUX(0x1, "I2S3_M1"),
			 KEEMBAY_MUX(0x2, "PWM_M2"),
			 KEEMBAY_MUX(0x3, "SD1_M3"),
			 KEEMBAY_MUX(0x4, "CAM_M4"),
			 KEEMBAY_MUX(0x5, "ETH_M5"),
			 KEEMBAY_MUX(0x6, "LCD_M6"),
			 KEEMBAY_MUX(0x7, "GPIO_M7")),
	KEEMBAY_PIN_DESC(15, "GPIO15",
			 KEEMBAY_MUX(0x0, "UART0_M0"),
			 KEEMBAY_MUX(0x1, "I2S3_M1"),
			 KEEMBAY_MUX(0x2, "UART0_M2"),
			 KEEMBAY_MUX(0x3, "SD1_M3"),
			 KEEMBAY_MUX(0x4, "CAM_M4"),
			 KEEMBAY_MUX(0x5, "SPI1_M5"),
			 KEEMBAY_MUX(0x6, "LCD_M6"),
			 KEEMBAY_MUX(0x7, "GPIO_M7")),
	KEEMBAY_PIN_DESC(16, "GPIO16",
			 KEEMBAY_MUX(0x0, "UART0_M0"),
			 KEEMBAY_MUX(0x1, "I2S3_M1"),
			 KEEMBAY_MUX(0x2, "UART0_M2"),
			 KEEMBAY_MUX(0x3, "SD1_M3"),
			 KEEMBAY_MUX(0x4, "CAM_M4"),
			 KEEMBAY_MUX(0x5, "SPI1_M5"),
			 KEEMBAY_MUX(0x6, "LCD_M6"),
			 KEEMBAY_MUX(0x7, "GPIO_M7")),
	KEEMBAY_PIN_DESC(17, "GPIO17",
			 KEEMBAY_MUX(0x0, "UART0_M0"),
			 KEEMBAY_MUX(0x1, "I2S3_M1"),
			 KEEMBAY_MUX(0x2, "I2S3_M2"),
			 KEEMBAY_MUX(0x3, "SD1_M3"),
			 KEEMBAY_MUX(0x4, "CAM_M4"),
			 KEEMBAY_MUX(0x5, "SPI1_M5"),
			 KEEMBAY_MUX(0x6, "LCD_M6"),
			 KEEMBAY_MUX(0x7, "GPIO_M7")),
	KEEMBAY_PIN_DESC(18, "GPIO18",
			 KEEMBAY_MUX(0x0, "UART1_M0"),
			 KEEMBAY_MUX(0x1, "SPI0_M1"),
			 KEEMBAY_MUX(0x2, "I2S3_M2"),
			 KEEMBAY_MUX(0x3, "SD1_M3"),
			 KEEMBAY_MUX(0x4, "CAM_M4"),
			 KEEMBAY_MUX(0x5, "SPI1_M5"),
			 KEEMBAY_MUX(0x6, "LCD_M6"),
			 KEEMBAY_MUX(0x7, "GPIO_M7")),
	KEEMBAY_PIN_DESC(19, "GPIO19",
			 KEEMBAY_MUX(0x0, "UART1_M0"),
			 KEEMBAY_MUX(0x1, "LCD_M1"),
			 KEEMBAY_MUX(0x2, "DEBUG_M2"),
			 KEEMBAY_MUX(0x3, "SD1_M3"),
			 KEEMBAY_MUX(0x4, "CAM_M4"),
			 KEEMBAY_MUX(0x5, "SPI1_M5"),
			 KEEMBAY_MUX(0x6, "LCD_M6"),
			 KEEMBAY_MUX(0x7, "GPIO_M7")),
	KEEMBAY_PIN_DESC(20, "GPIO20",
			 KEEMBAY_MUX(0x0, "UART1_M0"),
			 KEEMBAY_MUX(0x1, "LCD_M1"),
			 KEEMBAY_MUX(0x2, "DEBUG_M2"),
			 KEEMBAY_MUX(0x3, "CPR_M3"),
			 KEEMBAY_MUX(0x4, "CAM_M4"),
			 KEEMBAY_MUX(0x5, "SPI1_M5"),
			 KEEMBAY_MUX(0x6, "SLVDS0_M6"),
			 KEEMBAY_MUX(0x7, "GPIO_M7")),
	KEEMBAY_PIN_DESC(21, "GPIO21",
			 KEEMBAY_MUX(0x0, "UART1_M0"),
			 KEEMBAY_MUX(0x1, "LCD_M1"),
			 KEEMBAY_MUX(0x2, "DEBUG_M2"),
			 KEEMBAY_MUX(0x3, "CPR_M3"),
			 KEEMBAY_MUX(0x4, "CAM_M4"),
			 KEEMBAY_MUX(0x5, "I3C0_M5"),
			 KEEMBAY_MUX(0x6, "SLVDS0_M6"),
			 KEEMBAY_MUX(0x7, "GPIO_M7")),
	KEEMBAY_PIN_DESC(22, "GPIO22",
			 KEEMBAY_MUX(0x0, "I2C0_M0"),
			 KEEMBAY_MUX(0x1, "UART2_M1"),
			 KEEMBAY_MUX(0x2, "DEBUG_M2"),
			 KEEMBAY_MUX(0x3, "CPR_M3"),
			 KEEMBAY_MUX(0x4, "CAM_M4"),
			 KEEMBAY_MUX(0x5, "I3C0_M5"),
			 KEEMBAY_MUX(0x6, "SLVDS0_M6"),
			 KEEMBAY_MUX(0x7, "GPIO_M7")),
	KEEMBAY_PIN_DESC(23, "GPIO23",
			 KEEMBAY_MUX(0x0, "I2C0_M0"),
			 KEEMBAY_MUX(0x1, "UART2_M1"),
			 KEEMBAY_MUX(0x2, "DEBUG_M2"),
			 KEEMBAY_MUX(0x3, "CPR_M3"),
			 KEEMBAY_MUX(0x4, "CAM_M4"),
			 KEEMBAY_MUX(0x5, "I3C1_M5"),
			 KEEMBAY_MUX(0x6, "SLVDS0_M6"),
			 KEEMBAY_MUX(0x7, "GPIO_M7")),
	KEEMBAY_PIN_DESC(24, "GPIO24",
			 KEEMBAY_MUX(0x0, "I2C1_M0"),
			 KEEMBAY_MUX(0x1, "UART2_M1"),
			 KEEMBAY_MUX(0x2, "DEBUG_M2"),
			 KEEMBAY_MUX(0x3, "CPR_M3"),
			 KEEMBAY_MUX(0x4, "CAM_M4"),
			 KEEMBAY_MUX(0x5, "I3C1_M5"),
			 KEEMBAY_MUX(0x6, "SLVDS0_M6"),
			 KEEMBAY_MUX(0x7, "GPIO_M7")),
	KEEMBAY_PIN_DESC(25, "GPIO25",
			 KEEMBAY_MUX(0x0, "I2C1_M0"),
			 KEEMBAY_MUX(0x1, "UART2_M1"),
			 KEEMBAY_MUX(0x2, "SPI0_M2"),
			 KEEMBAY_MUX(0x3, "CPR_M3"),
			 KEEMBAY_MUX(0x4, "CAM_M4"),
			 KEEMBAY_MUX(0x5, "I3C2_M5"),
			 KEEMBAY_MUX(0x6, "SLVDS0_M6"),
			 KEEMBAY_MUX(0x7, "GPIO_M7")),
	KEEMBAY_PIN_DESC(26, "GPIO26",
			 KEEMBAY_MUX(0x0, "SPI0_M0"),
			 KEEMBAY_MUX(0x1, "I2C2_M1"),
			 KEEMBAY_MUX(0x2, "UART0_M2"),
			 KEEMBAY_MUX(0x3, "DSU_M3"),
			 KEEMBAY_MUX(0x4, "CAM_M4"),
			 KEEMBAY_MUX(0x5, "I3C2_M5"),
			 KEEMBAY_MUX(0x6, "SLVDS0_M6"),
			 KEEMBAY_MUX(0x7, "GPIO_M7")),
	KEEMBAY_PIN_DESC(27, "GPIO27",
			 KEEMBAY_MUX(0x0, "SPI0_M0"),
			 KEEMBAY_MUX(0x1, "I2C2_M1"),
			 KEEMBAY_MUX(0x2, "UART0_M2"),
			 KEEMBAY_MUX(0x3, "DSU_M3"),
			 KEEMBAY_MUX(0x4, "CAM_M4"),
			 KEEMBAY_MUX(0x5, "I3C0_M5"),
			 KEEMBAY_MUX(0x6, "SLVDS0_M6"),
			 KEEMBAY_MUX(0x7, "GPIO_M7")),
	KEEMBAY_PIN_DESC(28, "GPIO28",
			 KEEMBAY_MUX(0x0, "SPI0_M0"),
			 KEEMBAY_MUX(0x1, "I2C3_M1"),
			 KEEMBAY_MUX(0x2, "UART0_M2"),
			 KEEMBAY_MUX(0x3, "PWM_M3"),
			 KEEMBAY_MUX(0x4, "CAM_M4"),
			 KEEMBAY_MUX(0x5, "I3C1_M5"),
			 KEEMBAY_MUX(0x6, "SLVDS0_M6"),
			 KEEMBAY_MUX(0x7, "GPIO_M7")),
	KEEMBAY_PIN_DESC(29, "GPIO29",
			 KEEMBAY_MUX(0x0, "SPI0_M0"),
			 KEEMBAY_MUX(0x1, "I2C3_M1"),
			 KEEMBAY_MUX(0x2, "UART0_M2"),
			 KEEMBAY_MUX(0x3, "PWM_M3"),
			 KEEMBAY_MUX(0x4, "CAM_M4"),
			 KEEMBAY_MUX(0x5, "I3C2_M5"),
			 KEEMBAY_MUX(0x6, "SLVDS1_M6"),
			 KEEMBAY_MUX(0x7, "GPIO_M7")),
	KEEMBAY_PIN_DESC(30, "GPIO30",
			 KEEMBAY_MUX(0x0, "SPI0_M0"),
			 KEEMBAY_MUX(0x1, "I2S0_M1"),
			 KEEMBAY_MUX(0x2, "I2C4_M2"),
			 KEEMBAY_MUX(0x3, "PWM_M3"),
			 KEEMBAY_MUX(0x4, "CAM_M4"),
			 KEEMBAY_MUX(0x5, "LCD_M5"),
			 KEEMBAY_MUX(0x6, "SLVDS1_M6"),
			 KEEMBAY_MUX(0x7, "GPIO_M7")),
	KEEMBAY_PIN_DESC(31, "GPIO31",
			 KEEMBAY_MUX(0x0, "SPI0_M0"),
			 KEEMBAY_MUX(0x1, "I2S0_M1"),
			 KEEMBAY_MUX(0x2, "I2C4_M2"),
			 KEEMBAY_MUX(0x3, "PWM_M3"),
			 KEEMBAY_MUX(0x4, "CAM_M4"),
			 KEEMBAY_MUX(0x5, "UART1_M5"),
			 KEEMBAY_MUX(0x6, "SLVDS1_M6"),
			 KEEMBAY_MUX(0x7, "GPIO_M7")),
	KEEMBAY_PIN_DESC(32, "GPIO32",
			 KEEMBAY_MUX(0x0, "SD0_M0"),
			 KEEMBAY_MUX(0x1, "SPI0_M1"),
			 KEEMBAY_MUX(0x2, "UART1_M2"),
			 KEEMBAY_MUX(0x3, "PWM_M3"),
			 KEEMBAY_MUX(0x4, "CAM_M4"),
			 KEEMBAY_MUX(0x5, "PCIE_M5"),
			 KEEMBAY_MUX(0x6, "SLVDS1_M6"),
			 KEEMBAY_MUX(0x7, "GPIO_M7")),
	KEEMBAY_PIN_DESC(33, "GPIO33",
			 KEEMBAY_MUX(0x0, "SD0_M0"),
			 KEEMBAY_MUX(0x1, "SPI0_M1"),
			 KEEMBAY_MUX(0x2, "UART1_M2"),
			 KEEMBAY_MUX(0x3, "PWM_M3"),
			 KEEMBAY_MUX(0x4, "CAM_M4"),
			 KEEMBAY_MUX(0x5, "PCIE_M5"),
			 KEEMBAY_MUX(0x6, "SLVDS1_M6"),
			 KEEMBAY_MUX(0x7, "GPIO_M7")),
	KEEMBAY_PIN_DESC(34, "GPIO34",
			 KEEMBAY_MUX(0x0, "SD0_M0"),
			 KEEMBAY_MUX(0x1, "SPI0_M1"),
			 KEEMBAY_MUX(0x2, "I2C0_M2"),
			 KEEMBAY_MUX(0x3, "UART1_M3"),
			 KEEMBAY_MUX(0x4, "CAM_M4"),
			 KEEMBAY_MUX(0x5, "I2S0_M5"),
			 KEEMBAY_MUX(0x6, "SLVDS1_M6"),
			 KEEMBAY_MUX(0x7, "GPIO_M7")),
	KEEMBAY_PIN_DESC(35, "GPIO35",
			 KEEMBAY_MUX(0x0, "SD0_M0"),
			 KEEMBAY_MUX(0x1, "PCIE_M1"),
			 KEEMBAY_MUX(0x2, "I2C0_M2"),
			 KEEMBAY_MUX(0x3, "UART1_M3"),
			 KEEMBAY_MUX(0x4, "CAM_M4"),
			 KEEMBAY_MUX(0x5, "I2S0_M5"),
			 KEEMBAY_MUX(0x6, "SLVDS1_M6"),
			 KEEMBAY_MUX(0x7, "GPIO_M7")),
	KEEMBAY_PIN_DESC(36, "GPIO36",
			 KEEMBAY_MUX(0x0, "SD0_M0"),
			 KEEMBAY_MUX(0x1, "SPI3_M1"),
			 KEEMBAY_MUX(0x2, "I2C1_M2"),
			 KEEMBAY_MUX(0x3, "DEBUG_M3"),
			 KEEMBAY_MUX(0x4, "CAM_M4"),
			 KEEMBAY_MUX(0x5, "I2S0_M5"),
			 KEEMBAY_MUX(0x6, "SLVDS1_M6"),
			 KEEMBAY_MUX(0x7, "GPIO_M7")),
	KEEMBAY_PIN_DESC(37, "GPIO37",
			 KEEMBAY_MUX(0x0, "SD0_M0"),
			 KEEMBAY_MUX(0x1, "SPI3_M1"),
			 KEEMBAY_MUX(0x2, "I2C1_M2"),
			 KEEMBAY_MUX(0x3, "DEBUG_M3"),
			 KEEMBAY_MUX(0x4, "CAM_M4"),
			 KEEMBAY_MUX(0x5, "I2S0_M5"),
			 KEEMBAY_MUX(0x6, "SLVDS1_M6"),
			 KEEMBAY_MUX(0x7, "GPIO_M7")),
	KEEMBAY_PIN_DESC(38, "GPIO38",
			 KEEMBAY_MUX(0x0, "I3C1_M0"),
			 KEEMBAY_MUX(0x1, "SPI3_M1"),
			 KEEMBAY_MUX(0x2, "UART3_M2"),
			 KEEMBAY_MUX(0x3, "DEBUG_M3"),
			 KEEMBAY_MUX(0x4, "CAM_M4"),
			 KEEMBAY_MUX(0x5, "LCD_M5"),
			 KEEMBAY_MUX(0x6, "I2C2_M6"),
			 KEEMBAY_MUX(0x7, "GPIO_M7")),
	KEEMBAY_PIN_DESC(39, "GPIO39",
			 KEEMBAY_MUX(0x0, "I3C1_M0"),
			 KEEMBAY_MUX(0x1, "SPI3_M1"),
			 KEEMBAY_MUX(0x2, "UART3_M2"),
			 KEEMBAY_MUX(0x3, "DEBUG_M3"),
			 KEEMBAY_MUX(0x4, "CAM_M4"),
			 KEEMBAY_MUX(0x5, "LCD_M5"),
			 KEEMBAY_MUX(0x6, "I2C2_M6"),
			 KEEMBAY_MUX(0x7, "GPIO_M7")),
	KEEMBAY_PIN_DESC(40, "GPIO40",
			 KEEMBAY_MUX(0x0, "I2S2_M0"),
			 KEEMBAY_MUX(0x1, "SPI3_M1"),
			 KEEMBAY_MUX(0x2, "UART3_M2"),
			 KEEMBAY_MUX(0x3, "DEBUG_M3"),
			 KEEMBAY_MUX(0x4, "CAM_M4"),
			 KEEMBAY_MUX(0x5, "LCD_M5"),
			 KEEMBAY_MUX(0x6, "I2C3_M6"),
			 KEEMBAY_MUX(0x7, "GPIO_M7")),
	KEEMBAY_PIN_DESC(41, "GPIO41",
			 KEEMBAY_MUX(0x0, "ETH_M0"),
			 KEEMBAY_MUX(0x1, "SPI3_M1"),
			 KEEMBAY_MUX(0x2, "SPI3_M2"),
			 KEEMBAY_MUX(0x3, "DEBUG_M3"),
			 KEEMBAY_MUX(0x4, "CAM_M4"),
			 KEEMBAY_MUX(0x5, "LCD_M5"),
			 KEEMBAY_MUX(0x6, "I2C3_M6"),
			 KEEMBAY_MUX(0x7, "GPIO_M7")),
	KEEMBAY_PIN_DESC(42, "GPIO42",
			 KEEMBAY_MUX(0x0, "ETH_M0"),
			 KEEMBAY_MUX(0x1, "SD1_M1"),
			 KEEMBAY_MUX(0x2, "SPI3_M2"),
			 KEEMBAY_MUX(0x3, "CPR_M3"),
			 KEEMBAY_MUX(0x4, "CAM_M4"),
			 KEEMBAY_MUX(0x5, "LCD_M5"),
			 KEEMBAY_MUX(0x6, "I2C4_M6"),
			 KEEMBAY_MUX(0x7, "GPIO_M7")),
	KEEMBAY_PIN_DESC(43, "GPIO43",
			 KEEMBAY_MUX(0x0, "ETH_M0"),
			 KEEMBAY_MUX(0x1, "SD1_M1"),
			 KEEMBAY_MUX(0x2, "SPI3_M2"),
			 KEEMBAY_MUX(0x3, "CPR_M3"),
			 KEEMBAY_MUX(0x4, "I2S0_M4"),
			 KEEMBAY_MUX(0x5, "LCD_M5"),
			 KEEMBAY_MUX(0x6, "I2C4_M6"),
			 KEEMBAY_MUX(0x7, "GPIO_M7")),
	KEEMBAY_PIN_DESC(44, "GPIO44",
			 KEEMBAY_MUX(0x0, "ETH_M0"),
			 KEEMBAY_MUX(0x1, "SD1_M1"),
			 KEEMBAY_MUX(0x2, "SPI0_M2"),
			 KEEMBAY_MUX(0x3, "CPR_M3"),
			 KEEMBAY_MUX(0x4, "I2S0_M4"),
			 KEEMBAY_MUX(0x5, "LCD_M5"),
			 KEEMBAY_MUX(0x6, "CAM_M6"),
			 KEEMBAY_MUX(0x7, "GPIO_M7")),
	KEEMBAY_PIN_DESC(45, "GPIO45",
			 KEEMBAY_MUX(0x0, "ETH_M0"),
			 KEEMBAY_MUX(0x1, "SD1_M1"),
			 KEEMBAY_MUX(0x2, "SPI0_M2"),
			 KEEMBAY_MUX(0x3, "CPR_M3"),
			 KEEMBAY_MUX(0x4, "I2S0_M4"),
			 KEEMBAY_MUX(0x5, "LCD_M5"),
			 KEEMBAY_MUX(0x6, "CAM_M6"),
			 KEEMBAY_MUX(0x7, "GPIO_M7")),
	KEEMBAY_PIN_DESC(46, "GPIO46",
			 KEEMBAY_MUX(0x0, "ETH_M0"),
			 KEEMBAY_MUX(0x1, "SD1_M1"),
			 KEEMBAY_MUX(0x2, "SPI0_M2"),
			 KEEMBAY_MUX(0x3, "TPIU_M3"),
			 KEEMBAY_MUX(0x4, "I2S0_M4"),
			 KEEMBAY_MUX(0x5, "LCD_M5"),
			 KEEMBAY_MUX(0x6, "CAM_M6"),
			 KEEMBAY_MUX(0x7, "GPIO_M7")),
	KEEMBAY_PIN_DESC(47, "GPIO47",
			 KEEMBAY_MUX(0x0, "ETH_M0"),
			 KEEMBAY_MUX(0x1, "SD1_M1"),
			 KEEMBAY_MUX(0x2, "SPI0_M2"),
			 KEEMBAY_MUX(0x3, "TPIU_M3"),
			 KEEMBAY_MUX(0x4, "I2S0_M4"),
			 KEEMBAY_MUX(0x5, "LCD_M5"),
			 KEEMBAY_MUX(0x6, "CAM_M6"),
			 KEEMBAY_MUX(0x7, "GPIO_M7")),
	KEEMBAY_PIN_DESC(48, "GPIO48",
			 KEEMBAY_MUX(0x0, "ETH_M0"),
			 KEEMBAY_MUX(0x1, "SPI2_M1"),
			 KEEMBAY_MUX(0x2, "UART2_M2"),
			 KEEMBAY_MUX(0x3, "TPIU_M3"),
			 KEEMBAY_MUX(0x4, "I2S0_M4"),
			 KEEMBAY_MUX(0x5, "LCD_M5"),
			 KEEMBAY_MUX(0x6, "CAM_M6"),
			 KEEMBAY_MUX(0x7, "GPIO_M7")),
	KEEMBAY_PIN_DESC(49, "GPIO49",
			 KEEMBAY_MUX(0x0, "ETH_M0"),
			 KEEMBAY_MUX(0x1, "SPI2_M1"),
			 KEEMBAY_MUX(0x2, "UART2_M2"),
			 KEEMBAY_MUX(0x3, "TPIU_M3"),
			 KEEMBAY_MUX(0x4, "I2S1_M4"),
			 KEEMBAY_MUX(0x5, "LCD_M5"),
			 KEEMBAY_MUX(0x6, "CAM_M6"),
			 KEEMBAY_MUX(0x7, "GPIO_M7")),
	KEEMBAY_PIN_DESC(50, "GPIO50",
			 KEEMBAY_MUX(0x0, "ETH_M0"),
			 KEEMBAY_MUX(0x1, "SPI2_M1"),
			 KEEMBAY_MUX(0x2, "UART2_M2"),
			 KEEMBAY_MUX(0x3, "TPIU_M3"),
			 KEEMBAY_MUX(0x4, "I2S1_M4"),
			 KEEMBAY_MUX(0x5, "LCD_M5"),
			 KEEMBAY_MUX(0x6, "CAM_M6"),
			 KEEMBAY_MUX(0x7, "GPIO_M7")),
	KEEMBAY_PIN_DESC(51, "GPIO51",
			 KEEMBAY_MUX(0x0, "ETH_M0"),
			 KEEMBAY_MUX(0x1, "SPI2_M1"),
			 KEEMBAY_MUX(0x2, "UART2_M2"),
			 KEEMBAY_MUX(0x3, "TPIU_M3"),
			 KEEMBAY_MUX(0x4, "I2S1_M4"),
			 KEEMBAY_MUX(0x5, "LCD_M5"),
			 KEEMBAY_MUX(0x6, "CAM_M6"),
			 KEEMBAY_MUX(0x7, "GPIO_M7")),
	KEEMBAY_PIN_DESC(52, "GPIO52",
			 KEEMBAY_MUX(0x0, "ETH_M0"),
			 KEEMBAY_MUX(0x1, "SPI2_M1"),
			 KEEMBAY_MUX(0x2, "SD0_M2"),
			 KEEMBAY_MUX(0x3, "TPIU_M3"),
			 KEEMBAY_MUX(0x4, "I2S1_M4"),
			 KEEMBAY_MUX(0x5, "LCD_M5"),
			 KEEMBAY_MUX(0x6, "CAM_M6"),
			 KEEMBAY_MUX(0x7, "GPIO_M7")),
	KEEMBAY_PIN_DESC(53, "GPIO53",
			 KEEMBAY_MUX(0x0, "ETH_M0"),
			 KEEMBAY_MUX(0x1, "SPI2_M1"),
			 KEEMBAY_MUX(0x2, "SD0_M2"),
			 KEEMBAY_MUX(0x3, "TPIU_M3"),
			 KEEMBAY_MUX(0x4, "I2S2_M4"),
			 KEEMBAY_MUX(0x5, "LCD_M5"),
			 KEEMBAY_MUX(0x6, "CAM_M6"),
			 KEEMBAY_MUX(0x7, "GPIO_M7")),
	KEEMBAY_PIN_DESC(54, "GPIO54",
			 KEEMBAY_MUX(0x0, "ETH_M0"),
			 KEEMBAY_MUX(0x1, "SPI2_M1"),
			 KEEMBAY_MUX(0x2, "SD0_M2"),
			 KEEMBAY_MUX(0x3, "TPIU_M3"),
			 KEEMBAY_MUX(0x4, "I2S2_M4"),
			 KEEMBAY_MUX(0x5, "LCD_M5"),
			 KEEMBAY_MUX(0x6, "CAM_M6"),
			 KEEMBAY_MUX(0x7, "GPIO_M7")),
	KEEMBAY_PIN_DESC(55, "GPIO55",
			 KEEMBAY_MUX(0x0, "ETH_M0"),
			 KEEMBAY_MUX(0x1, "SPI2_M1"),
			 KEEMBAY_MUX(0x2, "SD1_M2"),
			 KEEMBAY_MUX(0x3, "TPIU_M3"),
			 KEEMBAY_MUX(0x4, "I2S2_M4"),
			 KEEMBAY_MUX(0x5, "LCD_M5"),
			 KEEMBAY_MUX(0x6, "CAM_M6"),
			 KEEMBAY_MUX(0x7, "GPIO_M7")),
	KEEMBAY_PIN_DESC(56, "GPIO56",
			 KEEMBAY_MUX(0x0, "ETH_M0"),
			 KEEMBAY_MUX(0x1, "SPI2_M1"),
			 KEEMBAY_MUX(0x2, "SD1_M2"),
			 KEEMBAY_MUX(0x3, "TPIU_M3"),
			 KEEMBAY_MUX(0x4, "I2S2_M4"),
			 KEEMBAY_MUX(0x5, "LCD_M5"),
			 KEEMBAY_MUX(0x6, "CAM_M6"),
			 KEEMBAY_MUX(0x7, "GPIO_M7")),
	KEEMBAY_PIN_DESC(57, "GPIO57",
			 KEEMBAY_MUX(0x0, "SPI1_M0"),
			 KEEMBAY_MUX(0x1, "I2S1_M1"),
			 KEEMBAY_MUX(0x2, "SD1_M2"),
			 KEEMBAY_MUX(0x3, "TPIU_M3"),
			 KEEMBAY_MUX(0x4, "UART0_M4"),
			 KEEMBAY_MUX(0x5, "LCD_M5"),
			 KEEMBAY_MUX(0x6, "CAM_M6"),
			 KEEMBAY_MUX(0x7, "GPIO_M7")),
	KEEMBAY_PIN_DESC(58, "GPIO58",
			 KEEMBAY_MUX(0x0, "SPI1_M0"),
			 KEEMBAY_MUX(0x1, "ETH_M1"),
			 KEEMBAY_MUX(0x2, "SD0_M2"),
			 KEEMBAY_MUX(0x3, "TPIU_M3"),
			 KEEMBAY_MUX(0x4, "UART0_M4"),
			 KEEMBAY_MUX(0x5, "LCD_M5"),
			 KEEMBAY_MUX(0x6, "CAM_M6"),
			 KEEMBAY_MUX(0x7, "GPIO_M7")),
	KEEMBAY_PIN_DESC(59, "GPIO59",
			 KEEMBAY_MUX(0x0, "SPI1_M0"),
			 KEEMBAY_MUX(0x1, "ETH_M1"),
			 KEEMBAY_MUX(0x2, "SD0_M2"),
			 KEEMBAY_MUX(0x3, "TPIU_M3"),
			 KEEMBAY_MUX(0x4, "UART0_M4"),
			 KEEMBAY_MUX(0x5, "LCD_M5"),
			 KEEMBAY_MUX(0x6, "CAM_M6"),
			 KEEMBAY_MUX(0x7, "GPIO_M7")),
	KEEMBAY_PIN_DESC(60, "GPIO60",
			 KEEMBAY_MUX(0x0, "SPI1_M0"),
			 KEEMBAY_MUX(0x1, "ETH_M1"),
			 KEEMBAY_MUX(0x2, "I3C1_M2"),
			 KEEMBAY_MUX(0x3, "TPIU_M3"),
			 KEEMBAY_MUX(0x4, "UART0_M4"),
			 KEEMBAY_MUX(0x5, "LCD_M5"),
			 KEEMBAY_MUX(0x6, "CAM_M6"),
			 KEEMBAY_MUX(0x7, "GPIO_M7")),
	KEEMBAY_PIN_DESC(61, "GPIO61",
			 KEEMBAY_MUX(0x0, "SPI1_M0"),
			 KEEMBAY_MUX(0x1, "ETH_M1"),
			 KEEMBAY_MUX(0x2, "SD0_M2"),
			 KEEMBAY_MUX(0x3, "TPIU_M3"),
			 KEEMBAY_MUX(0x4, "UART1_M4"),
			 KEEMBAY_MUX(0x5, "LCD_M5"),
			 KEEMBAY_MUX(0x6, "CAM_M6"),
			 KEEMBAY_MUX(0x7, "GPIO_M7")),
	KEEMBAY_PIN_DESC(62, "GPIO62",
			 KEEMBAY_MUX(0x0, "SPI1_M0"),
			 KEEMBAY_MUX(0x1, "ETH_M1"),
			 KEEMBAY_MUX(0x2, "SD1_M2"),
			 KEEMBAY_MUX(0x3, "TPIU_M3"),
			 KEEMBAY_MUX(0x4, "UART1_M4"),
			 KEEMBAY_MUX(0x5, "LCD_M5"),
			 KEEMBAY_MUX(0x6, "CAM_M6"),
			 KEEMBAY_MUX(0x7, "GPIO_M7")),
	KEEMBAY_PIN_DESC(63, "GPIO63",
			 KEEMBAY_MUX(0x0, "I2S1_M0"),
			 KEEMBAY_MUX(0x1, "SPI1_M1"),
			 KEEMBAY_MUX(0x2, "SD1_M2"),
			 KEEMBAY_MUX(0x3, "TPIU_M3"),
			 KEEMBAY_MUX(0x4, "UART1_M4"),
			 KEEMBAY_MUX(0x5, "LCD_M5"),
			 KEEMBAY_MUX(0x6, "CAM_M6"),
			 KEEMBAY_MUX(0x7, "GPIO_M7")),
	KEEMBAY_PIN_DESC(64, "GPIO64",
			 KEEMBAY_MUX(0x0, "I2S2_M0"),
			 KEEMBAY_MUX(0x1, "SPI1_M1"),
			 KEEMBAY_MUX(0x2, "ETH_M2"),
			 KEEMBAY_MUX(0x3, "TPIU_M3"),
			 KEEMBAY_MUX(0x4, "UART1_M4"),
			 KEEMBAY_MUX(0x5, "LCD_M5"),
			 KEEMBAY_MUX(0x6, "CAM_M6"),
			 KEEMBAY_MUX(0x7, "GPIO_M7")),
	KEEMBAY_PIN_DESC(65, "GPIO65",
			 KEEMBAY_MUX(0x0, "I3C0_M0"),
			 KEEMBAY_MUX(0x1, "SPI1_M1"),
			 KEEMBAY_MUX(0x2, "SD1_M2"),
			 KEEMBAY_MUX(0x3, "TPIU_M3"),
			 KEEMBAY_MUX(0x4, "SPI0_M4"),
			 KEEMBAY_MUX(0x5, "LCD_M5"),
			 KEEMBAY_MUX(0x6, "CAM_M6"),
			 KEEMBAY_MUX(0x7, "GPIO_M7")),
	KEEMBAY_PIN_DESC(66, "GPIO66",
			 KEEMBAY_MUX(0x0, "I3C0_M0"),
			 KEEMBAY_MUX(0x1, "ETH_M1"),
			 KEEMBAY_MUX(0x2, "I2C0_M2"),
			 KEEMBAY_MUX(0x3, "TPIU_M3"),
			 KEEMBAY_MUX(0x4, "SPI0_M4"),
			 KEEMBAY_MUX(0x5, "LCD_M5"),
			 KEEMBAY_MUX(0x6, "CAM_M6"),
			 KEEMBAY_MUX(0x7, "GPIO_M7")),
	KEEMBAY_PIN_DESC(67, "GPIO67",
			 KEEMBAY_MUX(0x0, "I3C1_M0"),
			 KEEMBAY_MUX(0x1, "ETH_M1"),
			 KEEMBAY_MUX(0x2, "I2C0_M2"),
			 KEEMBAY_MUX(0x3, "TPIU_M3"),
			 KEEMBAY_MUX(0x4, "SPI0_M4"),
			 KEEMBAY_MUX(0x5, "LCD_M5"),
			 KEEMBAY_MUX(0x6, "I2S3_M6"),
			 KEEMBAY_MUX(0x7, "GPIO_M7")),
	KEEMBAY_PIN_DESC(68, "GPIO68",
			 KEEMBAY_MUX(0x0, "I3C1_M0"),
			 KEEMBAY_MUX(0x1, "ETH_M1"),
			 KEEMBAY_MUX(0x2, "I2C1_M2"),
			 KEEMBAY_MUX(0x3, "TPIU_M3"),
			 KEEMBAY_MUX(0x4, "SPI0_M4"),
			 KEEMBAY_MUX(0x5, "LCD_M5"),
			 KEEMBAY_MUX(0x6, "I2S3_M6"),
			 KEEMBAY_MUX(0x7, "GPIO_M7")),
	KEEMBAY_PIN_DESC(69, "GPIO69",
			 KEEMBAY_MUX(0x0, "I3C2_M0"),
			 KEEMBAY_MUX(0x1, "ETH_M1"),
			 KEEMBAY_MUX(0x2, "I2C1_M2"),
			 KEEMBAY_MUX(0x3, "TPIU_M3"),
			 KEEMBAY_MUX(0x4, "SPI0_M4"),
			 KEEMBAY_MUX(0x5, "LCD_M5"),
			 KEEMBAY_MUX(0x6, "I2S3_M6"),
			 KEEMBAY_MUX(0x7, "GPIO_M7")),
	KEEMBAY_PIN_DESC(70, "GPIO70",
			 KEEMBAY_MUX(0x0, "I3C2_M0"),
			 KEEMBAY_MUX(0x1, "ETH_M1"),
			 KEEMBAY_MUX(0x2, "SPI0_M2"),
			 KEEMBAY_MUX(0x3, "TPIU_M3"),
			 KEEMBAY_MUX(0x4, "SD0_M4"),
			 KEEMBAY_MUX(0x5, "LCD_M5"),
			 KEEMBAY_MUX(0x6, "I2S3_M6"),
			 KEEMBAY_MUX(0x7, "GPIO_M7")),
	KEEMBAY_PIN_DESC(71, "GPIO71",
			 KEEMBAY_MUX(0x0, "I3C0_M0"),
			 KEEMBAY_MUX(0x1, "ETH_M1"),
			 KEEMBAY_MUX(0x2, "SLVDS1_M2"),
			 KEEMBAY_MUX(0x3, "TPIU_M3"),
			 KEEMBAY_MUX(0x4, "SD0_M4"),
			 KEEMBAY_MUX(0x5, "LCD_M5"),
			 KEEMBAY_MUX(0x6, "I2S3_M6"),
			 KEEMBAY_MUX(0x7, "GPIO_M7")),
	KEEMBAY_PIN_DESC(72, "GPIO72",
			 KEEMBAY_MUX(0x0, "I3C1_M0"),
			 KEEMBAY_MUX(0x1, "ETH_M1"),
			 KEEMBAY_MUX(0x2, "SLVDS1_M2"),
			 KEEMBAY_MUX(0x3, "TPIU_M3"),
			 KEEMBAY_MUX(0x4, "SD0_M4"),
			 KEEMBAY_MUX(0x5, "LCD_M5"),
			 KEEMBAY_MUX(0x6, "UART2_M6"),
			 KEEMBAY_MUX(0x7, "GPIO_M7")),
	KEEMBAY_PIN_DESC(73, "GPIO73",
			 KEEMBAY_MUX(0x0, "I3C2_M0"),
			 KEEMBAY_MUX(0x1, "ETH_M1"),
			 KEEMBAY_MUX(0x2, "SLVDS1_M2"),
			 KEEMBAY_MUX(0x3, "TPIU_M3"),
			 KEEMBAY_MUX(0x4, "SD0_M4"),
			 KEEMBAY_MUX(0x5, "LCD_M5"),
			 KEEMBAY_MUX(0x6, "UART2_M6"),
			 KEEMBAY_MUX(0x7, "GPIO_M7")),
	KEEMBAY_PIN_DESC(74, "GPIO74",
			 KEEMBAY_MUX(0x0, "I3C0_M0"),
			 KEEMBAY_MUX(0x1, "ETH_M1"),
			 KEEMBAY_MUX(0x2, "SLVDS1_M2"),
			 KEEMBAY_MUX(0x3, "TPIU_M3"),
			 KEEMBAY_MUX(0x4, "SD0_M4"),
			 KEEMBAY_MUX(0x5, "LCD_M5"),
			 KEEMBAY_MUX(0x6, "UART2_M6"),
			 KEEMBAY_MUX(0x7, "GPIO_M7")),
	KEEMBAY_PIN_DESC(75, "GPIO75",
			 KEEMBAY_MUX(0x0, "I3C0_M0"),
			 KEEMBAY_MUX(0x1, "ETH_M1"),
			 KEEMBAY_MUX(0x2, "SLVDS1_M2"),
			 KEEMBAY_MUX(0x3, "TPIU_M3"),
			 KEEMBAY_MUX(0x4, "SD0_M4"),
			 KEEMBAY_MUX(0x5, "LCD_M5"),
			 KEEMBAY_MUX(0x6, "UART2_M6"),
			 KEEMBAY_MUX(0x7, "GPIO_M7")),
	KEEMBAY_PIN_DESC(76, "GPIO76",
			 KEEMBAY_MUX(0x0, "I2C2_M0"),
			 KEEMBAY_MUX(0x1, "I3C0_M1"),
			 KEEMBAY_MUX(0x2, "SLVDS1_M2"),
			 KEEMBAY_MUX(0x3, "TPIU_M3"),
			 KEEMBAY_MUX(0x4, "ETH_M4"),
			 KEEMBAY_MUX(0x5, "LCD_M5"),
			 KEEMBAY_MUX(0x6, "UART3_M6"),
			 KEEMBAY_MUX(0x7, "GPIO_M7")),
	KEEMBAY_PIN_DESC(77, "GPIO77",
			 KEEMBAY_MUX(0x0, "PCIE_M0"),
			 KEEMBAY_MUX(0x1, "I3C1_M1"),
			 KEEMBAY_MUX(0x2, "SLVDS1_M2"),
			 KEEMBAY_MUX(0x3, "TPIU_M3"),
			 KEEMBAY_MUX(0x4, "I3C2_M4"),
			 KEEMBAY_MUX(0x5, "LCD_M5"),
			 KEEMBAY_MUX(0x6, "UART3_M6"),
			 KEEMBAY_MUX(0x7, "GPIO_M7")),
	KEEMBAY_PIN_DESC(78, "GPIO78",
			 KEEMBAY_MUX(0x0, "PCIE_M0"),
			 KEEMBAY_MUX(0x1, "I3C2_M1"),
			 KEEMBAY_MUX(0x2, "SLVDS1_M2"),
			 KEEMBAY_MUX(0x3, "TPIU_M3"),
			 KEEMBAY_MUX(0x4, "I3C2_M4"),
			 KEEMBAY_MUX(0x5, "LCD_M5"),
			 KEEMBAY_MUX(0x6, "UART3_M6"),
			 KEEMBAY_MUX(0x7, "GPIO_M7")),
	KEEMBAY_PIN_DESC(79, "GPIO79",
			 KEEMBAY_MUX(0x0, "PCIE_M0"),
			 KEEMBAY_MUX(0x1, "I2C2_M1"),
			 KEEMBAY_MUX(0x2, "SLVDS1_M2"),
			 KEEMBAY_MUX(0x3, "TPIU_M3"),
			 KEEMBAY_MUX(0x4, "I3C2_M4"),
			 KEEMBAY_MUX(0x5, "LCD_M5"),
			 KEEMBAY_MUX(0x6, "UART3_M6"),
			 KEEMBAY_MUX(0x7, "GPIO_M7")),
};

static inline u32 keembay_read_reg(void __iomem *base, unsigned int pin)
{
	return readl(base + KEEMBAY_GPIO_REG_OFFSET(pin));
}

static inline u32 keembay_read_gpio_reg(void __iomem *base, unsigned int pin)
{
	return keembay_read_reg(base, pin / KEEMBAY_GPIO_MAX_PER_REG);
}

static inline u32 keembay_read_pin(void __iomem *base, unsigned int pin)
{
	u32 val = keembay_read_gpio_reg(base, pin);

	return !!(val & BIT(pin % KEEMBAY_GPIO_MAX_PER_REG));
}

static inline void keembay_write_reg(u32 val, void __iomem *base, unsigned int pin)
{
	writel(val, base + KEEMBAY_GPIO_REG_OFFSET(pin));
}

static inline void keembay_write_gpio_reg(u32 val, void __iomem *base, unsigned int pin)
{
	keembay_write_reg(val, base, pin / KEEMBAY_GPIO_MAX_PER_REG);
}

static void keembay_gpio_invert(struct keembay_pinctrl *kpc, unsigned int pin)
{
	unsigned int val = keembay_read_reg(kpc->base1 + KEEMBAY_GPIO_MODE, pin);

	/*
	 * This IP doesn't support the falling edge and low level interrupt
	 * trigger. Invert API is used to mimic the falling edge and low
	 * level support
	 */

	val |= FIELD_PREP(KEEMBAY_GPIO_MODE_INV_MASK, KEEMBAY_GPIO_MODE_INV_VAL);
	keembay_write_reg(val, kpc->base1 + KEEMBAY_GPIO_MODE, pin);
}

static void keembay_gpio_restore_default(struct keembay_pinctrl *kpc, unsigned int pin)
{
	unsigned int val = keembay_read_reg(kpc->base1 + KEEMBAY_GPIO_MODE, pin);

	val &= FIELD_PREP(KEEMBAY_GPIO_MODE_INV_MASK, 0);
	keembay_write_reg(val, kpc->base1 + KEEMBAY_GPIO_MODE, pin);
}

static int keembay_request_gpio(struct pinctrl_dev *pctldev,
				struct pinctrl_gpio_range *range, unsigned int pin)
{
	struct keembay_pinctrl *kpc = pinctrl_dev_get_drvdata(pctldev);
	unsigned int val;

	if (pin >= kpc->npins)
		return -EINVAL;

	val = keembay_read_reg(kpc->base1 + KEEMBAY_GPIO_MODE, pin);
	val = FIELD_GET(KEEMBAY_GPIO_MODE_SELECT_MASK, val);

	/* As per Pin Mux Map, Modes 0 to 6 are for peripherals */
	if (val != KEEMBAY_GPIO_MODE_DEFAULT)
		return -EBUSY;

	return 0;
}

static int keembay_set_mux(struct pinctrl_dev *pctldev, unsigned int fun_sel,
			   unsigned int grp_sel)
{
	struct keembay_pinctrl *kpc = pinctrl_dev_get_drvdata(pctldev);
	const struct function_desc *func;
	struct group_desc *grp;
	unsigned int val;
	u8 pin_mode;
	int pin;

	grp = pinctrl_generic_get_group(pctldev, grp_sel);
	if (!grp)
		return -EINVAL;

	func = pinmux_generic_get_function(pctldev, fun_sel);
	if (!func)
		return -EINVAL;

	/* Change modes for pins in the selected group */
	pin = *grp->grp.pins;
	pin_mode = *(u8 *)(func->data);

	val = keembay_read_reg(kpc->base1 + KEEMBAY_GPIO_MODE, pin);
	val = u32_replace_bits(val, pin_mode, KEEMBAY_GPIO_MODE_SELECT_MASK);
	keembay_write_reg(val, kpc->base1 + KEEMBAY_GPIO_MODE, pin);

	return 0;
}

static u32 keembay_pinconf_get_pull(struct keembay_pinctrl *kpc, unsigned int pin)
{
	unsigned int val = keembay_read_reg(kpc->base1 + KEEMBAY_GPIO_MODE, pin);

	return FIELD_GET(KEEMBAY_GPIO_MODE_PULLUP_MASK, val);
}

static int keembay_pinconf_set_pull(struct keembay_pinctrl *kpc, unsigned int pin,
				    unsigned int pull)
{
	unsigned int val = keembay_read_reg(kpc->base1 + KEEMBAY_GPIO_MODE, pin);

	val = u32_replace_bits(val, pull, KEEMBAY_GPIO_MODE_PULLUP_MASK);
	keembay_write_reg(val, kpc->base1 + KEEMBAY_GPIO_MODE, pin);

	return 0;
}

static int keembay_pinconf_get_drive(struct keembay_pinctrl *kpc, unsigned int pin)
{
	unsigned int val = keembay_read_reg(kpc->base1 + KEEMBAY_GPIO_MODE, pin);

	val = FIELD_GET(KEEMBAY_GPIO_MODE_DRIVE_MASK, val) * 4;
	if (val)
		return val;

	return KEEMBAY_GPIO_MIN_STRENGTH;
}

static int keembay_pinconf_set_drive(struct keembay_pinctrl *kpc, unsigned int pin,
				     unsigned int drive)
{
	unsigned int val = keembay_read_reg(kpc->base1 + KEEMBAY_GPIO_MODE, pin);
	unsigned int strength = clamp_val(drive, KEEMBAY_GPIO_MIN_STRENGTH,
				 KEEMBAY_GPIO_MAX_STRENGTH) / 4;

	val = u32_replace_bits(val, strength, KEEMBAY_GPIO_MODE_DRIVE_MASK);
	keembay_write_reg(val, kpc->base1 + KEEMBAY_GPIO_MODE, pin);

	return 0;
}

static int keembay_pinconf_get_slew_rate(struct keembay_pinctrl *kpc, unsigned int pin)
{
	unsigned int val = keembay_read_reg(kpc->base1 + KEEMBAY_GPIO_MODE, pin);

	return !!(val & KEEMBAY_GPIO_MODE_SLEW_RATE);
}

static int keembay_pinconf_set_slew_rate(struct keembay_pinctrl *kpc, unsigned int pin,
					 unsigned int slew_rate)
{
	unsigned int val = keembay_read_reg(kpc->base1 + KEEMBAY_GPIO_MODE, pin);

	if (slew_rate)
		val |= KEEMBAY_GPIO_MODE_SLEW_RATE;
	else
		val &= ~KEEMBAY_GPIO_MODE_SLEW_RATE;

	keembay_write_reg(val, kpc->base1 + KEEMBAY_GPIO_MODE, pin);

	return 0;
}

static int keembay_pinconf_get_schmitt(struct keembay_pinctrl *kpc, unsigned int pin)
{
	unsigned int val = keembay_read_reg(kpc->base1 + KEEMBAY_GPIO_MODE, pin);

	return !!(val & KEEMBAY_GPIO_MODE_SCHMITT_EN);
}

static int keembay_pinconf_set_schmitt(struct keembay_pinctrl *kpc, unsigned int pin,
				       unsigned int schmitt_en)
{
	unsigned int val = keembay_read_reg(kpc->base1 + KEEMBAY_GPIO_MODE, pin);

	if (schmitt_en)
		val |= KEEMBAY_GPIO_MODE_SCHMITT_EN;
	else
		val &= ~KEEMBAY_GPIO_MODE_SCHMITT_EN;

	keembay_write_reg(val, kpc->base1 + KEEMBAY_GPIO_MODE, pin);

	return 0;
}

static int keembay_pinconf_get(struct pinctrl_dev *pctldev, unsigned int pin,
			       unsigned long *cfg)
{
	struct keembay_pinctrl *kpc = pinctrl_dev_get_drvdata(pctldev);
	unsigned int param = pinconf_to_config_param(*cfg);
	unsigned int val;

	if (pin >= kpc->npins)
		return -EINVAL;

	switch (param) {
	case PIN_CONFIG_BIAS_DISABLE:
		if (keembay_pinconf_get_pull(kpc, pin) != KEEMBAY_GPIO_DISABLE)
			return -EINVAL;
		break;

	case PIN_CONFIG_BIAS_PULL_UP:
		if (keembay_pinconf_get_pull(kpc, pin) != KEEMBAY_GPIO_PULL_UP)
			return -EINVAL;
		break;

	case PIN_CONFIG_BIAS_PULL_DOWN:
		if (keembay_pinconf_get_pull(kpc, pin) != KEEMBAY_GPIO_PULL_DOWN)
			return -EINVAL;
		break;

	case PIN_CONFIG_BIAS_BUS_HOLD:
		if (keembay_pinconf_get_pull(kpc, pin) != KEEMBAY_GPIO_BUS_HOLD)
			return -EINVAL;
		break;

	case PIN_CONFIG_INPUT_SCHMITT_ENABLE:
		if (!keembay_pinconf_get_schmitt(kpc, pin))
			return -EINVAL;
		break;

	case PIN_CONFIG_SLEW_RATE:
		val = keembay_pinconf_get_slew_rate(kpc, pin);
		*cfg = pinconf_to_config_packed(param, val);
		break;

	case PIN_CONFIG_DRIVE_STRENGTH:
		val = keembay_pinconf_get_drive(kpc, pin);
		*cfg = pinconf_to_config_packed(param, val);
		break;

	default:
		return -ENOTSUPP;
	}

	return 0;
}

static int keembay_pinconf_set(struct pinctrl_dev *pctldev, unsigned int pin,
			       unsigned long *cfg, unsigned int num_configs)
{
	struct keembay_pinctrl *kpc = pinctrl_dev_get_drvdata(pctldev);
	enum pin_config_param param;
	unsigned int arg, i;
	int ret = 0;

	if (pin >= kpc->npins)
		return -EINVAL;

	for (i = 0; i < num_configs; i++) {
		param = pinconf_to_config_param(cfg[i]);
		arg = pinconf_to_config_argument(cfg[i]);

		switch (param) {
		case PIN_CONFIG_BIAS_DISABLE:
			ret = keembay_pinconf_set_pull(kpc, pin, KEEMBAY_GPIO_DISABLE);
			break;

		case PIN_CONFIG_BIAS_PULL_UP:
			ret = keembay_pinconf_set_pull(kpc, pin, KEEMBAY_GPIO_PULL_UP);
			break;

		case PIN_CONFIG_BIAS_PULL_DOWN:
			ret = keembay_pinconf_set_pull(kpc, pin, KEEMBAY_GPIO_PULL_DOWN);
			break;

		case PIN_CONFIG_BIAS_BUS_HOLD:
			ret = keembay_pinconf_set_pull(kpc, pin, KEEMBAY_GPIO_BUS_HOLD);
			break;

		case PIN_CONFIG_INPUT_SCHMITT_ENABLE:
			ret = keembay_pinconf_set_schmitt(kpc, pin, arg);
			break;

		case PIN_CONFIG_SLEW_RATE:
			ret = keembay_pinconf_set_slew_rate(kpc, pin, arg);
			break;

		case PIN_CONFIG_DRIVE_STRENGTH:
			ret = keembay_pinconf_set_drive(kpc, pin, arg);
			break;

		default:
			return -ENOTSUPP;
		}
		if (ret)
			return ret;
	}
	return ret;
}

static const struct pinctrl_ops keembay_pctlops = {
	.get_groups_count	= pinctrl_generic_get_group_count,
	.get_group_name		= pinctrl_generic_get_group_name,
	.get_group_pins		= pinctrl_generic_get_group_pins,
	.dt_node_to_map		= pinconf_generic_dt_node_to_map_all,
	.dt_free_map		= pinconf_generic_dt_free_map,
};

static const struct pinmux_ops keembay_pmxops = {
	.get_functions_count	= pinmux_generic_get_function_count,
	.get_function_name	= pinmux_generic_get_function_name,
	.get_function_groups	= pinmux_generic_get_function_groups,
	.gpio_request_enable	= keembay_request_gpio,
	.set_mux		= keembay_set_mux,
};

static const struct pinconf_ops keembay_confops = {
	.is_generic	= true,
	.pin_config_get	= keembay_pinconf_get,
	.pin_config_set	= keembay_pinconf_set,
};

static struct pinctrl_desc keembay_pinctrl_desc = {
	.name		= "keembay-pinmux",
	.pctlops	= &keembay_pctlops,
	.pmxops		= &keembay_pmxops,
	.confops	= &keembay_confops,
	.owner		= THIS_MODULE,
};

static int keembay_gpio_get(struct gpio_chip *gc, unsigned int pin)
{
	struct keembay_pinctrl *kpc = gpiochip_get_data(gc);
	unsigned int val, offset;

	val = keembay_read_reg(kpc->base1 + KEEMBAY_GPIO_MODE, pin);
	offset = (val & KEEMBAY_GPIO_MODE_DIR) ? KEEMBAY_GPIO_DATA_IN : KEEMBAY_GPIO_DATA_OUT;

	return keembay_read_pin(kpc->base0 + offset, pin);
}

static int keembay_gpio_set(struct gpio_chip *gc, unsigned int pin, int val)
{
	struct keembay_pinctrl *kpc = gpiochip_get_data(gc);
	unsigned int reg_val;

	reg_val = keembay_read_gpio_reg(kpc->base0 + KEEMBAY_GPIO_DATA_OUT, pin);
	if (val)
		keembay_write_gpio_reg(reg_val | BIT(pin % KEEMBAY_GPIO_MAX_PER_REG),
				       kpc->base0 + KEEMBAY_GPIO_DATA_HIGH, pin);
	else
		keembay_write_gpio_reg(~reg_val | BIT(pin % KEEMBAY_GPIO_MAX_PER_REG),
				       kpc->base0 + KEEMBAY_GPIO_DATA_LOW, pin);

	return 0;
}

static int keembay_gpio_get_direction(struct gpio_chip *gc, unsigned int pin)
{
	struct keembay_pinctrl *kpc = gpiochip_get_data(gc);
	unsigned int val = keembay_read_reg(kpc->base1 + KEEMBAY_GPIO_MODE, pin);

	return !!(val & KEEMBAY_GPIO_MODE_DIR);
}

static int keembay_gpio_set_direction_in(struct gpio_chip *gc, unsigned int pin)
{
	struct keembay_pinctrl *kpc = gpiochip_get_data(gc);
	unsigned int val;

	val = keembay_read_reg(kpc->base1 + KEEMBAY_GPIO_MODE, pin);
	val |= KEEMBAY_GPIO_MODE_DIR;
	keembay_write_reg(val, kpc->base1 + KEEMBAY_GPIO_MODE, pin);

	return 0;
}

static int keembay_gpio_set_direction_out(struct gpio_chip *gc,
					  unsigned int pin, int value)
{
	struct keembay_pinctrl *kpc = gpiochip_get_data(gc);
	unsigned int val;

	val = keembay_read_reg(kpc->base1 + KEEMBAY_GPIO_MODE, pin);
	val &= ~KEEMBAY_GPIO_MODE_DIR;
	keembay_write_reg(val, kpc->base1 + KEEMBAY_GPIO_MODE, pin);

	return keembay_gpio_set(gc, pin, value);
}

static void keembay_gpio_irq_handler(struct irq_desc *desc)
{
	struct gpio_chip *gc = irq_desc_get_handler_data(desc);
	unsigned int kmb_irq = irq_desc_get_irq(desc);
	unsigned long reg, clump = 0, bit = 0;
	struct irq_chip *parent_chip;
	struct keembay_pinctrl *kpc;
	unsigned int src, pin, val;

	/* Identify GPIO interrupt number from GIC interrupt number */
	for (src = 0; src < KEEMBAY_GPIO_NUM_IRQ; src++) {
		if (kmb_irq == gc->irq.parents[src])
			break;
	}

	if (src == KEEMBAY_GPIO_NUM_IRQ)
		return;

	parent_chip = irq_desc_get_chip(desc);
	kpc = gpiochip_get_data(gc);

	chained_irq_enter(parent_chip, desc);
	reg = keembay_read_reg(kpc->base1 + KEEMBAY_GPIO_INT_CFG, src);

	/*
	 * Each Interrupt line can be shared by up to 4 GPIO pins. Enable bit
	 * and input values were checked to identify the source of the
	 * Interrupt. The checked enable bit positions are 7, 15, 23 and 31.
	 */
	for_each_set_clump8(bit, clump, &reg, BITS_PER_TYPE(typeof(reg))) {
		pin = clump & ~KEEMBAY_GPIO_IRQ_ENABLE;
		val = keembay_read_pin(kpc->base0 + KEEMBAY_GPIO_DATA_IN, pin);
		kmb_irq = irq_find_mapping(gc->irq.domain, pin);

		/* Checks if the interrupt is enabled */
		if (val && (clump & KEEMBAY_GPIO_IRQ_ENABLE))
			generic_handle_irq(kmb_irq);
	}
	chained_irq_exit(parent_chip, desc);
}

static void keembay_gpio_clear_irq(struct irq_data *data, unsigned long pos,
				   u32 src, irq_hw_number_t pin)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(data);
	struct keembay_pinctrl *kpc = gpiochip_get_data(gc);
	unsigned long trig = irqd_get_trigger_type(data);
	struct keembay_gpio_irq *irq = &kpc->irq[src];
	unsigned long val;

	/* Check if the value of pos/KEEMBAY_GPIO_NUM_IRQ is in valid range. */
	if ((pos / KEEMBAY_GPIO_NUM_IRQ) >= KEEMBAY_GPIO_MAX_PER_IRQ)
		return;

	/* Retains val register as it handles other interrupts as well. */
	val = keembay_read_reg(kpc->base1 + KEEMBAY_GPIO_INT_CFG, src);

	bitmap_set_value8(&val, 0, pos);
	keembay_write_reg(val, kpc->base1 + KEEMBAY_GPIO_INT_CFG, src);

	irq->num_share--;
	irq->pins[pos / KEEMBAY_GPIO_NUM_IRQ] = 0;

	if (trig & IRQ_TYPE_LEVEL_MASK)
		keembay_gpio_restore_default(kpc, pin);

	if (irq->trigger == IRQ_TYPE_LEVEL_HIGH)
		kpc->max_gpios_level_type++;
	else if (irq->trigger == IRQ_TYPE_EDGE_RISING)
		kpc->max_gpios_edge_type++;
}

static int keembay_find_free_slot(struct keembay_pinctrl *kpc, unsigned int src)
{
	unsigned long val = keembay_read_reg(kpc->base1 + KEEMBAY_GPIO_INT_CFG, src);

	return bitmap_find_free_region(&val, KEEMBAY_GPIO_MAX_PER_REG, 3) / KEEMBAY_GPIO_NUM_IRQ;
}

static int keembay_find_free_src(struct keembay_pinctrl *kpc, unsigned int trig)
{
	int src, type = 0;

	if (trig & IRQ_TYPE_LEVEL_MASK)
		type = IRQ_TYPE_LEVEL_HIGH;
	else if (trig & IRQ_TYPE_EDGE_BOTH)
		type = IRQ_TYPE_EDGE_RISING;

	for (src = 0; src < KEEMBAY_GPIO_NUM_IRQ; src++) {
		if (kpc->irq[src].trigger != type)
			continue;

		if (!keembay_read_reg(kpc->base1 + KEEMBAY_GPIO_INT_CFG, src) ||
		    kpc->irq[src].num_share < KEEMBAY_GPIO_MAX_PER_IRQ)
			return src;
	}

	return -EBUSY;
}

static void keembay_gpio_set_irq(struct keembay_pinctrl *kpc, int src,
				 int slot, irq_hw_number_t pin)
{
	unsigned long val = pin | KEEMBAY_GPIO_IRQ_ENABLE;
	struct keembay_gpio_irq *irq = &kpc->irq[src];
	unsigned long flags, reg;

	raw_spin_lock_irqsave(&kpc->lock, flags);
	reg = keembay_read_reg(kpc->base1 + KEEMBAY_GPIO_INT_CFG, src);
	bitmap_set_value8(&reg, val, slot * 8);
	keembay_write_reg(reg, kpc->base1 + KEEMBAY_GPIO_INT_CFG, src);
	raw_spin_unlock_irqrestore(&kpc->lock, flags);

	if (irq->trigger == IRQ_TYPE_LEVEL_HIGH)
		kpc->max_gpios_level_type--;
	else if (irq->trigger == IRQ_TYPE_EDGE_RISING)
		kpc->max_gpios_edge_type--;

	irq->source = src;
	irq->pins[slot] = pin;
	irq->num_share++;
}

static void keembay_gpio_irq_enable(struct irq_data *data)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(data);
	struct keembay_pinctrl *kpc = gpiochip_get_data(gc);
	unsigned int trig = irqd_get_trigger_type(data);
	irq_hw_number_t pin = irqd_to_hwirq(data);
	int src, slot;

	/* Check which Interrupt source and slot is available */
	src = keembay_find_free_src(kpc, trig);
	slot = keembay_find_free_slot(kpc, src);

	if (src < 0 || slot < 0)
		return;

	if (trig & KEEMBAY_GPIO_SENSE_LOW)
		keembay_gpio_invert(kpc, pin);

	keembay_gpio_set_irq(kpc, src, slot, pin);
}

static void keembay_gpio_irq_ack(struct irq_data *data)
{
	/*
	 * The keembay_gpio_irq_ack function is needed to handle_edge_irq.
	 * IRQ ack is not possible from the SOC perspective. The IP by itself
	 * is used for handling interrupts which do not come in short-time and
	 * not used as protocol or communication interrupts. All the interrupts
	 * are threaded IRQ interrupts. But this function is expected to be
	 * present as the gpio IP is registered with irq framework. Otherwise
	 * handle_edge_irq() fails.
	 */
}

static void keembay_gpio_irq_disable(struct irq_data *data)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(data);
	struct keembay_pinctrl *kpc = gpiochip_get_data(gc);
	irq_hw_number_t pin = irqd_to_hwirq(data);
	unsigned long reg, clump = 0, pos = 0;
	unsigned int src;

	for (src = 0; src < KEEMBAY_GPIO_NUM_IRQ; src++) {
		reg = keembay_read_reg(kpc->base1 + KEEMBAY_GPIO_INT_CFG, src);
		for_each_set_clump8(pos, clump, &reg, BITS_PER_TYPE(typeof(reg))) {
			if ((clump & ~KEEMBAY_GPIO_IRQ_ENABLE) == pin) {
				keembay_gpio_clear_irq(data, pos, src, pin);
				return;
			}
		}
	}
}

static int keembay_gpio_irq_set_type(struct irq_data *data, unsigned int type)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(data);
	struct keembay_pinctrl *kpc = gpiochip_get_data(gc);

	/* Change EDGE_BOTH as EDGE_RISING in order to claim the IRQ for power button */
	if (!kpc->max_gpios_edge_type && (type & IRQ_TYPE_EDGE_BOTH))
		type = IRQ_TYPE_EDGE_RISING;

	if (!kpc->max_gpios_level_type && (type & IRQ_TYPE_LEVEL_MASK))
		type = IRQ_TYPE_NONE;

	if (type & IRQ_TYPE_EDGE_BOTH)
		irq_set_handler_locked(data, handle_edge_irq);
	else if (type & IRQ_TYPE_LEVEL_MASK)
		irq_set_handler_locked(data, handle_level_irq);
	else
		return -EINVAL;

	return 0;
}

static int keembay_gpio_add_pin_ranges(struct gpio_chip *chip)
{
	struct keembay_pinctrl *kpc = gpiochip_get_data(chip);
	int ret;

	ret = gpiochip_add_pin_range(chip, dev_name(kpc->dev), 0, 0, chip->ngpio);
	if (ret)
		dev_err_probe(kpc->dev, ret, "failed to add GPIO pin range\n");
	return ret;
}

static struct irq_chip keembay_gpio_irqchip = {
	.name = "keembay-gpio",
	.irq_enable = keembay_gpio_irq_enable,
	.irq_disable = keembay_gpio_irq_disable,
	.irq_set_type = keembay_gpio_irq_set_type,
	.irq_ack = keembay_gpio_irq_ack,
};

static int keembay_gpiochip_probe(struct keembay_pinctrl *kpc,
				  struct platform_device *pdev)
{
	unsigned int i, level_line = 0, edge_line = 0;
	struct gpio_chip *gc = &kpc->chip;
	struct gpio_irq_chip *girq;

	/* Setup GPIO IRQ chip */
	girq			= &kpc->chip.irq;
	girq->chip		= &keembay_gpio_irqchip;
	girq->parent_handler	= keembay_gpio_irq_handler;
	girq->num_parents	= KEEMBAY_GPIO_NUM_IRQ;
	girq->parents		= devm_kcalloc(kpc->dev, girq->num_parents,
					       sizeof(*girq->parents), GFP_KERNEL);

	if (!girq->parents)
		return -ENOMEM;

	/* Setup GPIO chip */
	gc->label		= dev_name(kpc->dev);
	gc->parent		= kpc->dev;
	gc->request		= gpiochip_generic_request;
	gc->free		= gpiochip_generic_free;
	gc->get_direction	= keembay_gpio_get_direction;
	gc->direction_input	= keembay_gpio_set_direction_in;
	gc->direction_output	= keembay_gpio_set_direction_out;
	gc->get			= keembay_gpio_get;
	gc->set			= keembay_gpio_set;
	gc->set_config		= gpiochip_generic_config;
	gc->base		= -1;
	gc->ngpio		= kpc->npins;
	gc->add_pin_ranges	= keembay_gpio_add_pin_ranges;

	for (i = 0; i < KEEMBAY_GPIO_NUM_IRQ; i++) {
		struct keembay_gpio_irq *kmb_irq = &kpc->irq[i];
		int irq;

		irq = platform_get_irq_optional(pdev, i);
		if (irq <= 0)
			continue;

		girq->parents[i]	= irq;
		kmb_irq->line	= girq->parents[i];
		kmb_irq->source	= i;
		kmb_irq->trigger	= irq_get_trigger_type(girq->parents[i]);
		kmb_irq->num_share	= 0;

		if (kmb_irq->trigger == IRQ_TYPE_LEVEL_HIGH)
			level_line++;
		else
			edge_line++;
	}

	kpc->max_gpios_level_type = level_line * KEEMBAY_GPIO_MAX_PER_IRQ;
	kpc->max_gpios_edge_type = edge_line * KEEMBAY_GPIO_MAX_PER_IRQ;

	girq->default_type = IRQ_TYPE_NONE;
	girq->handler = handle_bad_irq;

	return devm_gpiochip_add_data(kpc->dev, gc, kpc);
}

static int keembay_build_groups(struct keembay_pinctrl *kpc)
{
	struct pingroup *grp;
	unsigned int i;

	kpc->ngroups = kpc->npins;
	grp = devm_kcalloc(kpc->dev, kpc->ngroups, sizeof(*grp), GFP_KERNEL);
	if (!grp)
		return -ENOMEM;

	/* Each pin is categorised as one group */
	for (i = 0; i < kpc->ngroups; i++) {
		const struct pinctrl_pin_desc *pdesc = keembay_pins + i;
		struct pingroup *kmb_grp = grp + i;

		kmb_grp->name = pdesc->name;
		kmb_grp->pins = (int *)&pdesc->number;
		pinctrl_generic_add_group(kpc->pctrl, kmb_grp->name,
					  kmb_grp->pins, 1, NULL);
	}

	return 0;
}

static int keembay_pinctrl_reg(struct keembay_pinctrl *kpc,  struct device *dev)
{
	int ret;

	keembay_pinctrl_desc.pins = keembay_pins;
	ret = of_property_read_u32(dev->of_node, "ngpios", &kpc->npins);
	if (ret < 0)
		return ret;
	keembay_pinctrl_desc.npins = kpc->npins;

	kpc->pctrl = devm_pinctrl_register(kpc->dev, &keembay_pinctrl_desc, kpc);

	return PTR_ERR_OR_ZERO(kpc->pctrl);
}

static int keembay_add_functions(struct keembay_pinctrl *kpc,
				 struct keembay_pinfunction *functions)
{
	unsigned int i;

	/* Assign the groups for each function */
	for (i = 0; i < kpc->nfuncs; i++) {
		struct keembay_pinfunction *func = &functions[i];
		const char **group_names;
		unsigned int grp_idx = 0;
		int j;

		group_names = devm_kcalloc(kpc->dev, func->func.ngroups,
					   sizeof(*group_names), GFP_KERNEL);
		if (!group_names)
			return -ENOMEM;

		for (j = 0; j < kpc->npins; j++) {
			const struct pinctrl_pin_desc *pdesc = &keembay_pins[j];
			struct keembay_mux_desc *mux;

			for (mux = pdesc->drv_data; mux->name; mux++) {
				if (!strcmp(mux->name, func->func.name))
					group_names[grp_idx++] = pdesc->name;
			}
		}

		func->func.groups = group_names;
	}

	/* Add all functions */
	for (i = 0; i < kpc->nfuncs; i++)
		pinmux_generic_add_pinfunction(kpc->pctrl, &functions[i].func,
					       &functions[i].mux_mode);

	return 0;
}

static int keembay_build_functions(struct keembay_pinctrl *kpc)
{
	struct keembay_pinfunction *keembay_funcs, *new_funcs;
	int i;

	/*
	 * Allocate maximum possible number of functions. Assume every pin
	 * being part of 8 (hw maximum) globally unique muxes.
	 */
	kpc->nfuncs = 0;
	keembay_funcs = devm_kcalloc(kpc->dev, kpc->npins * 8,
				     sizeof(*keembay_funcs), GFP_KERNEL);
	if (!keembay_funcs)
		return -ENOMEM;

	/* Setup 1 function for each unique mux */
	for (i = 0; i < kpc->npins; i++) {
		const struct pinctrl_pin_desc *pdesc = keembay_pins + i;
		struct keembay_mux_desc *mux;

		for (mux = pdesc->drv_data; mux->name; mux++) {
			struct keembay_pinfunction *fdesc;

			/* Check if we already have function for this mux */
			for (fdesc = keembay_funcs; fdesc->func.name; fdesc++) {
				if (!strcmp(mux->name, fdesc->func.name)) {
					fdesc->func.ngroups++;
					break;
				}
			}

			/* Setup new function for this mux we didn't see before */
			if (!fdesc->func.name) {
				fdesc->func.name = mux->name;
				fdesc->func.ngroups = 1;
				fdesc->mux_mode = mux->mode;
				kpc->nfuncs++;
			}
		}
	}

	/* Reallocate memory based on actual number of functions */
	new_funcs = devm_krealloc_array(kpc->dev, keembay_funcs,
					kpc->nfuncs, sizeof(*new_funcs),
					GFP_KERNEL);
	if (!new_funcs)
		return -ENOMEM;

	return keembay_add_functions(kpc, new_funcs);
}

static const struct keembay_pin_soc keembay_data = {
	.pins    = keembay_pins,
};

static const struct of_device_id keembay_pinctrl_match[] = {
	{ .compatible = "intel,keembay-pinctrl", .data = &keembay_data },
	{ }
};
MODULE_DEVICE_TABLE(of, keembay_pinctrl_match);

static int keembay_pinctrl_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct keembay_pinctrl *kpc;
	int ret;

	kpc = devm_kzalloc(dev, sizeof(*kpc), GFP_KERNEL);
	if (!kpc)
		return -ENOMEM;

	kpc->dev = dev;
	kpc->soc = device_get_match_data(dev);

	kpc->base0 = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(kpc->base0))
		return PTR_ERR(kpc->base0);

	kpc->base1 = devm_platform_ioremap_resource(pdev, 1);
	if (IS_ERR(kpc->base1))
		return PTR_ERR(kpc->base1);

	raw_spin_lock_init(&kpc->lock);

	ret = keembay_pinctrl_reg(kpc, dev);
	if (ret)
		return ret;

	ret = keembay_build_groups(kpc);
	if (ret)
		return ret;

	ret = keembay_build_functions(kpc);
	if (ret)
		return ret;

	ret = keembay_gpiochip_probe(kpc, pdev);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, kpc);

	return 0;
}

static struct platform_driver keembay_pinctrl_driver = {
	.probe = keembay_pinctrl_probe,
	.driver = {
		.name = "keembay-pinctrl",
		.of_match_table = keembay_pinctrl_match,
	},
};
module_platform_driver(keembay_pinctrl_driver);

MODULE_AUTHOR("Muhammad Husaini Zulkifli <muhammad.husaini.zulkifli@intel.com>");
MODULE_AUTHOR("Vijayakannan Ayyathurai <vijayakannan.ayyathurai@intel.com>");
MODULE_AUTHOR("Lakshmi Sowjanya D <lakshmi.sowjanya.d@intel.com>");
MODULE_DESCRIPTION("Intel Keem Bay SoC pinctrl/GPIO driver");
MODULE_LICENSE("GPL");
