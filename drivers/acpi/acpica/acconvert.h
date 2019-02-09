/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/******************************************************************************
 *
 * Module Name: acapps - common include for ACPI applications/tools
 *
 * Copyright (C) 2000 - 2018, Intel Corp.
 *
 *****************************************************************************/

#ifndef _ACCONVERT
#define _ACCONVERT

/* Definitions for comment state */

#define ASL_COMMENT_STANDARD    1
#define ASLCOMMENT_INLINE       2
#define ASL_COMMENT_OPEN_PAREN  3
#define ASL_COMMENT_CLOSE_PAREN 4
#define ASL_COMMENT_CLOSE_BRACE 5

/* Definitions for comment print function*/

#define AML_COMMENT_STANDARD    1
#define AMLCOMMENT_INLINE       2
#define AML_COMMENT_END_NODE    3
#define AML_NAMECOMMENT         4
#define AML_COMMENT_CLOSE_BRACE 5
#define AML_COMMENT_ENDBLK      6
#define AML_COMMENT_INCLUDE     7

#ifdef ACPI_ASL_COMPILER
/*
 * cvcompiler
 */
void
cv_process_comment(struct asl_comment_state current_state,
		   char *string_buffer, int c1);

void
cv_process_comment_type2(struct asl_comment_state current_state,
			 char *string_buffer);

u32 cv_calculate_comment_lengths(union acpi_parse_object *op);

void cv_process_comment_state(char input);

char *cv_append_inline_comment(char *inline_comment, char *to_add);

void cv_add_to_comment_list(char *to_add);

void cv_place_comment(u8 type, char *comment_string);

u32 cv_parse_op_block_type(union acpi_parse_object *op);

struct acpi_comment_node *cv_comment_node_calloc(void);

void cg_write_aml_def_block_comment(union acpi_parse_object *op);

void
cg_write_one_aml_comment(union acpi_parse_object *op,
			 char *comment_to_print, u8 input_option);

void cg_write_aml_comment(union acpi_parse_object *op);

/*
 * cvparser
 */
void
cv_init_file_tree(struct acpi_table_header *table,
		  u8 *aml_start, u32 aml_length);

void cv_clear_op_comments(union acpi_parse_object *op);

struct acpi_file_node *cv_filename_exists(char *filename,
					  struct acpi_file_node *head);

void cv_label_file_node(union acpi_parse_object *op);

void
cv_capture_list_comments(struct acpi_parse_state *parser_state,
			 struct acpi_comment_node *list_head,
			 struct acpi_comment_node *list_tail);

void cv_capture_comments_only(struct acpi_parse_state *parser_state);

void cv_capture_comments(struct acpi_walk_state *walk_state);

void cv_transfer_comments(union acpi_parse_object *op);

/*
 * cvdisasm
 */
void cv_switch_files(u32 level, union acpi_parse_object *op);

u8 cv_file_has_switched(union acpi_parse_object *op);

void cv_close_paren_write_comment(union acpi_parse_object *op, u32 level);

void cv_close_brace_write_comment(union acpi_parse_object *op, u32 level);

void
cv_print_one_comment_list(struct acpi_comment_node *comment_list, u32 level);

void
cv_print_one_comment_type(union acpi_parse_object *op,
			  u8 comment_type, char *end_str, u32 level);

#endif

#endif				/* _ACCONVERT */
