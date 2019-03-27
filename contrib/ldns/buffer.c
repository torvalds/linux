/*
 * buffer.c -- generic memory buffer .
 *
 * Copyright (c) 2001-2008, NLnet Labs. All rights reserved.
 *
 * See LICENSE for the license.
 *
 */

#include <ldns/config.h>

#include <ldns/ldns.h>
#include <ldns/buffer.h>

ldns_buffer *
ldns_buffer_new(size_t capacity)
{
	ldns_buffer *buffer = LDNS_MALLOC(ldns_buffer);

	if (!buffer) {
		return NULL;
	}
	
	buffer->_data = (uint8_t *) LDNS_XMALLOC(uint8_t, capacity);
	if (!buffer->_data) {
		LDNS_FREE(buffer);
		return NULL;
	}
	
	buffer->_position = 0;
	buffer->_limit = buffer->_capacity = capacity;
	buffer->_fixed = 0;
	buffer->_status = LDNS_STATUS_OK;
	
	ldns_buffer_invariant(buffer);
	
	return buffer;
}

void
ldns_buffer_new_frm_data(ldns_buffer *buffer, const void *data, size_t size)
{
	assert(data != NULL);

	buffer->_position = 0; 
	buffer->_limit = buffer->_capacity = size;
	buffer->_fixed = 0;
	buffer->_data = LDNS_XMALLOC(uint8_t, size);
	if(!buffer->_data) {
		buffer->_status = LDNS_STATUS_MEM_ERR;
		return;
	}
	memcpy(buffer->_data, data, size);
	buffer->_status = LDNS_STATUS_OK;
	
	ldns_buffer_invariant(buffer);
}

bool
ldns_buffer_set_capacity(ldns_buffer *buffer, size_t capacity)
{
	void *data;
	
	ldns_buffer_invariant(buffer);
	assert(buffer->_position <= capacity);

	data = (uint8_t *) LDNS_XREALLOC(buffer->_data, uint8_t, capacity);
	if (!data) {
		buffer->_status = LDNS_STATUS_MEM_ERR;
		return false;
	} else {
		buffer->_data = data;
		buffer->_limit = buffer->_capacity = capacity;
		return true;
	}
}

bool
ldns_buffer_reserve(ldns_buffer *buffer, size_t amount)
{
	ldns_buffer_invariant(buffer);
	assert(!buffer->_fixed);
	if (buffer->_capacity < buffer->_position + amount) {
		size_t new_capacity = buffer->_capacity * 3 / 2;

		if (new_capacity < buffer->_position + amount) {
			new_capacity = buffer->_position + amount;
		}
		if (!ldns_buffer_set_capacity(buffer, new_capacity)) {
			buffer->_status = LDNS_STATUS_MEM_ERR;
			return false;
		}
	}
	buffer->_limit = buffer->_capacity;
	return true;
}

int
ldns_buffer_printf(ldns_buffer *buffer, const char *format, ...)
{
	va_list args;
	int written = 0;
	size_t remaining;
	
	if (ldns_buffer_status_ok(buffer)) {
		ldns_buffer_invariant(buffer);
		assert(buffer->_limit == buffer->_capacity);

		remaining = ldns_buffer_remaining(buffer);
		va_start(args, format);
		written = vsnprintf((char *) ldns_buffer_current(buffer), remaining,
				    format, args);
		va_end(args);
		if (written == -1) {
			buffer->_status = LDNS_STATUS_INTERNAL_ERR;
			return -1;
		} else if ((size_t) written >= remaining) {
			if (!ldns_buffer_reserve(buffer, (size_t) written + 1)) {
				buffer->_status = LDNS_STATUS_MEM_ERR;
				return -1;
			}
			va_start(args, format);
			written = vsnprintf((char *) ldns_buffer_current(buffer),
			    ldns_buffer_remaining(buffer), format, args);
			va_end(args);
			if (written == -1) {
				buffer->_status = LDNS_STATUS_INTERNAL_ERR;
				return -1;
			}
		}
		buffer->_position += written;
	}
	return written;
}

void
ldns_buffer_free(ldns_buffer *buffer)
{
	if (!buffer) {
		return;
	}

	if (!buffer->_fixed)
		LDNS_FREE(buffer->_data);

	LDNS_FREE(buffer);
}

void *
ldns_buffer_export(ldns_buffer *buffer)
{
	buffer->_fixed = 1;
	return buffer->_data;
}

int
ldns_bgetc(ldns_buffer *buffer)
{
	if (!ldns_buffer_available_at(buffer, buffer->_position, sizeof(uint8_t))) {
		ldns_buffer_set_position(buffer, ldns_buffer_limit(buffer));
		/* ldns_buffer_rewind(buffer);*/
		return EOF;
	}
	return (int)ldns_buffer_read_u8(buffer);
}

void 
ldns_buffer_copy(ldns_buffer* result, const ldns_buffer* from)
{
	size_t tocopy = ldns_buffer_limit(from);

	if(tocopy > ldns_buffer_capacity(result))
		tocopy = ldns_buffer_capacity(result);
	ldns_buffer_clear(result);
	ldns_buffer_write(result, ldns_buffer_begin(from), tocopy);
	ldns_buffer_flip(result);
}
