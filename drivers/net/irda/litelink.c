/*********************************************************************
 *                
 * Filename:      litelink.c
 * Version:       1.1
 * Description:   Driver for the Parallax LiteLink dongle
 * Status:        Stable
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Fri May  7 12:50:33 1999
 * Modified at:   Fri Dec 17 09:14:23 1999
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 * 
 *     Copyright (c) 1999 Dag Brattli, All Rights Reserved.
 *     
 *     This program is free software; you can redistribute it and/or 
 *     modify it under the terms of the GNU General Public License as 
 *     published by the Free Software Foundation; either version 2 of 
 *     the License, or (at your option) any later version.
 * 
 *     This program is distributed in the hope that it will be useful,
 *     but WITHOUT ANY WARRANTY; without even the implied warranty of
 *     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *     GNU General Public License for more details.
 * 
 *     You should have received a copy of the GNU General Public License 
 *     along with this program; if not, write to the Free Software 
 *     Foundation, Inc., 59 Temple Place, Suite 330, Boston, 
 *     MA 02111-1307 USA
 *     
 ********************************************************************/

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/tty.h>
#include <linux/init.h>

#include <net/irda/irda.h>
#include <net/irda/irda_device.h>

#define MIN_DELAY 25      /* 15 us, but wait a little more to be sure */
#define MAX_DELAY 10000   /* 1 ms */

static void litelink_open(dongle_t *self, struct qos_info *qos);
static void litelink_close(dongle_t *self);
static int  litelink_change_speed(struct irda_task *task);
static int  litelink_reset(struct irda_task *task);

/* These are the baudrates supported */
static __u32 baud_rates[] = { 115200, 57600, 38400, 19200, 9600 };

static struct dongle_reg dongle = {
	.type = IRDA_LITELINK_DONGLE,
	.open = litelink_open,
	.close = litelink_close,
	.reset = litelink_reset,
	.change_speed = litelink_change_speed,
	.owner = THIS_MODULE,
};

static int __init litelink_init(void)
{
	return irda_device_register_dongle(&dongle);
}

static void __exit litelink_cleanup(void)
{
	irda_device_unregister_dongle(&dongle);
}

static void litelink_open(dongle_t *self, struct qos_info *qos)
{
	qos->baud_rate.bits &= IR_9600|IR_19200|IR_38400|IR_57600|IR_115200;
	qos->min_turn_time.bits = 0x7f; /* Needs 0.01 ms */
}

static void litelink_close(dongle_t *self)
{
	/* Power off dongle */
	self->set_dtr_rts(self->dev, FALSE, FALSE);
}

/*
 * Function litelink_change_speed (task)
 *
 *    Change speed of the Litelink dongle. To cycle through the available 
 *    baud rates, pulse RTS low for a few ms.  
 */
static int litelink_change_speed(struct irda_task *task)
{
	dongle_t *self = (dongle_t *) task->instance;
	__u32 speed = (__u32) task->param;
        int i;
	
	/* Clear RTS to reset dongle */
	self->set_dtr_rts(self->dev, TRUE, FALSE);

	/* Sleep a minimum of 15 us */
	udelay(MIN_DELAY);

	/* Go back to normal mode */
	self->set_dtr_rts(self->dev, TRUE, TRUE);
	
	/* Sleep a minimum of 15 us */
	udelay(MIN_DELAY);
	
	/* Cycle through avaiable baudrates until we reach the correct one */
	for (i=0; i<5 && baud_rates[i] != speed; i++) {
		/* Set DTR, clear RTS */
		self->set_dtr_rts(self->dev, FALSE, TRUE);
		
		/* Sleep a minimum of 15 us */
		udelay(MIN_DELAY);
		
		/* Set DTR, Set RTS */
		self->set_dtr_rts(self->dev, TRUE, TRUE);
		
		/* Sleep a minimum of 15 us */
		udelay(MIN_DELAY);
        }
	irda_task_next_state(task, IRDA_TASK_DONE);

	return 0;
}

/*
 * Function litelink_reset (task)
 *
 *      Reset the Litelink type dongle.
 *
 */
static int litelink_reset(struct irda_task *task)
{
	dongle_t *self = (dongle_t *) task->instance;

	/* Power on dongle */
	self->set_dtr_rts(self->dev, TRUE, TRUE);

	/* Sleep a minimum of 15 us */
	udelay(MIN_DELAY);

	/* Clear RTS to reset dongle */
	self->set_dtr_rts(self->dev, TRUE, FALSE);

	/* Sleep a minimum of 15 us */
	udelay(MIN_DELAY);

	/* Go back to normal mode */
	self->set_dtr_rts(self->dev, TRUE, TRUE);
	
	/* Sleep a minimum of 15 us */
	udelay(MIN_DELAY);

	/* This dongles speed defaults to 115200 bps */
	self->speed = 115200;

	irda_task_next_state(task, IRDA_TASK_DONE);

	return 0;
}

MODULE_AUTHOR("Dag Brattli <dagb@cs.uit.no>");
MODULE_DESCRIPTION("Parallax Litelink dongle driver");	
MODULE_LICENSE("GPL");
MODULE_ALIAS("irda-dongle-5"); /* IRDA_LITELINK_DONGLE */
		
/*
 * Function init_module (void)
 *
 *    Initialize Litelink module
 *
 */
module_init(litelink_init);

/*
 * Function cleanup_module (void)
 *
 *    Cleanup Litelink module
 *
 */
module_exit(litelink_cleanup);
