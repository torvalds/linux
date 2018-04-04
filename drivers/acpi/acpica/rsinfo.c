/*******************************************************************************
 *
 * Module Name: rsinfo - Dispatch and Info tables
 *
 ******************************************************************************/

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

#include <acpi/acpi.h>
#include "accommon.h"
#include "acresrc.h"

#define _COMPONENT          ACPI_RESOURCES
ACPI_MODULE_NAME("rsinfo")

/*
 * Resource dispatch and information tables. Any new resource types (either
 * Large or Small) must be reflected in each of these tables, so they are here
 * in one place.
 *
 * The tables for Large descriptors are indexed by bits 6:0 of the AML
 * descriptor type byte. The tables for Small descriptors are indexed by
 * bits 6:3 of the descriptor byte. The tables for internal resource
 * descriptors are indexed by the acpi_resource_type field.
 */
/* Dispatch table for resource-to-AML (Set Resource) conversion functions */
struct acpi_rsconvert_info *acpi_gbl_set_resource_dispatch[] = {
	acpi_rs_set_irq,	/* 0x00, ACPI_RESOURCE_TYPE_IRQ */
	acpi_rs_convert_dma,	/* 0x01, ACPI_RESOURCE_TYPE_DMA */
	acpi_rs_set_start_dpf,	/* 0x02, ACPI_RESOURCE_TYPE_START_DEPENDENT */
	acpi_rs_convert_end_dpf,	/* 0x03, ACPI_RESOURCE_TYPE_END_DEPENDENT */
	acpi_rs_convert_io,	/* 0x04, ACPI_RESOURCE_TYPE_IO */
	acpi_rs_convert_fixed_io,	/* 0x05, ACPI_RESOURCE_TYPE_FIXED_IO */
	acpi_rs_set_vendor,	/* 0x06, ACPI_RESOURCE_TYPE_VENDOR */
	acpi_rs_convert_end_tag,	/* 0x07, ACPI_RESOURCE_TYPE_END_TAG */
	acpi_rs_convert_memory24,	/* 0x08, ACPI_RESOURCE_TYPE_MEMORY24 */
	acpi_rs_convert_memory32,	/* 0x09, ACPI_RESOURCE_TYPE_MEMORY32 */
	acpi_rs_convert_fixed_memory32,	/* 0x0A, ACPI_RESOURCE_TYPE_FIXED_MEMORY32 */
	acpi_rs_convert_address16,	/* 0x0B, ACPI_RESOURCE_TYPE_ADDRESS16 */
	acpi_rs_convert_address32,	/* 0x0C, ACPI_RESOURCE_TYPE_ADDRESS32 */
	acpi_rs_convert_address64,	/* 0x0D, ACPI_RESOURCE_TYPE_ADDRESS64 */
	acpi_rs_convert_ext_address64,	/* 0x0E, ACPI_RESOURCE_TYPE_EXTENDED_ADDRESS64 */
	acpi_rs_convert_ext_irq,	/* 0x0F, ACPI_RESOURCE_TYPE_EXTENDED_IRQ */
	acpi_rs_convert_generic_reg,	/* 0x10, ACPI_RESOURCE_TYPE_GENERIC_REGISTER */
	acpi_rs_convert_gpio,	/* 0x11, ACPI_RESOURCE_TYPE_GPIO */
	acpi_rs_convert_fixed_dma,	/* 0x12, ACPI_RESOURCE_TYPE_FIXED_DMA */
	NULL,			/* 0x13, ACPI_RESOURCE_TYPE_SERIAL_BUS - Use subtype table below */
	acpi_rs_convert_pin_function,	/* 0x14, ACPI_RESOURCE_TYPE_PIN_FUNCTION */
	acpi_rs_convert_pin_config,	/* 0x15, ACPI_RESOURCE_TYPE_PIN_CONFIG */
	acpi_rs_convert_pin_group,	/* 0x16, ACPI_RESOURCE_TYPE_PIN_GROUP */
	acpi_rs_convert_pin_group_function,	/* 0x17, ACPI_RESOURCE_TYPE_PIN_GROUP_FUNCTION */
	acpi_rs_convert_pin_group_config,	/* 0x18, ACPI_RESOURCE_TYPE_PIN_GROUP_CONFIG */
};

/* Dispatch tables for AML-to-resource (Get Resource) conversion functions */

struct acpi_rsconvert_info *acpi_gbl_get_resource_dispatch[] = {
	/* Small descriptors */

