/*******************************************************************************
 *
 * Module Name: rsserial - GPIO/serial_bus resource descriptors
 *
 ******************************************************************************/

/*
 * Copyright (C) 2000 - 2016, Intel Corp.
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
ACPI_MODULE_NAME("rsserial")

/*******************************************************************************
 *
 * acpi_rs_convert_gpio
 *
 ******************************************************************************/
struct acpi_rsconvert_info acpi_rs_convert_gpio[18] = {
	{ACPI_RSC_INITGET, ACPI_RESOURCE_TYPE_GPIO,
	 ACPI_RS_SIZE(struct acpi_resource_gpio),
	 ACPI_RSC_TABLE_SIZE(acpi_rs_convert_gpio)},

	{ACPI_RSC_INITSET, ACPI_RESOURCE_NAME_GPIO,
	 sizeof(struct aml_resource_gpio),
	 0},

	/*
	 * These fields are contiguous in both the source and destination:
	 * revision_id
	 * connection_type
	 */
	{ACPI_RSC_MOVE8, ACPI_RS_OFFSET(data.gpio.revision_id),
	 AML_OFFSET(gpio.revision_id),
	 2},

	{ACPI_RSC_1BITFLAG, ACPI_RS_OFFSET(data.gpio.producer_consumer),
	 AML_OFFSET(gpio.flags),
	 0},

	{ACPI_RSC_1BITFLAG, ACPI_RS_OFFSET(data.gpio.sharable),
	 AML_OFFSET(gpio.int_flags),
	 3},

	{ACPI_RSC_1BITFLAG, ACPI_RS_OFFSET(data.gpio.wake_capable),
	 AML_OFFSET(gpio.int_flags),
	 4},

	{ACPI_RSC_2BITFLAG, ACPI_RS_OFFSET(data.gpio.io_restriction),
	 AML_OFFSET(gpio.int_flags),
	 0},

	{ACPI_RSC_1BITFLAG, ACPI_RS_OFFSET(data.gpio.triggering),
	 AML_OFFSET(gpio.int_flags),
	 0},

	{ACPI_RSC_2BITFLAG, ACPI_RS_OFFSET(data.gpio.polarity),
	 AML_OFFSET(gpio.int_flags),
	 1},

	{ACPI_RSC_MOVE8, ACPI_RS_OFFSET(data.gpio.pin_config),
	 AML_OFFSET(gpio.pin_config),
	 1},

	/*
	 * These fields are contiguous in both the source and destination:
	 * drive_strength
	 * debounce_timeout
	 */
	{ACPI_RSC_MOVE16, ACPI_RS_OFFSET(data.gpio.drive_strength),
	 AML_OFFSET(gpio.drive_strength),
	 2},

	/* Pin Table */

	{ACPI_RSC_COUNT_GPIO_PIN, ACPI_RS_OFFSET(data.gpio.pin_table_length),
	 AML_OFFSET(gpio.pin_table_offset),
	 AML_OFFSET(gpio.res_source_offset)},

	{ACPI_RSC_MOVE_GPIO_PIN, ACPI_RS_OFFSET(data.gpio.pin_table),
	 AML_OFFSET(gpio.pin_table_offset),
	 0},

	/* Resource Source */

	{ACPI_RSC_MOVE8, ACPI_RS_OFFSET(data.gpio.resource_source.index),
	 AML_OFFSET(gpio.res_source_index),
	 1},

	{ACPI_RSC_COUNT_GPIO_RES,
	 ACPI_RS_OFFSET(data.gpio.resource_source.string_length),
	 AML_OFFSET(gpio.res_source_offset),
	 AML_OFFSET(gpio.vendor_offset)},

	{ACPI_RSC_MOVE_GPIO_RES,
	 ACPI_RS_OFFSET(data.gpio.resource_source.string_ptr),
	 AML_OFFSET(gpio.res_source_offset),
	 0},

	/* Vendor Data */

	{ACPI_RSC_COUNT_GPIO_VEN, ACPI_RS_OFFSET(data.gpio.vendor_length),
	 AML_OFFSET(gpio.vendor_length),
	 1},

	{ACPI_RSC_MOVE_GPIO_RES, ACPI_RS_OFFSET(data.gpio.vendor_data),
	 AML_OFFSET(gpio.vendor_offset),
	 0},
};

/*******************************************************************************
 *
 * acpi_rs_convert_i2c_serial_bus
 *
 ******************************************************************************/

struct acpi_rsconvert_info acpi_rs_convert_i2c_serial_bus[17] = {
	{ACPI_RSC_INITGET, ACPI_RESOURCE_TYPE_SERIAL_BUS,
	 ACPI_RS_SIZE(struct acpi_resource_i2c_serialbus),
	 ACPI_RSC_TABLE_SIZE(acpi_rs_convert_i2c_serial_bus)},

