/*********************************************************************
 *
 * Filename:      toim3232-sir.c
 * Version:       1.0
 * Description:   Implementation of dongles based on the Vishay/Temic
 * 		  TOIM3232 SIR Endec chipset. Currently only the
 * 		  IRWave IR320ST-2 is tested, although it should work
 * 		  with any TOIM3232 or TOIM4232 chipset based RS232
 * 		  dongle with minimal modification.
 * 		  Based heavily on the Tekram driver (tekram.c),
 * 		  with thanks to Dag Brattli and Martin Diehl.
 * Status:        Experimental.
 * Author:        David Basden <davidb-irda@rcpt.to>
 * Created at:    Thu Feb 09 23:47:32 2006
 *
 *     Copyright (c) 2006 David Basden.
 *     Copyright (c) 1998-1999 Dag Brattli,
 *     Copyright (c) 2002 Martin Diehl,
 *     All Rights Reserved.
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
 * This driver has currently only been tested on the IRWave IR320ST-2
 *
 * PROTOCOL:
 *
 * The protocol for talking to the TOIM3232 is quite easy, and is
 * designed to interface with RS232 with only level convertors. The
 * BR/~D line on the chip is brought high to signal 'command mode',
 * where a command byte is sent to select the baudrate of the RS232
 * interface and the pulse length of the IRDA output. When BR/~D
 * is brought low, the dongle then changes to the selected baudrate,
 * and the RS232 interface is used for data until BR/~D is brought
 * high again. The initial speed for the TOIMx323 after RESET is
 * 9600 baud.  The baudrate for command-mode is the last selected
 * baud-rate, or 9600 after a RESET.
 *
 * The  dongle I have (below) adds some extra hardware on the front end,
 * but this is mostly directed towards pariasitic power from the RS232
 * line rather than changing very much about how to communicate with
 * the TOIM3232.
 *
 * The protocol to talk to the TOIM4232 chipset seems to be almost
 * identical to the TOIM3232 (and the 4232 datasheet is more detailed)
 * so this code will probably work on that as well, although I haven't
 * tested it on that hardware.
 *
 * Target dongle variations that might be common:
 *
 * DTR and RTS function:
 *   The data sheet for the 4232 has a sample implementation that hooks the
 *   DTR and RTS lines to the RESET and BaudRate/~Data lines of the
 *   chip (through line-converters). Given both DTR and RTS would have to
 *   be held low in normal operation, and the TOIMx232 requires +5V to
 *   signal ground, most dongle designers would almost certainly choose
 *   an implementation that kept at least one of DTR or RTS high in
 *   normal operation to provide power to the dongle, but will likely
 *   vary between designs.
 *
 * User specified command bits:
 *  There are two user-controllable output lines from the TOIMx232 that
 *  can be set low or high by setting the appropriate bits in the
 *  high-nibble of the command byte (when setting speed and pulse length).
 *  These might be used to switch on and off added hardware or extra
 *  dongle features.
 *
 *
 * Target hardware: IRWave IR320ST-2
 *
 * 	The IRWave IR320ST-2 is a simple dongle based on the Vishay/Temic
 * 	TOIM3232 SIR Endec and the Vishay/Temic TFDS4500 SIR IRDA transciever.
 * 	It uses a hex inverter and some discrete components to buffer and
 * 	line convert the RS232 down to 5V.
 *
 * 	The dongle is powered through a voltage regulator, fed by a large
 * 	capacitor. To switch the dongle on, DTR is brought high to charge
 * 	the capacitor and drive the voltage regulator. DTR isn't associated
 * 	with any control lines on the TOIM3232. Parisitic power is also taken
 * 	from the RTS, TD and RD lines when brought high, but through resistors.
 * 	When DTR is low, the circuit might lose power even with RTS high.
 *
 * 	RTS is inverted and attached to the BR/~D input pin. When RTS
 * 	is high, BR/~D is low, and the TOIM3232 is in the normal 'data' mode.
 * 	RTS is brought low, BR/~D is high, and the TOIM3232 is in 'command
 * 	mode'.
 *
 * 	For some unknown reason, the RESET line isn't actually connected
 * 	to anything. This means to reset the dongle to get it to a known
 * 	state (9600 baud) you must drop DTR and RTS low, wait for the power
 * 	capacitor to discharge, and then bring DTR (and RTS for data mode)
 * 	high again, and wait for the capacitor to charge, the power supply
 * 	to stabilise, and the oscillator clock to stabilise.
 *
 * 	Fortunately, if the current baudrate is known, the chipset can
 * 	easily change speed by entering command mode without having to
 * 	reset the dongle first.
 *
 * 	Major Components:
 *
 * 	- Vishay/Temic TOIM3232 SIR Endec to change RS232 pulse timings
 * 	  to IRDA pulse timings
 * 	- 3.6864MHz crystal to drive TOIM3232 clock oscillator
 * 	- DM74lS04M Inverting Hex line buffer for RS232 input buffering
 * 	  and level conversion
 * 	- PJ2951AC 150mA voltage regulator
 * 	- Vishay/Temic TFDS4500	SIR IRDA front-end transceiver
 *
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/sched.h>

#include <net/irda/irda.h>

#include "sir-dev.h"

static int toim3232delay = 150;	/* default is 150 ms */
module_param(toim3232delay, int, 0);
MODULE_PARM_DESC(toim3232delay, "toim3232 dongle write complete delay");