	NULL,			/* 0x00, Reserved */
	NULL,			/* 0x01, Reserved */
	NULL,			/* 0x02, Reserved */
	NULL,			/* 0x03, Reserved */
	acpi_rs_get_irq,	/* 0x04, ACPI_RESOURCE_NAME_IRQ */
	acpi_rs_convert_dma,	/* 0x05, ACPI_RESOURCE_NAME_DMA */
	acpi_rs_get_start_dpf,	/* 0x06, ACPI_RESOURCE_NAME_START_DEPENDENT */
	acpi_rs_convert_end_dpf,	/* 0x07, ACPI_RESOURCE_NAME_END_DEPENDENT */
	acpi_rs_convert_io,	/* 0x08, ACPI_RESOURCE_NAME_IO */
	acpi_rs_convert_fixed_io,	/* 0x09, ACPI_RESOURCE_NAME_FIXED_IO */
	acpi_rs_convert_fixed_dma,	/* 0x0A, ACPI_RESOURCE_NAME_FIXED_DMA */
	NULL,			/* 0x0B, Reserved */
	NULL,			/* 0x0C, Reserved */
	NULL,			/* 0x0D, Reserved */
	acpi_rs_get_vendor_small,	/* 0x0E, ACPI_RESOURCE_NAME_VENDOR_SMALL */
	acpi_rs_convert_end_tag,	/* 0x0F, ACPI_RESOURCE_NAME_END_TAG */

	/* Large descriptors */

	NULL,			/* 0x00, Reserved */
	acpi_rs_convert_memory24,	/* 0x01, ACPI_RESOURCE_NAME_MEMORY24 */
	acpi_rs_convert_generic_reg,	/* 0x02, ACPI_RESOURCE_NAME_GENERIC_REGISTER */
	NULL,			/* 0x03, Reserved */
	acpi_rs_get_vendor_large,	/* 0x04, ACPI_RESOURCE_NAME_VENDOR_LARGE */
	acpi_rs_convert_memory32,	/* 0x05, ACPI_RESOURCE_NAME_MEMORY32 */
	acpi_rs_convert_fixed_memory32,	/* 0x06, ACPI_RESOURCE_NAME_FIXED_MEMORY32 */
	acpi_rs_convert_address32,	/* 0x07, ACPI_RESOURCE_NAME_ADDRESS32 */
	acpi_rs_convert_address16,	/* 0x08, ACPI_RESOURCE_NAME_ADDRESS16 */
	acpi_rs_convert_ext_irq,	/* 0x09, ACPI_RESOURCE_NAME_EXTENDED_IRQ */
	acpi_rs_convert_address64,	/* 0x0A, ACPI_RESOURCE_NAME_ADDRESS64 */
	acpi_rs_convert_ext_address64,	/* 0x0B, ACPI_RESOURCE_NAME_EXTENDED_ADDRESS64 */
	acpi_rs_convert_gpio,	/* 0x0C, ACPI_RESOURCE_NAME_GPIO */
	acpi_rs_convert_pin_function,	/* 0x0D, ACPI_RESOURCE_NAME_PIN_FUNCTION */
	NULL,			/* 0x0E, ACPI_RESOURCE_NAME_SERIAL_BUS - Use subtype table below */
	acpi_rs_convert_pin_config,	/* 0x0F, ACPI_RESOURCE_NAME_PIN_CONFIG */
	acpi_rs_convert_pin_group,	/* 0x10, ACPI_RESOURCE_NAME_PIN_GROUP */
	acpi_rs_convert_pin_group_function,	/* 0x11, ACPI_RESOURCE_NAME_PIN_GROUP_FUNCTION */
	acpi_rs_convert_pin_group_config,	/* 0x12, ACPI_RESOURCE_NAME_PIN_GROUP_CONFIG */
};

/* Subtype table for serial_bus -- I2C, SPI, and UART */

struct acpi_rsconvert_info *acpi_gbl_convert_resource_serial_bus_dispatch[] = {
	NULL,
	acpi_rs_convert_i2c_serial_bus,
	acpi_rs_convert_spi_serial_bus,
	acpi_rs_convert_uart_serial_bus,
};

#if defined(ACPI_DEBUG_OUTPUT) || defined(ACPI_DISASSEMBLER) || defined(ACPI_DEBUGGER)

/* Dispatch table for resource dump functions */

