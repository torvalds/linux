// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2023 Red Hat
 */

#include "io-factory.h"

#include <linux/atomic.h>
#include <linux/blkdev.h>
#include <linux/err.h>
#include <linux/mount.h>

#include "logger.h"
#include "memory-alloc.h"
#include "numeric.h"

/*
 * The I/O factory object manages access to index storage, which is a contiguous range of blocks on
 * a block device.
 *
 * The factory holds the open device and is responsible for closing it. The factory has methods to
 * make helper structures that can be used to access sections of the index.
 */
struct io_factory {
	struct block_device *bdev;
	atomic_t ref_count;
};

/* The buffered reader allows efficient I/O by reading page-sized segments into a buffer. */
struct buffered_reader {
	struct io_factory *factory;
	struct dm_bufio_client *client;
	struct dm_buffer *buffer;
	sector_t limit;
	sector_t block_number;
	u8 *start;
	u8 *end;
};

#define MAX_READ_AHEAD_BLOCKS 4

/*
 * The buffered writer allows efficient I/O by buffering writes and committing page-sized segments
 * to storage.
 */
struct buffered_writer {
	struct io_factory *factory;
	struct dm_bufio_client *client;
	struct dm_buffer *buffer;
	sector_t limit;
	sector_t block_number;
	u8 *start;
	u8 *end;
	int error;
};

static void uds_get_io_factory(struct io_factory *factory)
{
	atomic_inc(&factory->ref_count);
}

int uds_make_io_factory(struct block_device *bdev, struct io_factory **factory_ptr)
{
	int result;
	struct io_factory *factory;

	result = vdo_allocate(1, struct io_factory, __func__, &factory);
	if (result != VDO_SUCCESS)
		return result;

	factory->bdev = bdev;
	atomic_set_release(&factory->ref_count, 1);

	*factory_ptr = factory;
	return UDS_SUCCESS;
}

int uds_replace_storage(struct io_factory *factory, struct block_device *bdev)
{
	factory->bdev = bdev;
	return UDS_SUCCESS;
}

/* Free an I/O factory once all references have been released. */
void uds_put_io_factory(struct io_factory *factory)
{
	if (atomic_add_return(-1, &factory->ref_count) <= 0)
		vdo_free(factory);
}

size_t uds_get_writable_size(struct io_factory *factory)
{
	return bdev_nr_bytes(factory->bdev);
}

/* Create a struct dm_bufio_client for an index region starting at offset. */
int uds_make_bufio(struct io_factory *factory, off_t block_offset, size_t block_size,
		   unsigned int reserved_buffers, struct dm_bufio_client **client_ptr)
{
	struct dm_bufio_client *client;

	client = dm_bufio_client_create(factory->bdev, block_size, reserved_buffers, 0,
					NULL, NULL, 0);
	if (IS_ERR(client))
		return -PTR_ERR(client);

	dm_bufio_set_sector_offset(client, block_offset * SECTORS_PER_BLOCK);
	*client_ptr = client;
	return UDS_SUCCESS;
}

static void read_ahead(struct buffered_reader *reader, sector_t block_number)
{
	if (block_number < reader->limit) {
		sector_t read_ahead = min((sector_t) MAX_READ_AHEAD_BLOCKS,
					  reader->limit - block_number);

		dm_bufio_prefetch(reader->client, block_number, read_ahead);
	}
}

void uds_free_buffered_reader(struct buffered_reader *reader)
{
	if (reader == NULL)
		return;

	if (reader->buffer != NULL)
		dm_bufio_release(reader->buffer);

	dm_bufio_client_destroy(reader->client);
	uds_put_io_factory(reader->factory);
	vdo_free(reader);
}

/* Create a buffered reader for an index region starting at offset. */
int uds_make_buffered_reader(struct io_factory *factory, off_t offset, u64 block_count,
			     struct buffered_reader **reader_ptr)
{
	int result;
	struct dm_bufio_client *client = NULL;
	struct buffered_reader *reader = NULL;

	result = uds_make_bufio(factory, offset, UDS_BLOCK_SIZE, 1, &client);
	if (result != UDS_SUCCESS)
		return result;

	result = vdo_allocate(1, struct buffered_reader, "buffered reader", &reader);
	if (result != VDO_SUCCESS) {
		dm_bufio_client_destroy(client);
		return result;
	}

	*reader = (struct buffered_reader) {
		.factory = factory,
		.client = client,
		.buffer = NULL,
		.limit = block_count,
		.block_number = 0,
		.start = NULL,
		.end = NULL,
	};

	read_ahead(reader, 0);
	uds_get_io_factory(factory);
	*reader_ptr = reader;
	return UDS_SUCCESS;
}

static int position_reader(struct buffered_reader *reader, sector_t block_number,
			   off_t offset)
{
	struct dm_buffer *buffer = NULL;
	void *data;

	if ((reader->end == NULL) || (block_number != reader->block_number)) {
		if (block_number >= reader->limit)
			return UDS_OUT_OF_RANGE;

		if (reader->buffer != NULL)
			dm_bufio_release(vdo_forget(reader->buffer));

		data = dm_bufio_read(reader->client, block_number, &buffer);
		if (IS_ERR(data))
			return -PTR_ERR(data);

		reader->buffer = buffer;
		reader->start = data;
		if (block_number == reader->block_number + 1)
			read_ahead(reader, block_number + 1);
	}

	reader->block_number = block_number;
	reader->end = reader->start + offset;
	return UDS_SUCCESS;
}

static size_t bytes_remaining_in_read_buffer(struct buffered_reader *reader)
{
	return (reader->end == NULL) ? 0 : reader->start + UDS_BLOCK_SIZE - reader->end;
}

