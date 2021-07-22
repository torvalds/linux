// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/*******************************************************************************
 *
 * Module Name: dbtest - Various debug-related tests
 *
 ******************************************************************************/

#include <acpi/acpi.h>
#include "accommon.h"
#include "acdebug.h"
#include "acnamesp.h"
#include "acpredef.h"
#include "acinterp.h"

#define _COMPONENT          ACPI_CA_DEBUGGER
ACPI_MODULE_NAME("dbtest")

/* Local prototypes */
static void acpi_db_test_all_objects(void);

static acpi_status
acpi_db_test_one_object(acpi_handle obj_handle,
			u32 nesting_level, void *context, void **return_value);

static acpi_status
acpi_db_test_integer_type(struct acpi_namespace_node *node, u32 bit_length);

static acpi_status
acpi_db_test_buffer_type(struct acpi_namespace_node *node, u32 bit_length);

static acpi_status
acpi_db_test_string_type(struct acpi_namespace_node *node, u32 byte_length);

static acpi_status acpi_db_test_package_type(struct acpi_namespace_node *node);

static acpi_status
acpi_db_test_field_unit_type(union acpi_operand_object *obj_desc);

static acpi_status
acpi_db_read_from_object(struct acpi_namespace_node *node,
			 acpi_object_type expected_type,
			 union acpi_object **value);

static acpi_status
acpi_db_write_to_object(struct acpi_namespace_node *node,
			union acpi_object *value);

static void acpi_db_evaluate_all_predefined_names(char *count_arg);

static acpi_status
acpi_db_evaluate_one_predefined_name(acpi_handle obj_handle,
				     u32 nesting_level,
				     void *context, void **return_value);

/*
 * Test subcommands
 */
static struct acpi_db_argument_info acpi_db_test_types[] = {
	{"OBJECTS"},
	{"PREDEFINED"},
	{NULL}			/* Must be null terminated */
};

#define CMD_TEST_OBJECTS        0
#define CMD_TEST_PREDEFINED     1

#define BUFFER_FILL_VALUE       0xFF

/*
 * Support for the special debugger read/write control methods.
 * These methods are installed into the current namespace and are
 * used to read and write the various namespace objects. The point
 * is to force the AML interpreter do all of the work.
 */
#define ACPI_DB_READ_METHOD     "\\_T98"
#define ACPI_DB_WRITE_METHOD    "\\_T99"

static acpi_handle read_handle = NULL;
static acpi_handle write_handle = NULL;

/* ASL Definitions of the debugger read/write control methods. AML below. */

#if 0
definition_block("ssdt.aml", "SSDT", 2, "Intel", "DEBUG", 0x00000001)
{
	method(_T98, 1, not_serialized) {	/* Read */
		return (de_ref_of(arg0))
	}
}

definition_block("ssdt2.aml", "SSDT", 2, "Intel", "DEBUG", 0x00000001)
{
	method(_T99, 2, not_serialized) {	/* Write */
		store(arg1, arg0)
	}
}
#endif

static unsigned char read_method_code[] = {
	0x53, 0x53, 0x44, 0x54, 0x2E, 0x00, 0x00, 0x00,	/* 00000000    "SSDT...." */
	0x02, 0xC9, 0x49, 0x6E, 0x74, 0x65, 0x6C, 0x00,	/* 00000008    "..Intel." */
	0x44, 0x45, 0x42, 0x55, 0x47, 0x00, 0x00, 0x00,	/* 00000010    "DEBUG..." */
	0x01, 0x00, 0x00, 0x00, 0x49, 0x4E, 0x54, 0x4C,	/* 00000018    "....INTL" */
	0x18, 0x12, 0x13, 0x20, 0x14, 0x09, 0x5F, 0x54,	/* 00000020    "... .._T" */
	0x39, 0x38, 0x01, 0xA4, 0x83, 0x68	/* 00000028    "98...h"   */
};

