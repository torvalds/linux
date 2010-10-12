/******************************************************************************
 *
 * Name: achware.h -- hardware specific interfaces
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2010, Intel Corp.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon
 *    including a substantially similar Disclaimer requirement for further
 *    binary redistribution.
 * 3. Neither the names of the above-listed copyright holders nor the names
 *    of any contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 */

#ifndef __ACHWARE_H__
#define __ACHWARE_H__

/* Values for the _SST predefined method */

#define ACPI_SST_INDICATOR_OFF  0
#define ACPI_SST_WORKING        1
#define ACPI_SST_WAKING         2
#define ACPI_SST_SLEEPING       3
#define ACPI_SST_SLEEP_CONTEXT  4

/*
 * hwacpi - high level functions
 */
acpi_status acpi_hw_set_mode(u32 mode);

u32 acpi_hw_get_mode(void);

/*
 * hwregs - ACPI Register I/O
 */
acpi_status
acpi_hw_validate_register(struct acpi_generic_address *reg,
			  u8 max_bit_width, u64 *address);

acpi_status acpi_hw_read(u32 *value, struct acpi_generic_address *reg);

acpi_status acpi_hw_write(u32 value, struct acpi_generic_address *reg);

struct acpi_bit_register_info *acpi_hw_get_bit_register_info(u32 register_id);

acpi_status acpi_hw_write_pm1_control(u32 pm1a_control, u32 pm1b_control);

acpi_status acpi_hw_register_read(u32 register_id, u32 *return_value);

acpi_status acpi_hw_register_write(u32 register_id, u32 value);

acpi_status acpi_hw_clear_acpi_status(void);

/*
 * hwvalid - Port I/O with validation
 */
acpi_status acpi_hw_read_port(acpi_io_address address, u32 *value, u32 width);

acpi_status acpi_hw_write_port(acpi_io_address address, u32 value, u32 width);

/*
 * hwgpe - GPE support
 */
u32 acpi_hw_get_gpe_register_bit(struct acpi_gpe_event_info *gpe_event_info,
			     struct acpi_gpe_register_info *gpe_register_info);

acpi_status
acpi_hw_low_set_gpe(struct acpi_gpe_event_info *gpe_event_info, u8 action);

acpi_status
acpi_hw_disable_gpe_block(struct acpi_gpe_xrupt_info *gpe_xrupt_info,
			  struct acpi_gpe_block_info *gpe_block, void *context);

acpi_status acpi_hw_clear_gpe(struct acpi_gpe_event_info *gpe_event_info);

acpi_status
acpi_hw_clear_gpe_block(struct acpi_gpe_xrupt_info *gpe_xrupt_info,
			struct acpi_gpe_block_info *gpe_block, void *context);

acpi_status
acpi_hw_get_gpe_status(struct acpi_gpe_event_info *gpe_event_info,
		       acpi_event_status * event_status);

acpi_status acpi_hw_disable_all_gpes(void);

acpi_status acpi_hw_enable_all_runtime_gpes(void);

acpi_status acpi_hw_enable_all_wakeup_gpes(void);

acpi_status
acpi_hw_enable_runtime_gpe_block(struct acpi_gpe_xrupt_info *gpe_xrupt_info,
				 struct acpi_gpe_block_info *gpe_block,
				 void *context);

#ifdef	ACPI_FUTURE_USAGE
/*
 * hwtimer - ACPI Timer prototypes
 */
acpi_status acpi_get_timer_resolution(u32 * resolution);

acpi_status acpi_get_timer(u32 * ticks);

acpi_status
acpi_get_timer_duration(u32 start_ticks, u32 end_ticks, u32 * time_elapsed);
#endif				/* ACPI_FUTURE_USAGE */

#endif				/* __ACHWARE_H__ */