	{ACPI_RSC_INITSET, ACPI_RESOURCE_NAME_SERIAL_BUS,
	 sizeof(struct aml_resource_i2c_serialbus),
	 0},

	{ACPI_RSC_MOVE8, ACPI_RS_OFFSET(data.common_serial_bus.revision_id),
	 AML_OFFSET(common_serial_bus.revision_id),
	 1},

	{ACPI_RSC_MOVE8, ACPI_RS_OFFSET(data.common_serial_bus.type),
	 AML_OFFSET(common_serial_bus.type),
	 1},

	{ACPI_RSC_1BITFLAG, ACPI_RS_OFFSET(data.common_serial_bus.slave_mode),
	 AML_OFFSET(common_serial_bus.flags),
	 0},

	{ACPI_RSC_1BITFLAG,
	 ACPI_RS_OFFSET(data.common_serial_bus.producer_consumer),
	 AML_OFFSET(common_serial_bus.flags),
	 1},

	{ACPI_RSC_1BITFLAG,
	 ACPI_RS_OFFSET(data.common_serial_bus.connection_sharing),
	 AML_OFFSET(common_serial_bus.flags),
	 2},

	{ACPI_RSC_MOVE8,
	 ACPI_RS_OFFSET(data.common_serial_bus.type_revision_id),
	 AML_OFFSET(common_serial_bus.type_revision_id),
	 1},

	{ACPI_RSC_MOVE16,
	 ACPI_RS_OFFSET(data.common_serial_bus.type_data_length),
	 AML_OFFSET(common_serial_bus.type_data_length),
	 1},

	/* Vendor data */

	{ACPI_RSC_COUNT_SERIAL_VEN,
	 ACPI_RS_OFFSET(data.common_serial_bus.vendor_length),
	 AML_OFFSET(common_serial_bus.type_data_length),
	 AML_RESOURCE_I2C_MIN_DATA_LEN},

	{ACPI_RSC_MOVE_SERIAL_VEN,
	 ACPI_RS_OFFSET(data.common_serial_bus.vendor_data),
	 0,
	 sizeof(struct aml_resource_i2c_serialbus)},

	/* Resource Source */

	{ACPI_RSC_MOVE8,
	 ACPI_RS_OFFSET(data.common_serial_bus.resource_source.index),
	 AML_OFFSET(common_serial_bus.res_source_index),
	 1},

	{ACPI_RSC_COUNT_SERIAL_RES,
	 ACPI_RS_OFFSET(data.common_serial_bus.resource_source.string_length),
	 AML_OFFSET(common_serial_bus.type_data_length),
	 sizeof(struct aml_resource_common_serialbus)},

	{ACPI_RSC_MOVE_SERIAL_RES,
	 ACPI_RS_OFFSET(data.common_serial_bus.resource_source.string_ptr),
	 AML_OFFSET(common_serial_bus.type_data_length),
	 sizeof(struct aml_resource_common_serialbus)},

	/* I2C bus type specific */

	{ACPI_RSC_1BITFLAG, ACPI_RS_OFFSET(data.i2c_serial_bus.access_mode),
	 AML_OFFSET(i2c_serial_bus.type_specific_flags),
	 0},

	{ACPI_RSC_MOVE32, ACPI_RS_OFFSET(data.i2c_serial_bus.connection_speed),
	 AML_OFFSET(i2c_serial_bus.connection_speed),
	 1},

	{ACPI_RSC_MOVE16, ACPI_RS_OFFSET(data.i2c_serial_bus.slave_address),
	 AML_OFFSET(i2c_serial_bus.slave_address),
	 1},
};

/*******************************************************************************
 *
 * acpi_rs_convert_spi_serial_bus
 *
 ******************************************************************************/

struct acpi_rsconvert_info acpi_rs_convert_spi_serial_bus[21] = {
	{ACPI_RSC_INITGET, ACPI_RESOURCE_TYPE_SERIAL_BUS,
	 ACPI_RS_SIZE(struct acpi_resource_spi_serialbus),
	 ACPI_RSC_TABLE_SIZE(acpi_rs_convert_spi_serial_bus)},

	{ACPI_RSC_INITSET, ACPI_RESOURCE_NAME_SERIAL_BUS,
	 sizeof(struct aml_resource_spi_serialbus),
	 0},

	{ACPI_RSC_MOVE8, ACPI_RS_OFFSET(data.common_serial_bus.revision_id),
	 AML_OFFSET(common_serial_bus.revision_id),
	 1},

