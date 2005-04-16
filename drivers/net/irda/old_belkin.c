/*********************************************************************
 *                
 * Filename:      old_belkin.c
 * Version:       1.1
 * Description:   Driver for the Belkin (old) SmartBeam dongle
 * Status:        Experimental...
 * Author:        Jean Tourrilhes <jt@hpl.hp.com>
 * Created at:    22/11/99
 * Modified at:   Fri Dec 17 09:13:32 1999
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 * 
 *     Copyright (c) 1999 Jean Tourrilhes, All Rights Reserved.
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

/*
 * Belkin is selling a dongle called the SmartBeam.
 * In fact, there is two hardware version of this dongle, of course with
 * the same name and looking the exactly same (grrr...).
 * I guess that I've got the old one, because inside I don't have
 * a jumper for IrDA/ASK...
 *
 * As far as I can make it from info on their web site, the old dongle 
 * support only 9600 b/s, which make our life much simpler as far as
 * the driver is concerned, but you might not like it very much ;-)
 * The new SmartBeam does 115 kb/s, and I've not tested it...
 *
 * Belkin claim that the correct driver for the old dongle (in Windows)
 * is the generic Parallax 9500a driver, but the Linux LiteLink driver
 * fails for me (probably because Linux-IrDA doesn't rate fallback),
 * so I created this really dumb driver...
 *
 * In fact, this driver doesn't do much. The only thing it does is to
 * prevent Linux-IrDA to use any other speed than 9600 b/s ;-) This
 * driver is called "old_belkin" so that when the new SmartBeam is supported
 * its driver can be called "belkin" instead of "new_belkin".
 *
 * Note : this driver was written without any info/help from Belkin,
 * so a lot of info here might be totally wrong. Blame me ;-)
 */

/* Let's guess */
#define MIN_DELAY 25      /* 15 us, but wait a little more to be sure */

static void old_belkin_open(dongle_t *self, struct qos_info *qos);
static void old_belkin_close(dongle_t *self);
static int  old_belkin_change_speed(struct irda_task *task);
static int  old_belkin_reset(struct irda_task *task);

/* These are the baudrates supported */
/* static __u32 baud_rates[] = { 9600 }; */

static struct dongle_reg dongle = {
	.type = IRDA_OLD_BELKIN_DONGLE,
	.open = old_belkin_open,
	.close = old_belkin_close,
	.reset = old_belkin_reset,
	.change_speed = old_belkin_change_speed,
	.owner = THIS_MODULE,
};

static int __init old_belkin_init(void)
{
	return irda_device_register_dongle(&dongle);
}

static void __exit old_belkin_cleanup(void)
{
	irda_device_unregister_dongle(&dongle);
}

static void old_belkin_open(dongle_t *self, struct qos_info *qos)
{
	/* Not too fast, please... */
	qos->baud_rate.bits &= IR_9600;
	/* Needs at least 10 ms (totally wild guess, can do probably better) */
	qos->min_turn_time.bits = 0x01;
}

static void old_belkin_close(dongle_t *self)
{
	/* Power off dongle */
	self->set_dtr_rts(self->dev, FALSE, FALSE);
}

/*
 * Function old_belkin_change_speed (task)
 *
 *    With only one speed available, not much to do...
 */
static int old_belkin_change_speed(struct irda_task *task)
{
	irda_task_next_state(task, IRDA_TASK_DONE);

	return 0;
}

/*
 * Function old_belkin_reset (task)
 *
 *      Reset the Old-Belkin type dongle.
 *
 */
static int old_belkin_reset(struct irda_task *task)
{
	dongle_t *self = (dongle_t *) task->instance;

	/* Power on dongle */
	self->set_dtr_rts(self->dev, TRUE, TRUE);

	/* Sleep a minimum of 15 us */
	udelay(MIN_DELAY);

	/* This dongles speed "defaults" to 9600 bps ;-) */
	self->speed = 9600;

	irda_task_next_state(task, IRDA_TASK_DONE);

	return 0;
}

MODULE_AUTHOR("Jean Tourrilhes <jt@hpl.hp.com>");
MODULE_DESCRIPTION("Belkin (old) SmartBeam dongle driver");	
MODULE_LICENSE("GPL");
MODULE_ALIAS("irda-dongle-7"); /* IRDA_OLD_BELKIN_DONGLE */

/*
 * Function init_module (void)
 *
 *    Initialize Old-Belkin module
 *
 */
module_init(old_belkin_init);

/*
 * Function cleanup_module (void)
 *
 *    Cleanup Old-Belkin module
 *
 */
module_exit(old_belkin_cleanup);
