/*********************************************************************
 *                
 * Filename:      actisys.c
 * Version:       1.0
 * Description:   Implementation for the ACTiSYS IR-220L and IR-220L+ 
 *                dongles
 * Status:        Beta.
 * Authors:       Dag Brattli <dagb@cs.uit.no> (initially)
 *		  Jean Tourrilhes <jt@hpl.hp.com> (new version)
 * Created at:    Wed Oct 21 20:02:35 1998
 * Modified at:   Fri Dec 17 09:10:43 1999
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 * 
 *     Copyright (c) 1998-1999 Dag Brattli, All Rights Reserved.
 *     Copyright (c) 1999 Jean Tourrilhes
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

/*
 * Changelog
 *
 * 0.8 -> 0.9999 - Jean
 *	o New initialisation procedure : much safer and correct
 *	o New procedure the change speed : much faster and simpler
 *	o Other cleanups & comments
 *	Thanks to Lichen Wang @ Actisys for his excellent help...
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/tty.h>
#include <linux/init.h>

#include <net/irda/irda.h>
#include <net/irda/irda_device.h>

/* 
 * Define the timing of the pulses we send to the dongle (to reset it, and
 * to toggle speeds). Basically, the limit here is the propagation speed of
 * the signals through the serial port, the dongle being much faster.  Any
 * serial port support 115 kb/s, so we are sure that pulses 8.5 us wide can
 * go through cleanly . If you are on the wild side, you can try to lower
 * this value (Actisys recommended me 2 us, and 0 us work for me on a P233!)
 */
#define MIN_DELAY 10	/* 10 us to be on the conservative side */

static int  actisys_change_speed(struct irda_task *task);
static int  actisys_reset(struct irda_task *task);
static void actisys_open(dongle_t *self, struct qos_info *qos);
static void actisys_close(dongle_t *self);

/* These are the baudrates supported, in the order available */
/* Note : the 220L doesn't support 38400, but we will fix that below */
static __u32 baud_rates[] = { 9600, 19200, 57600, 115200, 38400 };
#define MAX_SPEEDS 5

static struct dongle_reg dongle = {
	.type = IRDA_ACTISYS_DONGLE,
	.open = actisys_open,
	.close = actisys_close,
	.reset = actisys_reset,
	.change_speed = actisys_change_speed,
	.owner = THIS_MODULE,
};

static struct dongle_reg dongle_plus = {
	.type = IRDA_ACTISYS_PLUS_DONGLE,
	.open = actisys_open,
	.close = actisys_close,
	.reset = actisys_reset,
	.change_speed = actisys_change_speed,
	.owner = THIS_MODULE,
};

/*
 * Function actisys_change_speed (task)
 *
 *	There is two model of Actisys dongle we are dealing with,
 * the 220L and 220L+. At this point, only irattach knows with
 * kind the user has requested (it was an argument on irattach
 * command line).
 *	So, we register a dongle of each sort and let irattach
 * pick the right one...
 */
static int __init actisys_init(void)
{
	int ret;

	/* First, register an Actisys 220L dongle */
	ret = irda_device_register_dongle(&dongle);
	if (ret < 0)
		return ret;
	/* Now, register an Actisys 220L+ dongle */
	ret = irda_device_register_dongle(&dongle_plus);
	if (ret < 0) {
		irda_device_unregister_dongle(&dongle);
		return ret;
	}	
	return 0;
}

static void __exit actisys_cleanup(void)
{
	/* We have to remove both dongles */
	irda_device_unregister_dongle(&dongle);
	irda_device_unregister_dongle(&dongle_plus);
}

static void actisys_open(dongle_t *self, struct qos_info *qos)
{
	/* Power on the dongle */
	self->set_dtr_rts(self->dev, TRUE, TRUE);

	/* Set the speeds we can accept */
	qos->baud_rate.bits &= IR_9600|IR_19200|IR_38400|IR_57600|IR_115200;

	/* Remove support for 38400 if this is not a 220L+ dongle */
	if (self->issue->type == IRDA_ACTISYS_DONGLE)
		qos->baud_rate.bits &= ~IR_38400;
	
	qos->min_turn_time.bits = 0x7f; /* Needs 0.01 ms */
}

static void actisys_close(dongle_t *self)
{
	/* Power off the dongle */
	self->set_dtr_rts(self->dev, FALSE, FALSE);
}

