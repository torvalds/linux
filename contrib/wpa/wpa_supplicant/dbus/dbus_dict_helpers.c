/*
 * WPA Supplicant / dbus-based control interface
 * Copyright (c) 2006, Dan Williams <dcbw@redhat.com> and Red Hat, Inc.
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"
#include <dbus/dbus.h>

#include "common.h"
#include "wpabuf.h"
#include "dbus_dict_helpers.h"


/**
 * Start a dict in a dbus message.  Should be paired with a call to
 * wpa_dbus_dict_close_write().
 *
 * @param iter A valid dbus message iterator
 * @param iter_dict (out) A dict iterator to pass to further dict functions
 * @return TRUE on success, FALSE on failure
 *
 */
dbus_bool_t wpa_dbus_dict_open_write(DBusMessageIter *iter,
				     DBusMessageIter *iter_dict)
{
	dbus_bool_t result;

	if (!iter || !iter_dict)
		return FALSE;

	result = dbus_message_iter_open_container(
		iter,
		DBUS_TYPE_ARRAY,
		DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING
		DBUS_TYPE_STRING_AS_STRING
		DBUS_TYPE_VARIANT_AS_STRING
		DBUS_DICT_ENTRY_END_CHAR_AS_STRING,
		iter_dict);
	return result;
}


/**
 * End a dict element in a dbus message.  Should be paired with
 * a call to wpa_dbus_dict_open_write().
 *
 * @param iter valid dbus message iterator, same as passed to
 *    wpa_dbus_dict_open_write()
 * @param iter_dict a dbus dict iterator returned from
 *    wpa_dbus_dict_open_write()
 * @return TRUE on success, FALSE on failure
 *
 */
dbus_bool_t wpa_dbus_dict_close_write(DBusMessageIter *iter,
				      DBusMessageIter *iter_dict)
{
	if (!iter || !iter_dict)
		return FALSE;

	return dbus_message_iter_close_container(iter, iter_dict);
}


const char * wpa_dbus_type_as_string(const int type)
{
	switch (type) {
	case DBUS_TYPE_BYTE:
		return DBUS_TYPE_BYTE_AS_STRING;
	case DBUS_TYPE_BOOLEAN:
		return DBUS_TYPE_BOOLEAN_AS_STRING;
	case DBUS_TYPE_INT16:
		return DBUS_TYPE_INT16_AS_STRING;
	case DBUS_TYPE_UINT16:
		return DBUS_TYPE_UINT16_AS_STRING;
	case DBUS_TYPE_INT32:
		return DBUS_TYPE_INT32_AS_STRING;
	case DBUS_TYPE_UINT32:
		return DBUS_TYPE_UINT32_AS_STRING;
	case DBUS_TYPE_INT64:
		return DBUS_TYPE_INT64_AS_STRING;
	case DBUS_TYPE_UINT64:
		return DBUS_TYPE_UINT64_AS_STRING;
	case DBUS_TYPE_DOUBLE:
		return DBUS_TYPE_DOUBLE_AS_STRING;
	case DBUS_TYPE_STRING:
		return DBUS_TYPE_STRING_AS_STRING;
	case DBUS_TYPE_OBJECT_PATH:
		return DBUS_TYPE_OBJECT_PATH_AS_STRING;
	case DBUS_TYPE_ARRAY:
		return DBUS_TYPE_ARRAY_AS_STRING;
	default:
		return NULL;
	}
}


static dbus_bool_t _wpa_dbus_add_dict_entry_start(
	DBusMessageIter *iter_dict, DBusMessageIter *iter_dict_entry,
	const char *key, const int value_type)
{
	if (!dbus_message_iter_open_container(iter_dict,
					      DBUS_TYPE_DICT_ENTRY, NULL,
					      iter_dict_entry))
		return FALSE;

	return dbus_message_iter_append_basic(iter_dict_entry, DBUS_TYPE_STRING,
					      &key);
}


static dbus_bool_t _wpa_dbus_add_dict_entry_end(
	DBusMessageIter *iter_dict, DBusMessageIter *iter_dict_entry,
	DBusMessageIter *iter_dict_val)
{
	if (!dbus_message_iter_close_container(iter_dict_entry, iter_dict_val))
		return FALSE;

	return dbus_message_iter_close_container(iter_dict, iter_dict_entry);
}


