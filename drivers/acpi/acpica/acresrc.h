/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/******************************************************************************
 *
 * Name: acresrc.h - Resource Manager function prototypes
 *
 * Copyright (C) 2000 - 2019, Intel Corp.
 *
 *****************************************************************************/

#ifndef __ACRESRC_H__
#define __ACRESRC_H__

/* Need the AML resource descriptor structs */

#include "amlresrc.h"

/*
 * If possible, pack the following structures to byte alignment, since we
 * don't care about performance for debug output. Two cases where we cannot
 * pack the structures:
 *
 * 1) Hardware does not support misaligned memory transfers
 * 2) Compiler does not support pointers within packed structures
 */
#if (!defined(ACPI_MISALIGNMENT_NOT_SUPPORTED) && !defined(ACPI_PACKED_POINTERS_NOT_SUPPORTED))
#pragma pack(1)
#endif

/*
 * Individual entry for the resource conversion tables
 */
typedef const struct acpi_rsconvert_info {
	u8 opcode;
	u8 resource_offset;
	u8 aml_offset;
	u8 value;

} acpi_rsconvert_info;

/* Resource conversion opcodes */

typedef enum {
	ACPI_RSC_INITGET = 0,
	ACPI_RSC_INITSET,
	ACPI_RSC_FLAGINIT,
	ACPI_RSC_1BITFLAG,
	ACPI_RSC_2BITFLAG,
	ACPI_RSC_3BITFLAG,
	ACPI_RSC_ADDRESS,
	ACPI_RSC_BITMASK,
	ACPI_RSC_BITMASK16,
	ACPI_RSC_COUNT,
	ACPI_RSC_COUNT16,
	ACPI_RSC_COUNT_GPIO_PIN,
	ACPI_RSC_COUNT_GPIO_RES,
	ACPI_RSC_COUNT_GPIO_VEN,
	ACPI_RSC_COUNT_SERIAL_RES,
	ACPI_RSC_COUNT_SERIAL_VEN,
	ACPI_RSC_DATA8,
	ACPI_RSC_EXIT_EQ,
	ACPI_RSC_EXIT_LE,
	ACPI_RSC_EXIT_NE,
	ACPI_RSC_LENGTH,
	ACPI_RSC_MOVE_GPIO_PIN,
	ACPI_RSC_MOVE_GPIO_RES,
	ACPI_RSC_MOVE_SERIAL_RES,
	ACPI_RSC_MOVE_SERIAL_VEN,
	ACPI_RSC_MOVE8,
	ACPI_RSC_MOVE16,
	ACPI_RSC_MOVE32,
	ACPI_RSC_MOVE64,
	ACPI_RSC_SET8,
	ACPI_RSC_SOURCE,
	ACPI_RSC_SOURCEX
} ACPI_RSCONVERT_OPCODES;

/* Resource Conversion sub-opcodes */

#define ACPI_RSC_COMPARE_AML_LENGTH     0
#define ACPI_RSC_COMPARE_VALUE          1

#define ACPI_RSC_TABLE_SIZE(d)          (sizeof (d) / sizeof (struct acpi_rsconvert_info))

#define ACPI_RS_OFFSET(f)               (u8) ACPI_OFFSET (struct acpi_resource,f)
#define AML_OFFSET(f)                   (u8) ACPI_OFFSET (union aml_resource,f)

/*
 * Individual entry for the resource dump tables
 */
typedef const struct acpi_rsdump_info {
	u8 opcode;
	u8 offset;
	const char *name;
	const char **pointer;

} acpi_rsdump_info;

/* Values for the Opcode field above */

typedef enum {
	ACPI_RSD_TITLE = 0,
	ACPI_RSD_1BITFLAG,
	ACPI_RSD_2BITFLAG,
	ACPI_RSD_3BITFLAG,
	ACPI_RSD_ADDRESS,
	ACPI_RSD_DWORDLIST,
	ACPI_RSD_LITERAL,
	ACPI_RSD_LONGLIST,
	ACPI_RSD_SHORTLIST,
	ACPI_RSD_SHORTLISTX,
	ACPI_RSD_SOURCE,
	ACPI_RSD_STRING,
	ACPI_RSD_UINT8,
	ACPI_RSD_UINT16,
	ACPI_RSD_UINT32,
	ACPI_RSD_UINT64,
	ACPI_RSD_WORDLIST,
	ACPI_RSD_LABEL,
	ACPI_RSD_SOURCE_LABEL,

} ACPI_RSDUMP_OPCODES;

/* restore default alignment */

#pragma pack()

/* Resource tables indexed by internal resource type */

extern const u8 acpi_gbl_aml_resource_sizes[];
extern const u8 acpi_gbl_aml_resource_serial_bus_sizes[];
extern struct acpi_rsconvert_info *acpi_gbl_set_resource_dispatch[];

/* Resource tables indexed by raw AML resource descriptor type */

