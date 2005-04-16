/*********************************************************************
 *
 * Filename:      act200l.c
 * Version:       0.8
 * Description:   Implementation for the ACTiSYS ACT-IR200L dongle
 * Status:        Experimental.
 * Author:        SHIMIZU Takuya <tshimizu@ga2.so-net.ne.jp>
 * Created at:    Fri Aug  3 17:35:42 2001
 * Modified at:   Fri Aug 17 10:22:40 2001
 * Modified by:   SHIMIZU Takuya <tshimizu@ga2.so-net.ne.jp>
 *
 *     Copyright (c) 2001 SHIMIZU Takuya, All Rights Reserved.
 *
 *     This program is free software; you can redistribute it and/or
 *     modify it under the terms of the GNU General Public License as
 *     published by the Free Software Foundation; either version 2 of
 *     the License, or (at your option) any later version.
 *
 ********************************************************************/

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/tty.h>
#include <linux/init.h>

#include <net/irda/irda.h>
#include <net/irda/irda_device.h>

static int  act200l_reset(struct irda_task *task);
static void act200l_open(dongle_t *self, struct qos_info *qos);
static void act200l_close(dongle_t *self);
static int  act200l_change_speed(struct irda_task *task);

/* Regsiter 0: Control register #1 */
#define ACT200L_REG0    0x00
#define ACT200L_TXEN    0x01 /* Enable transmitter */
#define ACT200L_RXEN    0x02 /* Enable receiver */

/* Register 1: Control register #2 */
#define ACT200L_REG1    0x10
#define ACT200L_LODB    0x01 /* Load new baud rate count value */
#define ACT200L_WIDE    0x04 /* Expand the maximum allowable pulse */

/* Register 4: Output Power register */
#define ACT200L_REG4    0x40
#define ACT200L_OP0     0x01 /* Enable LED1C output */
#define ACT200L_OP1     0x02 /* Enable LED2C output */
#define ACT200L_BLKR    0x04

/* Register 5: Receive Mode register */
#define ACT200L_REG5    0x50
#define ACT200L_RWIDL   0x01 /* fixed 1.6us pulse mode */

/* Register 6: Receive Sensitivity register #1 */
#define ACT200L_REG6    0x60
#define ACT200L_RS0     0x01 /* receive threshold bit 0 */
#define ACT200L_RS1     0x02 /* receive threshold bit 1 */

/* Register 7: Receive Sensitivity register #2 */
#define ACT200L_REG7    0x70
#define ACT200L_ENPOS   0x04 /* Ignore the falling edge */

/* Register 8,9: Baud Rate Dvider register #1,#2 */
#define ACT200L_REG8    0x80
#define ACT200L_REG9    0x90

#define ACT200L_2400    0x5f
#define ACT200L_9600    0x17
#define ACT200L_19200   0x0b
#define ACT200L_38400   0x05
#define ACT200L_57600   0x03
#define ACT200L_115200  0x01

/* Register 13: Control register #3 */
#define ACT200L_REG13   0xd0
#define ACT200L_SHDW    0x01 /* Enable access to shadow registers */

/* Register 15: Status register */
#define ACT200L_REG15   0xf0

/* Register 21: Control register #4 */
#define ACT200L_REG21   0x50
#define ACT200L_EXCK    0x02 /* Disable clock output driver */
#define ACT200L_OSCL    0x04 /* oscillator in low power, medium accuracy mode */

static struct dongle_reg dongle = {
	.type = IRDA_ACT200L_DONGLE,
	.open = act200l_open,
	.close = act200l_close,
	.reset = act200l_reset,
	.change_speed = act200l_change_speed,
	.owner = THIS_MODULE,
};

static int __init act200l_init(void)
{
	return irda_device_register_dongle(&dongle);
}

static void __exit act200l_cleanup(void)
{
	irda_device_unregister_dongle(&dongle);
}

static void act200l_open(dongle_t *self, struct qos_info *qos)
{
	IRDA_DEBUG(2, "%s()\n", __FUNCTION__ );

	/* Power on the dongle */
	self->set_dtr_rts(self->dev, TRUE, TRUE);

	/* Set the speeds we can accept */
	qos->baud_rate.bits &= IR_9600|IR_19200|IR_38400|IR_57600|IR_115200;
	qos->min_turn_time.bits = 0x03;
}

static void act200l_close(dongle_t *self)
{
	IRDA_DEBUG(2, "%s()\n", __FUNCTION__ );

	/* Power off the dongle */
	self->set_dtr_rts(self->dev, FALSE, FALSE);
}

/*
 * Function act200l_change_speed (dev, speed)
 *
 *    Set the speed for the ACTiSYS ACT-IR200L type dongle.
 *
 */
