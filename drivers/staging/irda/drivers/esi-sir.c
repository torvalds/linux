/*********************************************************************
 *                
 * Filename:      esi.c
 * Version:       1.6
 * Description:   Driver for the Extended Systems JetEye PC dongle
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Sat Feb 21 18:54:38 1998
 * Modified at:   Sun Oct 27 22:01:04 2002
 * Modified by:   Martin Diehl <mad@mdiehl.de>
 * 
 *     Copyright (c) 1999 Dag Brattli, <dagb@cs.uit.no>,
 *     Copyright (c) 1998 Thomas Davis, <ratbert@radiks.net>,
 *     Copyright (c) 2002 Martin Diehl, <mad@mdiehl.de>,
 *     All Rights Reserved.
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

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/init.h>

#include <net/irda/irda.h>

#include "sir-dev.h"

static int esi_open(struct sir_dev *);
static int esi_close(struct sir_dev *);
static int esi_change_speed(struct sir_dev *, unsigned);
static int esi_reset(struct sir_dev *);

static struct dongle_driver esi = {
	.owner		= THIS_MODULE,
	.driver_name	= "JetEye PC ESI-9680 PC",
	.type		= IRDA_ESI_DONGLE,
	.open		= esi_open,
	.close		= esi_close,
	.reset		= esi_reset,
	.set_speed	= esi_change_speed,
};

static int __init esi_sir_init(void)
{
	return irda_register_dongle(&esi);
}

static void __exit esi_sir_cleanup(void)
{
	irda_unregister_dongle(&esi);
}

static int esi_open(struct sir_dev *dev)
{
	struct qos_info *qos = &dev->qos;

	/* Power up and set dongle to 9600 baud */
	sirdev_set_dtr_rts(dev, FALSE, TRUE);

	qos->baud_rate.bits &= IR_9600|IR_19200|IR_115200;
	qos->min_turn_time.bits = 0x01; /* Needs at least 10 ms */
	irda_qos_bits_to_value(qos);

	/* irda thread waits 50 msec for power settling */

	return 0;
}

static int esi_close(struct sir_dev *dev)
{
	/* Power off dongle */
	sirdev_set_dtr_rts(dev, FALSE, FALSE);

	return 0;
}

/*
 * Function esi_change_speed (task)
 *
 * Set the speed for the Extended Systems JetEye PC ESI-9680 type dongle
 * Apparently (see old esi-driver) no delays are needed here...
 *
 */
static int esi_change_speed(struct sir_dev *dev, unsigned speed)
{
	int ret = 0;
	int dtr, rts;
	
	switch (speed) {
	case 19200:
		dtr = TRUE;
		rts = FALSE;
		break;
	case 115200:
		dtr = rts = TRUE;
		break;
	default:
		ret = -EINVAL;
		speed = 9600;
		/* fall through */
	case 9600:
		dtr = FALSE;
		rts = TRUE;
		break;
	}

	/* Change speed of dongle */
	sirdev_set_dtr_rts(dev, dtr, rts);
	dev->speed = speed;

	return ret;
}

/*
 * Function esi_reset (task)
 *
 *    Reset dongle;
 *
 */
static int esi_reset(struct sir_dev *dev)
{
	sirdev_set_dtr_rts(dev, FALSE, FALSE);

	/* Hm, the old esi-driver left the dongle unpowered relying on
	 * the following speed change to repower. This might work for
	 * the esi because we only need the modem lines. However, now the
	 * general rule is reset must bring the dongle to some working
	 * well-known state because speed change might write to registers.
	 * The old esi-driver didn't any delay here - let's hope it' fine.
	 */

	sirdev_set_dtr_rts(dev, FALSE, TRUE);
	dev->speed = 9600;

	return 0;
}

MODULE_AUTHOR("Dag Brattli <dagb@cs.uit.no>");
MODULE_DESCRIPTION("Extended Systems JetEye PC dongle driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("irda-dongle-1"); /* IRDA_ESI_DONGLE */

module_init(esi_sir_init);
module_exit(esi_sir_cleanup);