static int reset_reader(struct buffered_reader *reader)
{
	sector_t block_number;

	if (bytes_remaining_in_read_buffer(reader) > 0)
		return UDS_SUCCESS;

	block_number = reader->block_number;
	if (reader->end != NULL)
		block_number++;

	return position_reader(reader, block_number, 0);
}

int uds_read_from_buffered_reader(struct buffered_reader *reader, u8 *data,
				  size_t length)
{
	int result = UDS_SUCCESS;
	size_t chunk_size;

	while (length > 0) {
		result = reset_reader(reader);
		if (result != UDS_SUCCESS)
			return result;

		chunk_size = min(length, bytes_remaining_in_read_buffer(reader));
		memcpy(data, reader->end, chunk_size);
		length -= chunk_size;
		data += chunk_size;
		reader->end += chunk_size;
	}

	return UDS_SUCCESS;
}

/*
 * Verify that the next data on the reader matches the required value. If the value matches, the
 * matching contents are consumed. If the value does not match, the reader state is unchanged.
 */
int uds_verify_buffered_data(struct buffered_reader *reader, const u8 *value,
			     size_t length)
{
	int result = UDS_SUCCESS;
	size_t chunk_size;
	sector_t start_block_number = reader->block_number;
	int start_offset = reader->end - reader->start;

	while (length > 0) {
		result = reset_reader(reader);
		if (result != UDS_SUCCESS) {
			result = UDS_CORRUPT_DATA;
			break;
		}

		chunk_size = min(length, bytes_remaining_in_read_buffer(reader));
		if (memcmp(value, reader->end, chunk_size) != 0) {
			result = UDS_CORRUPT_DATA;
			break;
		}

		length -= chunk_size;
		value += chunk_size;
		reader->end += chunk_size;
	}

	if (result != UDS_SUCCESS)
		position_reader(reader, start_block_number, start_offset);

	return result;
}

/* Create a buffered writer for an index region starting at offset. */
int uds_make_buffered_writer(struct io_factory *factory, off_t offset, u64 block_count,
			     struct buffered_writer **writer_ptr)
{
	int result;
	struct dm_bufio_client *client = NULL;
	struct buffered_writer *writer;

	result = uds_make_bufio(factory, offset, UDS_BLOCK_SIZE, 1, &client);
	if (result != UDS_SUCCESS)
		return result;

	result = vdo_allocate(1, struct buffered_writer, "buffered writer", &writer);
	if (result != VDO_SUCCESS) {
		dm_bufio_client_destroy(client);
		return result;
	}

	*writer = (struct buffered_writer) {
		.factory = factory,
		.client = client,
		.buffer = NULL,
		.limit = block_count,
		.start = NULL,
		.end = NULL,
		.block_number = 0,
		.error = UDS_SUCCESS,
	};

	uds_get_io_factory(factory);
	*writer_ptr = writer;
	return UDS_SUCCESS;
}

static size_t get_remaining_write_space(struct buffered_writer *writer)
{
	return writer->start + UDS_BLOCK_SIZE - writer->end;
}

static int __must_check prepare_next_buffer(struct buffered_writer *writer)
{
	struct dm_buffer *buffer = NULL;
	void *data;

	if (writer->block_number >= writer->limit) {
		writer->error = UDS_OUT_OF_RANGE;
		return UDS_OUT_OF_RANGE;
	}

	data = dm_bufio_new(writer->client, writer->block_number, &buffer);
	if (IS_ERR(data)) {
		writer->error = -PTR_ERR(data);
		return writer->error;
	}

	writer->buffer = buffer;
	writer->start = data;
	writer->end = data;
	return UDS_SUCCESS;
}

static int flush_previous_buffer(struct buffered_writer *writer)
{
	size_t available;

	if (writer->buffer == NULL)
		return writer->error;

	if (writer->error == UDS_SUCCESS) {
		available = get_remaining_write_space(writer);

		if (available > 0)
			memset(writer->end, 0, available);

		dm_bufio_mark_buffer_dirty(writer->buffer);
	}

	dm_bufio_release(writer->buffer);
	writer->buffer = NULL;
	writer->start = NULL;
	writer->end = NULL;
	writer->block_number++;
	return writer->error;
}

void uds_free_buffered_writer(struct buffered_writer *writer)
{
	int result;

	if (writer == NULL)
		return;

	flush_previous_buffer(writer);
	result = -dm_bufio_write_dirty_buffers(writer->client);
	if (result != UDS_SUCCESS)
		vdo_log_warning_strerror(result, "%s: failed to sync storage", __func__);

	dm_bufio_client_destroy(writer->client);
	uds_put_io_factory(writer->factory);
	vdo_free(writer);
}

/*
 * Append data to the buffer, writing as needed. If no data is provided, zeros are written instead.
 * If a write error occurs, it is recorded and returned on every subsequent write attempt.
 */
int uds_write_to_buffered_writer(struct buffered_writer *writer, const u8 *data,
				 size_t length)
{
	int result = writer->error;
	size_t chunk_size;

	while ((length > 0) && (result == UDS_SUCCESS)) {
		if (writer->buffer == NULL) {
			result = prepare_next_buffer(writer);
			continue;
		}

		chunk_size = min(length, get_remaining_write_space(writer));
		if (data == NULL) {
			memset(writer->end, 0, chunk_size);
		} else {
			memcpy(writer->end, data, chunk_size);
			data += chunk_size;
		}

		length -= chunk_size;
		writer->end += chunk_size;

		if (get_remaining_write_space(writer) == 0)
			result = uds_flush_buffered_writer(writer);
	}

	return result;
}

int uds_flush_buffered_writer(struct buffered_writer *writer)
{
	if (writer->error != UDS_SUCCESS)
		return writer->error;

	return flush_previous_buffer(writer);
}