static int act200l_change_speed(struct irda_task *task)
{
	dongle_t *self = (dongle_t *) task->instance;
	__u32 speed = (__u32) task->param;
	__u8 control[3];
	int ret = 0;

	IRDA_DEBUG(2, "%s()\n", __FUNCTION__ );

	self->speed_task = task;

	switch (task->state) {
	case IRDA_TASK_INIT:
		if (irda_task_execute(self, act200l_reset, NULL, task,
				(void *) speed))
		{
			/* Dongle need more time to reset */
			irda_task_next_state(task, IRDA_TASK_CHILD_WAIT);

			/* Give reset 1 sec to finish */
			ret = msecs_to_jiffies(1000);
		}
		break;
	case IRDA_TASK_CHILD_WAIT:
		IRDA_WARNING("%s(), resetting dongle timed out!\n",
			     __FUNCTION__);
		ret = -1;
		break;
	case IRDA_TASK_CHILD_DONE:
		/* Clear DTR and set RTS to enter command mode */
		self->set_dtr_rts(self->dev, FALSE, TRUE);

		switch (speed) {
		case 9600:
		default:
			control[0] = ACT200L_REG8 |  (ACT200L_9600       & 0x0f);
			control[1] = ACT200L_REG9 | ((ACT200L_9600 >> 4) & 0x0f);
			break;
		case 19200:
			control[0] = ACT200L_REG8 |  (ACT200L_19200       & 0x0f);
			control[1] = ACT200L_REG9 | ((ACT200L_19200 >> 4) & 0x0f);
			break;
		case 38400:
			control[0] = ACT200L_REG8 |  (ACT200L_38400       & 0x0f);
			control[1] = ACT200L_REG9 | ((ACT200L_38400 >> 4) & 0x0f);
			break;
		case 57600:
			control[0] = ACT200L_REG8 |  (ACT200L_57600       & 0x0f);
			control[1] = ACT200L_REG9 | ((ACT200L_57600 >> 4) & 0x0f);
			break;
		case 115200:
			control[0] = ACT200L_REG8 |  (ACT200L_115200       & 0x0f);
			control[1] = ACT200L_REG9 | ((ACT200L_115200 >> 4) & 0x0f);
			break;
		}
		control[2] = ACT200L_REG1 | ACT200L_LODB | ACT200L_WIDE;

		/* Write control bytes */
		self->write(self->dev, control, 3);
		irda_task_next_state(task, IRDA_TASK_WAIT);
		ret = msecs_to_jiffies(5);
		break;
	case IRDA_TASK_WAIT:
		/* Go back to normal mode */
		self->set_dtr_rts(self->dev, TRUE, TRUE);

		irda_task_next_state(task, IRDA_TASK_DONE);
		self->speed_task = NULL;
		break;
	default:
		IRDA_ERROR("%s(), unknown state %d\n",
			   __FUNCTION__, task->state);
		irda_task_next_state(task, IRDA_TASK_DONE);
		self->speed_task = NULL;
		ret = -1;
		break;
	}
	return ret;
}

/*
 * Function act200l_reset (driver)
 *
 *    Reset the ACTiSYS ACT-IR200L type dongle.
 */
static int act200l_reset(struct irda_task *task)
{
	dongle_t *self = (dongle_t *) task->instance;
	__u8 control[9] = {
		ACT200L_REG15,
		ACT200L_REG13 | ACT200L_SHDW,
		ACT200L_REG21 | ACT200L_EXCK | ACT200L_OSCL,
		ACT200L_REG13,
		ACT200L_REG7  | ACT200L_ENPOS,
		ACT200L_REG6  | ACT200L_RS0  | ACT200L_RS1,
		ACT200L_REG5  | ACT200L_RWIDL,
		ACT200L_REG4  | ACT200L_OP0  | ACT200L_OP1 | ACT200L_BLKR,
		ACT200L_REG0  | ACT200L_TXEN | ACT200L_RXEN
	};
	int ret = 0;

	IRDA_DEBUG(2, "%s()\n", __FUNCTION__ );

	self->reset_task = task;

	switch (task->state) {
	case IRDA_TASK_INIT:
		/* Power on the dongle */
		self->set_dtr_rts(self->dev, TRUE, TRUE);

		irda_task_next_state(task, IRDA_TASK_WAIT1);
		ret = msecs_to_jiffies(50);
		break;
	case IRDA_TASK_WAIT1:
		/* Reset the dongle : set RTS low for 25 ms */
		self->set_dtr_rts(self->dev, TRUE, FALSE);

		irda_task_next_state(task, IRDA_TASK_WAIT2);
		ret = msecs_to_jiffies(50);
		break;
	case IRDA_TASK_WAIT2:
		/* Clear DTR and set RTS to enter command mode */
		self->set_dtr_rts(self->dev, FALSE, TRUE);

		/* Write control bytes */
		self->write(self->dev, control, 9);
		irda_task_next_state(task, IRDA_TASK_WAIT3);
		ret = msecs_to_jiffies(15);
		break;
	case IRDA_TASK_WAIT3:
		/* Go back to normal mode */
		self->set_dtr_rts(self->dev, TRUE, TRUE);

		irda_task_next_state(task, IRDA_TASK_DONE);
		self->reset_task = NULL;
		break;
	default:
		IRDA_ERROR("%s(), unknown state %d\n",
			   __FUNCTION__, task->state);
		irda_task_next_state(task, IRDA_TASK_DONE);
		self->reset_task = NULL;
		ret = -1;
		break;
	}
	return ret;
}

MODULE_AUTHOR("SHIMIZU Takuya <tshimizu@ga2.so-net.ne.jp>");
MODULE_DESCRIPTION("ACTiSYS ACT-IR200L dongle driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("irda-dongle-10"); /* IRDA_ACT200L_DONGLE */

/*
 * Function init_module (void)
 *
 *    Initialize ACTiSYS ACT-IR200L module
 *
 */
module_init(act200l_init);

/*
 * Function cleanup_module (void)
 *
 *    Cleanup ACTiSYS ACT-IR200L module
 *
 */
module_exit(act200l_cleanup);
