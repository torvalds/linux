/*********************************************************************
 *                
 * Filename:      actisys.c
 * Version:       1.1
 * Description:   Implementation for the ACTiSYS IR-220L and IR-220L+ 
 *                dongles
 * Status:        Beta.
 * Authors:       Dag Brattli <dagb@cs.uit.no> (initially)
 *		  Jean Tourrilhes <jt@hpl.hp.com> (new version)
 *		  Martin Diehl <mad@mdiehl.de> (new version for sir_dev)
 * Created at:    Wed Oct 21 20:02:35 1998
 * Modified at:   Sun Oct 27 22:02:13 2002
 * Modified by:   Martin Diehl <mad@mdiehl.de>
 * 
 *     Copyright (c) 1998-1999 Dag Brattli, All Rights Reserved.
 *     Copyright (c) 1999 Jean Tourrilhes
 *     Copyright (c) 2002 Martin Diehl
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
 *
 * 1.0 -> 1.1 - Martin Diehl
 *	modified for new sir infrastructure
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/init.h>

#include <net/irda/irda.h>

#include "sir-dev.h"

/* 
 * Define the timing of the pulses we send to the dongle (to reset it, and
 * to toggle speeds). Basically, the limit here is the propagation speed of
 * the signals through the serial port, the dongle being much faster.  Any
 * serial port support 115 kb/s, so we are sure that pulses 8.5 us wide can
 * go through cleanly . If you are on the wild side, you can try to lower
 * this value (Actisys recommended me 2 us, and 0 us work for me on a P233!)
 */
#define MIN_DELAY 10	/* 10 us to be on the conservative side */

static int actisys_open(struct sir_dev *);
static int actisys_close(struct sir_dev *);
static int actisys_change_speed(struct sir_dev *, unsigned);
static int actisys_reset(struct sir_dev *);

/* These are the baudrates supported, in the order available */
/* Note : the 220L doesn't support 38400, but we will fix that below */
static unsigned baud_rates[] = { 9600, 19200, 57600, 115200, 38400 };

#define MAX_SPEEDS ARRAY_SIZE(baud_rates)

static struct dongle_driver act220l = {
	.owner		= THIS_MODULE,
	.driver_name	= "Actisys ACT-220L",
	.type		= IRDA_ACTISYS_DONGLE,
	.open		= actisys_open,
	.close		= actisys_close,
	.reset		= actisys_reset,
	.set_speed	= actisys_change_speed,
};

static struct dongle_driver act220l_plus = {
	.owner		= THIS_MODULE,
	.driver_name	= "Actisys ACT-220L+",
	.type		= IRDA_ACTISYS_PLUS_DONGLE,
	.open		= actisys_open,
	.close		= actisys_close,
	.reset		= actisys_reset,
	.set_speed	= actisys_change_speed,
};

static int __init actisys_sir_init(void)
{
	int ret;

	/* First, register an Actisys 220L dongle */
	ret = irda_register_dongle(&act220l);
	if (ret < 0)
		return ret;

	/* Now, register an Actisys 220L+ dongle */
	ret = irda_register_dongle(&act220l_plus);
	if (ret < 0) {
		irda_unregister_dongle(&act220l);
		return ret;
	}
	return 0;
}

static void __exit actisys_sir_cleanup(void)
{
	/* We have to remove both dongles */
	irda_unregister_dongle(&act220l_plus);
	irda_unregister_dongle(&act220l);
}

static int actisys_open(struct sir_dev *dev)
{
	struct qos_info *qos = &dev->qos;

	sirdev_set_dtr_rts(dev, TRUE, TRUE);

	/* Set the speeds we can accept */
	qos->baud_rate.bits &= IR_9600|IR_19200|IR_38400|IR_57600|IR_115200;

	/* Remove support for 38400 if this is not a 220L+ dongle */
	if (dev->dongle_drv->type == IRDA_ACTISYS_DONGLE)
		qos->baud_rate.bits &= ~IR_38400;

	qos->min_turn_time.bits = 0x7f; /* Needs 0.01 ms */
	irda_qos_bits_to_value(qos);

	/* irda thread waits 50 msec for power settling */

	return 0;
}

static int actisys_close(struct sir_dev *dev)
{
	/* Power off the dongle */
	sirdev_set_dtr_rts(dev, FALSE, FALSE);

	return 0;
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
static int actisys_change_speed(struct sir_dev *dev, unsigned speed)
{
	int ret = 0;
	int i = 0;

        IRDA_DEBUG(4, "%s(), speed=%d (was %d)\n", __func__,
        	speed, dev->speed);

	/* dongle was already resetted from irda_request state machine,
	 * we are in known state (dongle default)
	 */

	/* 
	 * Now, we can set the speed requested. Send RTS pulses until we
         * reach the target speed 
	 */
	for (i = 0; i < MAX_SPEEDS; i++) {
		if (speed == baud_rates[i]) {
			dev->speed = speed;
			break;
		}
		/* Set RTS low for 10 us */
		sirdev_set_dtr_rts(dev, TRUE, FALSE);
		udelay(MIN_DELAY);

		/* Set RTS high for 10 us */
		sirdev_set_dtr_rts(dev, TRUE, TRUE);
		udelay(MIN_DELAY);
	}

	/* Check if life is sweet... */
	if (i >= MAX_SPEEDS) {
		actisys_reset(dev);
		ret = -EINVAL;  /* This should not happen */
	}

	/* Basta lavoro, on se casse d'ici... */
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
 * <Martin : move above comment to irda_config_fsm>
 *	Then, we set the speed to 9600 b/s to get in a known state (see in
 * change_speed for details). It is needed because the IrDA stack
 * has tried to set the speed immediately after our first return,
 * so before we can be sure the dongle is up and running.
 */

static int actisys_reset(struct sir_dev *dev)
{
	/* Reset the dongle : set DTR low for 10 us */
	sirdev_set_dtr_rts(dev, FALSE, TRUE);
	udelay(MIN_DELAY);

	/* Go back to normal mode */
	sirdev_set_dtr_rts(dev, TRUE, TRUE);
	
	dev->speed = 9600;	/* That's the default */

	return 0;
}

MODULE_AUTHOR("Dag Brattli <dagb@cs.uit.no> - Jean Tourrilhes <jt@hpl.hp.com>");
MODULE_DESCRIPTION("ACTiSYS IR-220L and IR-220L+ dongle driver");	
MODULE_LICENSE("GPL");
MODULE_ALIAS("irda-dongle-2"); /* IRDA_ACTISYS_DONGLE */
MODULE_ALIAS("irda-dongle-3"); /* IRDA_ACTISYS_PLUS_DONGLE */

module_init(actisys_sir_init);
module_exit(actisys_sir_cleanup);
