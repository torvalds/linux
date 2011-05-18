#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/pm.h>

#include <mach/rk29_iomap.h>
#include <mach/gpio.h>
#include <mach/cru.h>

#define POWER_ON_PIN	RK29_PIN4_PA4
#define PLAY_ON_PIN	RK29_PIN6_PA7

static void rk29_pm_power_off(void)
{
	int count = 0;

	local_irq_disable();
	local_fiq_disable();

	printk(KERN_ERR "rk29_pm_power_off start...\n");

	/* arm enter slow mode */
	cru_writel((cru_readl(CRU_MODE_CON) & ~CRU_CPU_MODE_MASK) | CRU_CPU_MODE_SLOW, CRU_MODE_CON);
	LOOP(LOOPS_PER_USEC);

	while (1) {
		/* shut down the power by GPIO. */
		if (gpio_get_value(POWER_ON_PIN) == GPIO_HIGH) {
			printk("POWER_ON_PIN is high\n");
			gpio_set_value(POWER_ON_PIN, GPIO_LOW);
		}

		LOOP(5 * LOOPS_PER_MSEC);

		/* only normal power off can restart system safely */
		if (system_state != SYSTEM_POWER_OFF)
			continue;

		if (gpio_get_value(PLAY_ON_PIN) != GPIO_HIGH) {
			if (!count)
				printk("PLAY_ON_PIN is low\n");
			if (50 == count) /* break if keep low about 250ms */
				break;
			count++;
		} else {
			count = 0;
		}
	}

	printk("system reboot\n");
	gpio_set_value(POWER_ON_PIN, GPIO_HIGH);
	system_state = SYSTEM_RESTART;
	arm_pm_restart(0, NULL);

	while (1);
}

int __init board_power_init(void)
{
	gpio_request(POWER_ON_PIN, "poweronpin");
	gpio_set_value(POWER_ON_PIN, GPIO_HIGH);
	gpio_direction_output(POWER_ON_PIN, GPIO_HIGH);
	pm_power_off = rk29_pm_power_off;

	return 0;
}