	{ACPI_RSC_MOVE8, ACPI_RS_OFFSET(data.common_serial_bus.type),
	 AML_OFFSET(common_serial_bus.type),
	 1},

	{ACPI_RSC_1BITFLAG, ACPI_RS_OFFSET(data.common_serial_bus.slave_mode),
	 AML_OFFSET(common_serial_bus.flags),
	 0},

	{ACPI_RSC_1BITFLAG,
	 ACPI_RS_OFFSET(data.common_serial_bus.producer_consumer),
	 AML_OFFSET(common_serial_bus.flags),
	 1},

	{ACPI_RSC_1BITFLAG,
	 ACPI_RS_OFFSET(data.common_serial_bus.connection_sharing),
	 AML_OFFSET(common_serial_bus.flags),
	 2},

	{ACPI_RSC_MOVE8,
	 ACPI_RS_OFFSET(data.common_serial_bus.type_revision_id),
	 AML_OFFSET(common_serial_bus.type_revision_id),
	 1},

	{ACPI_RSC_MOVE16,
	 ACPI_RS_OFFSET(data.common_serial_bus.type_data_length),
	 AML_OFFSET(common_serial_bus.type_data_length),
	 1},

	/* Vendor data */

	{ACPI_RSC_COUNT_SERIAL_VEN,
	 ACPI_RS_OFFSET(data.common_serial_bus.vendor_length),
	 AML_OFFSET(common_serial_bus.type_data_length),
	 AML_RESOURCE_SPI_MIN_DATA_LEN},

	{ACPI_RSC_MOVE_SERIAL_VEN,
	 ACPI_RS_OFFSET(data.common_serial_bus.vendor_data),
	 0,
	 sizeof(struct aml_resource_spi_serialbus)},

	/* Resource Source */

	{ACPI_RSC_MOVE8,
	 ACPI_RS_OFFSET(data.common_serial_bus.resource_source.index),
	 AML_OFFSET(common_serial_bus.res_source_index),
	 1},

	{ACPI_RSC_COUNT_SERIAL_RES,
	 ACPI_RS_OFFSET(data.common_serial_bus.resource_source.string_length),
	 AML_OFFSET(common_serial_bus.type_data_length),
	 sizeof(struct aml_resource_common_serialbus)},

	{ACPI_RSC_MOVE_SERIAL_RES,
	 ACPI_RS_OFFSET(data.common_serial_bus.resource_source.string_ptr),
	 AML_OFFSET(common_serial_bus.type_data_length),
	 sizeof(struct aml_resource_common_serialbus)},

	/* Spi bus type specific  */

	{ACPI_RSC_1BITFLAG, ACPI_RS_OFFSET(data.spi_serial_bus.wire_mode),
	 AML_OFFSET(spi_serial_bus.type_specific_flags),
	 0},

	{ACPI_RSC_1BITFLAG, ACPI_RS_OFFSET(data.spi_serial_bus.device_polarity),
	 AML_OFFSET(spi_serial_bus.type_specific_flags),
	 1},

	{ACPI_RSC_MOVE8, ACPI_RS_OFFSET(data.spi_serial_bus.data_bit_length),
	 AML_OFFSET(spi_serial_bus.data_bit_length),
	 1},

	{ACPI_RSC_MOVE8, ACPI_RS_OFFSET(data.spi_serial_bus.clock_phase),
	 AML_OFFSET(spi_serial_bus.clock_phase),
	 1},

	{ACPI_RSC_MOVE8, ACPI_RS_OFFSET(data.spi_serial_bus.clock_polarity),
	 AML_OFFSET(spi_serial_bus.clock_polarity),
	 1},

	{ACPI_RSC_MOVE16, ACPI_RS_OFFSET(data.spi_serial_bus.device_selection),
	 AML_OFFSET(spi_serial_bus.device_selection),
	 1},

	{ACPI_RSC_MOVE32, ACPI_RS_OFFSET(data.spi_serial_bus.connection_speed),
	 AML_OFFSET(spi_serial_bus.connection_speed),
	 1},
};

/*******************************************************************************
 *
 * acpi_rs_convert_uart_serial_bus
 *
 ******************************************************************************/

struct acpi_rsconvert_info acpi_rs_convert_uart_serial_bus[23] = {
	{ACPI_RSC_INITGET, ACPI_RESOURCE_TYPE_SERIAL_BUS,
	 ACPI_RS_SIZE(struct acpi_resource_uart_serialbus),
	 ACPI_RSC_TABLE_SIZE(acpi_rs_convert_uart_serial_bus)},

	{ACPI_RSC_INITSET, ACPI_RESOURCE_NAME_SERIAL_BUS,
	 sizeof(struct aml_resource_uart_serialbus),
	 0},

