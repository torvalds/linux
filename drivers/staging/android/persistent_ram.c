/*
 * Copyright (C) 2012 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/rslib.h>
#include <linux/slab.h>
#include "persistent_ram.h"

struct persistent_ram_buffer {
	uint32_t    sig;
	uint32_t    start;
	uint32_t    size;
	uint8_t     data[0];
};

#define PERSISTENT_RAM_SIG (0x43474244) /* DBGC */

static LIST_HEAD(zone_list);

static void persistent_ram_encode_rs8(struct persistent_ram_zone *prz,
	uint8_t *data, size_t len, uint8_t *ecc)
{
	int i;
	uint16_t par[prz->ecc_size];

	/* Initialize the parity buffer */
	memset(par, 0, sizeof(par));
	encode_rs8(prz->rs_decoder, data, len, par, 0);
	for (i = 0; i < prz->ecc_size; i++)
		ecc[i] = par[i];
}

static int persistent_ram_decode_rs8(struct persistent_ram_zone *prz,
	void *data, size_t len, uint8_t *ecc)
{
	int i;
	uint16_t par[prz->ecc_size];

	for (i = 0; i < prz->ecc_size; i++)
		par[i] = ecc[i];
	return decode_rs8(prz->rs_decoder, data, par, len,
				NULL, 0, NULL, 0, NULL);
}

static void persistent_ram_update_ecc(struct persistent_ram_zone *prz,
	unsigned int count)
{
	struct persistent_ram_buffer *buffer = prz->buffer;
	uint8_t *buffer_end = buffer->data + prz->buffer_size;
	uint8_t *block;
	uint8_t *par;
	int ecc_block_size = prz->ecc_block_size;
	int ecc_size = prz->ecc_size;
	int size = prz->ecc_block_size;

	if (!prz->ecc)
		return;

	block = buffer->data + (buffer->start & ~(ecc_block_size - 1));
	par = prz->par_buffer +
	      (buffer->start / ecc_block_size) * prz->ecc_size;
	do {
		if (block + ecc_block_size > buffer_end)
			size = buffer_end - block;
		persistent_ram_encode_rs8(prz, block, size, par);
		block += ecc_block_size;
		par += ecc_size;
	} while (block < buffer->data + buffer->start + count);
}

static void persistent_ram_update_header_ecc(struct persistent_ram_zone *prz)
{
	struct persistent_ram_buffer *buffer = prz->buffer;

	if (!prz->ecc)
		return;

	persistent_ram_encode_rs8(prz, (uint8_t *)buffer, sizeof(*buffer),
				  prz->par_header);
}

static void persistent_ram_ecc_old(struct persistent_ram_zone *prz)
{
	struct persistent_ram_buffer *buffer = prz->buffer;
	uint8_t *block;
	uint8_t *par;

	if (!prz->ecc)
		return;

	block = buffer->data;
	par = prz->par_buffer;
	while (block < buffer->data + buffer->size) {
		int numerr;
		int size = prz->ecc_block_size;
		if (block + size > buffer->data + prz->buffer_size)
			size = buffer->data + prz->buffer_size - block;
		numerr = persistent_ram_decode_rs8(prz, block, size, par);
		if (numerr > 0) {
			pr_devel("persistent_ram: error in block %p, %d\n",
			       block, numerr);
			prz->corrected_bytes += numerr;
		} else if (numerr < 0) {
			pr_devel("persistent_ram: uncorrectable error in block %p\n",
				block);
			prz->bad_blocks++;
		}
		block += prz->ecc_block_size;
		par += prz->ecc_size;
	}
}

static int persistent_ram_init_ecc(struct persistent_ram_zone *prz,
	size_t buffer_size)
{
	int numerr;
	struct persistent_ram_buffer *buffer = prz->buffer;
	int ecc_blocks;

	if (!prz->ecc)
		return 0;

	prz->ecc_block_size = 128;
	prz->ecc_size = 16;
	prz->ecc_symsize = 8;
	prz->ecc_poly = 0x11d;

	ecc_blocks = DIV_ROUND_UP(prz->buffer_size, prz->ecc_block_size);
	prz->buffer_size -= (ecc_blocks + 1) * prz->ecc_size;

	if (prz->buffer_size > buffer_size) {
		pr_err("persistent_ram: invalid size %zu, non-ecc datasize %zu\n",
		       buffer_size, prz->buffer_size);
		return -EINVAL;
	}

	prz->par_buffer = buffer->data + prz->buffer_size;
	prz->par_header = prz->par_buffer + ecc_blocks * prz->ecc_size;

	/*
	 * first consecutive root is 0
	 * primitive element to generate roots = 1
	 */
	prz->rs_decoder = init_rs(prz->ecc_symsize, prz->ecc_poly, 0, 1,
				  prz->ecc_size);
	if (prz->rs_decoder == NULL) {
		pr_info("persistent_ram: init_rs failed\n");
		return -EINVAL;
	}

	prz->corrected_bytes = 0;
	prz->bad_blocks = 0;

	numerr = persistent_ram_decode_rs8(prz, buffer, sizeof(*buffer),
					   prz->par_header);
	if (numerr > 0) {
		pr_info("persistent_ram: error in header, %d\n", numerr);
		prz->corrected_bytes += numerr;
	} else if (numerr < 0) {
		pr_info("persistent_ram: uncorrectable error in header\n");
		prz->bad_blocks++;
	}

	return 0;
}