struct acpi_rsdump_info *acpi_gbl_dump_resource_dispatch[] = {
	acpi_rs_dump_irq,	/* ACPI_RESOURCE_TYPE_IRQ */
	acpi_rs_dump_dma,	/* ACPI_RESOURCE_TYPE_DMA */
	acpi_rs_dump_start_dpf,	/* ACPI_RESOURCE_TYPE_START_DEPENDENT */
	acpi_rs_dump_end_dpf,	/* ACPI_RESOURCE_TYPE_END_DEPENDENT */
	acpi_rs_dump_io,	/* ACPI_RESOURCE_TYPE_IO */
	acpi_rs_dump_fixed_io,	/* ACPI_RESOURCE_TYPE_FIXED_IO */
	acpi_rs_dump_vendor,	/* ACPI_RESOURCE_TYPE_VENDOR */
	acpi_rs_dump_end_tag,	/* ACPI_RESOURCE_TYPE_END_TAG */
	acpi_rs_dump_memory24,	/* ACPI_RESOURCE_TYPE_MEMORY24 */
	acpi_rs_dump_memory32,	/* ACPI_RESOURCE_TYPE_MEMORY32 */
	acpi_rs_dump_fixed_memory32,	/* ACPI_RESOURCE_TYPE_FIXED_MEMORY32 */
	acpi_rs_dump_address16,	/* ACPI_RESOURCE_TYPE_ADDRESS16 */
	acpi_rs_dump_address32,	/* ACPI_RESOURCE_TYPE_ADDRESS32 */
	acpi_rs_dump_address64,	/* ACPI_RESOURCE_TYPE_ADDRESS64 */
	acpi_rs_dump_ext_address64,	/* ACPI_RESOURCE_TYPE_EXTENDED_ADDRESS64 */
	acpi_rs_dump_ext_irq,	/* ACPI_RESOURCE_TYPE_EXTENDED_IRQ */
	acpi_rs_dump_generic_reg,	/* ACPI_RESOURCE_TYPE_GENERIC_REGISTER */
	acpi_rs_dump_gpio,	/* ACPI_RESOURCE_TYPE_GPIO */
	acpi_rs_dump_fixed_dma,	/* ACPI_RESOURCE_TYPE_FIXED_DMA */
	NULL,			/* ACPI_RESOURCE_TYPE_SERIAL_BUS */
	acpi_rs_dump_pin_function,	/* ACPI_RESOURCE_TYPE_PIN_FUNCTION */
	acpi_rs_dump_pin_config,	/* ACPI_RESOURCE_TYPE_PIN_CONFIG */
	acpi_rs_dump_pin_group,	/* ACPI_RESOURCE_TYPE_PIN_GROUP */
	acpi_rs_dump_pin_group_function,	/* ACPI_RESOURCE_TYPE_PIN_GROUP_FUNCTION */
	acpi_rs_dump_pin_group_config,	/* ACPI_RESOURCE_TYPE_PIN_GROUP_CONFIG */
};

struct acpi_rsdump_info *acpi_gbl_dump_serial_bus_dispatch[] = {
	NULL,
	acpi_rs_dump_i2c_serial_bus,	/* AML_RESOURCE_I2C_BUS_TYPE */
	acpi_rs_dump_spi_serial_bus,	/* AML_RESOURCE_SPI_BUS_TYPE */
	acpi_rs_dump_uart_serial_bus,	/* AML_RESOURCE_UART_BUS_TYPE */
};
#endif

/*
 * Base sizes for external AML resource descriptors, indexed by internal type.
 * Includes size of the descriptor header (1 byte for small descriptors,
 * 3 bytes for large descriptors)
 */
