/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2018-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023-2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _NPU_FIRMWARE_H
#define _NPU_FIRMWARE_H

/* -------------------------------------------------------------------------
 * Includes
 * -------------------------------------------------------------------------
 */
#include <linux/types.h>

/* -------------------------------------------------------------------------
 * Defines
 * -------------------------------------------------------------------------
 */
/* NPU Firmware Control/Status Register, written by FW and read HOST */
#define REG_NPU_FW_CTRL_STATUS      NPU_GPR0
/* written by HOST and read by FW for control */
#define REG_NPU_HOST_CTRL_STATUS    NPU_GPR1
/* Data value for control */
#define REG_NPU_HOST_CTRL_VALUE     NPU_GPR2
/* Simulates an interrupt for FW->HOST, used for pre-silicon */
#define REG_FW_TO_HOST_EVENT        NPU_GPR3
/* Read/Written by both host and dsp for sync between driver and dsp */
#define  REG_HOST_DSP_CTRL_STATUS    NPU_GPR4
/* Data value for debug */
#define REG_NPU_FW_DEBUG_DATA       NPU_GPR13

/* Started job count */
#define REG_FW_JOB_CNT_START        NPU_GPR14
/* Finished job count */
#define REG_FW_JOB_CNT_END          NPU_GPR15

/* NPU FW Control/Status Register */
/* bit fields definitions in CTRL STATUS REG */
#define FW_CTRL_STATUS_IPC_READY_BIT            0
#define FW_CTRL_STATUS_LOG_READY_BIT            1
#define FW_CTRL_STATUS_EXECUTE_THREAD_READY_BIT 2
#define FW_CTRL_STATUS_MAIN_THREAD_READY_BIT    3
#define FW_CTRL_STATUS_LOADED_ACO_BIT           4
#define FW_CTRL_STATUS_EXECUTING_ACO_BIT        5
#define FW_CTRL_STATUS_SHUTDOWN_DONE_BIT        12
#define FW_CTRL_STATUS_STACK_CORRUPT_BIT        13

/* 32 bit values of the bit fields above */
#define FW_CTRL_STATUS_IPC_READY_VAL    (1 << FW_CTRL_STATUS_IPC_READY_BIT)
#define FW_CTRL_STATUS_LOG_READY_VAL    (1 << FW_CTRL_STATUS_LOG_READY_BIT)
#define FW_CTRL_STATUS_EXECUTE_THREAD_READY_VAL \
		(1 << FW_CTRL_STATUS_EXECUTE_THREAD_READY_BIT)
#define FW_CTRL_STATUS_MAIN_THREAD_READY_VAL \
		(1 << FW_CTRL_STATUS_MAIN_THREAD_READY_BIT)
#define FW_CTRL_STATUS_LOADED_ACO_VAL \
			(1 << FW_CTRL_STATUS_LOADED_ACO_BIT)
#define FW_CTRL_STATUS_EXECUTING_ACO_VAL \
			(1 << FW_CTRL_STATUS_EXECUTING_ACO_BIT)
#define FW_CTRL_STATUS_SHUTDOWN_DONE_VAL \
			(1 << FW_CTRL_STATUS_SHUTDOWN_DONE_BIT)
#define FW_CTRL_STATUS_STACK_CORRUPT_VAL \
			(1 << FW_CTRL_STATUS_STACK_CORRUPT_BIT)

/* NPU HOST Control/Status Register */
/* bit fields definitions in CTRL STATUS REG */
/* Host has programmed IPC address into the REG_NPU_HOST_CTRL_VALUE register */
#define HOST_CTRL_STATUS_IPC_ADDRESS_READY_BIT      0
/* Host has enabled logging during boot */
#define HOST_CTRL_STATUS_BOOT_ENABLE_LOGGING_BIT    1
/* Host has enabled the clk gating of CAL during boot */
#define HOST_CTRL_STATUS_BOOT_ENABLE_CLK_GATE_BIT   2
/* Host requests to pause fw during boot up */
#define HOST_CTRL_STATUS_FW_PAUSE                   3
/* Host requests to disable watchdog */
#define HOST_CTRL_STATUS_DISABLE_WDOG_BIT  4

/* 32 bit values of the bit fields above */
#define HOST_CTRL_STATUS_IPC_ADDRESS_READY_VAL \
		(1 << HOST_CTRL_STATUS_IPC_ADDRESS_READY_BIT)
#define HOST_CTRL_STATUS_BOOT_ENABLE_LOGGING_VAL \
		(1 << HOST_CTRL_STATUS_BOOT_ENABLE_LOGGING_BIT)
#define HOST_CTRL_STATUS_BOOT_ENABLE_CLK_GATE_VAL \
		(1 << HOST_CTRL_STATUS_BOOT_ENABLE_CLK_GATE_BIT)
