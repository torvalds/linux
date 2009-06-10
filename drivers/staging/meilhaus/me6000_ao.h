/**
 * @file me6000_ao.h
 *
 * @brief Meilhaus ME-6000 analog output subdevice class.
 * @note Copyright (C) 2007 Meilhaus Electronic GmbH (support@meilhaus.de)
 * @author Guenter Gebhardt
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

#ifndef _ME6000_AO_H_
#define _ME6000_AO_H_

#include <linux/version.h>
#include "mesubdevice.h"
#include "mecirc_buf.h"
#include "meioctl.h"

#ifdef __KERNEL__

#define ME6000_AO_MAX_SUBDEVICES	16
#define ME6000_AO_FIFO_COUNT		8192

#define ME6000_AO_BASE_FREQUENCY	33000000L

#define ME6000_AO_MIN_ACQ_TICKS		0LL
#define ME6000_AO_MAX_ACQ_TICKS		0LL

#define ME6000_AO_MIN_CHAN_TICKS	66LL
#define ME6000_AO_MAX_CHAN_TICKS	0xFFFFFFFFLL

#define ME6000_AO_MIN_RANGE			-10000000
#define ME6000_AO_MAX_RANGE			9999694

#define ME6000_AO_MIN_RANGE_HIGH	0
#define ME6000_AO_MAX_RANGE_HIGH	49999237

#define ME6000_AO_MAX_DATA			0xFFFF

#ifdef ME_SYNAPSE
# define ME6000_AO_CIRC_BUF_SIZE_ORDER 		8	// 2^n PAGES =>> Maximum value of 1MB for Synapse
#else
# define ME6000_AO_CIRC_BUF_SIZE_ORDER 		5	// 2^n PAGES =>> 128KB
#endif
#define ME6000_AO_CIRC_BUF_SIZE 		PAGE_SIZE<<ME6000_AO_CIRC_BUF_SIZE_ORDER	// Buffer size in bytes.

#  ifdef _CBUFF_32b_t
#   define ME6000_AO_CIRC_BUF_COUNT	((ME6000_AO_CIRC_BUF_SIZE) / sizeof(uint32_t))	// Size in values
#  else
#   define ME6000_AO_CIRC_BUF_COUNT	((ME6000_AO_CIRC_BUF_SIZE) / sizeof(uint16_t))	// Size in values
#  endif

#  define ME6000_AO_CONTINOUS					0x0
#  define ME6000_AO_WRAP_MODE					0x1
#  define ME6000_AO_HW_MODE						0x2

#  define ME6000_AO_HW_WRAP_MODE				(ME6000_AO_WRAP_MODE | ME6000_AO_HW_MODE)
#  define ME6000_AO_SW_WRAP_MODE				ME6000_AO_WRAP_MODE

#  define ME6000_AO_INF_STOP_MODE				0x0
#  define ME6000_AO_ACQ_STOP_MODE				0x1
#  define ME6000_AO_SCAN_STOP_MODE				0x2

#  define ME6000_AO_EXTRA_HARDWARE				0x1
#  define ME6000_AO_HAS_FIFO					0x2

typedef enum ME6000_AO_STATUS {
	ao_status_none = 0,
	ao_status_single_configured,
	ao_status_single_run_wait,
	ao_status_single_run,
	ao_status_single_end_wait,
	ao_status_single_end,
	ao_status_stream_configured,
	ao_status_stream_run_wait,
	ao_status_stream_run,
	ao_status_stream_end_wait,
	ao_status_stream_end,
	ao_status_stream_fifo_error,
	ao_status_stream_buffer_error,
	ao_status_stream_error,
	ao_status_last
} ME6000_AO_STATUS;

typedef struct me6000_ao_timeout {
	unsigned long start_time;
	unsigned long delay;
} me6000_ao_timeout_t;

/**
 * @brief The ME-6000 analog output subdevice class.
 */
typedef struct me6000_ao_subdevice {
	/* Inheritance */
	me_subdevice_t base;					/**< The subdevice base class. */
	unsigned int ao_idx;

	/* Attributes */
	spinlock_t subdevice_lock;				/**< Spin lock to protect the subdevice from concurrent access. */
	spinlock_t *preload_reg_lock;			/**< Spin lock to protect preload_reg from concurrent access. */

	uint32_t *preload_flags;
	uint32_t *triggering_flags;

	/* Hardware feautres */
	unsigned int irq;						/**< The interrupt request number assigned by the PCI BIOS. */
	int fifo;								/**< If set this device has a FIFO. */

	//Range
	int min;
	int max;

	int single_value;						/**< Mirror of the output value in single mode. */
	int single_value_in_fifo;				/**< Mirror of the value written in single mode. */
	uint32_t ctrl_trg;						/**< Mirror of the trigger settings. */

	volatile int mode;						/**< Flags used for storing SW wraparound setup*/
	int stop_mode;							/**< The user defined stop condition flag. */
	unsigned int start_mode;
	unsigned int stop_count;				/**< The user defined dates presentation end count. */
	unsigned int stop_data_count;			/**< The stop presentation count. */
	unsigned int data_count;				/**< The real presentation count. */
	unsigned int preloaded_count;			/**< The next data addres in buffer. <= for wraparound mode. */
	int hardware_stop_delay;				/**< The time that stop can take. This is only to not show hardware bug to user. */

	volatile enum ME6000_AO_STATUS status;	/**< The current stream status flag. */
	me6000_ao_timeout_t timeout;			/**< The timeout for start in blocking and non-blocking mode. */

									/* Registers *//**< All registers are 32 bits long. */
	unsigned long ctrl_reg;
	unsigned long status_reg;
	unsigned long fifo_reg;
	unsigned long single_reg;
	unsigned long timer_reg;
	unsigned long irq_status_reg;
	unsigned long preload_reg;
	unsigned long irq_reset_reg;
#ifdef MEDEBUG_DEBUG_REG
	unsigned long reg_base;
#endif

	/* Software buffer */
	me_circ_buf_t circ_buf;					/**< Circular buffer holding measurment data. */
	wait_queue_head_t wait_queue;			/**< Wait queue to put on tasks waiting for data to arrive. */

	struct workqueue_struct *me6000_workqueue;
	struct delayed_work ao_control_task;

	volatile int ao_control_task_flag;		/**< Flag controling reexecuting of control task */

} me6000_ao_subdevice_t;

/**
 * @brief The constructor to generate a ME-6000 analog output subdevice instance.
 *
 * @param reg_base The register base address of the device as returned by the PCI BIOS.
 * @param ctrl_reg_lock Pointer to spin lock protecting the control register from concurrent access.
 * @param preload_flags Pointer to spin lock protecting the hold&trigger register from concurrent access.
 * @param ao_idx Subdevice number.
 * @param fifo Flag set if subdevice has hardware FIFO.
 * @param irq IRQ number.
 * @param high_range Flag set if subdevice has high curren output.
 * @param me6000_wq Queue for asynchronous task (1 queue for all subdevice on 1 board).
 *
 * @return Pointer to new instance on success.\n
 * NULL on error.
 */
me6000_ao_subdevice_t *me6000_ao_constructor(uint32_t reg_base,
					     spinlock_t * preload_reg_lock,
					     uint32_t * preload_flags,
					     uint32_t * triggering_flags,
					     int ao_idx,
					     int fifo,
					     int irq,
					     int high_range,
					     struct workqueue_struct
					     *me6000_wq);

#endif //__KERNEL__
#endif //_ME6000_AO_H_
