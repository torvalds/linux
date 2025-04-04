/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/******************************************************************************
 *
 * Module Name: acapps - common include for ACPI applications/tools
 *
 * Copyright (C) 2000 - 2025, Intel Corp.
 *
 *****************************************************************************/

#ifndef _ACAPPS
#define _ACAPPS

#ifdef ACPI_USE_STANDARD_HEADERS
#include <sys/stat.h>
#endif				/* ACPI_USE_STANDARD_HEADERS */

/* Common info for tool signons */

#define ACPICA_NAME                 "Intel ACPI Component Architecture"
#define ACPICA_COPYRIGHT            "Copyright (c) 2000 - 2025 Intel Corporation"

#if ACPI_MACHINE_WIDTH == 64
#define ACPI_WIDTH          " (64-bit version)"

#elif ACPI_MACHINE_WIDTH == 32
#define ACPI_WIDTH          " (32-bit version)"

#else
#error unknown ACPI_MACHINE_WIDTH
#define ACPI_WIDTH          " (unknown bit width, not 32 or 64)"

#endif

/* Macros for signons and file headers */

#define ACPI_COMMON_SIGNON(utility_name) \
	"\n%s\n%s version %8.8X\n%s\n\n", \
	ACPICA_NAME, \
	utility_name, ((u32) ACPI_CA_VERSION), \
	ACPICA_COPYRIGHT

#define ACPI_COMMON_HEADER(utility_name, prefix) \
	"%s%s\n%s%s version %8.8X%s\n%s%s\n%s\n", \
	prefix, ACPICA_NAME, \
	prefix, utility_name, ((u32) ACPI_CA_VERSION), ACPI_WIDTH, \
	prefix, ACPICA_COPYRIGHT, \
	prefix

#define ACPI_COMMON_BUILD_TIME \
	"Build date/time: %s %s\n", __DATE__, __TIME__

/* Macros for usage messages */

#define ACPI_USAGE_HEADER(usage) \
	printf ("Usage: %s\nOptions:\n", usage);

#define ACPI_USAGE_TEXT(description) \
	printf (description);

#define ACPI_OPTION(name, description) \
	printf ("  %-20s%s\n", name, description);

/* Check for unexpected exceptions */

#define ACPI_CHECK_STATUS(name, status, expected) \
	if (status != expected) \
	{ \
		acpi_os_printf ("Unexpected %s from %s (%s-%d)\n", \
			acpi_format_exception (status), #name, _acpi_module_name, __LINE__); \
	}

/* Check for unexpected non-AE_OK errors */

#define ACPI_CHECK_OK(name, status)   ACPI_CHECK_STATUS (name, status, AE_OK);

#define FILE_SUFFIX_DISASSEMBLY     "dsl"
#define FILE_SUFFIX_BINARY_TABLE    ".dat"	/* Needs the dot */

/* acfileio */

acpi_status
ac_get_all_tables_from_file(char *filename,
			    u8 get_only_aml_tables,
			    struct acpi_new_table_desc **return_list_head);

void ac_delete_table_list(struct acpi_new_table_desc *list_head);

u8 ac_is_file_binary(FILE * file);

acpi_status ac_validate_table_header(FILE * file, long table_offset);

/* Values for get_only_aml_tables */

#define ACPI_GET_ONLY_AML_TABLES    TRUE
#define ACPI_GET_ALL_TABLES         FALSE

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
acpi_dm_convert_parse_objects(union acpi_parse_object *parse_tree_root,
			      struct acpi_namespace_node *namespace_root);

/*
 * adfile
 */
acpi_status ad_initialize(void);

char *fl_generate_filename(char *input_filename, char *suffix);

acpi_status
fl_split_input_pathname(char *input_path,
			char **out_directory_path, char **out_filename);

char *fl_get_file_basename(char *file_pathname);

char *ad_generate_filename(char *prefix, char *table_id);

void
ad_write_table(struct acpi_table_header *table,
	       u32 length, char *table_name, char *oem_table_id);

#endif				/* _ACAPPS */
