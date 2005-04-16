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
#include <linux/init.h>

#include <net/irda/irda.h>

#include "sir-dev.h"

static int mcp2120_reset(struct sir_dev *dev);
static int mcp2120_open(struct sir_dev *dev);
static int mcp2120_close(struct sir_dev *dev);
static int mcp2120_change_speed(struct sir_dev *dev, unsigned speed);

#define MCP2120_9600    0x87
#define MCP2120_19200   0x8B
#define MCP2120_38400   0x85
#define MCP2120_57600   0x83
#define MCP2120_115200  0x81

#define MCP2120_COMMIT  0x11

static struct dongle_driver mcp2120 = {
	.owner		= THIS_MODULE,
	.driver_name	= "Microchip MCP2120",
	.type		= IRDA_MCP2120_DONGLE,
	.open		= mcp2120_open,
	.close		= mcp2120_close,
	.reset		= mcp2120_reset,
	.set_speed	= mcp2120_change_speed,
};

static int __init mcp2120_sir_init(void)
{
	return irda_register_dongle(&mcp2120);
}

static void __exit mcp2120_sir_cleanup(void)
{
	irda_unregister_dongle(&mcp2120);
}

static int mcp2120_open(struct sir_dev *dev)
{
	struct qos_info *qos = &dev->qos;

	IRDA_DEBUG(2, "%s()\n", __FUNCTION__);

	/* seems no explicit power-on required here and reset switching it on anyway */

	qos->baud_rate.bits &= IR_9600|IR_19200|IR_38400|IR_57600|IR_115200;
	qos->min_turn_time.bits = 0x01;
	irda_qos_bits_to_value(qos);

	return 0;
}

static int mcp2120_close(struct sir_dev *dev)
{
	IRDA_DEBUG(2, "%s()\n", __FUNCTION__);

	/* Power off dongle */
        /* reset and inhibit mcp2120 */
	sirdev_set_dtr_rts(dev, TRUE, TRUE);
	// sirdev_set_dtr_rts(dev, FALSE, FALSE);

	return 0;
}

/*
 * Function mcp2120_change_speed (dev, speed)
 *
 *    Set the speed for the MCP2120.
 *
 */

#define MCP2120_STATE_WAIT_SPEED	(SIRDEV_STATE_DONGLE_SPEED+1)

static int mcp2120_change_speed(struct sir_dev *dev, unsigned speed)
{
	unsigned state = dev->fsm.substate;
	unsigned delay = 0;
	u8 control[2];
	static int ret = 0;

	IRDA_DEBUG(2, "%s()\n", __FUNCTION__);

	switch (state) {
	case SIRDEV_STATE_DONGLE_SPEED:
		/* Set DTR to enter command mode */
		sirdev_set_dtr_rts(dev, TRUE, FALSE);
                udelay(500);

		ret = 0;
		switch (speed) {
		default:
			speed = 9600;
			ret = -EINVAL;
			/* fall through */
		case 9600:
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
		sirdev_raw_write(dev, control, 2);
		dev->speed = speed;

		state = MCP2120_STATE_WAIT_SPEED;
		delay = 100;
                //printk("mcp2120_change_speed: dongle_speed\n");
		break;

	case MCP2120_STATE_WAIT_SPEED:
		/* Go back to normal mode */
		sirdev_set_dtr_rts(dev, FALSE, FALSE);
                //printk("mcp2120_change_speed: mcp_wait\n");
		break;

	default:
		IRDA_ERROR("%s(), undefine state %d\n", __FUNCTION__, state);
		ret = -EINVAL;
		break;
	}
	dev->fsm.substate = state;
	return (delay > 0) ? delay : ret;
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

#define MCP2120_STATE_WAIT1_RESET	(SIRDEV_STATE_DONGLE_RESET+1)
#define MCP2120_STATE_WAIT2_RESET	(SIRDEV_STATE_DONGLE_RESET+2)

static int mcp2120_reset(struct sir_dev *dev)
{
	unsigned state = dev->fsm.substate;
	unsigned delay = 0;
	int ret = 0;

	IRDA_DEBUG(2, "%s()\n", __FUNCTION__);

	switch (state) {
	case SIRDEV_STATE_DONGLE_RESET:
                //printk("mcp2120_reset: dongle_reset\n");
		/* Reset dongle by setting RTS*/
		sirdev_set_dtr_rts(dev, TRUE, TRUE);
		state = MCP2120_STATE_WAIT1_RESET;
		delay = 50;
		break;

	case MCP2120_STATE_WAIT1_RESET:
                //printk("mcp2120_reset: mcp2120_wait1\n");
                /* clear RTS and wait for at least 30 ms. */
		sirdev_set_dtr_rts(dev, FALSE, FALSE);
		state = MCP2120_STATE_WAIT2_RESET;
		delay = 50;
		break;

	case MCP2120_STATE_WAIT2_RESET:
                //printk("mcp2120_reset mcp2120_wait2\n");
		/* Go back to normal mode */
		sirdev_set_dtr_rts(dev, FALSE, FALSE);
		break;

	default:
		IRDA_ERROR("%s(), undefined state %d\n", __FUNCTION__, state);
		ret = -EINVAL;
		break;
	}
	dev->fsm.substate = state;
	return (delay > 0) ? delay : ret;
}

MODULE_AUTHOR("Felix Tang <tangf@eyetap.org>");
MODULE_DESCRIPTION("Microchip MCP2120");
MODULE_LICENSE("GPL");
MODULE_ALIAS("irda-dongle-9"); /* IRDA_MCP2120_DONGLE */

module_init(mcp2120_sir_init);
module_exit(mcp2120_sir_cleanup);
