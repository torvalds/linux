/*
 * Some platform data for the RMI4 touchscreen that will override the __weak
 * platform data in the Ux500 machine if this driver is activated.
 */
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <mach/irqs.h>
#include "synaptics_i2c_rmi4.h"

/*
 * Synaptics RMI4 touchscreen interface on the U8500 UIB
 */

/*
 * Descriptor structure.
 * Describes the number of i2c devices on the bus that speak RMI.
 */
static struct synaptics_rmi4_platform_data rmi4_i2c_dev_platformdata = {
	.irq_number     = NOMADIK_GPIO_TO_IRQ(84),
	.irq_type       = (IRQF_TRIGGER_FALLING | IRQF_SHARED),
	.x_flip		= false,
	.y_flip		= true,
};

struct i2c_board_info __initdata mop500_i2c3_devices_u8500[] = {
	{
		I2C_BOARD_INFO("synaptics_rmi4_i2c", 0x4B),
		.platform_data = &rmi4_i2c_dev_platformdata,
	},
};
