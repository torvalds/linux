/******************************************************************************
 *
 * Module Name: acapps - common include for ACPI applications/tools
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2015, Intel Corp.
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

#ifndef _ACAPPS
#define _ACAPPS

/* Common info for tool signons */

#define ACPICA_NAME                 "Intel ACPI Component Architecture"
#define ACPICA_COPYRIGHT            "Copyright (c) 2000 - 2015 Intel Corporation"

#if ACPI_MACHINE_WIDTH == 64
#define ACPI_WIDTH          "-64"

#elif ACPI_MACHINE_WIDTH == 32
#define ACPI_WIDTH          "-32"

#else
#error unknown ACPI_MACHINE_WIDTH
#define ACPI_WIDTH          "-??"

#endif

/* Macros for signons and file headers */

#define ACPI_COMMON_SIGNON(utility_name) \
	"\n%s\n%s version %8.8X%s\n%s\n\n", \
	ACPICA_NAME, \
	utility_name, ((u32) ACPI_CA_VERSION), ACPI_WIDTH, \
	ACPICA_COPYRIGHT

#define ACPI_COMMON_HEADER(utility_name, prefix) \
	"%s%s\n%s%s version %8.8X%s\n%s%s\n%s\n", \
	prefix, ACPICA_NAME, \
	prefix, utility_name, ((u32) ACPI_CA_VERSION), ACPI_WIDTH, \
	prefix, ACPICA_COPYRIGHT, \
	prefix

/* Macros for usage messages */

#define ACPI_USAGE_HEADER(usage) \
	acpi_os_printf ("Usage: %s\nOptions:\n", usage);

#define ACPI_USAGE_TEXT(description) \
	acpi_os_printf (description);

#define ACPI_OPTION(name, description) \
	acpi_os_printf (" %-18s%s\n", name, description);

#define FILE_SUFFIX_DISASSEMBLY     "dsl"
#define FILE_SUFFIX_BINARY_TABLE    ".dat"	/* Needs the dot */

/*
 * getopt
 */
int acpi_getopt(int argc, char **argv, char *opts);

int acpi_getopt_argument(int argc, char **argv);

extern int acpi_gbl_optind;
extern int acpi_gbl_opterr;
extern int acpi_gbl_sub_opt_char;
extern char *acpi_gbl_optarg;

/*
 * cmfsize - Common get file size function
 */
u32 cm_get_file_size(ACPI_FILE file);

#ifndef ACPI_DUMP_APP
/*
 * adisasm
 */
acpi_status
ad_aml_disassemble(u8 out_to_file,
		   char *filename, char *prefix, char **out_filename);

void ad_print_statistics(void);

acpi_status ad_find_dsdt(u8 **dsdt_ptr, u32 *dsdt_length);

void ad_dump_tables(void);

acpi_status ad_get_local_tables(void);

acpi_status
ad_parse_table(struct acpi_table_header *table,
	       acpi_owner_id * owner_id, u8 load_table, u8 external);

acpi_status ad_display_tables(char *filename, struct acpi_table_header *table);

acpi_status ad_display_statistics(void);

/*
 * adwalk
 */
void
acpi_dm_cross_reference_namespace(union acpi_parse_object *parse_tree_root,
				  struct acpi_namespace_node *namespace_root,
				  acpi_owner_id owner_id);

void acpi_dm_dump_tree(union acpi_parse_object *origin);

void acpi_dm_find_orphan_methods(union acpi_parse_object *origin);

void
acpi_dm_finish_namespace_load(union acpi_parse_object *parse_tree_root,
			      struct acpi_namespace_node *namespace_root,
			      acpi_owner_id owner_id);

void
acpi_dm_convert_resource_indexes(union acpi_parse_object *parse_tree_root,
				 struct acpi_namespace_node *namespace_root);

/*
 * adfile
 */
acpi_status ad_initialize(void);

char *fl_generate_filename(char *input_filename, char *suffix);

acpi_status
fl_split_input_pathname(char *input_path,
			char **out_directory_path, char **out_filename);

char *ad_generate_filename(char *prefix, char *table_id);

void
ad_write_table(struct acpi_table_header *table,
	       u32 length, char *table_name, char *oem_table_id);
#endif

#endif				/* _ACAPPS */
