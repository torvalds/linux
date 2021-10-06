/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * cec-pin-priv.h - internal cec-pin header
 *
 * Copyright 2017 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
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
	/* Generate a start bit period that is too short */
	CEC_ST_TX_START_BIT_HIGH_SHORT,
	/* Generate a start bit period that is too long */
	CEC_ST_TX_START_BIT_HIGH_LONG,
	/* Drive CEC low for the start bit using the custom timing */
	CEC_ST_TX_START_BIT_LOW_CUSTOM,
	/* Drive CEC high for the start bit using the custom timing */
	CEC_ST_TX_START_BIT_HIGH_CUSTOM,
	/* Drive CEC low for the 0 bit */
	CEC_ST_TX_DATA_BIT_0_LOW,
	/* Drive CEC high for the 0 bit */
	CEC_ST_TX_DATA_BIT_0_HIGH,
	/* Generate a bit period that is too short */
	CEC_ST_TX_DATA_BIT_0_HIGH_SHORT,
	/* Generate a bit period that is too long */
	CEC_ST_TX_DATA_BIT_0_HIGH_LONG,
	/* Drive CEC low for the 1 bit */
	CEC_ST_TX_DATA_BIT_1_LOW,
	/* Drive CEC high for the 1 bit */
	CEC_ST_TX_DATA_BIT_1_HIGH,
	/* Generate a bit period that is too short */
	CEC_ST_TX_DATA_BIT_1_HIGH_SHORT,
	/* Generate a bit period that is too long */
	CEC_ST_TX_DATA_BIT_1_HIGH_LONG,
	/*
	 * Wait for start of sample time to check for Ack bit or first
	 * four initiator bits to check for Arbitration Lost.
	 */
	CEC_ST_TX_DATA_BIT_1_HIGH_PRE_SAMPLE,
	/* Wait for end of bit period after sampling */
	CEC_ST_TX_DATA_BIT_1_HIGH_POST_SAMPLE,
	/* Generate a bit period that is too short */
	CEC_ST_TX_DATA_BIT_1_HIGH_POST_SAMPLE_SHORT,
	/* Generate a bit period that is too long */
	CEC_ST_TX_DATA_BIT_1_HIGH_POST_SAMPLE_LONG,
	/* Drive CEC low for a data bit using the custom timing */
	CEC_ST_TX_DATA_BIT_LOW_CUSTOM,
	/* Drive CEC high for a data bit using the custom timing */
	CEC_ST_TX_DATA_BIT_HIGH_CUSTOM,
	/* Drive CEC low for a standalone pulse using the custom timing */
	CEC_ST_TX_PULSE_LOW_CUSTOM,
	/* Drive CEC high for a standalone pulse using the custom timing */
	CEC_ST_TX_PULSE_HIGH_CUSTOM,
	/* Start low drive */
	CEC_ST_TX_LOW_DRIVE,

	/* Rx states */

	/* Start bit low detected */
	CEC_ST_RX_START_BIT_LOW,
	/* Start bit high detected */
	CEC_ST_RX_START_BIT_HIGH,
	/* Wait for bit sample time */
	CEC_ST_RX_DATA_SAMPLE,
	/* Wait for earliest end of bit period after sampling */
	CEC_ST_RX_DATA_POST_SAMPLE,
	/* Wait for CEC to go low (i.e. end of bit period) */
	CEC_ST_RX_DATA_WAIT_FOR_LOW,
	/* Drive CEC low to send 0 Ack bit */
	CEC_ST_RX_ACK_LOW,
	/* End of 0 Ack time, wait for earliest end of bit period */
	CEC_ST_RX_ACK_LOW_POST,
	/* Wait for CEC to go high (i.e. end of bit period */
	CEC_ST_RX_ACK_HIGH_POST,
	/* Wait for earliest end of bit period and end of message */
	CEC_ST_RX_ACK_FINISH,
	/* Start low drive */
	CEC_ST_RX_LOW_DRIVE,

	/* Monitor pin using interrupts */
	CEC_ST_RX_IRQ,

	/* Total number of pin states */
	CEC_PIN_STATES
};

/* Error Injection */

/* Error injection modes */
#define CEC_ERROR_INJ_MODE_OFF				0
#define CEC_ERROR_INJ_MODE_ONCE				1
#define CEC_ERROR_INJ_MODE_ALWAYS			2
#define CEC_ERROR_INJ_MODE_TOGGLE			3
#define CEC_ERROR_INJ_MODE_MASK				3ULL

/* Receive error injection options */
#define CEC_ERROR_INJ_RX_NACK_OFFSET			0
#define CEC_ERROR_INJ_RX_LOW_DRIVE_OFFSET		2
#define CEC_ERROR_INJ_RX_ADD_BYTE_OFFSET		4
#define CEC_ERROR_INJ_RX_REMOVE_BYTE_OFFSET		6
#define CEC_ERROR_INJ_RX_ARB_LOST_OFFSET		8
#define CEC_ERROR_INJ_RX_MASK				0xffffULL