	{ACPI_RSC_MOVE8, ACPI_RS_OFFSET(data.common_serial_bus.revision_id),
	 AML_OFFSET(common_serial_bus.revision_id),
	 1},

	{ACPI_RSC_MOVE8, ACPI_RS_OFFSET(data.common_serial_bus.type),
	 AML_OFFSET(common_serial_bus.type),
	 1},

	{ACPI_RSC_1BITFLAG, ACPI_RS_OFFSET(data.common_serial_bus.slave_mode),
	 AML_OFFSET(common_serial_bus.flags),
	 0},

	{ACPI_RSC_1BITFLAG,
	 ACPI_RS_OFFSET(data.common_serial_bus.producer_consumer),
	 AML_OFFSET(common_serial_bus.flags),
	 1},

	{ACPI_RSC_1BITFLAG,
	 ACPI_RS_OFFSET(data.common_serial_bus.connection_sharing),
	 AML_OFFSET(common_serial_bus.flags),
	 2},

	{ACPI_RSC_MOVE8,
	 ACPI_RS_OFFSET(data.common_serial_bus.type_revision_id),
	 AML_OFFSET(common_serial_bus.type_revision_id),
	 1},

	{ACPI_RSC_MOVE16,
	 ACPI_RS_OFFSET(data.common_serial_bus.type_data_length),
	 AML_OFFSET(common_serial_bus.type_data_length),
	 1},

	/* Vendor data */

	{ACPI_RSC_COUNT_SERIAL_VEN,
	 ACPI_RS_OFFSET(data.common_serial_bus.vendor_length),
	 AML_OFFSET(common_serial_bus.type_data_length),
	 AML_RESOURCE_UART_MIN_DATA_LEN},

	{ACPI_RSC_MOVE_SERIAL_VEN,
	 ACPI_RS_OFFSET(data.common_serial_bus.vendor_data),
	 0,
	 sizeof(struct aml_resource_uart_serialbus)},

	/* Resource Source */

	{ACPI_RSC_MOVE8,
	 ACPI_RS_OFFSET(data.common_serial_bus.resource_source.index),
	 AML_OFFSET(common_serial_bus.res_source_index),
	 1},

	{ACPI_RSC_COUNT_SERIAL_RES,
	 ACPI_RS_OFFSET(data.common_serial_bus.resource_source.string_length),
	 AML_OFFSET(common_serial_bus.type_data_length),
	 sizeof(struct aml_resource_common_serialbus)},

	{ACPI_RSC_MOVE_SERIAL_RES,
	 ACPI_RS_OFFSET(data.common_serial_bus.resource_source.string_ptr),
	 AML_OFFSET(common_serial_bus.type_data_length),
	 sizeof(struct aml_resource_common_serialbus)},

	/* Uart bus type specific  */

	{ACPI_RSC_2BITFLAG, ACPI_RS_OFFSET(data.uart_serial_bus.flow_control),
	 AML_OFFSET(uart_serial_bus.type_specific_flags),
	 0},

	{ACPI_RSC_2BITFLAG, ACPI_RS_OFFSET(data.uart_serial_bus.stop_bits),
	 AML_OFFSET(uart_serial_bus.type_specific_flags),
	 2},

	{ACPI_RSC_3BITFLAG, ACPI_RS_OFFSET(data.uart_serial_bus.data_bits),
	 AML_OFFSET(uart_serial_bus.type_specific_flags),
	 4},

	{ACPI_RSC_1BITFLAG, ACPI_RS_OFFSET(data.uart_serial_bus.endian),
	 AML_OFFSET(uart_serial_bus.type_specific_flags),
	 7},

	{ACPI_RSC_MOVE8, ACPI_RS_OFFSET(data.uart_serial_bus.parity),
	 AML_OFFSET(uart_serial_bus.parity),
	 1},

	{ACPI_RSC_MOVE8, ACPI_RS_OFFSET(data.uart_serial_bus.lines_enabled),
	 AML_OFFSET(uart_serial_bus.lines_enabled),
	 1},

	{ACPI_RSC_MOVE16, ACPI_RS_OFFSET(data.uart_serial_bus.rx_fifo_size),
	 AML_OFFSET(uart_serial_bus.rx_fifo_size),
	 1},

	{ACPI_RSC_MOVE16, ACPI_RS_OFFSET(data.uart_serial_bus.tx_fifo_size),
	 AML_OFFSET(uart_serial_bus.tx_fifo_size),
	 1},

	{ACPI_RSC_MOVE32,
	 ACPI_RS_OFFSET(data.uart_serial_bus.default_baud_rate),
	 AML_OFFSET(uart_serial_bus.default_baud_rate),
	 1},
};
