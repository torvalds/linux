/*********************************************************************
 *
 * Filename:      act200l.c
 * Version:       0.8
 * Description:   Implementation for the ACTiSYS ACT-IR200L dongle
 * Status:        Experimental.
 * Author:        SHIMIZU Takuya <tshimizu@ga2.so-net.ne.jp>
 * Created at:    Fri Aug  3 17:35:42 2001
 * Modified at:   Fri Aug 17 10:22:40 2001
 * Modified by:   SHIMIZU Takuya <tshimizu@ga2.so-net.ne.jp>
 *
 *     Copyright (c) 2001 SHIMIZU Takuya, All Rights Reserved.
 *
 *     This program is free software; you can redistribute it and/or
 *     modify it under the terms of the GNU General Public License as
 *     published by the Free Software Foundation; either version 2 of
 *     the License, or (at your option) any later version.
 *
 ********************************************************************/

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/init.h>

#include <net/irda/irda.h>

#include "sir-dev.h"

static int act200l_reset(struct sir_dev *dev);
static int act200l_open(struct sir_dev *dev);
static int act200l_close(struct sir_dev *dev);
static int act200l_change_speed(struct sir_dev *dev, unsigned speed);

/* Regsiter 0: Control register #1 */
#define ACT200L_REG0    0x00
#define ACT200L_TXEN    0x01 /* Enable transmitter */
#define ACT200L_RXEN    0x02 /* Enable receiver */

/* Register 1: Control register #2 */
#define ACT200L_REG1    0x10
#define ACT200L_LODB    0x01 /* Load new baud rate count value */
#define ACT200L_WIDE    0x04 /* Expand the maximum allowable pulse */

/* Register 4: Output Power register */
#define ACT200L_REG4    0x40
#define ACT200L_OP0     0x01 /* Enable LED1C output */
#define ACT200L_OP1     0x02 /* Enable LED2C output */
#define ACT200L_BLKR    0x04

/* Register 5: Receive Mode register */
#define ACT200L_REG5    0x50
#define ACT200L_RWIDL   0x01 /* fixed 1.6us pulse mode */

/* Register 6: Receive Sensitivity register #1 */
#define ACT200L_REG6    0x60
#define ACT200L_RS0     0x01 /* receive threshold bit 0 */
#define ACT200L_RS1     0x02 /* receive threshold bit 1 */

/* Register 7: Receive Sensitivity register #2 */
#define ACT200L_REG7    0x70
#define ACT200L_ENPOS   0x04 /* Ignore the falling edge */

/* Register 8,9: Baud Rate Dvider register #1,#2 */
#define ACT200L_REG8    0x80
#define ACT200L_REG9    0x90

#define ACT200L_2400    0x5f
#define ACT200L_9600    0x17
#define ACT200L_19200   0x0b
#define ACT200L_38400   0x05
#define ACT200L_57600   0x03
#define ACT200L_115200  0x01

/* Register 13: Control register #3 */
#define ACT200L_REG13   0xd0
#define ACT200L_SHDW    0x01 /* Enable access to shadow registers */

/* Register 15: Status register */
#define ACT200L_REG15   0xf0

/* Register 21: Control register #4 */
#define ACT200L_REG21   0x50
#define ACT200L_EXCK    0x02 /* Disable clock output driver */
#define ACT200L_OSCL    0x04 /* oscillator in low power, medium accuracy mode */

static struct dongle_driver act200l = {
	.owner		= THIS_MODULE,
	.driver_name	= "ACTiSYS ACT-IR200L",
	.type		= IRDA_ACT200L_DONGLE,
	.open		= act200l_open,
	.close		= act200l_close,
	.reset		= act200l_reset,
	.set_speed	= act200l_change_speed,
};

static int __init act200l_sir_init(void)
{
	return irda_register_dongle(&act200l);
}

static void __exit act200l_sir_cleanup(void)
{
	irda_unregister_dongle(&act200l);
}

static int act200l_open(struct sir_dev *dev)
{
	struct qos_info *qos = &dev->qos;

	/* Power on the dongle */
	sirdev_set_dtr_rts(dev, TRUE, TRUE);

	/* Set the speeds we can accept */
	qos->baud_rate.bits &= IR_9600|IR_19200|IR_38400|IR_57600|IR_115200;
	qos->min_turn_time.bits = 0x03;
	irda_qos_bits_to_value(qos);

	/* irda thread waits 50 msec for power settling */

	return 0;
}

static int act200l_close(struct sir_dev *dev)
{
	/* Power off the dongle */
	sirdev_set_dtr_rts(dev, FALSE, FALSE);

	return 0;
}