static dbus_bool_t _wpa_dbus_add_dict_entry_basic(DBusMessageIter *iter_dict,
						  const char *key,
						  const int value_type,
						  const void *value)
{
	DBusMessageIter iter_dict_entry, iter_dict_val;
	const char *type_as_string = NULL;

	if (key == NULL)
		return FALSE;

	type_as_string = wpa_dbus_type_as_string(value_type);
	if (!type_as_string)
		return FALSE;

	if (!_wpa_dbus_add_dict_entry_start(iter_dict, &iter_dict_entry,
					    key, value_type) ||
	    !dbus_message_iter_open_container(&iter_dict_entry,
					      DBUS_TYPE_VARIANT,
					      type_as_string, &iter_dict_val) ||
	    !dbus_message_iter_append_basic(&iter_dict_val, value_type, value))
		return FALSE;

	return _wpa_dbus_add_dict_entry_end(iter_dict, &iter_dict_entry,
					    &iter_dict_val);
}


static dbus_bool_t _wpa_dbus_add_dict_entry_byte_array(
	DBusMessageIter *iter_dict, const char *key,
	const char *value, const dbus_uint32_t value_len)
{
	DBusMessageIter iter_dict_entry, iter_dict_val, iter_array;
	dbus_uint32_t i;

	if (!_wpa_dbus_add_dict_entry_start(iter_dict, &iter_dict_entry,
					    key, DBUS_TYPE_ARRAY) ||
	    !dbus_message_iter_open_container(&iter_dict_entry,
					      DBUS_TYPE_VARIANT,
					      DBUS_TYPE_ARRAY_AS_STRING
					      DBUS_TYPE_BYTE_AS_STRING,
					      &iter_dict_val) ||
	    !dbus_message_iter_open_container(&iter_dict_val, DBUS_TYPE_ARRAY,
					      DBUS_TYPE_BYTE_AS_STRING,
					      &iter_array))
		return FALSE;

	for (i = 0; i < value_len; i++) {
		if (!dbus_message_iter_append_basic(&iter_array,
						    DBUS_TYPE_BYTE,
						    &(value[i])))
			return FALSE;
	}

	if (!dbus_message_iter_close_container(&iter_dict_val, &iter_array))
		return FALSE;

	return _wpa_dbus_add_dict_entry_end(iter_dict, &iter_dict_entry,
					    &iter_dict_val);
}


/**
 * Add a string entry to the dict.
 *
 * @param iter_dict A valid DBusMessageIter returned from
 *    wpa_dbus_dict_open_write()
 * @param key The key of the dict item
 * @param value The string value
 * @return TRUE on success, FALSE on failure
 *
 */
dbus_bool_t wpa_dbus_dict_append_string(DBusMessageIter *iter_dict,
					const char *key, const char *value)
{
	if (!value)
		return FALSE;
	return _wpa_dbus_add_dict_entry_basic(iter_dict, key, DBUS_TYPE_STRING,
					      &value);
}


/**
 * Add a boolean entry to the dict.
 *
 * @param iter_dict A valid DBusMessageIter returned from
 *    wpa_dbus_dict_open_write()
 * @param key The key of the dict item
 * @param value The boolean value
 * @return TRUE on success, FALSE on failure
 *
 */
dbus_bool_t wpa_dbus_dict_append_bool(DBusMessageIter *iter_dict,
				      const char *key, const dbus_bool_t value)
{
	return _wpa_dbus_add_dict_entry_basic(iter_dict, key,
					      DBUS_TYPE_BOOLEAN, &value);
}


/**
 * Add a 16-bit signed integer entry to the dict.
 *
 * @param iter_dict A valid DBusMessageIter returned from
 *    wpa_dbus_dict_open_write()
 * @param key The key of the dict item
 * @param value The 16-bit signed integer value
 * @return TRUE on success, FALSE on failure
 *
 */
dbus_bool_t wpa_dbus_dict_append_int16(DBusMessageIter *iter_dict,
				       const char *key,
				       const dbus_int16_t value)
{
	return _wpa_dbus_add_dict_entry_basic(iter_dict, key, DBUS_TYPE_INT16,
					      &value);
}


/**
 * Add a 16-bit unsigned integer entry to the dict.
 *
 * @param iter_dict A valid DBusMessageIter returned from
 *    wpa_dbus_dict_open_write()
 * @param key The key of the dict item
 * @param value The 16-bit unsigned integer value
 * @return TRUE on success, FALSE on failure
 *
 */
dbus_bool_t wpa_dbus_dict_append_uint16(DBusMessageIter *iter_dict,
					const char *key,
					const dbus_uint16_t value)
{
	return _wpa_dbus_add_dict_entry_basic(iter_dict, key, DBUS_TYPE_UINT16,
					      &value);
}


/**
 * Add a 32-bit signed integer to the dict.
 *
 * @param iter_dict A valid DBusMessageIter returned from
 *    wpa_dbus_dict_open_write()
 * @param key The key of the dict item
 * @param value The 32-bit signed integer value
 * @return TRUE on success, FALSE on failure
 *
 */
