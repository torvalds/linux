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

#include <linux/device.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/list.h>
#include <linux/memblock.h>
#include <linux/rslib.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <asm/page.h>
#include "persistent_ram.h"

struct persistent_ram_buffer {
	uint32_t    sig;
	atomic_t    start;
	atomic_t    size;
	uint8_t     data[0];
};

#define PERSISTENT_RAM_SIG (0x43474244) /* DBGC */

static __initdata LIST_HEAD(persistent_ram_list);

static inline size_t buffer_size(struct persistent_ram_zone *prz)
{
	return atomic_read(&prz->buffer->size);
}

static inline size_t buffer_start(struct persistent_ram_zone *prz)
{
	return atomic_read(&prz->buffer->start);
}

/* increase and wrap the start pointer, returning the old value */
static inline size_t buffer_start_add(struct persistent_ram_zone *prz, size_t a)
{
	int old;
	int new;

	do {
		old = atomic_read(&prz->buffer->start);
		new = old + a;
		while (unlikely(new > prz->buffer_size))
			new -= prz->buffer_size;
	} while (atomic_cmpxchg(&prz->buffer->start, old, new) != old);

	return old;
}

/* increase the size counter until it hits the max size */
static inline void buffer_size_add(struct persistent_ram_zone *prz, size_t a)
{
	size_t old;
	size_t new;

	if (atomic_read(&prz->buffer->size) == prz->buffer_size)
		return;

	do {
		old = atomic_read(&prz->buffer->size);
		new = old + a;
		if (new > prz->buffer_size)
			new = prz->buffer_size;
	} while (atomic_cmpxchg(&prz->buffer->size, old, new) != old);
}

