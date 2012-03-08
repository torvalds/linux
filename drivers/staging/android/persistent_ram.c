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
#include <linux/slab.h>
#include "persistent_ram.h"

#ifdef CONFIG_ANDROID_RAM_CONSOLE_ERROR_CORRECTION
#include <linux/rslib.h>
#endif

struct persistent_ram_buffer {
	uint32_t    sig;
	uint32_t    start;
	uint32_t    size;
	uint8_t     data[0];
};

#define PERSISTENT_RAM_SIG (0x43474244) /* DBGC */

static LIST_HEAD(zone_list);

#define ECC_BLOCK_SIZE CONFIG_ANDROID_RAM_CONSOLE_ERROR_CORRECTION_DATA_SIZE
#define ECC_SIZE CONFIG_ANDROID_RAM_CONSOLE_ERROR_CORRECTION_ECC_SIZE
#define ECC_SYMSIZE CONFIG_ANDROID_RAM_CONSOLE_ERROR_CORRECTION_SYMBOL_SIZE
#define ECC_POLY CONFIG_ANDROID_RAM_CONSOLE_ERROR_CORRECTION_POLYNOMIAL

#ifdef CONFIG_ANDROID_RAM_CONSOLE_ERROR_CORRECTION
static void persistent_ram_encode_rs8(struct persistent_ram_zone *prz,
	uint8_t *data, size_t len, uint8_t *ecc)
{
	int i;
	uint16_t par[ECC_SIZE];
	/* Initialize the parity buffer */
	memset(par, 0, sizeof(par));
	encode_rs8(prz->rs_decoder, data, len, par, 0);
	for (i = 0; i < ECC_SIZE; i++)
		ecc[i] = par[i];
}

static int persistent_ram_decode_rs8(struct persistent_ram_zone *prz,
	void *data, size_t len, uint8_t *ecc)
{
	int i;
	uint16_t par[ECC_SIZE];
	for (i = 0; i < ECC_SIZE; i++)
		par[i] = ecc[i];
	return decode_rs8(prz->rs_decoder, data, par, len,
				NULL, 0, NULL, 0, NULL);
}
#endif

static void persistent_ram_update(struct persistent_ram_zone *prz,
	const void *s, unsigned int count)
{
	struct persistent_ram_buffer *buffer = prz->buffer;
#ifdef CONFIG_ANDROID_RAM_CONSOLE_ERROR_CORRECTION
	uint8_t *buffer_end = buffer->data + prz->buffer_size;
	uint8_t *block;
	uint8_t *par;
	int size = ECC_BLOCK_SIZE;
#endif
	memcpy(buffer->data + buffer->start, s, count);
#ifdef CONFIG_ANDROID_RAM_CONSOLE_ERROR_CORRECTION
	block = buffer->data + (buffer->start & ~(ECC_BLOCK_SIZE - 1));
	par = prz->par_buffer +
	      (buffer->start / ECC_BLOCK_SIZE) * ECC_SIZE;
	do {
		if (block + ECC_BLOCK_SIZE > buffer_end)
			size = buffer_end - block;
		persistent_ram_encode_rs8(prz, block, size, par);
		block += ECC_BLOCK_SIZE;
		par += ECC_SIZE;
	} while (block < buffer->data + buffer->start + count);
#endif
}

static void persistent_ram_update_header(struct persistent_ram_zone *prz)
{
#ifdef CONFIG_ANDROID_RAM_CONSOLE_ERROR_CORRECTION
	struct persistent_ram_buffer *buffer = prz->buffer;

	persistent_ram_encode_rs8(prz, (uint8_t *)buffer, sizeof(*buffer),
				  prz->par_header);
#endif
}

static void __init
persistent_ram_save_old(struct persistent_ram_zone *prz)
{
	struct persistent_ram_buffer *buffer = prz->buffer;
	size_t old_log_size = buffer->size;
	char *dest;
#ifdef CONFIG_ANDROID_RAM_CONSOLE_ERROR_CORRECTION
	uint8_t *block;
	uint8_t *par;

	block = buffer->data;
	par = prz->par_buffer;
	while (block < buffer->data + buffer->size) {
		int numerr;
		int size = ECC_BLOCK_SIZE;
		if (block + size > buffer->data + prz->buffer_size)
			size = buffer->data + prz->buffer_size - block;
		numerr = persistent_ram_decode_rs8(prz, block, size, par);
		if (numerr > 0) {
#if 0
			pr_info("persistent_ram: error in block %p, %d\n",
			       block, numerr);
#endif
			prz->corrected_bytes += numerr;
		} else if (numerr < 0) {
#if 0
			pr_info("persistent_ram: uncorrectable error in block %p\n",
				block);
#endif
			prz->bad_blocks++;
		}
		block += ECC_BLOCK_SIZE;
		par += ECC_SIZE;
	}
#endif

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
	persistent_ram_update_header(prz);

	return count;
}

ssize_t persistent_ram_ecc_string(struct persistent_ram_zone *prz,
	char *str, size_t len)
{
#ifdef CONFIG_ANDROID_RAM_CONSOLE_ERROR_CORRECTION
	ssize_t ret;

	if (prz->corrected_bytes || prz->bad_blocks)
		ret = snprintf(str, len, ""
			"\n%d Corrected bytes, %d unrecoverable blocks\n",
			prz->corrected_bytes, prz->bad_blocks);
	else
		ret = snprintf(str, len, "\nNo errors detected\n");

	return ret;
#else
	return 0;
#endif
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
			       void __iomem *mem, size_t buffer_size)
{
	struct persistent_ram_buffer *buffer = mem;
#ifdef CONFIG_ANDROID_RAM_CONSOLE_ERROR_CORRECTION
	int numerr;
#endif

	INIT_LIST_HEAD(&prz->node);

	prz->buffer = buffer;
	prz->buffer_size = buffer_size - sizeof(struct persistent_ram_buffer);

	if (prz->buffer_size > buffer_size) {
		pr_err("persistent_ram: buffer %p, invalid size %zu, datasize %zu\n",
		       buffer, buffer_size, prz->buffer_size);
		return -EINVAL;
	}

#ifdef CONFIG_ANDROID_RAM_CONSOLE_ERROR_CORRECTION
	prz->buffer_size -= (DIV_ROUND_UP(prz->buffer_size,
						ECC_BLOCK_SIZE) + 1) * ECC_SIZE;

	if (prz->buffer_size > buffer_size) {
		pr_err("persistent_ram: buffer %p, invalid size %zu, non-ecc datasize %zu\n",
		       buffer, buffer_size, prz->buffer_size);
		return -EINVAL;
	}

	prz->par_buffer = buffer->data + prz->buffer_size;
	prz->par_header = prz->par_buffer +
		DIV_ROUND_UP(prz->buffer_size, ECC_BLOCK_SIZE) * ECC_SIZE;


	/* first consecutive root is 0
	 * primitive element to generate roots = 1
	 */
	prz->rs_decoder = init_rs(ECC_SYMSIZE, ECC_POLY, 0, 1, ECC_SIZE);
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
#endif

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
			       void __iomem *mem, size_t buffer_size)
{
	return __persistent_ram_init(prz, mem, buffer_size);
}
