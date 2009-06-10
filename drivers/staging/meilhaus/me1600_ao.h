/**
 * @file me1600_ao.h
 *
 * @brief Meilhaus ME-1600 analog output subdevice class.
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

#ifndef _ME1600_AO_H_
#define _ME1600_AO_H_

# include <linux/version.h>
# include "mesubdevice.h"

# ifdef __KERNEL__

#  define ME1600_MAX_RANGES	2	/**< Specifies the maximum number of ranges in me1600_ao_subdevice_t::u_ranges und me1600_ao_subdevice_t::i_ranges. */

/**
 * @brief Defines a entry in the range table.
 */
typedef struct me1600_ao_range_entry {
	int32_t min;
	int32_t max;
} me1600_ao_range_entry_t;

typedef struct me1600_ao_timeout {
	unsigned long start_time;
	unsigned long delay;
} me1600_ao_timeout_t;

typedef struct me1600_ao_shadow {
	int count;
	unsigned long *registry;
	uint16_t *shadow;
	uint16_t *mirror;
	uint16_t synchronous;									/**< Synchronization list. */
	uint16_t trigger;										/**< Synchronization flag. */
} me1600_ao_shadow_t;

typedef enum ME1600_AO_STATUS {
	ao_status_none = 0,
	ao_status_single_configured,
	ao_status_single_run,
	ao_status_single_end,
	ao_status_last
} ME1600_AO_STATUS;

/**
 * @brief The ME-1600 analog output subdevice class.
 */
typedef struct me1600_ao_subdevice {
	/* Inheritance */
	me_subdevice_t base;									/**< The subdevice base class. */

	/* Attributes */
	int ao_idx;												/**< The index of the analog output subdevice on the device. */

	spinlock_t subdevice_lock;								/**< Spin lock to protect the subdevice from concurrent access. */
	spinlock_t *config_regs_lock;							/**< Spin lock to protect configuration registers from concurrent access. */

	int u_ranges_count;										/**< The number of voltage ranges available on this subdevice. */
	me1600_ao_range_entry_t u_ranges[ME1600_MAX_RANGES];	/**< Array holding the voltage ranges on this subdevice. */
	int i_ranges_count;										/**< The number of current ranges available on this subdevice. */
	me1600_ao_range_entry_t i_ranges[ME1600_MAX_RANGES];	/**< Array holding the current ranges on this subdevice. */

	/* Registers */
	unsigned long uni_bi_reg;								/**< Register for switching between unipoar and bipolar output mode. */
	unsigned long i_range_reg;								/**< Register for switching between ranges. */
	unsigned long sim_output_reg;							/**< Register used in order to update all channels simultaneously. */
	unsigned long current_on_reg;							/**< Register enabling current output on the fourth subdevice. */
#   ifdef PDEBUG_REG
	unsigned long reg_base;
#   endif

	ME1600_AO_STATUS status;
	me1600_ao_shadow_t *ao_regs_shadows;					/**< Addresses and shadows of output's registers. */
	spinlock_t *ao_shadows_lock;							/**< Protects the shadow's struct. */
	int mode;												/**< Mode in witch output should works. */
	wait_queue_head_t wait_queue;							/**< Wait queue to put on tasks waiting for data to arrive. */
	me1600_ao_timeout_t timeout;							/**< The timeout for start in blocking and non-blocking mode. */
	struct workqueue_struct *me1600_workqueue;
	struct delayed_work ao_control_task;

	volatile int ao_control_task_flag;						/**< Flag controling reexecuting of control task */
} me1600_ao_subdevice_t;

/**
 * @brief The constructor to generate a subdevice template instance.
 *
 * @param reg_base The register base address of the device as returned by the PCI BIOS.
 * @param ao_idx The index of the analog output subdevice on the device.
 * @param current Flag indicating that analog output with #ao_idx of 3 is capable of current output.
 * @param config_regs_lock Pointer to spin lock protecting the configuration registers and from concurrent access.
 *
 * @return Pointer to new instance on success.\n
 * NULL on error.
 */
me1600_ao_subdevice_t *me1600_ao_constructor(uint32_t reg_base,
					     unsigned int ao_idx,
					     int curr,
					     spinlock_t * config_regs_lock,
					     spinlock_t * ao_shadows_lock,
					     me1600_ao_shadow_t *
					     ao_regs_shadows,
					     struct workqueue_struct
					     *me1600_wq);

# endif	//__KERNEL__
#endif //_ME1600_AO_H_