/*
 * Function actisys_change_speed (task)
 *
 *    Change speed of the ACTiSYS IR-220L and IR-220L+ type IrDA dongles.
 *    To cycle through the available baud rates, pulse RTS low for a few us.
 *
 *	First, we reset the dongle to always start from a known state.
 *	Then, we cycle through the speeds by pulsing RTS low and then up.
 *	The dongle allow us to pulse quite fast, se we can set speed in one go,
 * which is must faster ( < 100 us) and less complex than what is found
 * in some other dongle drivers...
 *	Note that even if the new speed is the same as the current speed,
 * we reassert the speed. This make sure that things are all right,
 * and it's fast anyway...
 *	By the way, this function will work for both type of dongles,
 * because the additional speed is at the end of the sequence...
 */
static int actisys_change_speed(struct irda_task *task)
{
	dongle_t *self = (dongle_t *) task->instance;
	__u32 speed = (__u32) task->param;	/* Target speed */
	int ret = 0;
	int i = 0;

        IRDA_DEBUG(4, "%s(), speed=%d (was %d)\n", __FUNCTION__, speed, 
		   self->speed);

	/* Go to a known state by reseting the dongle */

	/* Reset the dongle : set DTR low for 10 us */
	self->set_dtr_rts(self->dev, FALSE, TRUE);
	udelay(MIN_DELAY);

	/* Go back to normal mode (we are now at 9600 b/s) */
	self->set_dtr_rts(self->dev, TRUE, TRUE);
 
	/* 
	 * Now, we can set the speed requested. Send RTS pulses until we
         * reach the target speed 
	 */
	for (i=0; i<MAX_SPEEDS; i++) {
		if (speed == baud_rates[i]) {
			self->speed = baud_rates[i];
			break;
		}
		/* Make sure previous pulse is finished */
		udelay(MIN_DELAY);

		/* Set RTS low for 10 us */
		self->set_dtr_rts(self->dev, TRUE, FALSE);
		udelay(MIN_DELAY);

		/* Set RTS high for 10 us */
		self->set_dtr_rts(self->dev, TRUE, TRUE);
	}

	/* Check if life is sweet... */
	if (i >= MAX_SPEEDS)
		ret = -1;  /* This should not happen */

	/* Basta lavoro, on se casse d'ici... */
	irda_task_next_state(task, IRDA_TASK_DONE);

	return ret;
}

/*
 * Function actisys_reset (task)
 *
 *      Reset the Actisys type dongle. Warning, this function must only be
 *      called with a process context!
 *
 * We need to do two things in this function :
 *	o first make sure that the dongle is in a state where it can operate
 *	o second put the dongle in a know state
 *
 *	The dongle is powered of the RTS and DTR lines. In the dongle, there
 * is a big capacitor to accommodate the current spikes. This capacitor
 * takes a least 50 ms to be charged. In theory, the Bios set those lines
 * up, so by the time we arrive here we should be set. It doesn't hurt
 * to be on the conservative side, so we will wait...
 *	Then, we set the speed to 9600 b/s to get in a known state (see in
 * change_speed for details). It is needed because the IrDA stack
 * has tried to set the speed immediately after our first return,
 * so before we can be sure the dongle is up and running.
 */
static int actisys_reset(struct irda_task *task)
{
	dongle_t *self = (dongle_t *) task->instance;
	int ret = 0;

	IRDA_ASSERT(task != NULL, return -1;);

	self->reset_task = task;

	switch (task->state) {
	case IRDA_TASK_INIT:
		/* Set both DTR & RTS to power up the dongle */
		/* In theory redundant with power up in actisys_open() */
		self->set_dtr_rts(self->dev, TRUE, TRUE);
		
		/* Sleep 50 ms to make sure capacitor is charged */
		ret = msecs_to_jiffies(50);
		irda_task_next_state(task, IRDA_TASK_WAIT);
		break;
	case IRDA_TASK_WAIT:			
		/* Reset the dongle : set DTR low for 10 us */
		self->set_dtr_rts(self->dev, FALSE, TRUE);
		udelay(MIN_DELAY);

		/* Go back to normal mode */
		self->set_dtr_rts(self->dev, TRUE, TRUE);
	
		irda_task_next_state(task, IRDA_TASK_DONE);
		self->reset_task = NULL;
		self->speed = 9600;	/* That's the default */
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

MODULE_AUTHOR("Dag Brattli <dagb@cs.uit.no> - Jean Tourrilhes <jt@hpl.hp.com>");
MODULE_DESCRIPTION("ACTiSYS IR-220L and IR-220L+ dongle driver");	
MODULE_LICENSE("GPL");
MODULE_ALIAS("irda-dongle-2"); /* IRDA_ACTISYS_DONGLE */
MODULE_ALIAS("irda-dongle-3"); /* IRDA_ACTISYS_PLUS_DONGLE */

		
/*
 * Function init_module (void)
 *
 *    Initialize Actisys module
 *
 */
module_init(actisys_init);

/*
 * Function cleanup_module (void)
 *
 *    Cleanup Actisys module
 *
 */
module_exit(actisys_cleanup);