dbus_bool_t wpa_dbus_dict_append_int32(DBusMessageIter *iter_dict,
				       const char *key,
				       const dbus_int32_t value)
{
	return _wpa_dbus_add_dict_entry_basic(iter_dict, key, DBUS_TYPE_INT32,
					      &value);
}


/**
 * Add a 32-bit unsigned integer entry to the dict.
 *
 * @param iter_dict A valid DBusMessageIter returned from
 *    wpa_dbus_dict_open_write()
 * @param key The key of the dict item
 * @param value The 32-bit unsigned integer value
 * @return TRUE on success, FALSE on failure
 *
 */
dbus_bool_t wpa_dbus_dict_append_uint32(DBusMessageIter *iter_dict,
					const char *key,
					const dbus_uint32_t value)
{
	return _wpa_dbus_add_dict_entry_basic(iter_dict, key, DBUS_TYPE_UINT32,
					      &value);
}


/**
 * Add a DBus object path entry to the dict.
 *
 * @param iter_dict A valid DBusMessageIter returned from
 *    wpa_dbus_dict_open_write()
 * @param key The key of the dict item
 * @param value The DBus object path value
 * @return TRUE on success, FALSE on failure
 *
 */
dbus_bool_t wpa_dbus_dict_append_object_path(DBusMessageIter *iter_dict,
					     const char *key,
					     const char *value)
{
	if (!value)
		return FALSE;
	return _wpa_dbus_add_dict_entry_basic(iter_dict, key,
					      DBUS_TYPE_OBJECT_PATH, &value);
}


/**
 * Add a byte array entry to the dict.
 *
 * @param iter_dict A valid DBusMessageIter returned from
 *    wpa_dbus_dict_open_write()
 * @param key The key of the dict item
 * @param value The byte array
 * @param value_len The length of the byte array, in bytes
 * @return TRUE on success, FALSE on failure
 *
 */
dbus_bool_t wpa_dbus_dict_append_byte_array(DBusMessageIter *iter_dict,
					    const char *key,
					    const char *value,
					    const dbus_uint32_t value_len)
{
	if (!key || (!value && value_len != 0))
		return FALSE;
	return _wpa_dbus_add_dict_entry_byte_array(iter_dict, key, value,
						   value_len);
}


/**
 * Begin an array entry in the dict
 *
 * @param iter_dict A valid DBusMessageIter returned from
 *                  wpa_dbus_dict_open_write()
 * @param key The key of the dict item
 * @param type The type of the contained data
 * @param iter_dict_entry A private DBusMessageIter provided by the caller to
 *                        be passed to wpa_dbus_dict_end_string_array()
 * @param iter_dict_val A private DBusMessageIter provided by the caller to
 *                      be passed to wpa_dbus_dict_end_string_array()
 * @param iter_array On return, the DBusMessageIter to be passed to
 *                   wpa_dbus_dict_string_array_add_element()
 * @return TRUE on success, FALSE on failure
 *
 */
dbus_bool_t wpa_dbus_dict_begin_array(DBusMessageIter *iter_dict,
				      const char *key, const char *type,
				      DBusMessageIter *iter_dict_entry,
				      DBusMessageIter *iter_dict_val,
				      DBusMessageIter *iter_array)
{
	char array_type[10];
	int err;

	err = os_snprintf(array_type, sizeof(array_type),
			  DBUS_TYPE_ARRAY_AS_STRING "%s",
			  type);
	if (os_snprintf_error(sizeof(array_type), err))
		return FALSE;

	if (!iter_dict || !iter_dict_entry || !iter_dict_val || !iter_array ||
	    !_wpa_dbus_add_dict_entry_start(iter_dict, iter_dict_entry,
					    key, DBUS_TYPE_ARRAY) ||
	    !dbus_message_iter_open_container(iter_dict_entry,
					      DBUS_TYPE_VARIANT,
					      array_type,
					      iter_dict_val))
		return FALSE;

	return dbus_message_iter_open_container(iter_dict_val, DBUS_TYPE_ARRAY,
						type, iter_array);
}


dbus_bool_t wpa_dbus_dict_begin_string_array(DBusMessageIter *iter_dict,
					     const char *key,
					     DBusMessageIter *iter_dict_entry,
					     DBusMessageIter *iter_dict_val,
					     DBusMessageIter *iter_array)
{
	return wpa_dbus_dict_begin_array(
		iter_dict, key,
		DBUS_TYPE_STRING_AS_STRING,
		iter_dict_entry, iter_dict_val, iter_array);
}