extern const u8 acpi_gbl_resource_struct_sizes[];
extern const u8 acpi_gbl_resource_struct_serial_bus_sizes[];
extern struct acpi_rsconvert_info *acpi_gbl_get_resource_dispatch[];

extern struct acpi_rsconvert_info
    *acpi_gbl_convert_resource_serial_bus_dispatch[];

struct acpi_vendor_walk_info {
	struct acpi_vendor_uuid *uuid;
	struct acpi_buffer *buffer;
	acpi_status status;
};

/*
 * rscreate
 */
acpi_status
acpi_rs_create_resource_list(union acpi_operand_object *aml_buffer,
			     struct acpi_buffer *output_buffer);

acpi_status
acpi_rs_create_aml_resources(struct acpi_buffer *resource_list,
			     struct acpi_buffer *output_buffer);

acpi_status
acpi_rs_create_pci_routing_table(union acpi_operand_object *package_object,
				 struct acpi_buffer *output_buffer);

/*
 * rsutils
 */

acpi_status
acpi_rs_get_prt_method_data(struct acpi_namespace_node *node,
			    struct acpi_buffer *ret_buffer);

acpi_status
acpi_rs_get_crs_method_data(struct acpi_namespace_node *node,
			    struct acpi_buffer *ret_buffer);

acpi_status
acpi_rs_get_prs_method_data(struct acpi_namespace_node *node,
			    struct acpi_buffer *ret_buffer);

acpi_status
acpi_rs_get_method_data(acpi_handle handle,
			const char *path, struct acpi_buffer *ret_buffer);

acpi_status
acpi_rs_set_srs_method_data(struct acpi_namespace_node *node,
			    struct acpi_buffer *ret_buffer);

acpi_status
acpi_rs_get_aei_method_data(struct acpi_namespace_node *node,
			    struct acpi_buffer *ret_buffer);

/*
 * rscalc
 */
acpi_status
acpi_rs_get_list_length(u8 *aml_buffer,
			u32 aml_buffer_length, acpi_size *size_needed);

acpi_status
acpi_rs_get_aml_length(struct acpi_resource *resource_list,
		       acpi_size resource_list_size, acpi_size *size_needed);

acpi_status
acpi_rs_get_pci_routing_table_length(union acpi_operand_object *package_object,
				     acpi_size *buffer_size_needed);

acpi_status
acpi_rs_convert_aml_to_resources(u8 * aml,
				 u32 length,
				 u32 offset, u8 resource_index, void **context);

acpi_status
acpi_rs_convert_resources_to_aml(struct acpi_resource *resource,
				 acpi_size aml_size_needed, u8 * output_buffer);

/*
 * rsaddr
 */
void
acpi_rs_set_address_common(union aml_resource *aml,
			   struct acpi_resource *resource);

u8
acpi_rs_get_address_common(struct acpi_resource *resource,
			   union aml_resource *aml);

/*
 * rsmisc
 */
acpi_status
acpi_rs_convert_aml_to_resource(struct acpi_resource *resource,
				union aml_resource *aml,
				struct acpi_rsconvert_info *info);

acpi_status
acpi_rs_convert_resource_to_aml(struct acpi_resource *resource,
				union aml_resource *aml,
				struct acpi_rsconvert_info *info);

/*
 * rsutils
 */
void
acpi_rs_move_data(void *destination,
		  void *source, u16 item_count, u8 move_type);

u8 acpi_rs_decode_bitmask(u16 mask, u8 * list);

u16 acpi_rs_encode_bitmask(u8 * list, u8 count);

acpi_rs_length
acpi_rs_get_resource_source(acpi_rs_length resource_length,
			    acpi_rs_length minimum_length,
			    struct acpi_resource_source *resource_source,
			    union aml_resource *aml, char *string_ptr);

acpi_rsdesc_size
acpi_rs_set_resource_source(union aml_resource *aml,
			    acpi_rs_length minimum_length,
			    struct acpi_resource_source *resource_source);

void
acpi_rs_set_resource_header(u8 descriptor_type,
			    acpi_rsdesc_size total_length,
			    union aml_resource *aml);

void
acpi_rs_set_resource_length(acpi_rsdesc_size total_length,
			    union aml_resource *aml);

/*
 * rsdump - Debugger support
 */
#ifdef ACPI_DEBUGGER
void acpi_rs_dump_resource_list(struct acpi_resource *resource);

void acpi_rs_dump_irq_list(u8 *route_table);
#endif

/*
 * Resource conversion tables
 */
