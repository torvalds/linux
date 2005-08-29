/*********************************************************************
 *
 *	sir_kthread.c:		dedicated thread to process scheduled
 *				sir device setup requests
 *
 *	Copyright (c) 2002 Martin Diehl
 *
 *	This program is free software; you can redistribute it and/or 
 *	modify it under the terms of the GNU General Public License as 
 *	published by the Free Software Foundation; either version 2 of 
 *	the License, or (at your option) any later version.
 *
 ********************************************************************/    

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/init.h>
#include <linux/smp_lock.h>
#include <linux/completion.h>
#include <linux/delay.h>

#include <net/irda/irda.h>

#include "sir-dev.h"

/**************************************************************************
 *
 * kIrDAd kernel thread and config state machine
 *
 */

struct irda_request_queue {
	struct list_head request_list;
	spinlock_t lock;
	task_t *thread;
	struct completion exit;
	wait_queue_head_t kick, done;
	atomic_t num_pending;
};

static struct irda_request_queue irda_rq_queue;

static int irda_queue_request(struct irda_request *rq)
{
	int ret = 0;
	unsigned long flags;

	if (!test_and_set_bit(0, &rq->pending)) {
		spin_lock_irqsave(&irda_rq_queue.lock, flags);
		list_add_tail(&rq->lh_request, &irda_rq_queue.request_list);
		wake_up(&irda_rq_queue.kick);
		atomic_inc(&irda_rq_queue.num_pending);
		spin_unlock_irqrestore(&irda_rq_queue.lock, flags);
		ret = 1;
	}
	return ret;
}

static void irda_request_timer(unsigned long data)
{
	struct irda_request *rq = (struct irda_request *)data;
	unsigned long flags;
	
	spin_lock_irqsave(&irda_rq_queue.lock, flags);
	list_add_tail(&rq->lh_request, &irda_rq_queue.request_list);
	wake_up(&irda_rq_queue.kick);
	spin_unlock_irqrestore(&irda_rq_queue.lock, flags);
}

static int irda_queue_delayed_request(struct irda_request *rq, unsigned long delay)
{
	int ret = 0;
	struct timer_list *timer = &rq->timer;

	if (!test_and_set_bit(0, &rq->pending)) {
		timer->expires = jiffies + delay;
		timer->function = irda_request_timer;
		timer->data = (unsigned long)rq;
		atomic_inc(&irda_rq_queue.num_pending);
		add_timer(timer);
		ret = 1;
	}
	return ret;
}

static void run_irda_queue(void)
{
	unsigned long flags;
	struct list_head *entry, *tmp;
	struct irda_request *rq;

	spin_lock_irqsave(&irda_rq_queue.lock, flags);
	list_for_each_safe(entry, tmp, &irda_rq_queue.request_list) {
		rq = list_entry(entry, struct irda_request, lh_request);
		list_del_init(entry);
		spin_unlock_irqrestore(&irda_rq_queue.lock, flags);

		clear_bit(0, &rq->pending);
		rq->func(rq->data);

		if (atomic_dec_and_test(&irda_rq_queue.num_pending))
			wake_up(&irda_rq_queue.done);

		spin_lock_irqsave(&irda_rq_queue.lock, flags);
	}
	spin_unlock_irqrestore(&irda_rq_queue.lock, flags);
}		

static int irda_thread(void *startup)
{
	DECLARE_WAITQUEUE(wait, current);

	daemonize("kIrDAd");

	irda_rq_queue.thread = current;

	complete((struct completion *)startup);

	while (irda_rq_queue.thread != NULL) {

		/* We use TASK_INTERRUPTIBLE, rather than
		 * TASK_UNINTERRUPTIBLE.  Andrew Morton made this
		 * change ; he told me that it is safe, because "signal
		 * blocking is now handled in daemonize()", he added
		 * that the problem is that "uninterruptible sleep
		 * contributes to load average", making user worry.
		 * Jean II */
		set_task_state(current, TASK_INTERRUPTIBLE);
		add_wait_queue(&irda_rq_queue.kick, &wait);
		if (list_empty(&irda_rq_queue.request_list))
			schedule();
		else
			__set_task_state(current, TASK_RUNNING);
		remove_wait_queue(&irda_rq_queue.kick, &wait);

		/* make swsusp happy with our thread */
		try_to_freeze();

		run_irda_queue();
	}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,35)
	reparent_to_init();