/* Transmit error injection options */
#define CEC_ERROR_INJ_TX_NO_EOM_OFFSET			16
#define CEC_ERROR_INJ_TX_EARLY_EOM_OFFSET		18
#define CEC_ERROR_INJ_TX_SHORT_BIT_OFFSET		20
#define CEC_ERROR_INJ_TX_LONG_BIT_OFFSET		22
#define CEC_ERROR_INJ_TX_CUSTOM_BIT_OFFSET		24
#define CEC_ERROR_INJ_TX_SHORT_START_OFFSET		26
#define CEC_ERROR_INJ_TX_LONG_START_OFFSET		28
#define CEC_ERROR_INJ_TX_CUSTOM_START_OFFSET		30
#define CEC_ERROR_INJ_TX_LAST_BIT_OFFSET		32
#define CEC_ERROR_INJ_TX_ADD_BYTES_OFFSET		34
#define CEC_ERROR_INJ_TX_REMOVE_BYTE_OFFSET		36
#define CEC_ERROR_INJ_TX_LOW_DRIVE_OFFSET		38
#define CEC_ERROR_INJ_TX_MASK				0xffffffffffff0000ULL

#define CEC_ERROR_INJ_RX_LOW_DRIVE_ARG_IDX		0
#define CEC_ERROR_INJ_RX_ARB_LOST_ARG_IDX		1

#define CEC_ERROR_INJ_TX_ADD_BYTES_ARG_IDX		2
#define CEC_ERROR_INJ_TX_SHORT_BIT_ARG_IDX		3
#define CEC_ERROR_INJ_TX_LONG_BIT_ARG_IDX		4
#define CEC_ERROR_INJ_TX_CUSTOM_BIT_ARG_IDX		5
#define CEC_ERROR_INJ_TX_LAST_BIT_ARG_IDX		6
#define CEC_ERROR_INJ_TX_LOW_DRIVE_ARG_IDX		7
#define CEC_ERROR_INJ_NUM_ARGS				8

/* Special CEC op values */
#define CEC_ERROR_INJ_OP_ANY				0x00000100

/* The default for the low/high time of the custom pulse */
#define CEC_TIM_CUSTOM_DEFAULT				1000

#define CEC_NUM_PIN_EVENTS				128
#define CEC_PIN_EVENT_FL_IS_HIGH			(1 << 0)
#define CEC_PIN_EVENT_FL_DROPPED			(1 << 1)

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
	bool				tx_toggle;
	struct cec_msg			rx_msg;
	u32				rx_bit;
	bool				rx_toggle;
	u32				rx_start_bit_low_too_short_cnt;
	u64				rx_start_bit_low_too_short_ts;
	u32				rx_start_bit_low_too_short_delta;
	u32				rx_start_bit_too_short_cnt;
	u64				rx_start_bit_too_short_ts;
	u32				rx_start_bit_too_short_delta;
	u32				rx_start_bit_too_long_cnt;
	u32				rx_data_bit_too_short_cnt;
	u64				rx_data_bit_too_short_ts;
	u32				rx_data_bit_too_short_delta;
	u32				rx_data_bit_too_long_cnt;
	u32				rx_low_drive_cnt;

	struct cec_msg			work_rx_msg;
	u8				work_tx_status;
	ktime_t				work_tx_ts;
	atomic_t			work_irq_change;
	atomic_t			work_pin_num_events;
	unsigned int			work_pin_events_wr;
	unsigned int			work_pin_events_rd;
	ktime_t				work_pin_ts[CEC_NUM_PIN_EVENTS];
	u8				work_pin_events[CEC_NUM_PIN_EVENTS];
	bool				work_pin_events_dropped;
	u32				work_pin_events_dropped_cnt;
	ktime_t				timer_ts;
	u32				timer_cnt;
	u32				timer_100us_overruns;
	u32				timer_300us_overruns;
	u32				timer_max_overrun;
	u32				timer_sum_overrun;

	u32				tx_custom_low_usecs;
	u32				tx_custom_high_usecs;
	bool				tx_ignore_nack_until_eom;
	bool				tx_custom_pulse;
	bool				tx_generated_poll;
	bool				tx_post_eom;
	u8				tx_extra_bytes;
	u32				tx_low_drive_cnt;
#ifdef CONFIG_CEC_PIN_ERROR_INJ
	u64				error_inj[CEC_ERROR_INJ_OP_ANY + 1];
	u8				error_inj_args[CEC_ERROR_INJ_OP_ANY + 1][CEC_ERROR_INJ_NUM_ARGS];
#endif
};

void cec_pin_start_timer(struct cec_pin *pin);

#ifdef CONFIG_CEC_PIN_ERROR_INJ
bool cec_pin_error_inj_parse_line(struct cec_adapter *adap, char *line);
int cec_pin_error_inj_show(struct cec_adapter *adap, struct seq_file *sf);

u16 cec_pin_rx_error_inj(struct cec_pin *pin);
u16 cec_pin_tx_error_inj(struct cec_pin *pin);
#endif

#endif