/**
 * Add a single string element to a string array dict entry
 *
 * @param iter_array A valid DBusMessageIter returned from
 *                   wpa_dbus_dict_begin_string_array()'s
 *                   iter_array parameter
 * @param elem The string element to be added to the dict entry's string array
 * @return TRUE on success, FALSE on failure
 *
 */
dbus_bool_t wpa_dbus_dict_string_array_add_element(DBusMessageIter *iter_array,
						   const char *elem)
{
	if (!iter_array || !elem)
		return FALSE;

	return dbus_message_iter_append_basic(iter_array, DBUS_TYPE_STRING,
					      &elem);
}


/**
 * Add a single byte array element to a string array dict entry
 *
 * @param iter_array A valid DBusMessageIter returned from
 *                   wpa_dbus_dict_begin_array()'s iter_array
 *                   parameter -- note that wpa_dbus_dict_begin_array()
 *                   must have been called with "ay" as the type
 * @param value The data to be added to the dict entry's array
 * @param value_len The length of the data
 * @return TRUE on success, FALSE on failure
 *
 */
dbus_bool_t wpa_dbus_dict_bin_array_add_element(DBusMessageIter *iter_array,
						const u8 *value,
						size_t value_len)
{
	DBusMessageIter iter_bytes;
	size_t i;

	if (!iter_array || !value ||
	    !dbus_message_iter_open_container(iter_array, DBUS_TYPE_ARRAY,
					      DBUS_TYPE_BYTE_AS_STRING,
					      &iter_bytes))
		return FALSE;

	for (i = 0; i < value_len; i++) {
		if (!dbus_message_iter_append_basic(&iter_bytes,
						    DBUS_TYPE_BYTE,
						    &(value[i])))
			return FALSE;
	}

	return dbus_message_iter_close_container(iter_array, &iter_bytes);
}


/**
 * End an array dict entry
 *
 * @param iter_dict A valid DBusMessageIter returned from
 *                  wpa_dbus_dict_open_write()
 * @param iter_dict_entry A private DBusMessageIter returned from
 *                        wpa_dbus_dict_begin_string_array() or
 *			  wpa_dbus_dict_begin_array()
 * @param iter_dict_val A private DBusMessageIter returned from
 *                      wpa_dbus_dict_begin_string_array() or
 *			wpa_dbus_dict_begin_array()
 * @param iter_array A DBusMessageIter returned from
 *                   wpa_dbus_dict_begin_string_array() or
 *		     wpa_dbus_dict_begin_array()
 * @return TRUE on success, FALSE on failure
 *
 */
dbus_bool_t wpa_dbus_dict_end_array(DBusMessageIter *iter_dict,
				    DBusMessageIter *iter_dict_entry,
				    DBusMessageIter *iter_dict_val,
				    DBusMessageIter *iter_array)
{
	if (!iter_dict || !iter_dict_entry || !iter_dict_val || !iter_array ||
	    !dbus_message_iter_close_container(iter_dict_val, iter_array))
		return FALSE;

	return _wpa_dbus_add_dict_entry_end(iter_dict, iter_dict_entry,
					    iter_dict_val);
}


/**
 * Convenience function to add an entire string array to the dict.
 *
 * @param iter_dict A valid DBusMessageIter returned from
 *                  wpa_dbus_dict_open_write()
 * @param key The key of the dict item
 * @param items The array of strings
 * @param num_items The number of strings in the array
 * @return TRUE on success, FALSE on failure
 *
 */
dbus_bool_t wpa_dbus_dict_append_string_array(DBusMessageIter *iter_dict,
					      const char *key,
					      const char **items,
					      const dbus_uint32_t num_items)
{
	DBusMessageIter iter_dict_entry, iter_dict_val, iter_array;
	dbus_uint32_t i;

	if (!key || (!items && num_items != 0) ||
	    !wpa_dbus_dict_begin_string_array(iter_dict, key,
					      &iter_dict_entry, &iter_dict_val,
					      &iter_array))
		return FALSE;

	for (i = 0; i < num_items; i++) {
		if (!wpa_dbus_dict_string_array_add_element(&iter_array,
							    items[i]))
			return FALSE;
	}

	return wpa_dbus_dict_end_string_array(iter_dict, &iter_dict_entry,
					      &iter_dict_val, &iter_array);
}


/**
 * Convenience function to add an wpabuf binary array to the dict.
 *
 * @param iter_dict A valid DBusMessageIter returned from
 *                  wpa_dbus_dict_open_write()
 * @param key The key of the dict item
 * @param items The array of wpabuf structures
 * @param num_items The number of strings in the array
 * @return TRUE on success, FALSE on failure
 *
 */