#endif
	complete_and_exit(&irda_rq_queue.exit, 0);
	/* never reached */
	return 0;
}


static void flush_irda_queue(void)
{
	if (atomic_read(&irda_rq_queue.num_pending)) {

		DECLARE_WAITQUEUE(wait, current);

		if (!list_empty(&irda_rq_queue.request_list))
			run_irda_queue();

		set_task_state(current, TASK_UNINTERRUPTIBLE);
		add_wait_queue(&irda_rq_queue.done, &wait);
		if (atomic_read(&irda_rq_queue.num_pending))
			schedule();
		else
			__set_task_state(current, TASK_RUNNING);
		remove_wait_queue(&irda_rq_queue.done, &wait);
	}
}

/* substate handler of the config-fsm to handle the cases where we want
 * to wait for transmit completion before changing the port configuration
 */

static int irda_tx_complete_fsm(struct sir_dev *dev)
{
	struct sir_fsm *fsm = &dev->fsm;
	unsigned next_state, delay;
	unsigned bytes_left;

	do {
		next_state = fsm->substate;	/* default: stay in current substate */
		delay = 0;

		switch(fsm->substate) {

		case SIRDEV_STATE_WAIT_XMIT:
			if (dev->drv->chars_in_buffer)
				bytes_left = dev->drv->chars_in_buffer(dev);
			else
				bytes_left = 0;
			if (!bytes_left) {
				next_state = SIRDEV_STATE_WAIT_UNTIL_SENT;
				break;
			}

			if (dev->speed > 115200)
				delay = (bytes_left*8*10000) / (dev->speed/100);
			else if (dev->speed > 0)
				delay = (bytes_left*10*10000) / (dev->speed/100);
			else
				delay = 0;
			/* expected delay (usec) until remaining bytes are sent */
			if (delay < 100) {
				udelay(delay);
				delay = 0;
				break;
			}
			/* sleep some longer delay (msec) */
			delay = (delay+999) / 1000;
			break;

		case SIRDEV_STATE_WAIT_UNTIL_SENT:
			/* block until underlaying hardware buffer are empty */
			if (dev->drv->wait_until_sent)
				dev->drv->wait_until_sent(dev);
			next_state = SIRDEV_STATE_TX_DONE;
			break;

		case SIRDEV_STATE_TX_DONE:
			return 0;

		default:
			IRDA_ERROR("%s - undefined state\n", __FUNCTION__);
			return -EINVAL;
		}
		fsm->substate = next_state;
	} while (delay == 0);
	return delay;
}

/*
 * Function irda_config_fsm
 *
 * State machine to handle the configuration of the device (and attached dongle, if any).
 * This handler is scheduled for execution in kIrDAd context, so we can sleep.
 * however, kIrDAd is shared by all sir_dev devices so we better don't sleep there too
 * long. Instead, for longer delays we start a timer to reschedule us later.
 * On entry, fsm->sem is always locked and the netdev xmit queue stopped.
 * Both must be unlocked/restarted on completion - but only on final exit.
 */

