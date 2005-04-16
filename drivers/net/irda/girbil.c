/*********************************************************************
 *                
 * Filename:      girbil.c
 * Version:       1.2
 * Description:   Implementation for the Greenwich GIrBIL dongle
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Sat Feb  6 21:02:33 1999
 * Modified at:   Fri Dec 17 09:13:20 1999
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 * 
 *     Copyright (c) 1999 Dag Brattli, All Rights Reserved.
 *      
 *     This program is free software; you can redistribute it and/or 
 *     modify it under the terms of the GNU General Public License as 
 *     published by the Free Software Foundation; either version 2 of 
 *     the License, or (at your option) any later version.
 *  
 *     Neither Dag Brattli nor University of Tromsø admit liability nor
 *     provide warranty for any of this software. This material is 
 *     provided "AS-IS" and at no charge.
 *     
 ********************************************************************/

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/tty.h>
#include <linux/init.h>

#include <net/irda/irda.h>
#include <net/irda/irda_device.h>

static int  girbil_reset(struct irda_task *task);
static void girbil_open(dongle_t *self, struct qos_info *qos);
static void girbil_close(dongle_t *self);
static int  girbil_change_speed(struct irda_task *task);

/* Control register 1 */
#define GIRBIL_TXEN    0x01 /* Enable transmitter */
#define GIRBIL_RXEN    0x02 /* Enable receiver */
#define GIRBIL_ECAN    0x04 /* Cancel self emmited data */
#define GIRBIL_ECHO    0x08 /* Echo control characters */

/* LED Current Register (0x2) */
#define GIRBIL_HIGH    0x20
#define GIRBIL_MEDIUM  0x21
#define GIRBIL_LOW     0x22

/* Baud register (0x3) */
#define GIRBIL_2400    0x30
#define GIRBIL_4800    0x31	
#define GIRBIL_9600    0x32
#define GIRBIL_19200   0x33
#define GIRBIL_38400   0x34	
#define GIRBIL_57600   0x35	
#define GIRBIL_115200  0x36

/* Mode register (0x4) */
#define GIRBIL_IRDA    0x40
#define GIRBIL_ASK     0x41

/* Control register 2 (0x5) */
#define GIRBIL_LOAD    0x51 /* Load the new baud rate value */

static struct dongle_reg dongle = {
	.type = IRDA_GIRBIL_DONGLE,
	.open = girbil_open,
	.close = girbil_close,
	.reset = girbil_reset,
	.change_speed = girbil_change_speed,
	.owner = THIS_MODULE,
};

static int __init girbil_init(void)
{
	return irda_device_register_dongle(&dongle);
}

static void __exit girbil_cleanup(void)
{
	irda_device_unregister_dongle(&dongle);
}

static void girbil_open(dongle_t *self, struct qos_info *qos)
{
	qos->baud_rate.bits &= IR_9600|IR_19200|IR_38400|IR_57600|IR_115200;
	qos->min_turn_time.bits = 0x03;
}

static void girbil_close(dongle_t *self)
{
	/* Power off dongle */
	self->set_dtr_rts(self->dev, FALSE, FALSE);
}

/*
 * Function girbil_change_speed (dev, speed)
 *
 *    Set the speed for the Girbil type dongle.
 *
 */
static int girbil_change_speed(struct irda_task *task)
{
	dongle_t *self = (dongle_t *) task->instance;
	__u32 speed = (__u32) task->param;
	__u8 control[2];
	int ret = 0;

	self->speed_task = task;

	switch (task->state) {
	case IRDA_TASK_INIT:
		/* Need to reset the dongle and go to 9600 bps before
                   programming */
		if (irda_task_execute(self, girbil_reset, NULL, task, 
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
		/* Set DTR and Clear RTS to enter command mode */
		self->set_dtr_rts(self->dev, FALSE, TRUE);

		switch (speed) {
		case 9600:
		default:
			control[0] = GIRBIL_9600;
			break;
		case 19200:
			control[0] = GIRBIL_19200;
			break;
		case 34800:
			control[0] = GIRBIL_38400;
			break;
		case 57600:
			control[0] = GIRBIL_57600;
			break;
		case 115200:
			control[0] = GIRBIL_115200;
			break;
		}
		control[1] = GIRBIL_LOAD;
		
		/* Write control bytes */
		self->write(self->dev, control, 2);
		irda_task_next_state(task, IRDA_TASK_WAIT);
		ret = msecs_to_jiffies(100);
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
 * Function girbil_reset (driver)
 *
 *      This function resets the girbil dongle.
 *
 *      Algorithm:
 *    	  0. set RTS, and wait at least 5 ms 
 *        1. clear RTS 
 */
static int girbil_reset(struct irda_task *task)
{
	dongle_t *self = (dongle_t *) task->instance;
	__u8 control = GIRBIL_TXEN | GIRBIL_RXEN;
	int ret = 0;

	self->reset_task = task;

	switch (task->state) {
	case IRDA_TASK_INIT:
		/* Reset dongle */
		self->set_dtr_rts(self->dev, TRUE, FALSE);
		irda_task_next_state(task, IRDA_TASK_WAIT1);
		/* Sleep at least 5 ms */
		ret = msecs_to_jiffies(20);
		break;
	case IRDA_TASK_WAIT1:
		/* Set DTR and clear RTS to enter command mode */
		self->set_dtr_rts(self->dev, FALSE, TRUE);
		irda_task_next_state(task, IRDA_TASK_WAIT2);
		ret = msecs_to_jiffies(20);
		break;
	case IRDA_TASK_WAIT2:
		/* Write control byte */
		self->write(self->dev, &control, 1);
		irda_task_next_state(task, IRDA_TASK_WAIT3);
		ret = msecs_to_jiffies(20);
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

MODULE_AUTHOR("Dag Brattli <dagb@cs.uit.no>");
MODULE_DESCRIPTION("Greenwich GIrBIL dongle driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("irda-dongle-4"); /* IRDA_GIRBIL_DONGLE */
	
/*
 * Function init_module (void)
 *
 *    Initialize Girbil module
 *
 */
module_init(girbil_init);

/*
 * Function cleanup_module (void)
 *
 *    Cleanup Girbil module
 *
 */
module_exit(girbil_cleanup);