dbus_bool_t wpa_dbus_dict_append_wpabuf_array(DBusMessageIter *iter_dict,
					      const char *key,
					      const struct wpabuf **items,
					      const dbus_uint32_t num_items)
{
	DBusMessageIter iter_dict_entry, iter_dict_val, iter_array;
	dbus_uint32_t i;

	if (!key ||
	    (!items && num_items != 0) ||
	    !wpa_dbus_dict_begin_array(iter_dict, key,
				       DBUS_TYPE_ARRAY_AS_STRING
				       DBUS_TYPE_BYTE_AS_STRING,
				       &iter_dict_entry, &iter_dict_val,
				       &iter_array))
		return FALSE;

	for (i = 0; i < num_items; i++) {
		if (!wpa_dbus_dict_bin_array_add_element(&iter_array,
							 wpabuf_head(items[i]),
							 wpabuf_len(items[i])))
			return FALSE;
	}

	return wpa_dbus_dict_end_array(iter_dict, &iter_dict_entry,
				       &iter_dict_val, &iter_array);
}


/*****************************************************/
/* Stuff for reading dicts                           */
/*****************************************************/

/**
 * Start reading from a dbus dict.
 *
 * @param iter A valid DBusMessageIter pointing to the start of the dict
 * @param iter_dict (out) A DBusMessageIter to be passed to
 *    wpa_dbus_dict_read_next_entry()
 * @error on failure a descriptive error
 * @return TRUE on success, FALSE on failure
 *
 */
dbus_bool_t wpa_dbus_dict_open_read(DBusMessageIter *iter,
				    DBusMessageIter *iter_dict,
				    DBusError *error)
{
	int type;

	wpa_printf(MSG_MSGDUMP, "%s: start reading a dict entry", __func__);
	if (!iter || !iter_dict) {
		dbus_set_error_const(error, DBUS_ERROR_FAILED,
				     "[internal] missing message iterators");
		return FALSE;
	}

	type = dbus_message_iter_get_arg_type(iter);
	if (type != DBUS_TYPE_ARRAY ||
	    dbus_message_iter_get_element_type(iter) != DBUS_TYPE_DICT_ENTRY) {
		wpa_printf(MSG_DEBUG,
			   "%s: unexpected message argument types (arg=%c element=%c)",
			   __func__, type,
			   type != DBUS_TYPE_ARRAY ? '?' :
			   dbus_message_iter_get_element_type(iter));
		dbus_set_error_const(error, DBUS_ERROR_INVALID_ARGS,
				     "unexpected message argument types");
		return FALSE;
	}

	dbus_message_iter_recurse(iter, iter_dict);
	return TRUE;
}


#define BYTE_ARRAY_CHUNK_SIZE 34
#define BYTE_ARRAY_ITEM_SIZE (sizeof(char))

static dbus_bool_t _wpa_dbus_dict_entry_get_byte_array(
	DBusMessageIter *iter, struct wpa_dbus_dict_entry *entry)
{
	dbus_uint32_t count = 0;
	dbus_bool_t success = FALSE;
	char *buffer, *nbuffer;

	entry->bytearray_value = NULL;
	entry->array_type = DBUS_TYPE_BYTE;

	buffer = os_calloc(BYTE_ARRAY_CHUNK_SIZE, BYTE_ARRAY_ITEM_SIZE);
	if (!buffer)
		return FALSE;

	entry->array_len = 0;
	while (dbus_message_iter_get_arg_type(iter) == DBUS_TYPE_BYTE) {
		char byte;

		if ((count % BYTE_ARRAY_CHUNK_SIZE) == 0 && count != 0) {
			nbuffer = os_realloc_array(
				buffer, count + BYTE_ARRAY_CHUNK_SIZE,
				BYTE_ARRAY_ITEM_SIZE);
			if (nbuffer == NULL) {
				os_free(buffer);
				wpa_printf(MSG_ERROR,
					   "dbus: %s out of memory trying to retrieve the string array",
					   __func__);
				goto done;
			}
			buffer = nbuffer;
		}

		dbus_message_iter_get_basic(iter, &byte);
		buffer[count] = byte;
		entry->array_len = ++count;
		dbus_message_iter_next(iter);
	}
	entry->bytearray_value = buffer;
	wpa_hexdump_key(MSG_MSGDUMP, "dbus: byte array contents",
			entry->bytearray_value, entry->array_len);

	/* Zero-length arrays are valid. */
	if (entry->array_len == 0) {
		os_free(entry->bytearray_value);
		entry->bytearray_value = NULL;
	}

	success = TRUE;

done:
	return success;
}


#define STR_ARRAY_CHUNK_SIZE 8
#define STR_ARRAY_ITEM_SIZE (sizeof(char *))