static void notrace persistent_ram_encode_rs8(struct persistent_ram_zone *prz,
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

static void notrace persistent_ram_update_ecc(struct persistent_ram_zone *prz,
	unsigned int start, unsigned int count)
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

	block = buffer->data + (start & ~(ecc_block_size - 1));
	par = prz->par_buffer + (start / ecc_block_size) * prz->ecc_size;

	do {
		if (block + ecc_block_size > buffer_end)
			size = buffer_end - block;
		persistent_ram_encode_rs8(prz, block, size, par);
		block += ecc_block_size;
		par += ecc_size;
	} while (block < buffer->data + start + count);
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
	while (block < buffer->data + buffer_size(prz)) {
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

static void notrace persistent_ram_update(struct persistent_ram_zone *prz,
	const void *s, unsigned int start, unsigned int count)
{
	struct persistent_ram_buffer *buffer = prz->buffer;
	memcpy(buffer->data + start, s, count);
	persistent_ram_update_ecc(prz, start, count);
}

static void __init
persistent_ram_save_old(struct persistent_ram_zone *prz)
{
	struct persistent_ram_buffer *buffer = prz->buffer;
	size_t size = buffer_size(prz);
	size_t start = buffer_start(prz);
	char *dest;

	persistent_ram_ecc_old(prz);

	dest = kmalloc(size, GFP_KERNEL);
	if (dest == NULL) {
		pr_err("persistent_ram: failed to allocate buffer\n");
		return;
	}

	prz->old_log = dest;
	prz->old_log_size = size;
	memcpy(prz->old_log, &buffer->data[start], size - start);
	memcpy(prz->old_log + size - start, &buffer->data[0], start);
}

int notrace persistent_ram_write(struct persistent_ram_zone *prz,
	const void *s, unsigned int count)
{
	int rem;
	int c = count;
	size_t start;

	if (unlikely(c > prz->buffer_size)) {
		s += c - prz->buffer_size;
		c = prz->buffer_size;
	}

	buffer_size_add(prz, c);

	start = buffer_start_add(prz, c);

	rem = prz->buffer_size - start;
	if (unlikely(rem < c)) {
		persistent_ram_update(prz, s, start, rem);
		s += rem;
		c -= rem;
		start = 0;
	}
	persistent_ram_update(prz, s, start, c);

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

static void *persistent_ram_vmap(phys_addr_t start, size_t size)
{
	struct page **pages;
	phys_addr_t page_start;
	unsigned int page_count;
	pgprot_t prot;
	unsigned int i;
	void *vaddr;

	page_start = start - offset_in_page(start);
	page_count = DIV_ROUND_UP(size + offset_in_page(start), PAGE_SIZE);

	prot = pgprot_noncached(PAGE_KERNEL);

	pages = kmalloc(sizeof(struct page *) * page_count, GFP_KERNEL);
	if (!pages) {
		pr_err("%s: Failed to allocate array for %u pages\n", __func__,
			page_count);
		return NULL;
	}

	for (i = 0; i < page_count; i++) {
		phys_addr_t addr = page_start + i * PAGE_SIZE;
		pages[i] = pfn_to_page(addr >> PAGE_SHIFT);
	}
	vaddr = vmap(pages, page_count, VM_MAP, prot);
	kfree(pages);

	return vaddr;
}

static void *persistent_ram_iomap(phys_addr_t start, size_t size)
{
	if (!request_mem_region(start, size, "persistent_ram")) {
		pr_err("request mem region (0x%llx@0x%llx) failed\n",
			(unsigned long long)size, (unsigned long long)start);
		return NULL;
	}

	return ioremap(start, size);
}

static int persistent_ram_buffer_map(phys_addr_t start, phys_addr_t size,
		struct persistent_ram_zone *prz)
{
	if (pfn_valid(start >> PAGE_SHIFT))
		prz->vaddr = persistent_ram_vmap(start, size);
	else
		prz->vaddr = persistent_ram_iomap(start, size);

	if (!prz->vaddr) {
		pr_err("%s: Failed to map 0x%llx pages at 0x%llx\n", __func__,
			(unsigned long long)size, (unsigned long long)start);
		return -ENOMEM;
	}

	prz->buffer = prz->vaddr + offset_in_page(start);
	prz->buffer_size = size - sizeof(struct persistent_ram_buffer);

	return 0;
}

static int __init persistent_ram_buffer_init(const char *name,
		struct persistent_ram_zone *prz)
{
	int i;
	struct persistent_ram *ram;
	struct persistent_ram_descriptor *desc;
	phys_addr_t start;

	list_for_each_entry(ram, &persistent_ram_list, node) {
		start = ram->start;
		for (i = 0; i < ram->num_descs; i++) {
			desc = &ram->descs[i];
			if (!strcmp(desc->name, name))
				return persistent_ram_buffer_map(start,
						desc->size, prz);
			start += desc->size;
		}
	}

	return -EINVAL;
}

static int __init persistent_ram_post_init(struct persistent_ram_zone *prz, bool ecc)
{
	int ret;

	prz->ecc = ecc;

	ret = persistent_ram_init_ecc(prz, prz->buffer_size);
	if (ret)
		return ret;

	if (prz->buffer->sig == PERSISTENT_RAM_SIG) {
		if (buffer_size(prz) > prz->buffer_size ||
		    buffer_start(prz) > buffer_size(prz))
			pr_info("persistent_ram: found existing invalid buffer,"
				" size %zu, start %zu\n",
			       buffer_size(prz), buffer_start(prz));
		else {
			pr_info("persistent_ram: found existing buffer,"
				" size %zu, start %zu\n",
			       buffer_size(prz), buffer_start(prz));
			persistent_ram_save_old(prz);
		}
	} else {
		pr_info("persistent_ram: no valid data in buffer"
			" (sig = 0x%08x)\n", prz->buffer->sig);
	}

	prz->buffer->sig = PERSISTENT_RAM_SIG;
	atomic_set(&prz->buffer->start, 0);
	atomic_set(&prz->buffer->size, 0);

	return 0;
}

struct persistent_ram_zone * __init persistent_ram_new(phys_addr_t start,
						       size_t size,
						       bool ecc)
{
	struct persistent_ram_zone *prz;
	int ret = -ENOMEM;

	prz = kzalloc(sizeof(struct persistent_ram_zone), GFP_KERNEL);
	if (!prz) {
		pr_err("persistent_ram: failed to allocate persistent ram zone\n");
		goto err;
	}

	ret = persistent_ram_buffer_map(start, size, prz);
	if (ret)
		goto err;

	persistent_ram_post_init(prz, ecc);
	persistent_ram_update_header_ecc(prz);

	return prz;
err:
	kfree(prz);
	return ERR_PTR(ret);
}

static  __init
struct persistent_ram_zone *__persistent_ram_init(struct device *dev, bool ecc)
{
	struct persistent_ram_zone *prz;
	int ret = -ENOMEM;

	prz = kzalloc(sizeof(struct persistent_ram_zone), GFP_KERNEL);
	if (!prz) {
		pr_err("persistent_ram: failed to allocate persistent ram zone\n");
		goto err;
	}

	ret = persistent_ram_buffer_init(dev_name(dev), prz);
	if (ret) {
		pr_err("persistent_ram: failed to initialize buffer\n");
		goto err;
	}

	persistent_ram_post_init(prz, ecc);

	return prz;
err:
	kfree(prz);
	return ERR_PTR(ret);
}

struct persistent_ram_zone * __init
persistent_ram_init_ringbuffer(struct device *dev, bool ecc)
{
	return __persistent_ram_init(dev, ecc);
}

int __init persistent_ram_early_init(struct persistent_ram *ram)
{
	int ret;

	ret = memblock_reserve(ram->start, ram->size);
	if (ret) {
		pr_err("Failed to reserve persistent memory from %08lx-%08lx\n",
			(long)ram->start, (long)(ram->start + ram->size - 1));
		return ret;
	}

	list_add_tail(&ram->node, &persistent_ram_list);

	pr_info("Initialized persistent memory from %08lx-%08lx\n",
		(long)ram->start, (long)(ram->start + ram->size - 1));

	return 0;
}
