// SPDX-License-Identifier: MIT
/*
 * Copyright (C) 2017 Oracle Corporation
 * Authors: Hans de Goede <hdegoede@redhat.com>
 */

#include "vbox_drv.h"
#include "vboxvideo_vbe.h"
#include "hgsmi_defs.h"

/* One-at-a-Time Hash from http://www.burtleburtle.net/bob/hash/doobs.html */
static u32 hgsmi_hash_process(u32 hash, const u8 *data, int size)
{
	while (size--) {
		hash += *data++;
		hash += (hash << 10);
		hash ^= (hash >> 6);
	}

	return hash;
}

static u32 hgsmi_hash_end(u32 hash)
{
	hash += (hash << 3);
	hash ^= (hash >> 11);
	hash += (hash << 15);

	return hash;
}

/* Not really a checksum but that is the naming used in all vbox code */
static u32 hgsmi_checksum(u32 offset,
			  const struct hgsmi_buffer_header *header,
			  const struct hgsmi_buffer_tail *tail)
{
	u32 checksum;

	checksum = hgsmi_hash_process(0, (u8 *)&offset, sizeof(offset));
	checksum = hgsmi_hash_process(checksum, (u8 *)header, sizeof(*header));
	/* 4 -> Do not checksum the checksum itself */
	checksum = hgsmi_hash_process(checksum, (u8 *)tail, 4);

	return hgsmi_hash_end(checksum);
}

void *hgsmi_buffer_alloc(struct gen_pool *guest_pool, size_t size,
			 u8 channel, u16 channel_info)
{
	struct hgsmi_buffer_header *h;
	struct hgsmi_buffer_tail *t;
	size_t total_size;
	dma_addr_t offset;

	total_size = size + sizeof(*h) + sizeof(*t);
	h = gen_pool_dma_alloc(guest_pool, total_size, &offset);
	if (!h)
		return NULL;

	t = (struct hgsmi_buffer_tail *)((u8 *)h + sizeof(*h) + size);

	h->flags = HGSMI_BUFFER_HEADER_F_SEQ_SINGLE;
	h->data_size = size;
	h->channel = channel;
	h->channel_info = channel_info;
	memset(&h->u.header_data, 0, sizeof(h->u.header_data));

	t->reserved = 0;
	t->checksum = hgsmi_checksum(offset, h, t);

	return (u8 *)h + sizeof(*h);
}

void hgsmi_buffer_free(struct gen_pool *guest_pool, void *buf)
{
	struct hgsmi_buffer_header *h =
		(struct hgsmi_buffer_header *)((u8 *)buf - sizeof(*h));
	size_t total_size = h->data_size + sizeof(*h) +
					     sizeof(struct hgsmi_buffer_tail);

	gen_pool_free(guest_pool, (unsigned long)h, total_size);
}

int hgsmi_buffer_submit(struct gen_pool *guest_pool, void *buf)
{
	phys_addr_t offset;

	offset = gen_pool_virt_to_phys(guest_pool, (unsigned long)buf -
				       sizeof(struct hgsmi_buffer_header));
	outl(offset, VGA_PORT_HGSMI_GUEST);
	/* Make the compiler aware that the host has changed memory. */
	mb();

	return 0;
}