#if 0
static int toim3232flipdtr = 0;	/* default is DTR high to reset */
module_param(toim3232flipdtr, int, 0);
MODULE_PARM_DESC(toim3232flipdtr, "toim3232 dongle invert DTR (Reset)");

static int toim3232fliprts = 0;	/* default is RTS high for baud change */
module_param(toim3232fliptrs, int, 0);
MODULE_PARM_DESC(toim3232fliprts, "toim3232 dongle invert RTS (BR/D)");
#endif

static int toim3232_open(struct sir_dev *);
static int toim3232_close(struct sir_dev *);
static int toim3232_change_speed(struct sir_dev *, unsigned);
static int toim3232_reset(struct sir_dev *);

#define TOIM3232_115200 0x00
#define TOIM3232_57600  0x01
#define TOIM3232_38400  0x02
#define TOIM3232_19200  0x03
#define TOIM3232_9600   0x06
#define TOIM3232_2400   0x0A

#define TOIM3232_PW     0x10 /* Pulse select bit */

static struct dongle_driver toim3232 = {
	.owner		= THIS_MODULE,
	.driver_name	= "Vishay TOIM3232",
	.type		= IRDA_TOIM3232_DONGLE,
	.open		= toim3232_open,
	.close		= toim3232_close,
	.reset		= toim3232_reset,
	.set_speed	= toim3232_change_speed,
};

static int __init toim3232_sir_init(void)
{
	if (toim3232delay < 1  ||  toim3232delay > 500)
		toim3232delay = 200;
	IRDA_DEBUG(1, "%s - using %d ms delay\n",
		toim3232.driver_name, toim3232delay);
	return irda_register_dongle(&toim3232);
}

static void __exit toim3232_sir_cleanup(void)
{
	irda_unregister_dongle(&toim3232);
}

static int toim3232_open(struct sir_dev *dev)
{
	struct qos_info *qos = &dev->qos;

	IRDA_DEBUG(2, "%s()\n", __func__);

	/* Pull the lines high to start with.
	 *
	 * For the IR320ST-2, we need to charge the main supply capacitor to
	 * switch the device on. We keep DTR high throughout to do this.
	 * When RTS, TD and RD are high, they will also trickle-charge the
	 * cap. RTS is high for data transmission, and low for baud rate select.
	 * 	-- DGB
	 */
	sirdev_set_dtr_rts(dev, TRUE, TRUE);

	/* The TOI3232 supports many speeds between 1200bps and 115000bps.
	 * We really only care about those supported by the IRDA spec, but
	 * 38400 seems to be implemented in many places */
	qos->baud_rate.bits &= IR_2400|IR_9600|IR_19200|IR_38400|IR_57600|IR_115200;

	/* From the tekram driver. Not sure what a reasonable value is -- DGB */
	qos->min_turn_time.bits = 0x01; /* Needs at least 10 ms */
	irda_qos_bits_to_value(qos);

	/* irda thread waits 50 msec for power settling */

	return 0;
}

static int toim3232_close(struct sir_dev *dev)
{
	IRDA_DEBUG(2, "%s()\n", __func__);

	/* Power off dongle */
	sirdev_set_dtr_rts(dev, FALSE, FALSE);

	return 0;
}

/*
 * Function toim3232change_speed (dev, state, speed)
 *
 *    Set the speed for the TOIM3232 based dongle. Warning, this
 *    function must be called with a process context!
 *
 *    Algorithm
 *    1. keep DTR high but clear RTS to bring into baud programming mode
 *    2. wait at least 7us to enter programming mode
 *    3. send control word to set baud rate and timing
 *    4. wait at least 1us
 *    5. bring RTS high to enter DATA mode (RS232 is passed through to transceiver)
 *    6. should take effect immediately (although probably worth waiting)
 */

