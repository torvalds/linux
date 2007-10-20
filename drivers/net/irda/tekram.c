/*********************************************************************
 *                
 * Filename:      tekram.c
 * Version:       1.2
 * Description:   Implementation of the Tekram IrMate IR-210B dongle
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Wed Oct 21 20:02:35 1998
 * Modified at:   Fri Dec 17 09:13:09 1999
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 * 
 *     Copyright (c) 1998-1999 Dag Brattli, All Rights Reserved.
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

static void tekram_open(dongle_t *self, struct qos_info *qos);
static void tekram_close(dongle_t *self);
static int  tekram_change_speed(struct irda_task *task);
static int  tekram_reset(struct irda_task *task);

#define TEKRAM_115200 0x00
#define TEKRAM_57600  0x01
#define TEKRAM_38400  0x02
#define TEKRAM_19200  0x03
#define TEKRAM_9600   0x04

#define TEKRAM_PW     0x10 /* Pulse select bit */

static struct dongle_reg dongle = {
	.type = IRDA_TEKRAM_DONGLE,
	.open  = tekram_open,
	.close = tekram_close,
	.reset = tekram_reset,
	.change_speed = tekram_change_speed,
	.owner = THIS_MODULE,
};

static int __init tekram_init(void)
{
	return irda_device_register_dongle(&dongle);
}

static void __exit tekram_cleanup(void)
{
	irda_device_unregister_dongle(&dongle);
}

static void tekram_open(dongle_t *self, struct qos_info *qos)
{
	IRDA_DEBUG(2, "%s()\n", __FUNCTION__ );

	qos->baud_rate.bits &= IR_9600|IR_19200|IR_38400|IR_57600|IR_115200;
	qos->min_turn_time.bits = 0x01; /* Needs at least 10 ms */	
	irda_qos_bits_to_value(qos);
}

static void tekram_close(dongle_t *self)
{
	IRDA_DEBUG(2, "%s()\n", __FUNCTION__ );

	/* Power off dongle */
	self->set_dtr_rts(self->dev, FALSE, FALSE);

	if (self->reset_task)
		irda_task_delete(self->reset_task);
	if (self->speed_task)
		irda_task_delete(self->speed_task);
}

/*
 * Function tekram_change_speed (dev, state, speed)
 *
 *    Set the speed for the Tekram IRMate 210 type dongle. Warning, this 
 *    function must be called with a process context!
 *
 *    Algorithm
 *    1. clear DTR 
 *    2. set RTS, and wait at least 7 us
 *    3. send Control Byte to the IR-210 through TXD to set new baud rate
 *       wait until the stop bit of Control Byte is sent (for 9600 baud rate, 
 *       it takes about 100 msec)
 *    5. clear RTS (return to NORMAL Operation)
 *    6. wait at least 50 us, new setting (baud rate, etc) takes effect here 
 *       after
 */