#define HOST_CTRL_STATUS_FW_PAUSE_VAL \
		(1 << HOST_CTRL_STATUS_FW_PAUSE)
#define HOST_CTRL_STATUS_DISABLE_WDOG_VAL \
		(1 << HOST_CTRL_STATUS_DISABLE_WDOG_BIT)


/* NPU HOST DSP Control/Status Register */
/* notification of power up */
/* following bits are set by host and read by dsp */
#define HOST_DSP_CTRL_STATUS_PWR_UP_BIT         0
/* notification of power dwn */
#define HOST_DSP_CTRL_STATUS_PWR_DWN_BIT        1
/* following bits are set by dsp and read by host */
/* notification of power up acknowlegement*/
#define HOST_DSP_CTRL_STATUS_PWR_UP_ACK_BIT     4
/* notification of power down acknowlegement*/
#define HOST_DSP_CTRL_STATUS_PWR_DWN_ACK_BIT    5


/* 32 bit values of the bit fields above */
#define HOST_DSP_CTRL_STATUS_PWR_UP_VAL \
		(1 << HOST_DSP_CTRL_STATUS_PWR_UP_BIT)
#define HOST_DSP_CTRL_STATUS_PWR_DWN_VAL \
		(1 << HOST_DSP_CTRL_STATUS_PWR_DWN_BIT)
#define HOST_DSP_CTRL_STATUS_PWR_UP_ACK_VAL \
		(1 << HOST_DSP_CTRL_STATUS_PWR_UP_ACK_BIT)
#define HOST_DSP_CTRL_STATUS_PWR_DWN_ACK_VAL \
		(1 << HOST_DSP_CTRL_STATUS_PWR_DWN_ACK_BIT)

/* Queue table header definition */
struct hfi_queue_tbl_header {
	uint32_t qtbl_version; /* queue table version number */
	uint32_t qtbl_size; /* total tables+queues size in bytes */
	uint32_t qtbl_qhdr0_offset; /* offset of the 1st queue header entry */
	uint32_t qtbl_qhdr_size; /* queue header size */
	uint32_t qtbl_num_q; /* total number of queues */
	uint32_t qtbl_num_active_q; /* number of active queues */
};

/* Queue header definition */
struct hfi_queue_header {
	uint32_t qhdr_status; /* 0 == inactive, 1 == active */
	/* 4 byte-aligned start offset from start of q table */
	uint32_t qhdr_start_offset;
	/* queue type */
	uint32_t qhdr_type;
	/* in bytes, value of 0 means packets are variable size.*/
	uint32_t qhdr_q_size;
	/* size of the Queue packet entries, in bytes, 0 means variable size */
	uint32_t qhdr_pkt_size;

	uint32_t qhdr_pkt_drop_cnt;
	/* receiver watermark in # of queue packets */
	uint32_t qhdr_rx_wm;
	/* transmitter watermark in # of queue packets */
	uint32_t qhdr_tx_wm;
	/*
	 * set to request an interrupt from transmitter
	 * if qhdr_tx_wm is reached
	 */
	uint32_t qhdr_rx_req;
	/*
	 * set to request an interrupt from receiver
	 * if qhdr_rx_wm is reached
	 */
	uint32_t qhdr_tx_req;
	uint32_t qhdr_rx_irq_status; /* Not used */
	uint32_t qhdr_tx_irq_status; /* Not used */
	uint32_t qhdr_read_idx; /* read index in bytes */
	uint32_t qhdr_write_idx; /* write index in bytes */
};

/* in bytes */
#define HFI_QUEUE_TABLE_HEADER_SIZE  (sizeof(struct hfi_queue_tbl_header))
#define HFI_QUEUE_HEADER_SIZE        (sizeof(struct hfi_queue_header))
#define HFI_QUEUE_TABLE_SIZE         (HFI_QUEUE_TABLE_HEADER_SIZE + \
				(NPU_HFI_NUMBER_OF_QS * HFI_QUEUE_HEADER_SIZE))

/* Queue Indexes */
#define IPC_QUEUE_CMD_HIGH_PRIORITY 0 /* High priority Queue APPS->M0 */
#define IPC_QUEUE_APPS_EXEC         1 /* APPS Execute Queue APPS->M0 */
#define IPC_QUEUE_DSP_EXEC          2 /* DSP Execute Queue DSP->M0 */
#define IPC_QUEUE_APPS_RSP          3 /* APPS Message Queue M0->APPS */
#define IPC_QUEUE_DSP_RSP           4 /* DSP Message Queue DSP->APPS */
#define IPC_QUEUE_LOG               5 /* Log Message Queue M0->APPS */

#define NPU_HFI_NUMBER_OF_QS        6
#define NPU_HFI_NUMBER_OF_ACTIVE_QS 6

#define NPU_HFI_QUEUES_PER_CHANNEL  2

#endif /* _NPU_FIRMWARE_H */