static dbus_bool_t _wpa_dbus_dict_entry_get_string_array(
	DBusMessageIter *iter, int array_type,
	struct wpa_dbus_dict_entry *entry)
{
	dbus_uint32_t count = 0;
	char **buffer, **nbuffer;

	entry->strarray_value = NULL;
	entry->array_len = 0;
	entry->array_type = DBUS_TYPE_STRING;

	buffer = os_calloc(STR_ARRAY_CHUNK_SIZE, STR_ARRAY_ITEM_SIZE);
	if (buffer == NULL)
		return FALSE;

	while (dbus_message_iter_get_arg_type(iter) == DBUS_TYPE_STRING) {
		const char *value;
		char *str;

		if ((count % STR_ARRAY_CHUNK_SIZE) == 0 && count != 0) {
			nbuffer = os_realloc_array(
				buffer, count + STR_ARRAY_CHUNK_SIZE,
				STR_ARRAY_ITEM_SIZE);
			if (nbuffer == NULL) {
				wpa_printf(MSG_ERROR,
					   "dbus: %s out of memory trying to retrieve the string array",
					   __func__);
				goto fail;
			}
			buffer = nbuffer;
		}

		dbus_message_iter_get_basic(iter, &value);
		wpa_printf(MSG_MSGDUMP, "%s: string_array value: %s",
			   __func__, wpa_debug_show_keys ? value : "[omitted]");
		str = os_strdup(value);
		if (str == NULL) {
			wpa_printf(MSG_ERROR,
				   "dbus: %s out of memory trying to duplicate the string array",
				   __func__);
			goto fail;
		}
		buffer[count++] = str;
		dbus_message_iter_next(iter);
	}
	entry->strarray_value = buffer;
	entry->array_len = count;
	wpa_printf(MSG_MSGDUMP, "%s: string_array length %u",
		   __func__, entry->array_len);

	/* Zero-length arrays are valid. */
	if (entry->array_len == 0) {
		os_free(entry->strarray_value);
		entry->strarray_value = NULL;
	}

	return TRUE;

fail:
	while (count > 0) {
		count--;
		os_free(buffer[count]);
	}
	os_free(buffer);
	return FALSE;
}


#define BIN_ARRAY_CHUNK_SIZE 10
#define BIN_ARRAY_ITEM_SIZE (sizeof(struct wpabuf *))

static dbus_bool_t _wpa_dbus_dict_entry_get_binarray(
	DBusMessageIter *iter, struct wpa_dbus_dict_entry *entry)
{
	struct wpa_dbus_dict_entry tmpentry;
	size_t buflen = 0;
	int i, type;

	entry->array_type = WPAS_DBUS_TYPE_BINARRAY;
	entry->array_len = 0;
	entry->binarray_value = NULL;

	type = dbus_message_iter_get_arg_type(iter);
	wpa_printf(MSG_MSGDUMP, "%s: parsing binarray type %c", __func__, type);
	if (type == DBUS_TYPE_INVALID) {
		/* Likely an empty array of arrays */
		return TRUE;
	}
	if (type != DBUS_TYPE_ARRAY) {
		wpa_printf(MSG_DEBUG, "%s: not an array type: %c",
			   __func__, type);
		return FALSE;
	}

	type = dbus_message_iter_get_element_type(iter);
	if (type != DBUS_TYPE_BYTE) {
		wpa_printf(MSG_DEBUG, "%s: unexpected element type %c",
			   __func__, type);
		return FALSE;
	}

	while (dbus_message_iter_get_arg_type(iter) == DBUS_TYPE_ARRAY) {
		DBusMessageIter iter_array;

		if (entry->array_len == buflen) {
			struct wpabuf **newbuf;

			buflen += BIN_ARRAY_CHUNK_SIZE;

			newbuf = os_realloc_array(entry->binarray_value,
						  buflen, BIN_ARRAY_ITEM_SIZE);
			if (!newbuf)
				goto cleanup;
			entry->binarray_value = newbuf;
		}

		dbus_message_iter_recurse(iter, &iter_array);
		os_memset(&tmpentry, 0, sizeof(tmpentry));
		tmpentry.type = DBUS_TYPE_ARRAY;
		if (_wpa_dbus_dict_entry_get_byte_array(&iter_array, &tmpentry)
		    == FALSE)
			goto cleanup;

		entry->binarray_value[entry->array_len] =
			wpabuf_alloc_ext_data((u8 *) tmpentry.bytearray_value,
					      tmpentry.array_len);
		if (entry->binarray_value[entry->array_len] == NULL) {
			wpa_dbus_dict_entry_clear(&tmpentry);
			goto cleanup;
		}
		entry->array_len++;
		dbus_message_iter_next(iter);
	}
	wpa_printf(MSG_MSGDUMP, "%s: binarray length %u",
		   __func__, entry->array_len);

	return TRUE;

 cleanup:
	for (i = 0; i < (int) entry->array_len; i++)
		wpabuf_free(entry->binarray_value[i]);
	os_free(entry->binarray_value);
	entry->array_len = 0;
	entry->binarray_value = NULL;
	return FALSE;
}


