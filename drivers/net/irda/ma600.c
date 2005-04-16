/*********************************************************************
 *                
 * Filename:      ma600.c
 * Version:       0.1
 * Description:   Implementation of the MA600 dongle
 * Status:        Experimental.
 * Author:        Leung <95Etwl@alumni.ee.ust.hk> http://www.engsvr.ust/~eetwl95
 * Created at:    Sat Jun 10 20:02:35 2000
 * Modified at:   
 * Modified by:   
 *
 * Note: very thanks to Mr. Maru Wang <maru@mobileaction.com.tw> for providing 
 *       information on the MA600 dongle
 * 
 *     Copyright (c) 2000 Leung, All Rights Reserved.
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

/* define this macro for release version */
//#define NDEBUG

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/tty.h>
#include <linux/init.h>

#include <net/irda/irda.h>
#include <net/irda/irda_device.h>

#ifndef NDEBUG
	#undef IRDA_DEBUG
	#define IRDA_DEBUG(n, args...) (printk(KERN_DEBUG args))

	#undef ASSERT
	#define ASSERT(expr, func) \
	if(!(expr)) { \
	        printk( "Assertion failed! %s,%s,%s,line=%d\n",\
        	#expr,__FILE__,__FUNCTION__,__LINE__); \
	        func}
#endif

/* convert hex value to ascii hex */
static const char hexTbl[] = "0123456789ABCDEF";


static void ma600_open(dongle_t *self, struct qos_info *qos);
static void ma600_close(dongle_t *self);
static int  ma600_change_speed(struct irda_task *task);
static int  ma600_reset(struct irda_task *task);

/* control byte for MA600 */
#define MA600_9600	0x00
#define MA600_19200	0x01
#define MA600_38400	0x02
#define MA600_57600	0x03
#define MA600_115200	0x04
#define MA600_DEV_ID1	0x05
#define MA600_DEV_ID2	0x06
#define MA600_2400	0x08

static struct dongle_reg dongle = {
	.type = IRDA_MA600_DONGLE,
	.open = ma600_open,
	.close = ma600_close,
	.reset = ma600_reset,
	.change_speed = ma600_change_speed,
	.owner = THIS_MODULE,
};

static int __init ma600_init(void)
{
	IRDA_DEBUG(2, "%s()\n", __FUNCTION__);
	return irda_device_register_dongle(&dongle);
}

static void __exit ma600_cleanup(void)
{
	IRDA_DEBUG(2, "%s()\n", __FUNCTION__);
	irda_device_unregister_dongle(&dongle);
}

/*
	Power on:
		(0) Clear RTS and DTR for 1 second
		(1) Set RTS and DTR for 1 second
		(2) 9600 bps now
	Note: assume RTS, DTR are clear before
*/
static void ma600_open(dongle_t *self, struct qos_info *qos)
{
	IRDA_DEBUG(2, "%s()\n", __FUNCTION__);

	qos->baud_rate.bits &= IR_2400|IR_9600|IR_19200|IR_38400
				|IR_57600|IR_115200;
	qos->min_turn_time.bits = 0x01;		/* Needs at least 1 ms */	
	irda_qos_bits_to_value(qos);

	//self->set_dtr_rts(self->dev, FALSE, FALSE);
	// should wait 1 second

	self->set_dtr_rts(self->dev, TRUE, TRUE);
	// should wait 1 second
}

static void ma600_close(dongle_t *self)
{
	IRDA_DEBUG(2, "%s()\n", __FUNCTION__);

	/* Power off dongle */
	self->set_dtr_rts(self->dev, FALSE, FALSE);
}

static __u8 get_control_byte(__u32 speed)
{
	__u8 byte;

	switch (speed) {
	default:
	case 115200:
		byte = MA600_115200;
		break;
	case 57600:
		byte = MA600_57600;
		break;
	case 38400:
		byte = MA600_38400;
		break;
	case 19200:
		byte = MA600_19200;
		break;
	case 9600:
		byte = MA600_9600;
		break;
	case 2400:
		byte = MA600_2400;
		break;
	}

	return byte;
}

/*
 * Function ma600_change_speed (dev, state, speed)
 *
 *    Set the speed for the MA600 type dongle. Warning, this 
 *    function must be called with a process context!
 *
 *    Algorithm
 *    1. Reset
 *    2. clear RTS, set DTR and wait for 1ms
 *    3. send Control Byte to the MA600 through TXD to set new baud rate
 *       wait until the stop bit of Control Byte is sent (for 9600 baud rate, 
 *       it takes about 10 msec)
 *    4. set RTS, set DTR (return to NORMAL Operation)
 *    5. wait at least 10 ms, new setting (baud rate, etc) takes effect here 
 *       after
 */
static int ma600_change_speed(struct irda_task *task)
{
	dongle_t *self = (dongle_t *) task->instance;
	__u32 speed = (__u32) task->param;
	static __u8 byte;
	__u8 byte_echo;
	int ret = 0;
	
	IRDA_DEBUG(2, "%s()\n", __FUNCTION__);

	ASSERT(task != NULL, return -1;);

	if (self->speed_task && self->speed_task != task) {
		IRDA_DEBUG(0, "%s(), busy!\n", __FUNCTION__);
		return msecs_to_jiffies(10);
	} else {
		self->speed_task = task;
	}

	switch (task->state) {
	case IRDA_TASK_INIT:
	case IRDA_TASK_CHILD_INIT:
		/* 
		 * Need to reset the dongle and go to 9600 bps before
                 * programming 
		 */
		if (irda_task_execute(self, ma600_reset, NULL, task, 
				      (void *) speed)) {
			/* Dongle need more time to reset */
			irda_task_next_state(task, IRDA_TASK_CHILD_WAIT);
	
			/* give 1 second to finish */
			ret = msecs_to_jiffies(1000);
		} else {
			irda_task_next_state(task, IRDA_TASK_CHILD_DONE);
		}
		break;

	case IRDA_TASK_CHILD_WAIT:
		IRDA_WARNING("%s(), resetting dongle timed out!\n",
			     __FUNCTION__);
		ret = -1;
		break;

	case IRDA_TASK_CHILD_DONE:
		/* Set DTR, Clear RTS */
		self->set_dtr_rts(self->dev, TRUE, FALSE);
	
		ret = msecs_to_jiffies(1);		/* Sleep 1 ms */
		irda_task_next_state(task, IRDA_TASK_WAIT);
		break;

	case IRDA_TASK_WAIT:
		speed = (__u32) task->param;
		byte = get_control_byte(speed);

		/* Write control byte */
		self->write(self->dev, &byte, sizeof(byte));
		
		irda_task_next_state(task, IRDA_TASK_WAIT1);

		/* Wait at least 10 ms */
		ret = msecs_to_jiffies(15);
		break;

	case IRDA_TASK_WAIT1:
		/* Read control byte echo */
		self->read(self->dev, &byte_echo, sizeof(byte_echo));

		if(byte != byte_echo) {
			/* if control byte != echo, I don't know what to do */
			printk(KERN_WARNING "%s() control byte written != read!\n", __FUNCTION__);
			printk(KERN_WARNING "control byte = 0x%c%c\n", 
			       hexTbl[(byte>>4)&0x0f], hexTbl[byte&0x0f]);
			printk(KERN_WARNING "byte echo = 0x%c%c\n", 
			       hexTbl[(byte_echo>>4) & 0x0f], 
			       hexTbl[byte_echo & 0x0f]);
		#ifndef NDEBUG
		} else {
			IRDA_DEBUG(2, "%s() control byte write read OK\n", __FUNCTION__);
		#endif
		}

		/* Set DTR, Set RTS */
		self->set_dtr_rts(self->dev, TRUE, TRUE);

		irda_task_next_state(task, IRDA_TASK_WAIT2);

		/* Wait at least 10 ms */
		ret = msecs_to_jiffies(10);
		break;

	case IRDA_TASK_WAIT2:
		irda_task_next_state(task, IRDA_TASK_DONE);
		self->speed_task = NULL;
		break;

	default:
		IRDA_ERROR("%s(), unknown state %d\n",
			   __FUNCTION__, task->state);
		irda_task_next_state(task, IRDA_TASK_DONE);
		self->speed_task = NULL;
		ret = -1;
		break;
	}
	return ret;
}

/*
 * Function ma600_reset (driver)
 *
 *      This function resets the ma600 dongle. Warning, this function 
 *      must be called with a process context!! 
 *
 *      Algorithm:
 *    	  0. DTR=0, RTS=1 and wait 10 ms
 *    	  1. DTR=1, RTS=1 and wait 10 ms
 *        2. 9600 bps now
 */
int ma600_reset(struct irda_task *task)
{
	dongle_t *self = (dongle_t *) task->instance;
	int ret = 0;

	IRDA_DEBUG(2, "%s()\n", __FUNCTION__);

	ASSERT(task != NULL, return -1;);

	if (self->reset_task && self->reset_task != task) {
		IRDA_DEBUG(0, "%s(), busy!\n", __FUNCTION__);
		return msecs_to_jiffies(10);
	} else
		self->reset_task = task;
	
	switch (task->state) {
	case IRDA_TASK_INIT:
		/* Clear DTR and Set RTS */
		self->set_dtr_rts(self->dev, FALSE, TRUE);
		irda_task_next_state(task, IRDA_TASK_WAIT1);
		ret = msecs_to_jiffies(10);		/* Sleep 10 ms */
		break;
	case IRDA_TASK_WAIT1:
		/* Set DTR and RTS */
		self->set_dtr_rts(self->dev, TRUE, TRUE);
		irda_task_next_state(task, IRDA_TASK_WAIT2);
		ret = msecs_to_jiffies(10);		/* Sleep 10 ms */
		break;
	case IRDA_TASK_WAIT2:
		irda_task_next_state(task, IRDA_TASK_DONE);
		self->reset_task = NULL;
		break;
	default:
		IRDA_ERROR("%s(), unknown state %d\n",
			   __FUNCTION__, task->state);
		irda_task_next_state(task, IRDA_TASK_DONE);		
		self->reset_task = NULL;
		ret = -1;
	}
	return ret;
}

MODULE_AUTHOR("Leung <95Etwl@alumni.ee.ust.hk> http://www.engsvr.ust/~eetwl95");
MODULE_DESCRIPTION("MA600 dongle driver version 0.1");
MODULE_LICENSE("GPL");
MODULE_ALIAS("irda-dongle-11"); /* IRDA_MA600_DONGLE */
		
/*
 * Function init_module (void)
 *
 *    Initialize MA600 module
 *
 */
module_init(ma600_init);

/*
 * Function cleanup_module (void)
 *
 *    Cleanup MA600 module
 *
 */
module_exit(ma600_cleanup);

