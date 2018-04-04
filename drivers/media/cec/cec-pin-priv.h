/*
 * cec-pin-priv.h - internal cec-pin header
 *
 * Copyright 2017 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
 *
 * This program is free software; you may redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef LINUX_CEC_PIN_PRIV_H
#define LINUX_CEC_PIN_PRIV_H

#include <linux/types.h>
#include <linux/atomic.h>
#include <media/cec-pin.h>

enum cec_pin_state {
	/* CEC is off */
	CEC_ST_OFF,
	/* CEC is idle, waiting for Rx or Tx */
	CEC_ST_IDLE,

	/* Tx states */

	/* Pending Tx, waiting for Signal Free Time to expire */
	CEC_ST_TX_WAIT,
	/* Low-drive was detected, wait for bus to go high */
	CEC_ST_TX_WAIT_FOR_HIGH,
	/* Drive CEC low for the start bit */
	CEC_ST_TX_START_BIT_LOW,
	/* Drive CEC high for the start bit */
	CEC_ST_TX_START_BIT_HIGH,
	/* Drive CEC low for the 0 bit */
	CEC_ST_TX_DATA_BIT_0_LOW,
	/* Drive CEC high for the 0 bit */
	CEC_ST_TX_DATA_BIT_0_HIGH,
	/* Drive CEC low for the 1 bit */
	CEC_ST_TX_DATA_BIT_1_LOW,
	/* Drive CEC high for the 1 bit */
	CEC_ST_TX_DATA_BIT_1_HIGH,
	/*
	 * Wait for start of sample time to check for Ack bit or first
	 * four initiator bits to check for Arbitration Lost.
	 */
	CEC_ST_TX_DATA_BIT_1_HIGH_PRE_SAMPLE,
	/* Wait for end of bit period after sampling */
	CEC_ST_TX_DATA_BIT_1_HIGH_POST_SAMPLE,

	/* Rx states */

	/* Start bit low detected */
	CEC_ST_RX_START_BIT_LOW,
	/* Start bit high detected */
	CEC_ST_RX_START_BIT_HIGH,
	/* Wait for bit sample time */
	CEC_ST_RX_DATA_SAMPLE,
	/* Wait for earliest end of bit period after sampling */
	CEC_ST_RX_DATA_POST_SAMPLE,
	/* Wait for CEC to go high (i.e. end of bit period */
	CEC_ST_RX_DATA_HIGH,
	/* Drive CEC low to send 0 Ack bit */
	CEC_ST_RX_ACK_LOW,
	/* End of 0 Ack time, wait for earliest end of bit period */
	CEC_ST_RX_ACK_LOW_POST,
	/* Wait for CEC to go high (i.e. end of bit period */
	CEC_ST_RX_ACK_HIGH_POST,
	/* Wait for earliest end of bit period and end of message */
	CEC_ST_RX_ACK_FINISH,

	/* Start low drive */
	CEC_ST_LOW_DRIVE,
	/* Monitor pin using interrupts */
	CEC_ST_RX_IRQ,

	/* Total number of pin states */
	CEC_PIN_STATES
};

#define CEC_NUM_PIN_EVENTS 128

#define CEC_PIN_IRQ_UNCHANGED	0
#define CEC_PIN_IRQ_DISABLE	1
#define CEC_PIN_IRQ_ENABLE	2

struct cec_pin {
	struct cec_adapter		*adap;
	const struct cec_pin_ops	*ops;
	struct task_struct		*kthread;
	wait_queue_head_t		kthread_waitq;
	struct hrtimer			timer;
	ktime_t				ts;
	unsigned int			wait_usecs;
	u16				la_mask;
	bool				enabled;
	bool				monitor_all;
	bool				rx_eom;
	bool				enable_irq_failed;
	enum cec_pin_state		state;
	struct cec_msg			tx_msg;
	u32				tx_bit;
	bool				tx_nacked;
	u32				tx_signal_free_time;
	struct cec_msg			rx_msg;
	u32				rx_bit;

	struct cec_msg			work_rx_msg;
	u8				work_tx_status;
	ktime_t				work_tx_ts;
	atomic_t			work_irq_change;
	atomic_t			work_pin_events;
	unsigned int			work_pin_events_wr;
	unsigned int			work_pin_events_rd;
	ktime_t				work_pin_ts[CEC_NUM_PIN_EVENTS];
	bool				work_pin_is_high[CEC_NUM_PIN_EVENTS];
	ktime_t				timer_ts;
	u32				timer_cnt;
	u32				timer_100ms_overruns;
	u32				timer_300ms_overruns;
	u32				timer_max_overrun;
	u32				timer_sum_overrun;
};

#endif