extern struct acpi_rsconvert_info acpi_rs_convert_dma[];
extern struct acpi_rsconvert_info acpi_rs_convert_end_dpf[];
extern struct acpi_rsconvert_info acpi_rs_convert_io[];
extern struct acpi_rsconvert_info acpi_rs_convert_fixed_io[];
extern struct acpi_rsconvert_info acpi_rs_convert_end_tag[];
extern struct acpi_rsconvert_info acpi_rs_convert_memory24[];
extern struct acpi_rsconvert_info acpi_rs_convert_generic_reg[];
extern struct acpi_rsconvert_info acpi_rs_convert_memory32[];
extern struct acpi_rsconvert_info acpi_rs_convert_fixed_memory32[];
extern struct acpi_rsconvert_info acpi_rs_convert_address32[];
extern struct acpi_rsconvert_info acpi_rs_convert_address16[];
extern struct acpi_rsconvert_info acpi_rs_convert_ext_irq[];
extern struct acpi_rsconvert_info acpi_rs_convert_address64[];
extern struct acpi_rsconvert_info acpi_rs_convert_ext_address64[];
extern struct acpi_rsconvert_info acpi_rs_convert_gpio[];
extern struct acpi_rsconvert_info acpi_rs_convert_fixed_dma[];
extern struct acpi_rsconvert_info acpi_rs_convert_i2c_serial_bus[];
extern struct acpi_rsconvert_info acpi_rs_convert_spi_serial_bus[];
extern struct acpi_rsconvert_info acpi_rs_convert_uart_serial_bus[];
extern struct acpi_rsconvert_info acpi_rs_convert_pin_function[];
extern struct acpi_rsconvert_info acpi_rs_convert_pin_config[];
extern struct acpi_rsconvert_info acpi_rs_convert_pin_group[];
extern struct acpi_rsconvert_info acpi_rs_convert_pin_group_function[];
extern struct acpi_rsconvert_info acpi_rs_convert_pin_group_config[];

/* These resources require separate get/set tables */

extern struct acpi_rsconvert_info acpi_rs_get_irq[];
extern struct acpi_rsconvert_info acpi_rs_get_start_dpf[];
extern struct acpi_rsconvert_info acpi_rs_get_vendor_small[];
extern struct acpi_rsconvert_info acpi_rs_get_vendor_large[];

extern struct acpi_rsconvert_info acpi_rs_set_irq[];
extern struct acpi_rsconvert_info acpi_rs_set_start_dpf[];
extern struct acpi_rsconvert_info acpi_rs_set_vendor[];

#if defined(ACPI_DEBUG_OUTPUT) || defined(ACPI_DEBUGGER)
/*
 * rsinfo
 */
extern struct acpi_rsdump_info *acpi_gbl_dump_resource_dispatch[];
extern struct acpi_rsdump_info *acpi_gbl_dump_serial_bus_dispatch[];

/*
 * rsdumpinfo
 */
extern struct acpi_rsdump_info acpi_rs_dump_irq[];
extern struct acpi_rsdump_info acpi_rs_dump_prt[];
extern struct acpi_rsdump_info acpi_rs_dump_dma[];
extern struct acpi_rsdump_info acpi_rs_dump_start_dpf[];
extern struct acpi_rsdump_info acpi_rs_dump_end_dpf[];
extern struct acpi_rsdump_info acpi_rs_dump_io[];
extern struct acpi_rsdump_info acpi_rs_dump_io_flags[];
extern struct acpi_rsdump_info acpi_rs_dump_fixed_io[];
extern struct acpi_rsdump_info acpi_rs_dump_vendor[];
extern struct acpi_rsdump_info acpi_rs_dump_end_tag[];
extern struct acpi_rsdump_info acpi_rs_dump_memory24[];
extern struct acpi_rsdump_info acpi_rs_dump_memory32[];
extern struct acpi_rsdump_info acpi_rs_dump_memory_flags[];
extern struct acpi_rsdump_info acpi_rs_dump_fixed_memory32[];
extern struct acpi_rsdump_info acpi_rs_dump_address16[];
extern struct acpi_rsdump_info acpi_rs_dump_address32[];
extern struct acpi_rsdump_info acpi_rs_dump_address64[];
extern struct acpi_rsdump_info acpi_rs_dump_ext_address64[];
extern struct acpi_rsdump_info acpi_rs_dump_ext_irq[];
extern struct acpi_rsdump_info acpi_rs_dump_generic_reg[];
extern struct acpi_rsdump_info acpi_rs_dump_gpio[];
extern struct acpi_rsdump_info acpi_rs_dump_pin_function[];
extern struct acpi_rsdump_info acpi_rs_dump_fixed_dma[];
extern struct acpi_rsdump_info acpi_rs_dump_common_serial_bus[];
extern struct acpi_rsdump_info acpi_rs_dump_i2c_serial_bus[];
extern struct acpi_rsdump_info acpi_rs_dump_spi_serial_bus[];
extern struct acpi_rsdump_info acpi_rs_dump_uart_serial_bus[];
extern struct acpi_rsdump_info acpi_rs_dump_general_flags[];
extern struct acpi_rsdump_info acpi_rs_dump_pin_config[];
extern struct acpi_rsdump_info acpi_rs_dump_pin_group[];
extern struct acpi_rsdump_info acpi_rs_dump_pin_group_function[];
extern struct acpi_rsdump_info acpi_rs_dump_pin_group_config[];
#endif

#endif				/* __ACRESRC_H__ */
