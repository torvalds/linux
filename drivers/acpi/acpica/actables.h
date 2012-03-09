/******************************************************************************
 *
 * Name: actables.h - ACPI table management
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2012, Intel Corp.
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
 * tbfadt - FADT parse/convert/validate
 */
void acpi_tb_parse_fadt(u32 table_index);

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

acpi_status acpi_tb_verify_table(struct acpi_table_desc *table_desc);

acpi_status
acpi_tb_add_table(struct acpi_table_desc *table_desc, u32 *table_index);

acpi_status
acpi_tb_store_table(acpi_physical_address address,
		    struct acpi_table_header *table,
		    u32 length, u8 flags, u32 *table_index);

void acpi_tb_delete_table(struct acpi_table_desc *table_desc);

void acpi_tb_terminate(void);

acpi_status acpi_tb_delete_namespace_by_owner(u32 table_index);

acpi_status acpi_tb_allocate_owner_id(u32 table_index);

acpi_status acpi_tb_release_owner_id(u32 table_index);

acpi_status acpi_tb_get_owner_id(u32 table_index, acpi_owner_id *owner_id);

u8 acpi_tb_is_table_loaded(u32 table_index);

void acpi_tb_set_table_loaded_flag(u32 table_index, u8 is_loaded);

/*
 * tbutils - table manager utilities
 */
acpi_status acpi_tb_initialize_facs(void);

u8 acpi_tb_tables_loaded(void);

void
acpi_tb_print_table_header(acpi_physical_address address,
			   struct acpi_table_header *header);

u8 acpi_tb_checksum(u8 *buffer, u32 length);

acpi_status
acpi_tb_verify_checksum(struct acpi_table_header *table, u32 length);

void acpi_tb_check_dsdt_header(void);

struct acpi_table_header *acpi_tb_copy_dsdt(u32 table_index);

void
acpi_tb_install_table(acpi_physical_address address,
		      char *signature, u32 table_index);

acpi_status acpi_tb_parse_root_table(acpi_physical_address rsdp_address);

#endif				/* __ACTABLES_H__ */