static int tekram_change_speed(struct irda_task *task)
{
	dongle_t *self = (dongle_t *) task->instance;
	__u32 speed = (__u32) task->param;
	__u8 byte;
	int ret = 0;
	
	IRDA_DEBUG(2, "%s()\n", __FUNCTION__ );

	IRDA_ASSERT(task != NULL, return -1;);

	if (self->speed_task && self->speed_task != task) {
		IRDA_DEBUG(0, "%s(), busy!\n", __FUNCTION__ );
		return msecs_to_jiffies(10);
	} else
		self->speed_task = task;

	switch (speed) {
	default:
	case 9600:
		byte = TEKRAM_PW|TEKRAM_9600;
		break;
	case 19200:
		byte = TEKRAM_PW|TEKRAM_19200;
		break;
	case 38400:
		byte = TEKRAM_PW|TEKRAM_38400;
		break;
	case 57600:
		byte = TEKRAM_PW|TEKRAM_57600;
		break;
	case 115200:
		byte = TEKRAM_115200;
		break;
	}

	switch (task->state) {
	case IRDA_TASK_INIT:
	case IRDA_TASK_CHILD_INIT:		
		/* 
		 * Need to reset the dongle and go to 9600 bps before
                 * programming 
		 */
		if (irda_task_execute(self, tekram_reset, NULL, task, 
				      (void *) speed))
		{
			/* Dongle need more time to reset */
			irda_task_next_state(task, IRDA_TASK_CHILD_WAIT);

			/* Give reset 1 sec to finish */
			ret = msecs_to_jiffies(1000);
		} else
			irda_task_next_state(task, IRDA_TASK_CHILD_DONE);
		break;
	case IRDA_TASK_CHILD_WAIT:
		IRDA_WARNING("%s(), resetting dongle timed out!\n",
			     __FUNCTION__);
		ret = -1;
		break;
	case IRDA_TASK_CHILD_DONE:
		/* Set DTR, Clear RTS */
		self->set_dtr_rts(self->dev, TRUE, FALSE);
	
		/* Wait at least 7us */
		udelay(14);

		/* Write control byte */
		self->write(self->dev, &byte, 1);
		
		irda_task_next_state(task, IRDA_TASK_WAIT);

		/* Wait at least 100 ms */
		ret = msecs_to_jiffies(150);
		break;
	case IRDA_TASK_WAIT:
		/* Set DTR, Set RTS */
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
 * Function tekram_reset (driver)
 *
 *      This function resets the tekram dongle. Warning, this function 
 *      must be called with a process context!! 
 *
 *      Algorithm:
 *    	  0. Clear RTS and DTR, and wait 50 ms (power off the IR-210 )
 *        1. clear RTS 
 *        2. set DTR, and wait at least 1 ms 
 *        3. clear DTR to SPACE state, wait at least 50 us for further 
 *         operation
 */
int tekram_reset(struct irda_task *task)
{
	dongle_t *self = (dongle_t *) task->instance;
	int ret = 0;

	IRDA_DEBUG(2, "%s()\n", __FUNCTION__ );

	IRDA_ASSERT(task != NULL, return -1;);

	if (self->reset_task && self->reset_task != task) {
		IRDA_DEBUG(0, "%s(), busy!\n", __FUNCTION__ );
		return msecs_to_jiffies(10);
	} else
		self->reset_task = task;
	
	/* Power off dongle */
	//self->set_dtr_rts(self->dev, FALSE, FALSE);
	self->set_dtr_rts(self->dev, TRUE, TRUE);

	switch (task->state) {
	case IRDA_TASK_INIT:
		irda_task_next_state(task, IRDA_TASK_WAIT1);

		/* Sleep 50 ms */
		ret = msecs_to_jiffies(50);
		break;
	case IRDA_TASK_WAIT1:
		/* Clear DTR, Set RTS */
		self->set_dtr_rts(self->dev, FALSE, TRUE); 

		irda_task_next_state(task, IRDA_TASK_WAIT2);
		
		/* Should sleep 1 ms */
		ret = msecs_to_jiffies(1);
		break;
	case IRDA_TASK_WAIT2:
		/* Set DTR, Set RTS */
		self->set_dtr_rts(self->dev, TRUE, TRUE);
	
		/* Wait at least 50 us */
		udelay(75);

		irda_task_next_state(task, IRDA_TASK_DONE);
		self->reset_task = NULL;
		break;
	default:
		IRDA_ERROR("%s(), unknown state %d\n",
			   __FUNCTION__, task->state);
		irda_task_next_state(task, IRDA_TASK_DONE);		
		self->reset_task = NULL;
		ret = -1;
	}
	return ret;
}

MODULE_AUTHOR("Dag Brattli <dagb@cs.uit.no>");
MODULE_DESCRIPTION("Tekram IrMate IR-210B dongle driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("irda-dongle-0"); /* IRDA_TEKRAM_DONGLE */
		
/*
 * Function init_module (void)
 *
 *    Initialize Tekram module
 *
 */
module_init(tekram_init);

/*
 * Function cleanup_module (void)
 *
 *    Cleanup Tekram module
 *
 */
module_exit(tekram_cleanup);