static void irda_config_fsm(void *data)
{
	struct sir_dev *dev = data;
	struct sir_fsm *fsm = &dev->fsm;
	int next_state;
	int ret = -1;
	unsigned delay;

	IRDA_DEBUG(2, "%s(), <%ld>\n", __FUNCTION__, jiffies); 

	do {
		IRDA_DEBUG(3, "%s - state=0x%04x / substate=0x%04x\n",
			__FUNCTION__, fsm->state, fsm->substate);

		next_state = fsm->state;
		delay = 0;

		switch(fsm->state) {

		case SIRDEV_STATE_DONGLE_OPEN:
			if (dev->dongle_drv != NULL) {
				ret = sirdev_put_dongle(dev);
				if (ret) {
					fsm->result = -EINVAL;
					next_state = SIRDEV_STATE_ERROR;
					break;
				}
			}

			/* Initialize dongle */
			ret = sirdev_get_dongle(dev, fsm->param);
			if (ret) {
				fsm->result = ret;
				next_state = SIRDEV_STATE_ERROR;
				break;
			}

			/* Dongles are powered through the modem control lines which
			 * were just set during open. Before resetting, let's wait for
			 * the power to stabilize. This is what some dongle drivers did
			 * in open before, while others didn't - should be safe anyway.
			 */

			delay = 50;
			fsm->substate = SIRDEV_STATE_DONGLE_RESET;
			next_state = SIRDEV_STATE_DONGLE_RESET;

			fsm->param = 9600;

			break;

		case SIRDEV_STATE_DONGLE_CLOSE:
			/* shouldn't we just treat this as success=? */
			if (dev->dongle_drv == NULL) {
				fsm->result = -EINVAL;
				next_state = SIRDEV_STATE_ERROR;
				break;
			}

			ret = sirdev_put_dongle(dev);
			if (ret) {
				fsm->result = ret;
				next_state = SIRDEV_STATE_ERROR;
				break;
			}
			next_state = SIRDEV_STATE_DONE;
			break;

		case SIRDEV_STATE_SET_DTR_RTS:
			ret = sirdev_set_dtr_rts(dev,
				(fsm->param&0x02) ? TRUE : FALSE,
				(fsm->param&0x01) ? TRUE : FALSE);
			next_state = SIRDEV_STATE_DONE;
			break;

		case SIRDEV_STATE_SET_SPEED:
			fsm->substate = SIRDEV_STATE_WAIT_XMIT;
			next_state = SIRDEV_STATE_DONGLE_CHECK;
			break;

		case SIRDEV_STATE_DONGLE_CHECK:
			ret = irda_tx_complete_fsm(dev);
			if (ret < 0) {
				fsm->result = ret;
				next_state = SIRDEV_STATE_ERROR;
				break;
			}
			if ((delay=ret) != 0)
				break;

			if (dev->dongle_drv) {
				fsm->substate = SIRDEV_STATE_DONGLE_RESET;
				next_state = SIRDEV_STATE_DONGLE_RESET;
			}
			else {
				dev->speed = fsm->param;
				next_state = SIRDEV_STATE_PORT_SPEED;
			}
			break;

		case SIRDEV_STATE_DONGLE_RESET:
			if (dev->dongle_drv->reset) {
				ret = dev->dongle_drv->reset(dev);	
				if (ret < 0) {
					fsm->result = ret;
					next_state = SIRDEV_STATE_ERROR;
					break;
				}
			}
			else
				ret = 0;
			if ((delay=ret) == 0) {
				/* set serial port according to dongle default speed */
				if (dev->drv->set_speed)
					dev->drv->set_speed(dev, dev->speed);
				fsm->substate = SIRDEV_STATE_DONGLE_SPEED;
				next_state = SIRDEV_STATE_DONGLE_SPEED;
			}
			break;

		case SIRDEV_STATE_DONGLE_SPEED:				
			if (dev->dongle_drv->reset) {
				ret = dev->dongle_drv->set_speed(dev, fsm->param);
				if (ret < 0) {
					fsm->result = ret;
					next_state = SIRDEV_STATE_ERROR;
					break;
				}
			}
			else
				ret = 0;
			if ((delay=ret) == 0)
				next_state = SIRDEV_STATE_PORT_SPEED;
			break;

		case SIRDEV_STATE_PORT_SPEED:
			/* Finally we are ready to change the serial port speed */
			if (dev->drv->set_speed)
				dev->drv->set_speed(dev, dev->speed);
			dev->new_speed = 0;
			next_state = SIRDEV_STATE_DONE;
			break;

		case SIRDEV_STATE_DONE:
			/* Signal network layer so it can send more frames */
			netif_wake_queue(dev->netdev);
			next_state = SIRDEV_STATE_COMPLETE;
			break;

		default:
			IRDA_ERROR("%s - undefined state\n", __FUNCTION__);
			fsm->result = -EINVAL;
			/* fall thru */

		case SIRDEV_STATE_ERROR:
			IRDA_ERROR("%s - error: %d\n", __FUNCTION__, fsm->result);

#if 0	/* don't enable this before we have netdev->tx_timeout to recover */
			netif_stop_queue(dev->netdev);
#else
			netif_wake_queue(dev->netdev);
#endif
			/* fall thru */

		case SIRDEV_STATE_COMPLETE:
			/* config change finished, so we are not busy any longer */
			sirdev_enable_rx(dev);
			up(&fsm->sem);
			return;
		}
		fsm->state = next_state;
	} while(!delay);

	irda_queue_delayed_request(&fsm->rq, msecs_to_jiffies(delay));
}