static dbus_bool_t _wpa_dbus_dict_entry_get_array(
	DBusMessageIter *iter_dict_val, struct wpa_dbus_dict_entry *entry)
{
	int array_type = dbus_message_iter_get_element_type(iter_dict_val);
	dbus_bool_t success = FALSE;
	DBusMessageIter iter_array;

	wpa_printf(MSG_MSGDUMP, "%s: array_type %c", __func__, array_type);

	dbus_message_iter_recurse(iter_dict_val, &iter_array);

	switch (array_type) {
	case DBUS_TYPE_BYTE:
		success = _wpa_dbus_dict_entry_get_byte_array(&iter_array,
							      entry);
		break;
	case DBUS_TYPE_STRING:
		success = _wpa_dbus_dict_entry_get_string_array(&iter_array,
								array_type,
								entry);
		break;
	case DBUS_TYPE_ARRAY:
		success = _wpa_dbus_dict_entry_get_binarray(&iter_array, entry);
		break;
	default:
		wpa_printf(MSG_MSGDUMP, "%s: unsupported array type %c",
			   __func__, array_type);
		break;
	}

	return success;
}


static dbus_bool_t _wpa_dbus_dict_fill_value_from_variant(
	struct wpa_dbus_dict_entry *entry, DBusMessageIter *iter)
{
	const char *v;

	switch (entry->type) {
	case DBUS_TYPE_OBJECT_PATH:
		dbus_message_iter_get_basic(iter, &v);
		wpa_printf(MSG_MSGDUMP, "%s: object path value: %s",
			   __func__, v);
		entry->str_value = os_strdup(v);
		if (entry->str_value == NULL)
			return FALSE;
		break;
	case DBUS_TYPE_STRING:
		dbus_message_iter_get_basic(iter, &v);
		wpa_printf(MSG_MSGDUMP, "%s: string value: %s",
			   __func__, wpa_debug_show_keys ? v : "[omitted]");
		entry->str_value = os_strdup(v);
		if (entry->str_value == NULL)
			return FALSE;
		break;
	case DBUS_TYPE_BOOLEAN:
		dbus_message_iter_get_basic(iter, &entry->bool_value);
		wpa_printf(MSG_MSGDUMP, "%s: boolean value: %d",
			   __func__, entry->bool_value);
		break;
	case DBUS_TYPE_BYTE:
		dbus_message_iter_get_basic(iter, &entry->byte_value);
		wpa_printf(MSG_MSGDUMP, "%s: byte value: %d",
			   __func__, entry->byte_value);
		break;
	case DBUS_TYPE_INT16:
		dbus_message_iter_get_basic(iter, &entry->int16_value);
		wpa_printf(MSG_MSGDUMP, "%s: int16 value: %d",
			   __func__, entry->int16_value);
		break;
	case DBUS_TYPE_UINT16:
		dbus_message_iter_get_basic(iter, &entry->uint16_value);
		wpa_printf(MSG_MSGDUMP, "%s: uint16 value: %d",
			   __func__, entry->uint16_value);
		break;
	case DBUS_TYPE_INT32:
		dbus_message_iter_get_basic(iter, &entry->int32_value);
		wpa_printf(MSG_MSGDUMP, "%s: int32 value: %d",
			   __func__, entry->int32_value);
		break;
	case DBUS_TYPE_UINT32:
		dbus_message_iter_get_basic(iter, &entry->uint32_value);
		wpa_printf(MSG_MSGDUMP, "%s: uint32 value: %d",
			   __func__, entry->uint32_value);
		break;
	case DBUS_TYPE_INT64:
		dbus_message_iter_get_basic(iter, &entry->int64_value);
		wpa_printf(MSG_MSGDUMP, "%s: int64 value: %lld",
			   __func__, (long long int) entry->int64_value);
		break;
	case DBUS_TYPE_UINT64:
		dbus_message_iter_get_basic(iter, &entry->uint64_value);
		wpa_printf(MSG_MSGDUMP, "%s: uint64 value: %llu",
			   __func__,
			   (unsigned long long int) entry->uint64_value);
		break;
	case DBUS_TYPE_DOUBLE:
		dbus_message_iter_get_basic(iter, &entry->double_value);
		wpa_printf(MSG_MSGDUMP, "%s: double value: %f",
			   __func__, entry->double_value);
		break;
	case DBUS_TYPE_ARRAY:
		return _wpa_dbus_dict_entry_get_array(iter, entry);
	default:
		wpa_printf(MSG_MSGDUMP, "%s: unsupported type %c",
			   __func__, entry->type);
		return FALSE;
	}