/*
 * Function act200l_change_speed (dev, speed)
 *
 *    Set the speed for the ACTiSYS ACT-IR200L type dongle.
 *
 */
static int act200l_change_speed(struct sir_dev *dev, unsigned speed)
{
	u8 control[3];
	int ret = 0;

	/* Clear DTR and set RTS to enter command mode */
	sirdev_set_dtr_rts(dev, FALSE, TRUE);

	switch (speed) {
	default:
		ret = -EINVAL;
		/* fall through */
	case 9600:
		control[0] = ACT200L_REG8 |  (ACT200L_9600       & 0x0f);
		control[1] = ACT200L_REG9 | ((ACT200L_9600 >> 4) & 0x0f);
		break;
	case 19200:
		control[0] = ACT200L_REG8 |  (ACT200L_19200       & 0x0f);
		control[1] = ACT200L_REG9 | ((ACT200L_19200 >> 4) & 0x0f);
		break;
	case 38400:
		control[0] = ACT200L_REG8 |  (ACT200L_38400       & 0x0f);
		control[1] = ACT200L_REG9 | ((ACT200L_38400 >> 4) & 0x0f);
		break;
	case 57600:
		control[0] = ACT200L_REG8 |  (ACT200L_57600       & 0x0f);
		control[1] = ACT200L_REG9 | ((ACT200L_57600 >> 4) & 0x0f);
		break;
	case 115200:
		control[0] = ACT200L_REG8 |  (ACT200L_115200       & 0x0f);
		control[1] = ACT200L_REG9 | ((ACT200L_115200 >> 4) & 0x0f);
		break;
	}
	control[2] = ACT200L_REG1 | ACT200L_LODB | ACT200L_WIDE;

	/* Write control bytes */
	sirdev_raw_write(dev, control, 3);
	msleep(5);

	/* Go back to normal mode */
	sirdev_set_dtr_rts(dev, TRUE, TRUE);

	dev->speed = speed;
	return ret;
}

/*
 * Function act200l_reset (driver)
 *
 *    Reset the ACTiSYS ACT-IR200L type dongle.
 */

#define ACT200L_STATE_WAIT1_RESET	(SIRDEV_STATE_DONGLE_RESET+1)
#define ACT200L_STATE_WAIT2_RESET	(SIRDEV_STATE_DONGLE_RESET+2)

static int act200l_reset(struct sir_dev *dev)
{
	unsigned state = dev->fsm.substate;
	unsigned delay = 0;
	static const u8 control[9] = {
		ACT200L_REG15,
		ACT200L_REG13 | ACT200L_SHDW,
		ACT200L_REG21 | ACT200L_EXCK | ACT200L_OSCL,
		ACT200L_REG13,
		ACT200L_REG7  | ACT200L_ENPOS,
		ACT200L_REG6  | ACT200L_RS0  | ACT200L_RS1,
		ACT200L_REG5  | ACT200L_RWIDL,
		ACT200L_REG4  | ACT200L_OP0  | ACT200L_OP1 | ACT200L_BLKR,
		ACT200L_REG0  | ACT200L_TXEN | ACT200L_RXEN
	};
	int ret = 0;

	switch (state) {
	case SIRDEV_STATE_DONGLE_RESET:
		/* Reset the dongle : set RTS low for 25 ms */
		sirdev_set_dtr_rts(dev, TRUE, FALSE);
		state = ACT200L_STATE_WAIT1_RESET;
		delay = 50;
		break;

	case ACT200L_STATE_WAIT1_RESET:
		/* Clear DTR and set RTS to enter command mode */
		sirdev_set_dtr_rts(dev, FALSE, TRUE);

		udelay(25);			/* better wait for some short while */

		/* Write control bytes */
		sirdev_raw_write(dev, control, sizeof(control));
		state = ACT200L_STATE_WAIT2_RESET;
		delay = 15;
		break;

	case ACT200L_STATE_WAIT2_RESET:
		/* Go back to normal mode */
		sirdev_set_dtr_rts(dev, TRUE, TRUE);
		dev->speed = 9600;
		break;
	default:
		net_err_ratelimited("%s(), unknown state %d\n",
				    __func__, state);
		ret = -1;
		break;
	}
	dev->fsm.substate = state;
	return (delay > 0) ? delay : ret;
}

MODULE_AUTHOR("SHIMIZU Takuya <tshimizu@ga2.so-net.ne.jp>");
MODULE_DESCRIPTION("ACTiSYS ACT-IR200L dongle driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("irda-dongle-10"); /* IRDA_ACT200L_DONGLE */

module_init(act200l_sir_init);
module_exit(act200l_sir_cleanup);
