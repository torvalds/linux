/**
 * @file me4600_ai.h
 *
 * @brief Meilhaus ME-4000 analog input subdevice class.
 * @note Copyright (C) 2007 Meilhaus Electronic GmbH (support@meilhaus.de)
 * @author Guenter Gebhardt
 * @author Krzysztof Gantzke  (k.gantzke@meilhaus.de)
 */

/*
 * Copyright (C) 2007 Meilhaus Electronic GmbH (support@meilhaus.de)
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef _ME4600_AI_H_
#define _ME4600_AI_H_

#include <linux/version.h>
#include "mesubdevice.h"
#include "meioctl.h"
#include "mecirc_buf.h"

#ifdef __KERNEL__

#define ME4600_AI_MAX_DATA				0xFFFF

#ifdef ME_SYNAPSE
# define ME4600_AI_CIRC_BUF_SIZE_ORDER	8	// 2^n PAGES =>> Maximum value of 1MB for Synapse
#else
# define ME4600_AI_CIRC_BUF_SIZE_ORDER	5	// 2^n PAGES =>> 128KB
#endif
#define ME4600_AI_CIRC_BUF_SIZE 		PAGE_SIZE<<ME4600_AI_CIRC_BUF_SIZE_ORDER	// Buffer size in bytes.

#ifdef _CBUFF_32b_t
# define ME4600_AI_CIRC_BUF_COUNT		((ME4600_AI_CIRC_BUF_SIZE) / sizeof(uint32_t))	// Size in values
#else
# define ME4600_AI_CIRC_BUF_COUNT		((ME4600_AI_CIRC_BUF_SIZE) / sizeof(uint16_t))	// Size in values
#endif

#define ME4600_AI_FIFO_HALF				1024	//ME4600_AI_FIFO_COUNT/2                //1024
#define ME4600_AI_FIFO_MAX_SC			1352	//0.66*ME4600_AI_FIFO_COUNT             //1352

typedef enum ME4600_AI_STATUS {
	ai_status_none = 0,
	ai_status_single_configured,
	ai_status_stream_configured,
	ai_status_stream_run_wait,
	ai_status_stream_run,
	ai_status_stream_end_wait,
	ai_status_stream_end,
	ai_status_stream_fifo_error,
	ai_status_stream_buffer_error,
	ai_status_stream_error,
	ai_status_last
} ME4600_AI_STATUS;

typedef struct me4600_single_config_entry {
	unsigned short status;
	uint32_t entry;
	uint32_t ctrl;
} me4600_single_config_entry_t;

typedef struct me4600_range_entry {
	int min;
	int max;
} me4600_range_entry_t;

typedef struct me4600_ai_ISM {
	volatile unsigned int global_read;				/**< The number of data read in total. */
	volatile unsigned int read;						/**< The number of data read for this chunck. */
	volatile unsigned int next;						/**< The number of data request by user. */
} me4600_ai_ISM_t;

typedef struct me4600_ai_timeout {
	unsigned long start_time;
	unsigned long delay;
} me4600_ai_timeout_t;

/**
 * @brief The ME-4000 analog input subdevice class.
 */
typedef struct me4600_ai_subdevice {
	/* Inheritance */
	me_subdevice_t base;							/**< The subdevice base class. */

	/* Attributes */
	spinlock_t subdevice_lock;						/**< Spin lock to protect the subdevice from concurrent access. */
	spinlock_t *ctrl_reg_lock;						/**< Spin lock to protect #ctrl_reg from concurrent access. */

	/* Hardware feautres */
	unsigned int irq;								/**< The interrupt request number assigned by the PCI BIOS. */
	int isolated;									/**< Marks if this subdevice is on an optoisolated device. */
	int sh;											/**< Marks if this subdevice has sample and hold devices. */

	unsigned int channels;							/**< The number of channels available on this subdevice. */
	me4600_single_config_entry_t single_config[32];	/**< The configuration set for single acquisition. */

	unsigned int data_required;						/**< The number of data request by user. */
	unsigned int fifo_irq_threshold;				/**< The user adjusted FIFO high water interrupt level. */
	unsigned int chan_list_len;						/**< The length of the user defined channel list. */

	me4600_ai_ISM_t ISM;							/**< The information request by Interrupt-State-Machine. */
	volatile enum ME4600_AI_STATUS status;			/**< The current stream status flag. */
	me4600_ai_timeout_t timeout;					/**< The timeout for start in blocking and non-blocking mode. */

											/* Registers *//**< All registers are 32 bits long. */
	unsigned long ctrl_reg;
	unsigned long status_reg;
	unsigned long channel_list_reg;
	unsigned long data_reg;
	unsigned long chan_timer_reg;
	unsigned long chan_pre_timer_reg;
	unsigned long scan_timer_low_reg;
	unsigned long scan_timer_high_reg;
	unsigned long scan_pre_timer_low_reg;
	unsigned long scan_pre_timer_high_reg;
	unsigned long start_reg;
	unsigned long irq_status_reg;
	unsigned long sample_counter_reg;

	unsigned int ranges_len;
	me4600_range_entry_t ranges[4];					/**< The ranges available on this subdevice. */

	/* Software buffer */
	me_circ_buf_t circ_buf;							/**< Circular buffer holding measurment data. */
	wait_queue_head_t wait_queue;					/**< Wait queue to put on tasks waiting for data to arrive. */

	struct workqueue_struct *me4600_workqueue;
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20)
	struct work_struct ai_control_task;
#else
	struct delayed_work ai_control_task;
#endif

	volatile int ai_control_task_flag;				/**< Flag controling reexecuting of control task */

#ifdef MEDEBUG_DEBUG_REG
	unsigned long reg_base;
#endif
} me4600_ai_subdevice_t;

/**
 * @brief The constructor to generate a ME-4000 analog input subdevice instance.
 *
 * @param reg_base The register base address of the device as returned by the PCI BIOS.
 * @param channels The number of analog input channels available on this subdevice.
 * @param channels The number of analog input ranges available on this subdevice.
 * @param isolated Flag indicating if this device is opto isolated.
 * @param sh Flag indicating if sample and hold devices are available.
 * @param irq The irq number assigned by PCI BIOS.
 * @param ctrl_reg_lock Pointer to spin lock protecting the control register from concurrent access.
 *
 * @return Pointer to new instance on success.\n
 * NULL on error.
 */
me4600_ai_subdevice_t *me4600_ai_constructor(uint32_t reg_base,
					     unsigned int channels,
					     unsigned int ranges,
					     int isolated,
					     int sh,
					     int irq,
					     spinlock_t * ctrl_reg_lock,
					     struct workqueue_struct
					     *me4600_wq);

#endif
#endif