static unsigned char write_method_code[] = {
	0x53, 0x53, 0x44, 0x54, 0x2E, 0x00, 0x00, 0x00,	/* 00000000    "SSDT...." */
	0x02, 0x15, 0x49, 0x6E, 0x74, 0x65, 0x6C, 0x00,	/* 00000008    "..Intel." */
	0x44, 0x45, 0x42, 0x55, 0x47, 0x00, 0x00, 0x00,	/* 00000010    "DEBUG..." */
	0x01, 0x00, 0x00, 0x00, 0x49, 0x4E, 0x54, 0x4C,	/* 00000018    "....INTL" */
	0x18, 0x12, 0x13, 0x20, 0x14, 0x09, 0x5F, 0x54,	/* 00000020    "... .._T" */
	0x39, 0x39, 0x02, 0x70, 0x69, 0x68	/* 00000028    "99.pih"   */
};

/*******************************************************************************
 *
 * FUNCTION:    acpi_db_execute_test
 *
 * PARAMETERS:  type_arg        - Subcommand
 *
 * RETURN:      None
 *
 * DESCRIPTION: Execute various debug tests.
 *
 * Note: Code is prepared for future expansion of the TEST command.
 *
 ******************************************************************************/

void acpi_db_execute_test(char *type_arg)
{
	u32 temp;

	acpi_ut_strupr(type_arg);
	temp = acpi_db_match_argument(type_arg, acpi_db_test_types);
	if (temp == ACPI_TYPE_NOT_FOUND) {
		acpi_os_printf("Invalid or unsupported argument\n");
		return;
	}

	switch (temp) {
	case CMD_TEST_OBJECTS:

		acpi_db_test_all_objects();
		break;

	case CMD_TEST_PREDEFINED:

		acpi_db_evaluate_all_predefined_names(NULL);
		break;

	default:
		break;
	}
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_db_test_all_objects
 *
 * PARAMETERS:  None
 *
 * RETURN:      None
 *
 * DESCRIPTION: This test implements the OBJECTS subcommand. It exercises the
 *              namespace by reading/writing/comparing all data objects such
 *              as integers, strings, buffers, fields, buffer fields, etc.
 *
 ******************************************************************************/

static void acpi_db_test_all_objects(void)
{
	acpi_status status;

	/* Install the debugger read-object control method if necessary */

	if (!read_handle) {
		status = acpi_install_method(read_method_code);
		if (ACPI_FAILURE(status)) {
			acpi_os_printf
			    ("%s, Could not install debugger read method\n",
			     acpi_format_exception(status));
			return;
		}

		status =
		    acpi_get_handle(NULL, ACPI_DB_READ_METHOD, &read_handle);
		if (ACPI_FAILURE(status)) {
			acpi_os_printf
			    ("Could not obtain handle for debug method %s\n",
			     ACPI_DB_READ_METHOD);
			return;
		}
	}

	/* Install the debugger write-object control method if necessary */

	if (!write_handle) {
		status = acpi_install_method(write_method_code);
		if (ACPI_FAILURE(status)) {
			acpi_os_printf
			    ("%s, Could not install debugger write method\n",
			     acpi_format_exception(status));
			return;
		}

		status =
		    acpi_get_handle(NULL, ACPI_DB_WRITE_METHOD, &write_handle);
		if (ACPI_FAILURE(status)) {
			acpi_os_printf
			    ("Could not obtain handle for debug method %s\n",
			     ACPI_DB_WRITE_METHOD);
			return;
		}
	}

	/* Walk the entire namespace, testing each supported named data object */

	(void)acpi_walk_namespace(ACPI_TYPE_ANY, ACPI_ROOT_OBJECT,
				  ACPI_UINT32_MAX, acpi_db_test_one_object,
				  NULL, NULL, NULL);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_db_test_one_object
 *
 * PARAMETERS:  acpi_walk_callback
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Test one namespace object. Supported types are Integer,
 *              String, Buffer, Package, buffer_field, and field_unit.
 *              All other object types are simply ignored.
 *
 ******************************************************************************/

static acpi_status
acpi_db_test_one_object(acpi_handle obj_handle,
			u32 nesting_level, void *context, void **return_value)
{
	struct acpi_namespace_node *node;
	union acpi_operand_object *obj_desc;
	acpi_object_type local_type;
	u32 bit_length = 0;
	u32 byte_length = 0;
	acpi_status status = AE_OK;

	node = ACPI_CAST_PTR(struct acpi_namespace_node, obj_handle);
	obj_desc = node->object;

	/*
	 * For the supported types, get the actual bit length or
	 * byte length. Map the type to one of Integer/String/Buffer.
	 */
	switch (node->type) {
	case ACPI_TYPE_INTEGER:

		/* Integer width is either 32 or 64 */

		local_type = ACPI_TYPE_INTEGER;
		bit_length = acpi_gbl_integer_bit_width;
		break;

	case ACPI_TYPE_STRING:

		local_type = ACPI_TYPE_STRING;
		byte_length = obj_desc->string.length;
		break;

	case ACPI_TYPE_BUFFER:

		local_type = ACPI_TYPE_BUFFER;
		byte_length = obj_desc->buffer.length;
		bit_length = byte_length * 8;
		break;

	case ACPI_TYPE_PACKAGE:

		local_type = ACPI_TYPE_PACKAGE;
		break;

	case ACPI_TYPE_FIELD_UNIT:
	case ACPI_TYPE_LOCAL_REGION_FIELD:
	case ACPI_TYPE_LOCAL_INDEX_FIELD:
	case ACPI_TYPE_LOCAL_BANK_FIELD:

		local_type = ACPI_TYPE_FIELD_UNIT;
		break;

	case ACPI_TYPE_BUFFER_FIELD:
		/*
		 * The returned object will be a Buffer if the field length
		 * is larger than the size of an Integer (32 or 64 bits
		 * depending on the DSDT version).
		 */
		local_type = ACPI_TYPE_INTEGER;
		if (obj_desc) {
			bit_length = obj_desc->common_field.bit_length;
			byte_length = ACPI_ROUND_BITS_UP_TO_BYTES(bit_length);
			if (bit_length > acpi_gbl_integer_bit_width) {
				local_type = ACPI_TYPE_BUFFER;
			}
		}
		break;

	default:

		/* Ignore all non-data types - Methods, Devices, Scopes, etc. */

		return (AE_OK);
	}

	/* Emit the common prefix: Type:Name */

	acpi_os_printf("%14s: %4.4s",
		       acpi_ut_get_type_name(node->type), node->name.ascii);

	if (!obj_desc) {
		acpi_os_printf(" No attached sub-object, ignoring\n");
		return (AE_OK);
	}

	/* At this point, we have resolved the object to one of the major types */

	switch (local_type) {
	case ACPI_TYPE_INTEGER:

		status = acpi_db_test_integer_type(node, bit_length);
		break;

	case ACPI_TYPE_STRING:

		status = acpi_db_test_string_type(node, byte_length);
		break;

	case ACPI_TYPE_BUFFER:

		status = acpi_db_test_buffer_type(node, bit_length);
		break;

	case ACPI_TYPE_PACKAGE:

		status = acpi_db_test_package_type(node);
		break;

	case ACPI_TYPE_FIELD_UNIT:

		status = acpi_db_test_field_unit_type(obj_desc);
		break;

	default:

		acpi_os_printf(" Ignoring, type not implemented (%2.2X)",
			       local_type);
		break;
	}

	/* Exit on error, but don't abort the namespace walk */

	if (ACPI_FAILURE(status)) {
		status = AE_OK;
	}

	acpi_os_printf("\n");
	return (status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_db_test_integer_type
 *
 * PARAMETERS:  node                - Parent NS node for the object
 *              bit_length          - Actual length of the object. Used for
 *                                    support of arbitrary length field_unit
 *                                    and buffer_field objects.
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Test read/write for an Integer-valued object. Performs a
 *              write/read/compare of an arbitrary new value, then performs
 *              a write/read/compare of the original value.
 *
 ******************************************************************************/

static acpi_status
acpi_db_test_integer_type(struct acpi_namespace_node *node, u32 bit_length)
{
	union acpi_object *temp1 = NULL;
	union acpi_object *temp2 = NULL;
	union acpi_object *temp3 = NULL;
	union acpi_object write_value;
	u64 value_to_write;
	acpi_status status;

	if (bit_length > 64) {
		acpi_os_printf(" Invalid length for an Integer: %u",
			       bit_length);
		return (AE_OK);
	}

	/* Read the original value */

	status = acpi_db_read_from_object(node, ACPI_TYPE_INTEGER, &temp1);
	if (ACPI_FAILURE(status)) {
		return (status);
	}

	acpi_os_printf(ACPI_DEBUG_LENGTH_FORMAT " %8.8X%8.8X",
		       bit_length, ACPI_ROUND_BITS_UP_TO_BYTES(bit_length),
		       ACPI_FORMAT_UINT64(temp1->integer.value));

	value_to_write = ACPI_UINT64_MAX >> (64 - bit_length);
	if (temp1->integer.value == value_to_write) {
		value_to_write = 0;
	}
	/* Write a new value */

	write_value.type = ACPI_TYPE_INTEGER;
	write_value.integer.value = value_to_write;
	status = acpi_db_write_to_object(node, &write_value);
	if (ACPI_FAILURE(status)) {
		goto exit;
	}

	/* Ensure that we can read back the new value */

	status = acpi_db_read_from_object(node, ACPI_TYPE_INTEGER, &temp2);
	if (ACPI_FAILURE(status)) {
		goto exit;
	}

	if (temp2->integer.value != value_to_write) {
		acpi_os_printf(" MISMATCH 2: %8.8X%8.8X, expecting %8.8X%8.8X",
			       ACPI_FORMAT_UINT64(temp2->integer.value),
			       ACPI_FORMAT_UINT64(value_to_write));
	}

	/* Write back the original value */

	write_value.integer.value = temp1->integer.value;
	status = acpi_db_write_to_object(node, &write_value);
	if (ACPI_FAILURE(status)) {
		goto exit;
	}

	/* Ensure that we can read back the original value */

	status = acpi_db_read_from_object(node, ACPI_TYPE_INTEGER, &temp3);
	if (ACPI_FAILURE(status)) {
		goto exit;
	}

	if (temp3->integer.value != temp1->integer.value) {
		acpi_os_printf(" MISMATCH 3: %8.8X%8.8X, expecting %8.8X%8.8X",
			       ACPI_FORMAT_UINT64(temp3->integer.value),
			       ACPI_FORMAT_UINT64(temp1->integer.value));
	}

exit:
	if (temp1) {
		acpi_os_free(temp1);
	}
	if (temp2) {
		acpi_os_free(temp2);
	}
	if (temp3) {
		acpi_os_free(temp3);
	}
	return (AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_db_test_buffer_type
 *
 * PARAMETERS:  node                - Parent NS node for the object
 *              bit_length          - Actual length of the object.
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Test read/write for an Buffer-valued object. Performs a
 *              write/read/compare of an arbitrary new value, then performs
 *              a write/read/compare of the original value.
 *
 ******************************************************************************/

static acpi_status
acpi_db_test_buffer_type(struct acpi_namespace_node *node, u32 bit_length)
{
	union acpi_object *temp1 = NULL;
	union acpi_object *temp2 = NULL;
	union acpi_object *temp3 = NULL;
	u8 *buffer;
	union acpi_object write_value;
	acpi_status status;
	u32 byte_length;
	u32 i;
	u8 extra_bits;

	byte_length = ACPI_ROUND_BITS_UP_TO_BYTES(bit_length);
	if (byte_length == 0) {
		acpi_os_printf(" Ignoring zero length buffer");
		return (AE_OK);
	}

	/* Allocate a local buffer */

	buffer = ACPI_ALLOCATE_ZEROED(byte_length);
	if (!buffer) {
		return (AE_NO_MEMORY);
	}

	/* Read the original value */

	status = acpi_db_read_from_object(node, ACPI_TYPE_BUFFER, &temp1);
	if (ACPI_FAILURE(status)) {
		goto exit;
	}

	/* Emit a few bytes of the buffer */

	acpi_os_printf(ACPI_DEBUG_LENGTH_FORMAT, bit_length,
		       temp1->buffer.length);
	for (i = 0; ((i < 8) && (i < byte_length)); i++) {
		acpi_os_printf(" %2.2X", temp1->buffer.pointer[i]);
	}
	acpi_os_printf("... ");

	/*
	 * Write a new value.
	 *
	 * Handle possible extra bits at the end of the buffer. Can
	 * happen for field_units larger than an integer, but the bit
	 * count is not an integral number of bytes. Zero out the
	 * unused bits.
	 */
	memset(buffer, BUFFER_FILL_VALUE, byte_length);
	extra_bits = bit_length % 8;
	if (extra_bits) {
		buffer[byte_length - 1] = ACPI_MASK_BITS_ABOVE(extra_bits);
	}

	write_value.type = ACPI_TYPE_BUFFER;
	write_value.buffer.length = byte_length;
	write_value.buffer.pointer = buffer;

	status = acpi_db_write_to_object(node, &write_value);
	if (ACPI_FAILURE(status)) {
		goto exit;
	}

	/* Ensure that we can read back the new value */

	status = acpi_db_read_from_object(node, ACPI_TYPE_BUFFER, &temp2);
	if (ACPI_FAILURE(status)) {
		goto exit;
	}

	if (memcmp(temp2->buffer.pointer, buffer, byte_length)) {
		acpi_os_printf(" MISMATCH 2: New buffer value");
	}

	/* Write back the original value */

	write_value.buffer.length = byte_length;
	write_value.buffer.pointer = temp1->buffer.pointer;

	status = acpi_db_write_to_object(node, &write_value);
	if (ACPI_FAILURE(status)) {
		goto exit;
	}

	/* Ensure that we can read back the original value */

	status = acpi_db_read_from_object(node, ACPI_TYPE_BUFFER, &temp3);
	if (ACPI_FAILURE(status)) {
		goto exit;
	}

	if (memcmp(temp1->buffer.pointer, temp3->buffer.pointer, byte_length)) {
		acpi_os_printf(" MISMATCH 3: While restoring original buffer");
	}

exit:
	ACPI_FREE(buffer);
	if (temp1) {
		acpi_os_free(temp1);
	}
	if (temp2) {
		acpi_os_free(temp2);
	}
	if (temp3) {
		acpi_os_free(temp3);
	}
	return (status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_db_test_string_type
 *
 * PARAMETERS:  node                - Parent NS node for the object
 *              byte_length         - Actual length of the object.
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Test read/write for an String-valued object. Performs a
 *              write/read/compare of an arbitrary new value, then performs
 *              a write/read/compare of the original value.
 *
 ******************************************************************************/

static acpi_status
acpi_db_test_string_type(struct acpi_namespace_node *node, u32 byte_length)
{
	union acpi_object *temp1 = NULL;
	union acpi_object *temp2 = NULL;
	union acpi_object *temp3 = NULL;
	char *value_to_write = "Test String from AML Debugger";
	union acpi_object write_value;
	acpi_status status;

	/* Read the original value */

	status = acpi_db_read_from_object(node, ACPI_TYPE_STRING, &temp1);
	if (ACPI_FAILURE(status)) {
		return (status);
	}

	acpi_os_printf(ACPI_DEBUG_LENGTH_FORMAT " \"%s\"",
		       (temp1->string.length * 8), temp1->string.length,
		       temp1->string.pointer);

	/* Write a new value */

	write_value.type = ACPI_TYPE_STRING;
	write_value.string.length = strlen(value_to_write);
	write_value.string.pointer = value_to_write;

	status = acpi_db_write_to_object(node, &write_value);
	if (ACPI_FAILURE(status)) {
		goto exit;
	}

	/* Ensure that we can read back the new value */

	status = acpi_db_read_from_object(node, ACPI_TYPE_STRING, &temp2);
	if (ACPI_FAILURE(status)) {
		goto exit;
	}

	if (strcmp(temp2->string.pointer, value_to_write)) {
		acpi_os_printf(" MISMATCH 2: %s, expecting %s",
			       temp2->string.pointer, value_to_write);
	}

	/* Write back the original value */

	write_value.string.length = strlen(temp1->string.pointer);
	write_value.string.pointer = temp1->string.pointer;

	status = acpi_db_write_to_object(node, &write_value);
	if (ACPI_FAILURE(status)) {
		goto exit;
	}

	/* Ensure that we can read back the original value */

	status = acpi_db_read_from_object(node, ACPI_TYPE_STRING, &temp3);
	if (ACPI_FAILURE(status)) {
		goto exit;
	}

	if (strcmp(temp1->string.pointer, temp3->string.pointer)) {
		acpi_os_printf(" MISMATCH 3: %s, expecting %s",
			       temp3->string.pointer, temp1->string.pointer);
	}

exit:
	if (temp1) {
		acpi_os_free(temp1);
	}
	if (temp2) {
		acpi_os_free(temp2);
	}
	if (temp3) {
		acpi_os_free(temp3);
	}
	return (status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_db_test_package_type
 *
 * PARAMETERS:  node                - Parent NS node for the object
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Test read for a Package object.
 *
 ******************************************************************************/

static acpi_status acpi_db_test_package_type(struct acpi_namespace_node *node)
{
	union acpi_object *temp1 = NULL;
	acpi_status status;

	/* Read the original value */

	status = acpi_db_read_from_object(node, ACPI_TYPE_PACKAGE, &temp1);
	if (ACPI_FAILURE(status)) {
		return (status);
	}

	acpi_os_printf(" %.2X Elements", temp1->package.count);
	acpi_os_free(temp1);
	return (status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_db_test_field_unit_type
 *
 * PARAMETERS:  obj_desc                - A field unit object
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Test read/write on a named field unit.
 *
 ******************************************************************************/

static acpi_status
acpi_db_test_field_unit_type(union acpi_operand_object *obj_desc)
{
	union acpi_operand_object *region_obj;
	u32 bit_length = 0;
	u32 byte_length = 0;
	acpi_status status = AE_OK;
	union acpi_operand_object *ret_buffer_desc;

	/* Supported spaces are memory/io/pci_config */

	region_obj = obj_desc->field.region_obj;
	switch (region_obj->region.space_id) {
	case ACPI_ADR_SPACE_SYSTEM_MEMORY:
	case ACPI_ADR_SPACE_SYSTEM_IO:
	case ACPI_ADR_SPACE_PCI_CONFIG:

		/* Need the interpreter to execute */

		acpi_ut_acquire_mutex(ACPI_MTX_INTERPRETER);
		acpi_ut_acquire_mutex(ACPI_MTX_NAMESPACE);

		/* Exercise read-then-write */

		status =
		    acpi_ex_read_data_from_field(NULL, obj_desc,
						 &ret_buffer_desc);
		if (status == AE_OK) {
			acpi_ex_write_data_to_field(ret_buffer_desc, obj_desc,
						    NULL);
			acpi_ut_remove_reference(ret_buffer_desc);
		}

		acpi_ut_release_mutex(ACPI_MTX_NAMESPACE);
		acpi_ut_release_mutex(ACPI_MTX_INTERPRETER);

		bit_length = obj_desc->common_field.bit_length;
		byte_length = ACPI_ROUND_BITS_UP_TO_BYTES(bit_length);

		acpi_os_printf(ACPI_DEBUG_LENGTH_FORMAT " [%s]", bit_length,
			       byte_length,
			       acpi_ut_get_region_name(region_obj->region.
						       space_id));
		return (status);

	default:

		acpi_os_printf
		    ("      %s address space is not supported in this command [%4.4s]",
		     acpi_ut_get_region_name(region_obj->region.space_id),
		     region_obj->region.node->name.ascii);
		return (AE_OK);
	}
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_db_read_from_object
 *
 * PARAMETERS:  node                - Parent NS node for the object
 *              expected_type       - Object type expected from the read
 *              value               - Where the value read is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Performs a read from the specified object by invoking the
 *              special debugger control method that reads the object. Thus,
 *              the AML interpreter is doing all of the work, increasing the
 *              validity of the test.
 *
 ******************************************************************************/

static acpi_status
acpi_db_read_from_object(struct acpi_namespace_node *node,
			 acpi_object_type expected_type,
			 union acpi_object **value)
{
	union acpi_object *ret_value;
	struct acpi_object_list param_objects;
	union acpi_object params[2];
	struct acpi_buffer return_obj;
	acpi_status status;

	params[0].type = ACPI_TYPE_LOCAL_REFERENCE;
	params[0].reference.actual_type = node->type;
	params[0].reference.handle = ACPI_CAST_PTR(acpi_handle, node);

	param_objects.count = 1;
	param_objects.pointer = params;

	return_obj.length = ACPI_ALLOCATE_BUFFER;

	acpi_gbl_method_executing = TRUE;
	status = acpi_evaluate_object(read_handle, NULL,
				      &param_objects, &return_obj);

	acpi_gbl_method_executing = FALSE;
	if (ACPI_FAILURE(status)) {
		acpi_os_printf("Could not read from object, %s",
			       acpi_format_exception(status));
		return (status);
	}

	ret_value = (union acpi_object *)return_obj.pointer;

	switch (ret_value->type) {
	case ACPI_TYPE_INTEGER:
	case ACPI_TYPE_BUFFER:
	case ACPI_TYPE_STRING:
	case ACPI_TYPE_PACKAGE:
		/*
		 * Did we receive the type we wanted? Most important for the
		 * Integer/Buffer case (when a field is larger than an Integer,
		 * it should return a Buffer).
		 */
		if (ret_value->type != expected_type) {
			acpi_os_printf
			    (" Type mismatch: Expected %s, Received %s",
			     acpi_ut_get_type_name(expected_type),
			     acpi_ut_get_type_name(ret_value->type));

			acpi_os_free(return_obj.pointer);
			return (AE_TYPE);
		}

		*value = ret_value;
		break;

	default:

		acpi_os_printf(" Unsupported return object type, %s",
			       acpi_ut_get_type_name(ret_value->type));

		acpi_os_free(return_obj.pointer);
		return (AE_TYPE);
	}

	return (status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_db_write_to_object
 *
 * PARAMETERS:  node                - Parent NS node for the object
 *              value               - Value to be written
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Performs a write to the specified object by invoking the
 *              special debugger control method that writes the object. Thus,
 *              the AML interpreter is doing all of the work, increasing the
 *              validity of the test.
 *
 ******************************************************************************/

static acpi_status
acpi_db_write_to_object(struct acpi_namespace_node *node,
			union acpi_object *value)
{
	struct acpi_object_list param_objects;
	union acpi_object params[2];
	acpi_status status;

	params[0].type = ACPI_TYPE_LOCAL_REFERENCE;
	params[0].reference.actual_type = node->type;
	params[0].reference.handle = ACPI_CAST_PTR(acpi_handle, node);

	/* Copy the incoming user parameter */

	memcpy(&params[1], value, sizeof(union acpi_object));

	param_objects.count = 2;
	param_objects.pointer = params;

	acpi_gbl_method_executing = TRUE;
	status = acpi_evaluate_object(write_handle, NULL, &param_objects, NULL);
	acpi_gbl_method_executing = FALSE;

	if (ACPI_FAILURE(status)) {
		acpi_os_printf("Could not write to object, %s",
			       acpi_format_exception(status));
	}

	return (status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_db_evaluate_all_predefined_names
 *
 * PARAMETERS:  count_arg           - Max number of methods to execute
 *
 * RETURN:      None
 *
 * DESCRIPTION: Namespace batch execution. Execute predefined names in the
 *              namespace, up to the max count, if specified.
 *
 ******************************************************************************/

static void acpi_db_evaluate_all_predefined_names(char *count_arg)
{
	struct acpi_db_execute_walk info;

	info.count = 0;
	info.max_count = ACPI_UINT32_MAX;

	if (count_arg) {
		info.max_count = strtoul(count_arg, NULL, 0);
	}

	/* Search all nodes in namespace */

	(void)acpi_walk_namespace(ACPI_TYPE_ANY, ACPI_ROOT_OBJECT,
				  ACPI_UINT32_MAX,
				  acpi_db_evaluate_one_predefined_name, NULL,
				  (void *)&info, NULL);

	acpi_os_printf("Evaluated %u predefined names in the namespace\n",
		       info.count);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_db_evaluate_one_predefined_name
 *
 * PARAMETERS:  Callback from walk_namespace
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Batch execution module. Currently only executes predefined
 *              ACPI names.
 *
 ******************************************************************************/

static acpi_status
acpi_db_evaluate_one_predefined_name(acpi_handle obj_handle,
				     u32 nesting_level,
				     void *context, void **return_value)
{
	struct acpi_namespace_node *node =
	    (struct acpi_namespace_node *)obj_handle;
	struct acpi_db_execute_walk *info =
	    (struct acpi_db_execute_walk *)context;
	char *pathname;
	const union acpi_predefined_info *predefined;
	struct acpi_device_info *obj_info;
	struct acpi_object_list param_objects;
	union acpi_object params[ACPI_METHOD_NUM_ARGS];
	union acpi_object *this_param;
	struct acpi_buffer return_obj;
	acpi_status status;
	u16 arg_type_list;
	u8 arg_count;
	u8 arg_type;
	u32 i;

	/* The name must be a predefined ACPI name */

	predefined = acpi_ut_match_predefined_method(node->name.ascii);
	if (!predefined) {
		return (AE_OK);
	}

	if (node->type == ACPI_TYPE_LOCAL_SCOPE) {
		return (AE_OK);
	}

	pathname = acpi_ns_get_normalized_pathname(node, TRUE);
	if (!pathname) {
		return (AE_OK);
	}

	/* Get the object info for number of method parameters */

	status = acpi_get_object_info(obj_handle, &obj_info);
	if (ACPI_FAILURE(status)) {
		ACPI_FREE(pathname);
		return (status);
	}

	param_objects.count = 0;
	param_objects.pointer = NULL;

	if (obj_info->type == ACPI_TYPE_METHOD) {

		/* Setup default parameters (with proper types) */

		arg_type_list = predefined->info.argument_list;
		arg_count = METHOD_GET_ARG_COUNT(arg_type_list);

		/*
		 * Setup the ACPI-required number of arguments, regardless of what
		 * the actual method defines. If there is a difference, then the
		 * method is wrong and a warning will be issued during execution.
		 */
		this_param = params;
		for (i = 0; i < arg_count; i++) {
			arg_type = METHOD_GET_NEXT_TYPE(arg_type_list);
			this_param->type = arg_type;

			switch (arg_type) {
			case ACPI_TYPE_INTEGER:

				this_param->integer.value = 1;
				break;

			case ACPI_TYPE_STRING:

				this_param->string.pointer =
				    "This is the default argument string";
				this_param->string.length =
				    strlen(this_param->string.pointer);
				break;

			case ACPI_TYPE_BUFFER:

				this_param->buffer.pointer = (u8 *)params;	/* just a garbage buffer */
				this_param->buffer.length = 48;
				break;

			case ACPI_TYPE_PACKAGE:

				this_param->package.elements = NULL;
				this_param->package.count = 0;
				break;

			default:

				acpi_os_printf
				    ("%s: Unsupported argument type: %u\n",
				     pathname, arg_type);
				break;
			}

			this_param++;
		}

		param_objects.count = arg_count;
		param_objects.pointer = params;
	}

	ACPI_FREE(obj_info);
	return_obj.pointer = NULL;
	return_obj.length = ACPI_ALLOCATE_BUFFER;

	/* Do the actual method execution */

	acpi_gbl_method_executing = TRUE;

	status = acpi_evaluate_object(node, NULL, &param_objects, &return_obj);

	acpi_os_printf("%-32s returned %s\n",
		       pathname, acpi_format_exception(status));
	acpi_gbl_method_executing = FALSE;
	ACPI_FREE(pathname);

	/* Ignore status from method execution */

	status = AE_OK;

	/* Update count, check if we have executed enough methods */

	info->count++;
	if (info->count >= info->max_count) {
		status = AE_CTRL_TERMINATE;
	}

	return (status);
}