	return TRUE;
}


/**
 * Read the current key/value entry from the dict.  Entries are dynamically
 * allocated when needed and must be freed after use with the
 * wpa_dbus_dict_entry_clear() function.
 *
 * The returned entry object will be filled with the type and value of the next
 * entry in the dict, or the type will be DBUS_TYPE_INVALID if an error
 * occurred.
 *
 * @param iter_dict A valid DBusMessageIter returned from
 *    wpa_dbus_dict_open_read()
 * @param entry A valid dict entry object into which the dict key and value
 *    will be placed
 * @return TRUE on success, FALSE on failure
 *
 */
dbus_bool_t wpa_dbus_dict_get_entry(DBusMessageIter *iter_dict,
				    struct wpa_dbus_dict_entry * entry)
{
	DBusMessageIter iter_dict_entry, iter_dict_val;
	int type;
	const char *key;

	if (!iter_dict || !entry ||
	    dbus_message_iter_get_arg_type(iter_dict) != DBUS_TYPE_DICT_ENTRY) {
		wpa_printf(MSG_DEBUG, "%s: not a dict entry", __func__);
		goto error;
	}

	dbus_message_iter_recurse(iter_dict, &iter_dict_entry);
	dbus_message_iter_get_basic(&iter_dict_entry, &key);
	wpa_printf(MSG_MSGDUMP, "%s: dict entry key: %s", __func__, key);
	entry->key = key;

	if (!dbus_message_iter_next(&iter_dict_entry)) {
		wpa_printf(MSG_DEBUG, "%s: no variant in dict entry", __func__);
		goto error;
	}
	type = dbus_message_iter_get_arg_type(&iter_dict_entry);
	if (type != DBUS_TYPE_VARIANT) {
		wpa_printf(MSG_DEBUG,
			   "%s: unexpected dict entry variant type: %c",
			   __func__, type);
		goto error;
	}

	dbus_message_iter_recurse(&iter_dict_entry, &iter_dict_val);
	entry->type = dbus_message_iter_get_arg_type(&iter_dict_val);
	wpa_printf(MSG_MSGDUMP, "%s: dict entry variant content type: %c",
		   __func__, entry->type);
	entry->array_type = DBUS_TYPE_INVALID;
	if (!_wpa_dbus_dict_fill_value_from_variant(entry, &iter_dict_val)) {
		wpa_printf(MSG_DEBUG,
			   "%s: failed to fetch dict values from variant",
			   __func__);
		goto error;
	}

	dbus_message_iter_next(iter_dict);
	return TRUE;

error:
	if (entry) {
		wpa_dbus_dict_entry_clear(entry);
		entry->type = DBUS_TYPE_INVALID;
		entry->array_type = DBUS_TYPE_INVALID;
	}

	return FALSE;
}


/**
 * Return whether or not there are additional dictionary entries.
 *
 * @param iter_dict A valid DBusMessageIter returned from
 *    wpa_dbus_dict_open_read()
 * @return TRUE if more dict entries exists, FALSE if no more dict entries
 * exist
 */
dbus_bool_t wpa_dbus_dict_has_dict_entry(DBusMessageIter *iter_dict)
{
	if (!iter_dict)
		return FALSE;
	return dbus_message_iter_get_arg_type(iter_dict) ==
		DBUS_TYPE_DICT_ENTRY;
}


/**
 * Free any memory used by the entry object.
 *
 * @param entry The entry object
 */
void wpa_dbus_dict_entry_clear(struct wpa_dbus_dict_entry *entry)
{
	unsigned int i;

	if (!entry)
		return;
	switch (entry->type) {
	case DBUS_TYPE_OBJECT_PATH:
	case DBUS_TYPE_STRING:
		os_free(entry->str_value);
		break;
	case DBUS_TYPE_ARRAY:
		switch (entry->array_type) {
		case DBUS_TYPE_BYTE:
			os_free(entry->bytearray_value);
			break;
		case DBUS_TYPE_STRING:
			if (!entry->strarray_value)
				break;
			for (i = 0; i < entry->array_len; i++)
				os_free(entry->strarray_value[i]);
			os_free(entry->strarray_value);
			break;
		case WPAS_DBUS_TYPE_BINARRAY:
			for (i = 0; i < entry->array_len; i++)
				wpabuf_free(entry->binarray_value[i]);
			os_free(entry->binarray_value);
			break;
		}
		break;
	}

	os_memset(entry, 0, sizeof(struct wpa_dbus_dict_entry));
}