#define TOIM3232_STATE_WAIT_SPEED	(SIRDEV_STATE_DONGLE_SPEED + 1)

static int toim3232_change_speed(struct sir_dev *dev, unsigned speed)
{
	unsigned state = dev->fsm.substate;
	unsigned delay = 0;
	u8 byte;
	static int ret = 0;

	IRDA_DEBUG(2, "%s()\n", __func__);

	switch(state) {
	case SIRDEV_STATE_DONGLE_SPEED:

		/* Figure out what we are going to send as a control byte */
		switch (speed) {
		case 2400:
			byte = TOIM3232_PW|TOIM3232_2400;
			break;
		default:
			speed = 9600;
			ret = -EINVAL;
			/* fall thru */
		case 9600:
			byte = TOIM3232_PW|TOIM3232_9600;
			break;
		case 19200:
			byte = TOIM3232_PW|TOIM3232_19200;
			break;
		case 38400:
			byte = TOIM3232_PW|TOIM3232_38400;
			break;
		case 57600:
			byte = TOIM3232_PW|TOIM3232_57600;
			break;
		case 115200:
			byte = TOIM3232_115200;
			break;
		}

		/* Set DTR, Clear RTS: Go into baud programming mode */
		sirdev_set_dtr_rts(dev, TRUE, FALSE);

		/* Wait at least 7us */
		udelay(14);

		/* Write control byte */
		sirdev_raw_write(dev, &byte, 1);

		dev->speed = speed;

		state = TOIM3232_STATE_WAIT_SPEED;
		delay = toim3232delay;
		break;

	case TOIM3232_STATE_WAIT_SPEED:
		/* Have transmitted control byte * Wait for 'at least 1us' */
		udelay(14);

		/* Set DTR, Set RTS: Go into normal data mode */
		sirdev_set_dtr_rts(dev, TRUE, TRUE);

		/* Wait (TODO: check this is needed) */
		udelay(50);
		break;

	default:
		printk(KERN_ERR "%s - undefined state %d\n", __func__, state);
		ret = -EINVAL;
		break;
	}

	dev->fsm.substate = state;
	return (delay > 0) ? delay : ret;
}

/*
 * Function toim3232reset (driver)
 *
 *      This function resets the toim3232 dongle. Warning, this function
 *      must be called with a process context!!
 *
 * What we should do is:
 * 	0. Pull RESET high
 * 	1. Wait for at least 7us
 * 	2. Pull RESET low
 * 	3. Wait for at least 7us
 * 	4. Pull BR/~D high
 * 	5. Wait for at least 7us
 * 	6. Send control byte to set baud rate
 * 	7. Wait at least 1us after stop bit
 * 	8. Pull BR/~D low
 * 	9. Should then be in data mode
 *
 * Because the IR320ST-2 doesn't have the RESET line connected for some reason,
 * we'll have to do something else.
 *
 * The default speed after a RESET is 9600, so lets try just bringing it up in
 * data mode after switching it off, waiting for the supply capacitor to
 * discharge, and then switch it back on. This isn't actually pulling RESET
 * high, but it seems to have the same effect.
 *
 * This behaviour will probably work on dongles that have the RESET line connected,
 * but if not, add a flag for the IR320ST-2, and implment the above-listed proper
 * behaviour.
 *
 * RTS is inverted and then fed to BR/~D, so to put it in programming mode, we
 * need to have pull RTS low
 */

static int toim3232_reset(struct sir_dev *dev)
{
	IRDA_DEBUG(2, "%s()\n", __func__);

	/* Switch off both DTR and RTS to switch off dongle */
	sirdev_set_dtr_rts(dev, FALSE, FALSE);

	/* Should sleep a while. This might be evil doing it this way.*/
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout(msecs_to_jiffies(50));

	/* Set DTR, Set RTS (data mode) */
	sirdev_set_dtr_rts(dev, TRUE, TRUE);

	/* Wait at least 10 ms for power to stabilize again */
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout(msecs_to_jiffies(10));

	/* Speed should now be 9600 */
	dev->speed = 9600;

	return 0;
}

MODULE_AUTHOR("David Basden <davidb-linux@rcpt.to>");
MODULE_DESCRIPTION("Vishay/Temic TOIM3232 based dongle driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("irda-dongle-12"); /* IRDA_TOIM3232_DONGLE */

module_init(toim3232_sir_init);
module_exit(toim3232_sir_cleanup);
