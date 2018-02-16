/******************************************************************************
 *
 * Name: actables.h - ACPI table management
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2018, Intel Corp.
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

#ifndef __ACTABLES_H__
#define __ACTABLES_H__

acpi_status acpi_allocate_root_table(u32 initial_table_count);

/*
 * tbxfroot - Root pointer utilities
 */
u32 acpi_tb_get_rsdp_length(struct acpi_table_rsdp *rsdp);

acpi_status acpi_tb_validate_rsdp(struct acpi_table_rsdp *rsdp);

u8 *acpi_tb_scan_memory_for_rsdp(u8 *start_address, u32 length);

/*
 * tbdata - table data structure management
 */
acpi_status
acpi_tb_get_next_table_descriptor(u32 *table_index,
				  struct acpi_table_desc **table_desc);

void
acpi_tb_init_table_descriptor(struct acpi_table_desc *table_desc,
			      acpi_physical_address address,
			      u8 flags, struct acpi_table_header *table);

acpi_status
acpi_tb_acquire_temp_table(struct acpi_table_desc *table_desc,
			   acpi_physical_address address, u8 flags);

void acpi_tb_release_temp_table(struct acpi_table_desc *table_desc);

acpi_status acpi_tb_validate_temp_table(struct acpi_table_desc *table_desc);

acpi_status
acpi_tb_verify_temp_table(struct acpi_table_desc *table_desc,
			  char *signature, u32 *table_index);

u8 acpi_tb_is_table_loaded(u32 table_index);

void acpi_tb_set_table_loaded_flag(u32 table_index, u8 is_loaded);

/*
 * tbfadt - FADT parse/convert/validate
 */
void acpi_tb_parse_fadt(void);

void acpi_tb_create_local_fadt(struct acpi_table_header *table, u32 length);

/*
 * tbfind - find ACPI table
 */
acpi_status
acpi_tb_find_table(char *signature,
		   char *oem_id, char *oem_table_id, u32 *table_index);

/*
 * tbinstal - Table removal and deletion
 */
acpi_status acpi_tb_resize_root_table_list(void);

acpi_status acpi_tb_validate_table(struct acpi_table_desc *table_desc);

void acpi_tb_invalidate_table(struct acpi_table_desc *table_desc);

void acpi_tb_override_table(struct acpi_table_desc *old_table_desc);

acpi_status
acpi_tb_acquire_table(struct acpi_table_desc *table_desc,
		      struct acpi_table_header **table_ptr,
		      u32 *table_length, u8 *table_flags);

void
acpi_tb_release_table(struct acpi_table_header *table,
		      u32 table_length, u8 table_flags);

acpi_status
acpi_tb_install_standard_table(acpi_physical_address address,
			       u8 flags,
			       u8 reload, u8 override, u32 *table_index);

void acpi_tb_uninstall_table(struct acpi_table_desc *table_desc);

acpi_status
acpi_tb_load_table(u32 table_index, struct acpi_namespace_node *parent_node);

acpi_status
acpi_tb_install_and_load_table(acpi_physical_address address,
			       u8 flags, u8 override, u32 *table_index);

acpi_status acpi_tb_unload_table(u32 table_index);

void acpi_tb_notify_table(u32 event, void *table);

void acpi_tb_terminate(void);

acpi_status acpi_tb_delete_namespace_by_owner(u32 table_index);

acpi_status acpi_tb_allocate_owner_id(u32 table_index);

acpi_status acpi_tb_release_owner_id(u32 table_index);

acpi_status acpi_tb_get_owner_id(u32 table_index, acpi_owner_id *owner_id);

/*
 * tbutils - table manager utilities
 */
acpi_status acpi_tb_initialize_facs(void);

void
acpi_tb_print_table_header(acpi_physical_address address,
			   struct acpi_table_header *header);

u8 acpi_tb_checksum(u8 *buffer, u32 length);

acpi_status
acpi_tb_verify_checksum(struct acpi_table_header *table, u32 length);

void acpi_tb_check_dsdt_header(void);

struct acpi_table_header *acpi_tb_copy_dsdt(u32 table_index);

void
acpi_tb_install_table_with_override(struct acpi_table_desc *new_table_desc,
				    u8 override, u32 *table_index);

acpi_status acpi_tb_parse_root_table(acpi_physical_address rsdp_address);

acpi_status
acpi_tb_get_table(struct acpi_table_desc *table_desc,
		  struct acpi_table_header **out_table);

void acpi_tb_put_table(struct acpi_table_desc *table_desc);

/*
 * tbxfload
 */
acpi_status acpi_tb_load_namespace(void);

#endif				/* __ACTABLES_H__ */
