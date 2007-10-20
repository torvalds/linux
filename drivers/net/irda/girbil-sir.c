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
#include <linux/init.h>

#include <net/irda/irda.h>

#include "sir-dev.h"

static int girbil_reset(struct sir_dev *dev);
static int girbil_open(struct sir_dev *dev);
static int girbil_close(struct sir_dev *dev);
static int girbil_change_speed(struct sir_dev *dev, unsigned speed);

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

static struct dongle_driver girbil = {
	.owner		= THIS_MODULE,
	.driver_name	= "Greenwich GIrBIL",
	.type		= IRDA_GIRBIL_DONGLE,
	.open		= girbil_open,
	.close		= girbil_close,
	.reset		= girbil_reset,
	.set_speed	= girbil_change_speed,
};

static int __init girbil_sir_init(void)
{
	return irda_register_dongle(&girbil);
}

static void __exit girbil_sir_cleanup(void)
{
	irda_unregister_dongle(&girbil);
}

static int girbil_open(struct sir_dev *dev)
{
	struct qos_info *qos = &dev->qos;

	IRDA_DEBUG(2, "%s()\n", __FUNCTION__);

	/* Power on dongle */
	sirdev_set_dtr_rts(dev, TRUE, TRUE);

	qos->baud_rate.bits &= IR_9600|IR_19200|IR_38400|IR_57600|IR_115200;
	qos->min_turn_time.bits = 0x03;
	irda_qos_bits_to_value(qos);

	/* irda thread waits 50 msec for power settling */

	return 0;
}

static int girbil_close(struct sir_dev *dev)
{
	IRDA_DEBUG(2, "%s()\n", __FUNCTION__);

	/* Power off dongle */
	sirdev_set_dtr_rts(dev, FALSE, FALSE);

	return 0;
}

/*
 * Function girbil_change_speed (dev, speed)
 *
 *    Set the speed for the Girbil type dongle.
 *
 */

#define GIRBIL_STATE_WAIT_SPEED	(SIRDEV_STATE_DONGLE_SPEED + 1)

static int girbil_change_speed(struct sir_dev *dev, unsigned speed)
{
	unsigned state = dev->fsm.substate;
	unsigned delay = 0;
	u8 control[2];
	static int ret = 0;

	IRDA_DEBUG(2, "%s()\n", __FUNCTION__);

	/* dongle alread reset - port and dongle at default speed */

	switch(state) {

	case SIRDEV_STATE_DONGLE_SPEED:

		/* Set DTR and Clear RTS to enter command mode */
		sirdev_set_dtr_rts(dev, FALSE, TRUE);

		udelay(25);		/* better wait a little while */

		ret = 0;
		switch (speed) {
		default:
			ret = -EINVAL;
			/* fall through */
		case 9600:
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
		sirdev_raw_write(dev, control, 2);

		dev->speed = speed;

		state = GIRBIL_STATE_WAIT_SPEED;
		delay = 100;
		break;

	case GIRBIL_STATE_WAIT_SPEED:
		/* Go back to normal mode */
		sirdev_set_dtr_rts(dev, TRUE, TRUE);

		udelay(25);		/* better wait a little while */
		break;

	default:
		IRDA_ERROR("%s - undefined state %d\n", __FUNCTION__, state);
		ret = -EINVAL;
		break;
	}
	dev->fsm.substate = state;
	return (delay > 0) ? delay : ret;
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


#define GIRBIL_STATE_WAIT1_RESET	(SIRDEV_STATE_DONGLE_RESET + 1)
#define GIRBIL_STATE_WAIT2_RESET	(SIRDEV_STATE_DONGLE_RESET + 2)
#define GIRBIL_STATE_WAIT3_RESET	(SIRDEV_STATE_DONGLE_RESET + 3)

static int girbil_reset(struct sir_dev *dev)
{
	unsigned state = dev->fsm.substate;
	unsigned delay = 0;
	u8 control = GIRBIL_TXEN | GIRBIL_RXEN;
	int ret = 0;

	IRDA_DEBUG(2, "%s()\n", __FUNCTION__);

	switch (state) {
	case SIRDEV_STATE_DONGLE_RESET:
		/* Reset dongle */
		sirdev_set_dtr_rts(dev, TRUE, FALSE);
		/* Sleep at least 5 ms */
		delay = 20;
		state = GIRBIL_STATE_WAIT1_RESET;
		break;

	case GIRBIL_STATE_WAIT1_RESET:
		/* Set DTR and clear RTS to enter command mode */
		sirdev_set_dtr_rts(dev, FALSE, TRUE);
		delay = 20;
		state = GIRBIL_STATE_WAIT2_RESET;
		break;

	case GIRBIL_STATE_WAIT2_RESET:
		/* Write control byte */
		sirdev_raw_write(dev, &control, 1);
		delay = 20;
		state = GIRBIL_STATE_WAIT3_RESET;
		break;

	case GIRBIL_STATE_WAIT3_RESET:
		/* Go back to normal mode */
		sirdev_set_dtr_rts(dev, TRUE, TRUE);
		dev->speed = 9600;
		break;

	default:
		IRDA_ERROR("%s(), undefined state %d\n", __FUNCTION__, state);
		ret = -1;
		break;
	}
	dev->fsm.substate = state;
	return (delay > 0) ? delay : ret;
}

MODULE_AUTHOR("Dag Brattli <dagb@cs.uit.no>");
MODULE_DESCRIPTION("Greenwich GIrBIL dongle driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("irda-dongle-4"); /* IRDA_GIRBIL_DONGLE */

module_init(girbil_sir_init);
module_exit(girbil_sir_cleanup);
