/*
 * buffer.c -- generic memory buffer .
 *
 * Copyright (c) 2001-2008, NLnet Labs. All rights reserved.
 *
 * See LICENSE for the license.
 *
 */
/**
 * \file
 *
 * This file contains the definition of sldns_buffer, and functions to manipulate those.
 */
#include "config.h"
#include "sldns/sbuffer.h"
#include <stdarg.h>

sldns_buffer *
sldns_buffer_new(size_t capacity)
{
	sldns_buffer *buffer = (sldns_buffer*)malloc(sizeof(sldns_buffer));

	if (!buffer) {
		return NULL;
	}
	
	buffer->_data = (uint8_t *) malloc(capacity);
	if (!buffer->_data) {
		free(buffer);
		return NULL;
	}
	
	buffer->_position = 0;
	buffer->_limit = buffer->_capacity = capacity;
	buffer->_fixed = 0;
	buffer->_vfixed = 0;
	buffer->_status_err = 0;
	
	sldns_buffer_invariant(buffer);
	
	return buffer;
}

void
sldns_buffer_new_frm_data(sldns_buffer *buffer, void *data, size_t size)
{
	assert(data != NULL);

	buffer->_position = 0; 
	buffer->_limit = buffer->_capacity = size;
	buffer->_fixed = 0;
	buffer->_vfixed = 0;
	if (!buffer->_fixed && buffer->_data)
		free(buffer->_data);
	buffer->_data = malloc(size);
	if(!buffer->_data) {
		buffer->_status_err = 1;
		return;
	}
	memcpy(buffer->_data, data, size);
	buffer->_status_err = 0;
	
	sldns_buffer_invariant(buffer);
}

void
sldns_buffer_init_frm_data(sldns_buffer *buffer, void *data, size_t size)
{
	memset(buffer, 0, sizeof(*buffer));
	buffer->_data = data;
	buffer->_capacity = buffer->_limit = size;
	buffer->_fixed = 1;
	buffer->_vfixed = 0;
}

void
sldns_buffer_init_vfixed_frm_data(sldns_buffer *buffer, void *data, size_t size)
{
	memset(buffer, 0, sizeof(*buffer));
	buffer->_data = data;
	buffer->_capacity = buffer->_limit = size;
	buffer->_fixed = 1;
	buffer->_vfixed = 1;
}

int
sldns_buffer_set_capacity(sldns_buffer *buffer, size_t capacity)
{
	void *data;
	
	sldns_buffer_invariant(buffer);
	assert(buffer->_position <= capacity && !buffer->_fixed);

	data = (uint8_t *) realloc(buffer->_data, capacity);
	if (!data) {
		buffer->_status_err = 1;
		return 0;
	} else {
		buffer->_data = data;
		buffer->_limit = buffer->_capacity = capacity;
		return 1;
	}
}

int
sldns_buffer_reserve(sldns_buffer *buffer, size_t amount)
{
	sldns_buffer_invariant(buffer);
	assert(!buffer->_fixed);
	if (buffer->_capacity < buffer->_position + amount) {
		size_t new_capacity = buffer->_capacity * 3 / 2;

		if (new_capacity < buffer->_position + amount) {
			new_capacity = buffer->_position + amount;
		}
		if (!sldns_buffer_set_capacity(buffer, new_capacity)) {
			buffer->_status_err = 1;
			return 0;
		}
	}
	buffer->_limit = buffer->_capacity;
	return 1;
}

int
sldns_buffer_printf(sldns_buffer *buffer, const char *format, ...)
{
	va_list args;
	int written = 0;
	size_t remaining;
	
	if (sldns_buffer_status_ok(buffer)) {
		sldns_buffer_invariant(buffer);
		assert(buffer->_limit == buffer->_capacity);

		remaining = sldns_buffer_remaining(buffer);
		va_start(args, format);
		written = vsnprintf((char *) sldns_buffer_current(buffer), remaining,
				    format, args);
		va_end(args);
		if (written == -1) {
			buffer->_status_err = 1;
			return -1;
		} else if (!buffer->_vfixed && (size_t) written >= remaining) {
			if (!sldns_buffer_reserve(buffer, (size_t) written + 1)) {
				buffer->_status_err = 1;
				return -1;
			}
			va_start(args, format);
			written = vsnprintf((char *) sldns_buffer_current(buffer),
			    sldns_buffer_remaining(buffer), format, args);
			va_end(args);
			if (written == -1) {
				buffer->_status_err = 1;
				return -1;
			}
		}
		buffer->_position += written;
	}
	return written;
}

void
sldns_buffer_free(sldns_buffer *buffer)
{
	if (!buffer) {
		return;
	}

	if (!buffer->_fixed)
		free(buffer->_data);

	free(buffer);
}

void *
sldns_buffer_export(sldns_buffer *buffer)
{
	buffer->_fixed = 1;
	return buffer->_data;
}

void 
sldns_buffer_copy(sldns_buffer* result, sldns_buffer* from)
{
	size_t tocopy = sldns_buffer_limit(from);

	if(tocopy > sldns_buffer_capacity(result))
		tocopy = sldns_buffer_capacity(result);
	sldns_buffer_clear(result);
	sldns_buffer_write(result, sldns_buffer_begin(from), tocopy);
	sldns_buffer_flip(result);
}
