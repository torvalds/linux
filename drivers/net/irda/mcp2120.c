/*********************************************************************
 *            
 *    
 * Filename:      mcp2120.c
 * Version:       1.0
 * Description:   Implementation for the MCP2120 (Microchip)
 * Status:        Experimental.
 * Author:        Felix Tang (tangf@eyetap.org)
 * Created at:    Sun Mar 31 19:32:12 EST 2002
 * Based on code by:   Dag Brattli <dagb@cs.uit.no>
 * 
 *     Copyright (c) 2002 Felix Tang, All Rights Reserved.
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

static int  mcp2120_reset(struct irda_task *task);
static void mcp2120_open(dongle_t *self, struct qos_info *qos);
static void mcp2120_close(dongle_t *self);
static int  mcp2120_change_speed(struct irda_task *task);

#define MCP2120_9600    0x87
#define MCP2120_19200   0x8B
#define MCP2120_38400   0x85
#define MCP2120_57600   0x83
#define MCP2120_115200  0x81

#define MCP2120_COMMIT  0x11

static struct dongle_reg dongle = {
	.type = IRDA_MCP2120_DONGLE,
	.open = mcp2120_open,
	.close = mcp2120_close,
	.reset = mcp2120_reset,
	.change_speed = mcp2120_change_speed,
	.owner = THIS_MODULE,
};

static int __init mcp2120_init(void)
{
	return irda_device_register_dongle(&dongle);
}

static void __exit mcp2120_cleanup(void)
{
	irda_device_unregister_dongle(&dongle);
}

static void mcp2120_open(dongle_t *self, struct qos_info *qos)
{
	qos->baud_rate.bits &= IR_9600|IR_19200|IR_38400|IR_57600|IR_115200;
	qos->min_turn_time.bits = 0x01;
}

static void mcp2120_close(dongle_t *self)
{
	/* Power off dongle */
        /* reset and inhibit mcp2120 */
	self->set_dtr_rts(self->dev, TRUE, TRUE);
	//self->set_dtr_rts(self->dev, FALSE, FALSE);
}

/*
 * Function mcp2120_change_speed (dev, speed)
 *
 *    Set the speed for the MCP2120.
 *
 */
static int mcp2120_change_speed(struct irda_task *task)
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
                //printk("Dmcp2120_change_speed irda_task_init\n");
		if (irda_task_execute(self, mcp2120_reset, NULL, task, 
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
		/* Set DTR to enter command mode */
		self->set_dtr_rts(self->dev, TRUE, FALSE);
                udelay(500);

		switch (speed) {
		case 9600:
		default:
			control[0] = MCP2120_9600;
                        //printk("mcp2120 9600\n");
			break;
		case 19200:
			control[0] = MCP2120_19200;
                        //printk("mcp2120 19200\n");
			break;
		case 34800:
			control[0] = MCP2120_38400;
                        //printk("mcp2120 38400\n");
			break;
		case 57600:
			control[0] = MCP2120_57600;
                        //printk("mcp2120 57600\n");
			break;
		case 115200:
                        control[0] = MCP2120_115200;
                        //printk("mcp2120 115200\n");
			break;
		}
	        control[1] = MCP2120_COMMIT;
	
		/* Write control bytes */
                self->write(self->dev, control, 2);
 
                irda_task_next_state(task, IRDA_TASK_WAIT);
		ret = msecs_to_jiffies(100);
                //printk("mcp2120_change_speed irda_child_done\n");
		break;
	case IRDA_TASK_WAIT:
		/* Go back to normal mode */
		self->set_dtr_rts(self->dev, FALSE, FALSE);
		irda_task_next_state(task, IRDA_TASK_DONE);
		self->speed_task = NULL;
                //printk("mcp2120_change_speed irda_task_wait\n");
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
 * Function mcp2120_reset (driver)
 *
 *      This function resets the mcp2120 dongle.
 *      
 *      Info: -set RTS to reset mcp2120
 *            -set DTR to set mcp2120 software command mode
 *            -mcp2120 defaults to 9600 baud after reset
 *
 *      Algorithm:
 *      0. Set RTS to reset mcp2120.
 *      1. Clear RTS and wait for device reset timer of 30 ms (max).
 *      
 */


static int mcp2120_reset(struct irda_task *task)
{
	dongle_t *self = (dongle_t *) task->instance;
	int ret = 0;

	self->reset_task = task;

	switch (task->state) {
	case IRDA_TASK_INIT:
                //printk("mcp2120_reset irda_task_init\n");
		/* Reset dongle by setting RTS*/
		self->set_dtr_rts(self->dev, TRUE, TRUE);
		irda_task_next_state(task, IRDA_TASK_WAIT1);
		ret = msecs_to_jiffies(50);
		break;
	case IRDA_TASK_WAIT1:
                //printk("mcp2120_reset irda_task_wait1\n");
                /* clear RTS and wait for at least 30 ms. */
		self->set_dtr_rts(self->dev, FALSE, FALSE);
		irda_task_next_state(task, IRDA_TASK_WAIT2);
		ret = msecs_to_jiffies(50);
		break;
	case IRDA_TASK_WAIT2:
                //printk("mcp2120_reset irda_task_wait2\n");
		/* Go back to normal mode */
		self->set_dtr_rts(self->dev, FALSE, FALSE);
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

MODULE_AUTHOR("Felix Tang <tangf@eyetap.org>");
MODULE_DESCRIPTION("Microchip MCP2120");
MODULE_LICENSE("GPL");
MODULE_ALIAS("irda-dongle-9"); /* IRDA_MCP2120_DONGLE */
	
/*
 * Function init_module (void)
 *
 *    Initialize MCP2120 module
 *
 */
module_init(mcp2120_init);

/*
 * Function cleanup_module (void)
 *
 *    Cleanup MCP2120 module
 *
 */
module_exit(mcp2120_cleanup);
