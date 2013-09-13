/******************************************************************************
 *
 * Name: acevents.h - Event subcomponent prototypes and defines
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2013, Intel Corp.
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

#ifndef __ACEVENTS_H__
#define __ACEVENTS_H__

/*
 * evevent
 */
acpi_status acpi_ev_initialize_events(void);

acpi_status acpi_ev_install_xrupt_handlers(void);

u32 acpi_ev_fixed_event_detect(void);

/*
 * evmisc
 */
u8 acpi_ev_is_notify_object(struct acpi_namespace_node *node);

u32 acpi_ev_get_gpe_number_index(u32 gpe_number);

acpi_status
acpi_ev_queue_notify_request(struct acpi_namespace_node *node,
			     u32 notify_value);

/*
 * evglock - Global Lock support
 */
acpi_status acpi_ev_init_global_lock_handler(void);

ACPI_HW_DEPENDENT_RETURN_OK(acpi_status
			    acpi_ev_acquire_global_lock(u16 timeout))
 ACPI_HW_DEPENDENT_RETURN_OK(acpi_status acpi_ev_release_global_lock(void))
 acpi_status acpi_ev_remove_global_lock_handler(void);

/*
 * evgpe - Low-level GPE support
 */
u32 acpi_ev_gpe_detect(struct acpi_gpe_xrupt_info *gpe_xrupt_list);

acpi_status
acpi_ev_update_gpe_enable_mask(struct acpi_gpe_event_info *gpe_event_info);

acpi_status acpi_ev_enable_gpe(struct acpi_gpe_event_info *gpe_event_info);

acpi_status
acpi_ev_add_gpe_reference(struct acpi_gpe_event_info *gpe_event_info);

acpi_status
acpi_ev_remove_gpe_reference(struct acpi_gpe_event_info *gpe_event_info);

struct acpi_gpe_event_info *acpi_ev_get_gpe_event_info(acpi_handle gpe_device,
						       u32 gpe_number);

struct acpi_gpe_event_info *acpi_ev_low_get_gpe_info(u32 gpe_number,
						     struct acpi_gpe_block_info
						     *gpe_block);

acpi_status acpi_ev_finish_gpe(struct acpi_gpe_event_info *gpe_event_info);

/*
 * evgpeblk - Upper-level GPE block support
 */
acpi_status
acpi_ev_create_gpe_block(struct acpi_namespace_node *gpe_device,
			 struct acpi_generic_address *gpe_block_address,
			 u32 register_count,
			 u8 gpe_block_base_number,
			 u32 interrupt_number,
			 struct acpi_gpe_block_info **return_gpe_block);

acpi_status
acpi_ev_initialize_gpe_block(struct acpi_gpe_xrupt_info *gpe_xrupt_info,
			     struct acpi_gpe_block_info *gpe_block,
			     void *context);

ACPI_HW_DEPENDENT_RETURN_OK(acpi_status
			    acpi_ev_delete_gpe_block(struct acpi_gpe_block_info
						     *gpe_block))

u32
acpi_ev_gpe_dispatch(struct acpi_namespace_node *gpe_device,
		     struct acpi_gpe_event_info *gpe_event_info,
		     u32 gpe_number);

/*
 * evgpeinit - GPE initialization and update
 */
acpi_status acpi_ev_gpe_initialize(void);

ACPI_HW_DEPENDENT_RETURN_VOID(void
			      acpi_ev_update_gpes(acpi_owner_id table_owner_id))

 acpi_status
acpi_ev_match_gpe_method(acpi_handle obj_handle,
			 u32 level, void *context, void **return_value);

/*
 * evgpeutil - GPE utilities
 */
acpi_status
acpi_ev_walk_gpe_list(acpi_gpe_callback gpe_walk_callback, void *context);

u8 acpi_ev_valid_gpe_event(struct acpi_gpe_event_info *gpe_event_info);

acpi_status
acpi_ev_get_gpe_device(struct acpi_gpe_xrupt_info *gpe_xrupt_info,
		       struct acpi_gpe_block_info *gpe_block, void *context);

struct acpi_gpe_xrupt_info *acpi_ev_get_gpe_xrupt_block(u32 interrupt_number);

acpi_status acpi_ev_delete_gpe_xrupt(struct acpi_gpe_xrupt_info *gpe_xrupt);

acpi_status
acpi_ev_delete_gpe_handlers(struct acpi_gpe_xrupt_info *gpe_xrupt_info,
			    struct acpi_gpe_block_info *gpe_block,
			    void *context);

/*
 * evhandler - Address space handling
 */
u8
acpi_ev_has_default_handler(struct acpi_namespace_node *node,
			    acpi_adr_space_type space_id);

acpi_status acpi_ev_install_region_handlers(void);

acpi_status
acpi_ev_install_space_handler(struct acpi_namespace_node *node,
			      acpi_adr_space_type space_id,
			      acpi_adr_space_handler handler,
			      acpi_adr_space_setup setup, void *context);

/*
 * evregion - Operation region support
 */
acpi_status acpi_ev_initialize_op_regions(void);

acpi_status
acpi_ev_address_space_dispatch(union acpi_operand_object *region_obj,
			       union acpi_operand_object *field_obj,
			       u32 function,
			       u32 region_offset, u32 bit_width, u64 *value);

acpi_status
acpi_ev_attach_region(union acpi_operand_object *handler_obj,
		      union acpi_operand_object *region_obj,
		      u8 acpi_ns_is_locked);

void
acpi_ev_detach_region(union acpi_operand_object *region_obj,
		      u8 acpi_ns_is_locked);

acpi_status
acpi_ev_execute_reg_methods(struct acpi_namespace_node *node,
			    acpi_adr_space_type space_id);

acpi_status
acpi_ev_execute_reg_method(union acpi_operand_object *region_obj, u32 function);

/*
 * evregini - Region initialization and setup
 */
acpi_status
acpi_ev_system_memory_region_setup(acpi_handle handle,
				   u32 function,
				   void *handler_context,
				   void **region_context);

acpi_status
acpi_ev_io_space_region_setup(acpi_handle handle,
			      u32 function,
			      void *handler_context, void **region_context);

acpi_status
acpi_ev_pci_config_region_setup(acpi_handle handle,
				u32 function,
				void *handler_context, void **region_context);

acpi_status
acpi_ev_cmos_region_setup(acpi_handle handle,
			  u32 function,
			  void *handler_context, void **region_context);

acpi_status
acpi_ev_pci_bar_region_setup(acpi_handle handle,
			     u32 function,
			     void *handler_context, void **region_context);

acpi_status
acpi_ev_default_region_setup(acpi_handle handle,
			     u32 function,
			     void *handler_context, void **region_context);

acpi_status
acpi_ev_initialize_region(union acpi_operand_object *region_obj,
			  u8 acpi_ns_locked);

/*
 * evsci - SCI (System Control Interrupt) handling/dispatch
 */
u32 ACPI_SYSTEM_XFACE acpi_ev_gpe_xrupt_handler(void *context);

u32 acpi_ev_install_sci_handler(void);

acpi_status acpi_ev_remove_sci_handler(void);

u32 acpi_ev_initialize_SCI(u32 program_SCI);

ACPI_HW_DEPENDENT_RETURN_VOID(void acpi_ev_terminate(void))
#endif				/* __ACEVENTS_H__  */
