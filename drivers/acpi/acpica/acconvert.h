/******************************************************************************
 *
 * Module Name: acapps - common include for ACPI applications/tools
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