ssize_t persistent_ram_ecc_string(struct persistent_ram_zone *prz,
	char *str, size_t len)
{
	ssize_t ret;

	if (prz->corrected_bytes || prz->bad_blocks)
		ret = snprintf(str, len, ""
			"\n%d Corrected bytes, %d unrecoverable blocks\n",
			prz->corrected_bytes, prz->bad_blocks);
	else
		ret = snprintf(str, len, "\nNo errors detected\n");

	return ret;
}

static void persistent_ram_update(struct persistent_ram_zone *prz,
	const void *s, unsigned int count)
{
	struct persistent_ram_buffer *buffer = prz->buffer;
	memcpy(buffer->data + buffer->start, s, count);
	persistent_ram_update_ecc(prz, count);
}

static void __init
persistent_ram_save_old(struct persistent_ram_zone *prz)
{
	struct persistent_ram_buffer *buffer = prz->buffer;
	size_t old_log_size = buffer->size;
	char *dest;

	persistent_ram_ecc_old(prz);

	dest = kmalloc(old_log_size, GFP_KERNEL);
	if (dest == NULL) {
		pr_err("persistent_ram: failed to allocate buffer\n");
		return;
	}

	prz->old_log = dest;
	prz->old_log_size = old_log_size;
	memcpy(prz->old_log,
	       &buffer->data[buffer->start], buffer->size - buffer->start);
	memcpy(prz->old_log + buffer->size - buffer->start,
	       &buffer->data[0], buffer->start);
}

int persistent_ram_write(struct persistent_ram_zone *prz,
	const void *s, unsigned int count)
{
	int rem;
	int c = count;
	struct persistent_ram_buffer *buffer = prz->buffer;

	if (c > prz->buffer_size) {
		s += c - prz->buffer_size;
		c = prz->buffer_size;
	}
	rem = prz->buffer_size - buffer->start;
	if (rem < c) {
		persistent_ram_update(prz, s, rem);
		s += rem;
		c -= rem;
		buffer->start = 0;
		buffer->size = prz->buffer_size;
	}
	persistent_ram_update(prz, s, c);

	buffer->start += c;
	if (buffer->size < prz->buffer_size)
		buffer->size += c;
	persistent_ram_update_header_ecc(prz);

	return count;
}

size_t persistent_ram_old_size(struct persistent_ram_zone *prz)
{
	return prz->old_log_size;
}

void *persistent_ram_old(struct persistent_ram_zone *prz)
{
	return prz->old_log;
}

void persistent_ram_free_old(struct persistent_ram_zone *prz)
{
	kfree(prz->old_log);
	prz->old_log = NULL;
	prz->old_log_size = 0;
}

static int __init __persistent_ram_init(struct persistent_ram_zone *prz,
			       void __iomem *mem, size_t buffer_size, bool ecc)
{
	struct persistent_ram_buffer *buffer = mem;
	int ret;

	INIT_LIST_HEAD(&prz->node);

	prz->buffer = buffer;
	prz->buffer_size = buffer_size - sizeof(struct persistent_ram_buffer);

	if (prz->buffer_size > buffer_size) {
		pr_err("persistent_ram: buffer %p, invalid size %zu, datasize %zu\n",
		       buffer, buffer_size, prz->buffer_size);
		return -EINVAL;
	}

	prz->ecc = ecc;
	ret = persistent_ram_init_ecc(prz, buffer_size);
	if (ret)
		return ret;

	if (buffer->sig == PERSISTENT_RAM_SIG) {
		if (buffer->size > prz->buffer_size
		    || buffer->start > buffer->size)
			pr_info("persistent_ram: found existing invalid buffer, size %d, start %d\n",
			       buffer->size, buffer->start);
		else {
			pr_info("persistent_ram: found existing buffer, size %d, start %d\n",
			       buffer->size, buffer->start);
			persistent_ram_save_old(prz);
		}
	} else {
		pr_info("persistent_ram: no valid data in buffer (sig = 0x%08x)\n",
			buffer->sig);
	}

	buffer->sig = PERSISTENT_RAM_SIG;
	buffer->start = 0;
	buffer->size = 0;

	list_add_tail(&prz->node, &zone_list);

	return 0;
}

int __init persistent_ram_init_ringbuffer(struct persistent_ram_zone *prz,
			       void __iomem *mem, size_t buffer_size, bool ecc)
{
	return __persistent_ram_init(prz, mem, buffer_size, true);
}
