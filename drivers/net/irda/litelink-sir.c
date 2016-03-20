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
 *     along with this program; if not, see <http://www.gnu.org/licenses/>.
 *     
 ********************************************************************/

/*
 * Modified at:   Thu Jan 15 2003
 * Modified by:   Eugene Crosser <crosser@average.org>
 *
 * Convert to "new" IRDA infrastructure for kernel 2.6
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/init.h>

#include <net/irda/irda.h>

#include "sir-dev.h"

#define MIN_DELAY 25      /* 15 us, but wait a little more to be sure */
#define MAX_DELAY 10000   /* 1 ms */

static int litelink_open(struct sir_dev *dev);
static int litelink_close(struct sir_dev *dev);
static int litelink_change_speed(struct sir_dev *dev, unsigned speed);
static int litelink_reset(struct sir_dev *dev);

/* These are the baudrates supported - 9600 must be last one! */
static unsigned baud_rates[] = { 115200, 57600, 38400, 19200, 9600 };

static struct dongle_driver litelink = {
	.owner		= THIS_MODULE,
	.driver_name	= "Parallax LiteLink",
	.type		= IRDA_LITELINK_DONGLE,
	.open		= litelink_open,
	.close		= litelink_close,
	.reset		= litelink_reset,
	.set_speed	= litelink_change_speed,
};

static int __init litelink_sir_init(void)
{
	return irda_register_dongle(&litelink);
}

static void __exit litelink_sir_cleanup(void)
{
	irda_unregister_dongle(&litelink);
}

static int litelink_open(struct sir_dev *dev)
{
	struct qos_info *qos = &dev->qos;

	/* Power up dongle */
	sirdev_set_dtr_rts(dev, TRUE, TRUE);

	/* Set the speeds we can accept */
	qos->baud_rate.bits &= IR_115200|IR_57600|IR_38400|IR_19200|IR_9600;
	qos->min_turn_time.bits = 0x7f; /* Needs 0.01 ms */
	irda_qos_bits_to_value(qos);

	/* irda thread waits 50 msec for power settling */

	return 0;
}

static int litelink_close(struct sir_dev *dev)
{
	/* Power off dongle */
	sirdev_set_dtr_rts(dev, FALSE, FALSE);

	return 0;
}

/*
 * Function litelink_change_speed (task)
 *
 *    Change speed of the Litelink dongle. To cycle through the available 
 *    baud rates, pulse RTS low for a few ms.  
 */
static int litelink_change_speed(struct sir_dev *dev, unsigned speed)
{
        int i;

	/* dongle already reset by irda-thread - current speed (dongle and
	 * port) is the default speed (115200 for litelink!)
	 */

	/* Cycle through avaiable baudrates until we reach the correct one */
	for (i = 0; baud_rates[i] != speed; i++) {

		/* end-of-list reached due to invalid speed request */
		if (baud_rates[i] == 9600)
			break;

		/* Set DTR, clear RTS */
		sirdev_set_dtr_rts(dev, FALSE, TRUE);

		/* Sleep a minimum of 15 us */
		udelay(MIN_DELAY);

		/* Set DTR, Set RTS */
		sirdev_set_dtr_rts(dev, TRUE, TRUE);

		/* Sleep a minimum of 15 us */
		udelay(MIN_DELAY);
        }

	dev->speed = baud_rates[i];

	/* invalid baudrate should not happen - but if, we return -EINVAL and
	 * the dongle configured for 9600 so the stack has a chance to recover
	 */

	return (dev->speed == speed) ? 0 : -EINVAL;
}

/*
 * Function litelink_reset (task)
 *
 *      Reset the Litelink type dongle.
 *
 */
static int litelink_reset(struct sir_dev *dev)
{
	/* probably the power-up can be dropped here, but with only
	 * 15 usec delay it's not worth the risk unless somebody with
	 * the hardware confirms it doesn't break anything...
	 */

	/* Power on dongle */
	sirdev_set_dtr_rts(dev, TRUE, TRUE);

	/* Sleep a minimum of 15 us */
	udelay(MIN_DELAY);

	/* Clear RTS to reset dongle */
	sirdev_set_dtr_rts(dev, TRUE, FALSE);

	/* Sleep a minimum of 15 us */
	udelay(MIN_DELAY);

	/* Go back to normal mode */
	sirdev_set_dtr_rts(dev, TRUE, TRUE);

	/* Sleep a minimum of 15 us */
	udelay(MIN_DELAY);

	/* This dongles speed defaults to 115200 bps */
	dev->speed = 115200;

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
module_init(litelink_sir_init);

/*
 * Function cleanup_module (void)
 *
 *    Cleanup Litelink module
 *
 */
module_exit(litelink_sir_cleanup);