const u8 acpi_gbl_aml_resource_sizes[] = {
	sizeof(struct aml_resource_irq),	/* ACPI_RESOURCE_TYPE_IRQ (optional Byte 3 always created) */
	sizeof(struct aml_resource_dma),	/* ACPI_RESOURCE_TYPE_DMA */
	sizeof(struct aml_resource_start_dependent),	/* ACPI_RESOURCE_TYPE_START_DEPENDENT (optional Byte 1 always created) */
	sizeof(struct aml_resource_end_dependent),	/* ACPI_RESOURCE_TYPE_END_DEPENDENT */
	sizeof(struct aml_resource_io),	/* ACPI_RESOURCE_TYPE_IO */
	sizeof(struct aml_resource_fixed_io),	/* ACPI_RESOURCE_TYPE_FIXED_IO */
	sizeof(struct aml_resource_vendor_small),	/* ACPI_RESOURCE_TYPE_VENDOR */
	sizeof(struct aml_resource_end_tag),	/* ACPI_RESOURCE_TYPE_END_TAG */
	sizeof(struct aml_resource_memory24),	/* ACPI_RESOURCE_TYPE_MEMORY24 */
	sizeof(struct aml_resource_memory32),	/* ACPI_RESOURCE_TYPE_MEMORY32 */
	sizeof(struct aml_resource_fixed_memory32),	/* ACPI_RESOURCE_TYPE_FIXED_MEMORY32 */
	sizeof(struct aml_resource_address16),	/* ACPI_RESOURCE_TYPE_ADDRESS16 */
	sizeof(struct aml_resource_address32),	/* ACPI_RESOURCE_TYPE_ADDRESS32 */
	sizeof(struct aml_resource_address64),	/* ACPI_RESOURCE_TYPE_ADDRESS64 */
	sizeof(struct aml_resource_extended_address64),	/*ACPI_RESOURCE_TYPE_EXTENDED_ADDRESS64 */
	sizeof(struct aml_resource_extended_irq),	/* ACPI_RESOURCE_TYPE_EXTENDED_IRQ */
	sizeof(struct aml_resource_generic_register),	/* ACPI_RESOURCE_TYPE_GENERIC_REGISTER */
	sizeof(struct aml_resource_gpio),	/* ACPI_RESOURCE_TYPE_GPIO */
	sizeof(struct aml_resource_fixed_dma),	/* ACPI_RESOURCE_TYPE_FIXED_DMA */
	sizeof(struct aml_resource_common_serialbus),	/* ACPI_RESOURCE_TYPE_SERIAL_BUS */
	sizeof(struct aml_resource_pin_function),	/* ACPI_RESOURCE_TYPE_PIN_FUNCTION */
	sizeof(struct aml_resource_pin_config),	/* ACPI_RESOURCE_TYPE_PIN_CONFIG */
	sizeof(struct aml_resource_pin_group),	/* ACPI_RESOURCE_TYPE_PIN_GROUP */
	sizeof(struct aml_resource_pin_group_function),	/* ACPI_RESOURCE_TYPE_PIN_GROUP_FUNCTION */
	sizeof(struct aml_resource_pin_group_config),	/* ACPI_RESOURCE_TYPE_PIN_GROUP_CONFIG */
};

const u8 acpi_gbl_resource_struct_sizes[] = {
	/* Small descriptors */

	0,
	0,
	0,
	0,
	ACPI_RS_SIZE(struct acpi_resource_irq),
	ACPI_RS_SIZE(struct acpi_resource_dma),
	ACPI_RS_SIZE(struct acpi_resource_start_dependent),
	ACPI_RS_SIZE_MIN,
	ACPI_RS_SIZE(struct acpi_resource_io),
	ACPI_RS_SIZE(struct acpi_resource_fixed_io),
	ACPI_RS_SIZE(struct acpi_resource_fixed_dma),
	0,
	0,
	0,
	ACPI_RS_SIZE(struct acpi_resource_vendor),
	ACPI_RS_SIZE_MIN,

	/* Large descriptors */

	0,
	ACPI_RS_SIZE(struct acpi_resource_memory24),
	ACPI_RS_SIZE(struct acpi_resource_generic_register),
	0,
	ACPI_RS_SIZE(struct acpi_resource_vendor),
	ACPI_RS_SIZE(struct acpi_resource_memory32),
	ACPI_RS_SIZE(struct acpi_resource_fixed_memory32),
	ACPI_RS_SIZE(struct acpi_resource_address32),
	ACPI_RS_SIZE(struct acpi_resource_address16),
	ACPI_RS_SIZE(struct acpi_resource_extended_irq),
	ACPI_RS_SIZE(struct acpi_resource_address64),
	ACPI_RS_SIZE(struct acpi_resource_extended_address64),
	ACPI_RS_SIZE(struct acpi_resource_gpio),
	ACPI_RS_SIZE(struct acpi_resource_pin_function),
	ACPI_RS_SIZE(struct acpi_resource_common_serialbus),
	ACPI_RS_SIZE(struct acpi_resource_pin_config),
	ACPI_RS_SIZE(struct acpi_resource_pin_group),
	ACPI_RS_SIZE(struct acpi_resource_pin_group_function),
	ACPI_RS_SIZE(struct acpi_resource_pin_group_config),
};

const u8 acpi_gbl_aml_resource_serial_bus_sizes[] = {
	0,
	sizeof(struct aml_resource_i2c_serialbus),
	sizeof(struct aml_resource_spi_serialbus),
	sizeof(struct aml_resource_uart_serialbus),
};

const u8 acpi_gbl_resource_struct_serial_bus_sizes[] = {
	0,
	ACPI_RS_SIZE(struct acpi_resource_i2c_serialbus),
	ACPI_RS_SIZE(struct acpi_resource_spi_serialbus),
	ACPI_RS_SIZE(struct acpi_resource_uart_serialbus),
};