/* schedule some device configuration task for execution by kIrDAd
 * on behalf of the above state machine.
 * can be called from process or interrupt/tasklet context.
 */

int sirdev_schedule_request(struct sir_dev *dev, int initial_state, unsigned param)
{
	struct sir_fsm *fsm = &dev->fsm;
	int xmit_was_down;

	IRDA_DEBUG(2, "%s - state=0x%04x / param=%u\n", __FUNCTION__, initial_state, param);

	if (down_trylock(&fsm->sem)) {
		if (in_interrupt()  ||  in_atomic()  ||  irqs_disabled()) {
			IRDA_DEBUG(1, "%s(), state machine busy!\n", __FUNCTION__);
			return -EWOULDBLOCK;
		} else
			down(&fsm->sem);
	}

	if (fsm->state == SIRDEV_STATE_DEAD) {
		/* race with sirdev_close should never happen */
		IRDA_ERROR("%s(), instance staled!\n", __FUNCTION__);
		up(&fsm->sem);
		return -ESTALE;		/* or better EPIPE? */
	}

	xmit_was_down = netif_queue_stopped(dev->netdev);
	netif_stop_queue(dev->netdev);
	atomic_set(&dev->enable_rx, 0);

	fsm->state = initial_state;
	fsm->param = param;
	fsm->result = 0;

	INIT_LIST_HEAD(&fsm->rq.lh_request);
	fsm->rq.pending = 0;
	fsm->rq.func = irda_config_fsm;
	fsm->rq.data = dev;

	if (!irda_queue_request(&fsm->rq)) {	/* returns 0 on error! */
		atomic_set(&dev->enable_rx, 1);
		if (!xmit_was_down)
			netif_wake_queue(dev->netdev);		
		up(&fsm->sem);
		return -EAGAIN;
	}
	return 0;
}

int __init irda_thread_create(void)
{
	struct completion startup;
	int pid;

	spin_lock_init(&irda_rq_queue.lock);
	irda_rq_queue.thread = NULL;
	INIT_LIST_HEAD(&irda_rq_queue.request_list);
	init_waitqueue_head(&irda_rq_queue.kick);
	init_waitqueue_head(&irda_rq_queue.done);
	atomic_set(&irda_rq_queue.num_pending, 0);

	init_completion(&startup);
	pid = kernel_thread(irda_thread, &startup, CLONE_FS|CLONE_FILES);
	if (pid <= 0)
		return -EAGAIN;
	else
		wait_for_completion(&startup);

	return 0;
}

void __exit irda_thread_join(void) 
{
	if (irda_rq_queue.thread) {
		flush_irda_queue();
		init_completion(&irda_rq_queue.exit);
		irda_rq_queue.thread = NULL;
		wake_up(&irda_rq_queue.kick);		
		wait_for_completion(&irda_rq_queue.exit);
	}
}

