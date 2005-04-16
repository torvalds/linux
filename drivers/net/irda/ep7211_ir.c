/*
 * IR port driver for the Cirrus Logic EP7211 processor.
 *
 * Copyright 2001, Blue Mug Inc.  All rights reserved.
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/tty.h>
#include <linux/init.h>

#include <net/irda/irda.h>
#include <net/irda/irda_device.h>

#include <asm/io.h>
#include <asm/hardware.h>

#define MIN_DELAY 25      /* 15 us, but wait a little more to be sure */
#define MAX_DELAY 10000   /* 1 ms */

static void ep7211_ir_open(dongle_t *self, struct qos_info *qos);
static void ep7211_ir_close(dongle_t *self);
static int  ep7211_ir_change_speed(struct irda_task *task);
static int  ep7211_ir_reset(struct irda_task *task);

static struct dongle_reg dongle = {
	.type = IRDA_EP7211_IR,
	.open = ep7211_ir_open,
	.close = ep7211_ir_close,
	.reset = ep7211_ir_reset,
	.change_speed = ep7211_ir_change_speed,
	.owner = THIS_MODULE,
};

static void ep7211_ir_open(dongle_t *self, struct qos_info *qos)
{
	unsigned int syscon1, flags;

	save_flags(flags); cli();

	/* Turn on the SIR encoder. */
	syscon1 = clps_readl(SYSCON1);
	syscon1 |= SYSCON1_SIREN;
	clps_writel(syscon1, SYSCON1);

	/* XXX: We should disable modem status interrupts on the first
		UART (interrupt #14). */

	restore_flags(flags);
}

static void ep7211_ir_close(dongle_t *self)
{
	unsigned int syscon1, flags;

	save_flags(flags); cli();

	/* Turn off the SIR encoder. */
	syscon1 = clps_readl(SYSCON1);
	syscon1 &= ~SYSCON1_SIREN;
	clps_writel(syscon1, SYSCON1);

	/* XXX: If we've disabled the modem status interrupts, we should
		reset them back to their original state. */

	restore_flags(flags);
}

/*
 * Function ep7211_ir_change_speed (task)
 *
 *    Change speed of the EP7211 I/R port. We don't really have to do anything
 *    for the EP7211 as long as the rate is being changed at the serial port
 *    level.
 */
static int ep7211_ir_change_speed(struct irda_task *task)
{
	irda_task_next_state(task, IRDA_TASK_DONE);
	return 0;
}

/*
 * Function ep7211_ir_reset (task)
 *
 *      Reset the EP7211 I/R. We don't really have to do anything.
 *
 */
static int ep7211_ir_reset(struct irda_task *task)
{
	irda_task_next_state(task, IRDA_TASK_DONE);
	return 0;
}

/*
 * Function ep7211_ir_init(void)
 *
 *    Initialize EP7211 I/R module
 *
 */
static int __init ep7211_ir_init(void)
{
	return irda_device_register_dongle(&dongle);
}

/*
 * Function ep7211_ir_cleanup(void)
 *
 *    Cleanup EP7211 I/R module
 *
 */
static void __exit ep7211_ir_cleanup(void)
{
	irda_device_unregister_dongle(&dongle);
}

MODULE_AUTHOR("Jon McClintock <jonm@bluemug.com>");
MODULE_DESCRIPTION("EP7211 I/R driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("irda-dongle-8"); /* IRDA_EP7211_IR */
		
module_init(ep7211_ir_init);
module_exit(ep7211_ir_cleanup);
