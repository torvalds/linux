/*
 * Copyright 2012 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 */

/*
 * Implementation of mpipe gxio calls.
 */

#include <linux/errno.h>
#include <linux/io.h>
#include <linux/module.h>

#include <gxio/iorpc_globals.h>
#include <gxio/iorpc_mpipe.h>
#include <gxio/iorpc_mpipe_info.h>
#include <gxio/kiorpc.h>
#include <gxio/mpipe.h>

/* HACK: Avoid pointless "shadow" warnings. */
#define link link_shadow

int gxio_mpipe_init(gxio_mpipe_context_t *context, unsigned int mpipe_index)
{
	char file[32];

	int fd;
	int i;

	if (mpipe_index >= GXIO_MPIPE_INSTANCE_MAX)
		return -EINVAL;

	snprintf(file, sizeof(file), "mpipe/%d/iorpc", mpipe_index);
	fd = hv_dev_open((HV_VirtAddr) file, 0);

	context->fd = fd;

	if (fd < 0) {
		if (fd >= GXIO_ERR_MIN && fd <= GXIO_ERR_MAX)
			return fd;
		else
			return -ENODEV;
	}

	/* Map in the MMIO space. */
	context->mmio_cfg_base = (void __force *)
		iorpc_ioremap(fd, HV_MPIPE_CONFIG_MMIO_OFFSET,
			      HV_MPIPE_CONFIG_MMIO_SIZE);
	if (context->mmio_cfg_base == NULL)
		goto cfg_failed;

	context->mmio_fast_base = (void __force *)
		iorpc_ioremap(fd, HV_MPIPE_FAST_MMIO_OFFSET,
			      HV_MPIPE_FAST_MMIO_SIZE);
	if (context->mmio_fast_base == NULL)
		goto fast_failed;

	/* Initialize the stacks. */
	for (i = 0; i < 8; i++)
		context->__stacks.stacks[i] = 255;

	context->instance = mpipe_index;

	return 0;

      fast_failed:
	iounmap((void __force __iomem *)(context->mmio_cfg_base));
      cfg_failed:
	hv_dev_close(context->fd);
	context->fd = -1;
	return -ENODEV;
}

EXPORT_SYMBOL_GPL(gxio_mpipe_init);

int gxio_mpipe_destroy(gxio_mpipe_context_t *context)
{
	iounmap((void __force __iomem *)(context->mmio_cfg_base));
	iounmap((void __force __iomem *)(context->mmio_fast_base));
	return hv_dev_close(context->fd);
}

EXPORT_SYMBOL_GPL(gxio_mpipe_destroy);

static int16_t gxio_mpipe_buffer_sizes[8] =
	{ 128, 256, 512, 1024, 1664, 4096, 10368, 16384 };

gxio_mpipe_buffer_size_enum_t gxio_mpipe_buffer_size_to_buffer_size_enum(size_t
									 size)
{
	int i;
	for (i = 0; i < 7; i++)
		if (size <= gxio_mpipe_buffer_sizes[i])
			break;
	return i;
}

EXPORT_SYMBOL_GPL(gxio_mpipe_buffer_size_to_buffer_size_enum);

size_t gxio_mpipe_buffer_size_enum_to_buffer_size(gxio_mpipe_buffer_size_enum_t
						  buffer_size_enum)
{
	if (buffer_size_enum > 7)
		buffer_size_enum = 7;

	return gxio_mpipe_buffer_sizes[buffer_size_enum];
}

EXPORT_SYMBOL_GPL(gxio_mpipe_buffer_size_enum_to_buffer_size);

size_t gxio_mpipe_calc_buffer_stack_bytes(unsigned long buffers)
{
	const int BUFFERS_PER_LINE = 12;

	/* Count the number of cachlines. */
	unsigned long lines =
		(buffers + BUFFERS_PER_LINE - 1) / BUFFERS_PER_LINE;

	/* Convert to bytes. */
	return lines * CHIP_L2_LINE_SIZE();
}

EXPORT_SYMBOL_GPL(gxio_mpipe_calc_buffer_stack_bytes);

int gxio_mpipe_init_buffer_stack(gxio_mpipe_context_t *context,
				 unsigned int stack,
				 gxio_mpipe_buffer_size_enum_t
				 buffer_size_enum, void *mem, size_t mem_size,
				 unsigned int mem_flags)
{
	int result;

	memset(mem, 0, mem_size);

	result = gxio_mpipe_init_buffer_stack_aux(context, mem, mem_size,
						  mem_flags, stack,
						  buffer_size_enum);
	if (result < 0)
		return result;

	/* Save the stack. */
	context->__stacks.stacks[buffer_size_enum] = stack;

	return 0;
}

EXPORT_SYMBOL_GPL(gxio_mpipe_init_buffer_stack);

int gxio_mpipe_init_notif_ring(gxio_mpipe_context_t *context,
			       unsigned int ring,
			       void *mem, size_t mem_size,
			       unsigned int mem_flags)
{
	return gxio_mpipe_init_notif_ring_aux(context, mem, mem_size,
					      mem_flags, ring);
}

EXPORT_SYMBOL_GPL(gxio_mpipe_init_notif_ring);

int gxio_mpipe_init_notif_group_and_buckets(gxio_mpipe_context_t *context,
					    unsigned int group,
					    unsigned int ring,
					    unsigned int num_rings,
					    unsigned int bucket,
					    unsigned int num_buckets,
					    gxio_mpipe_bucket_mode_t mode)
{
	int i;
	int result;

	gxio_mpipe_bucket_info_t bucket_info = { {
						  .group = group,
						  .mode = mode,
						  }
	};

	gxio_mpipe_notif_group_bits_t bits = { {0} };

	for (i = 0; i < num_rings; i++)
		gxio_mpipe_notif_group_add_ring(&bits, ring + i);

	result = gxio_mpipe_init_notif_group(context, group, bits);
	if (result != 0)
		return result;

	for (i = 0; i < num_buckets; i++) {
		bucket_info.notifring = ring + (i % num_rings);

		result = gxio_mpipe_init_bucket(context, bucket + i,
						bucket_info);
		if (result != 0)
			return result;
	}

	return 0;
}

EXPORT_SYMBOL_GPL(gxio_mpipe_init_notif_group_and_buckets);

int gxio_mpipe_init_edma_ring(gxio_mpipe_context_t *context,
			      unsigned int ring, unsigned int channel,
			      void *mem, size_t mem_size,
			      unsigned int mem_flags)
{
	memset(mem, 0, mem_size);

	return gxio_mpipe_init_edma_ring_aux(context, mem, mem_size, mem_flags,
					     ring, channel);
}

EXPORT_SYMBOL_GPL(gxio_mpipe_init_edma_ring);

void gxio_mpipe_rules_init(gxio_mpipe_rules_t *rules,
			   gxio_mpipe_context_t *context)
{
	rules->context = context;
	memset(&rules->list, 0, sizeof(rules->list));
}

EXPORT_SYMBOL_GPL(gxio_mpipe_rules_init);

int gxio_mpipe_rules_begin(gxio_mpipe_rules_t *rules,
			   unsigned int bucket, unsigned int num_buckets,
			   gxio_mpipe_rules_stacks_t *stacks)
{
	int i;
	int stack = 255;

	gxio_mpipe_rules_list_t *list = &rules->list;

	/* Current rule. */
	gxio_mpipe_rules_rule_t *rule =
		(gxio_mpipe_rules_rule_t *) (list->rules + list->head);

	unsigned int head = list->tail;

	/*
	 * Align next rule properly.
	 *Note that "dmacs_and_vlans" will also be aligned.
	 */
	unsigned int pad = 0;
	while (((head + pad) % __alignof__(gxio_mpipe_rules_rule_t)) != 0)
		pad++;

	/*
	 * Verify room.
	 * ISSUE: Mark rules as broken on error?
	 */
	if (head + pad + sizeof(*rule) >= sizeof(list->rules))
		return GXIO_MPIPE_ERR_RULES_FULL;

	/* Verify num_buckets is a power of 2. */
	if (__builtin_popcount(num_buckets) != 1)
		return GXIO_MPIPE_ERR_RULES_INVALID;

	/* Add padding to previous rule. */
	rule->size += pad;

	/* Start a new rule. */
	list->head = head + pad;

	rule = (gxio_mpipe_rules_rule_t *) (list->rules + list->head);

	/* Default some values. */
	rule->headroom = 2;
	rule->tailroom = 0;
	rule->capacity = 16384;

	/* Save the bucket info. */
	rule->bucket_mask = num_buckets - 1;
	rule->bucket_first = bucket;

	for (i = 8 - 1; i >= 0; i--) {
		int maybe =
			stacks ? stacks->stacks[i] : rules->context->__stacks.
			stacks[i];
		if (maybe != 255)
			stack = maybe;
		rule->stacks.stacks[i] = stack;
	}

	if (stack == 255)
		return GXIO_MPIPE_ERR_RULES_INVALID;

	/* NOTE: Only entries at the end of the array can be 255. */
	for (i = 8 - 1; i > 0; i--) {
		if (rule->stacks.stacks[i] == 255) {
			rule->stacks.stacks[i] = stack;
			rule->capacity =
				gxio_mpipe_buffer_size_enum_to_buffer_size(i -
									   1);
		}
	}

	rule->size = sizeof(*rule);
	list->tail = list->head + rule->size;

	return 0;
}

EXPORT_SYMBOL_GPL(gxio_mpipe_rules_begin);

int gxio_mpipe_rules_add_channel(gxio_mpipe_rules_t *rules,
				 unsigned int channel)
{
	gxio_mpipe_rules_list_t *list = &rules->list;

	gxio_mpipe_rules_rule_t *rule =
		(gxio_mpipe_rules_rule_t *) (list->rules + list->head);

	/* Verify channel. */
	if (channel >= 32)
		return GXIO_MPIPE_ERR_RULES_INVALID;

	/* Verify begun. */
	if (list->tail == 0)
		return GXIO_MPIPE_ERR_RULES_EMPTY;

	rule->channel_bits |= (1UL << channel);

	return 0;
}

EXPORT_SYMBOL_GPL(gxio_mpipe_rules_add_channel);

int gxio_mpipe_rules_set_headroom(gxio_mpipe_rules_t *rules, uint8_t headroom)
{
	gxio_mpipe_rules_list_t *list = &rules->list;

	gxio_mpipe_rules_rule_t *rule =
		(gxio_mpipe_rules_rule_t *) (list->rules + list->head);

	/* Verify begun. */
	if (list->tail == 0)
		return GXIO_MPIPE_ERR_RULES_EMPTY;

	rule->headroom = headroom;

	return 0;
}

EXPORT_SYMBOL_GPL(gxio_mpipe_rules_set_headroom);

int gxio_mpipe_rules_commit(gxio_mpipe_rules_t *rules)
{
	gxio_mpipe_rules_list_t *list = &rules->list;
	unsigned int size =
		offsetof(gxio_mpipe_rules_list_t, rules) + list->tail;
	return gxio_mpipe_commit_rules(rules->context, list, size);
}

EXPORT_SYMBOL_GPL(gxio_mpipe_rules_commit);

int gxio_mpipe_iqueue_init(gxio_mpipe_iqueue_t *iqueue,
			   gxio_mpipe_context_t *context,
			   unsigned int ring,
			   void *mem, size_t mem_size, unsigned int mem_flags)
{
	/* The init call below will verify that "mem_size" is legal. */
	unsigned int num_entries = mem_size / sizeof(gxio_mpipe_idesc_t);

	iqueue->context = context;
	iqueue->idescs = (gxio_mpipe_idesc_t *)mem;
	iqueue->ring = ring;
	iqueue->num_entries = num_entries;
	iqueue->mask_num_entries = num_entries - 1;
	iqueue->log2_num_entries = __builtin_ctz(num_entries);
	iqueue->head = 1;
#ifdef __BIG_ENDIAN__
	iqueue->swapped = 0;
#endif

	/* Initialize the "tail". */
	__gxio_mmio_write(mem, iqueue->head);

	return gxio_mpipe_init_notif_ring(context, ring, mem, mem_size,
					  mem_flags);
}

EXPORT_SYMBOL_GPL(gxio_mpipe_iqueue_init);

int gxio_mpipe_equeue_init(gxio_mpipe_equeue_t *equeue,
			   gxio_mpipe_context_t *context,
			   unsigned int ering,
			   unsigned int channel,
			   void *mem, unsigned int mem_size,
			   unsigned int mem_flags)
{
	/* The init call below will verify that "mem_size" is legal. */
	unsigned int num_entries = mem_size / sizeof(gxio_mpipe_edesc_t);

	/* Offset used to read number of completed commands. */
	MPIPE_EDMA_POST_REGION_ADDR_t offset;

	int result = gxio_mpipe_init_edma_ring(context, ering, channel,
					       mem, mem_size, mem_flags);
	if (result < 0)
		return result;

	memset(equeue, 0, sizeof(*equeue));

	offset.word = 0;
	offset.region =
		MPIPE_MMIO_ADDR__REGION_VAL_EDMA -
		MPIPE_MMIO_ADDR__REGION_VAL_IDMA;
	offset.ring = ering;

	__gxio_dma_queue_init(&equeue->dma_queue,
			      context->mmio_fast_base + offset.word,
			      num_entries);
	equeue->edescs = mem;
	equeue->mask_num_entries = num_entries - 1;
	equeue->log2_num_entries = __builtin_ctz(num_entries);
	equeue->context = context;
	equeue->ering = ering;
	equeue->channel = channel;

	return 0;
}

EXPORT_SYMBOL_GPL(gxio_mpipe_equeue_init);

int gxio_mpipe_set_timestamp(gxio_mpipe_context_t *context,
			     const struct timespec *ts)
{
	cycles_t cycles = get_cycles();
	return gxio_mpipe_set_timestamp_aux(context, (uint64_t)ts->tv_sec,
					    (uint64_t)ts->tv_nsec,
					    (uint64_t)cycles);
}

int gxio_mpipe_get_timestamp(gxio_mpipe_context_t *context,
			     struct timespec *ts)
{
	int ret;
	cycles_t cycles_prev, cycles_now, clock_rate;
	cycles_prev = get_cycles();
	ret = gxio_mpipe_get_timestamp_aux(context, (uint64_t *)&ts->tv_sec,
					   (uint64_t *)&ts->tv_nsec,
					   (uint64_t *)&cycles_now);
	if (ret < 0) {
		return ret;
	}

	clock_rate = get_clock_rate();
	ts->tv_nsec -= (cycles_now - cycles_prev) * 1000000000LL / clock_rate;
	if (ts->tv_nsec < 0) {
		ts->tv_nsec += 1000000000LL;
		ts->tv_sec -= 1;
	}
	return ret;
}

int gxio_mpipe_adjust_timestamp(gxio_mpipe_context_t *context, int64_t delta)
{
	return gxio_mpipe_adjust_timestamp_aux(context, delta);
}

/* Get our internal context used for link name access.  This context is
 *  special in that it is not associated with an mPIPE service domain.
 */
static gxio_mpipe_context_t *_gxio_get_link_context(void)
{
	static gxio_mpipe_context_t context;
	static gxio_mpipe_context_t *contextp;
	static int tried_open = 0;
	static DEFINE_MUTEX(mutex);

	mutex_lock(&mutex);

	if (!tried_open) {
		int i = 0;
		tried_open = 1;

		/*
		 * "4" here is the maximum possible number of mPIPE shims; it's
		 * an exaggeration but we shouldn't ever go beyond 2 anyway.
		 */
		for (i = 0; i < 4; i++) {
			char file[80];

			snprintf(file, sizeof(file), "mpipe/%d/iorpc_info", i);
			context.fd = hv_dev_open((HV_VirtAddr) file, 0);
			if (context.fd < 0)
				continue;

			contextp = &context;
			break;
		}
	}

	mutex_unlock(&mutex);

	return contextp;
}

int gxio_mpipe_link_instance(const char *link_name)
{
	_gxio_mpipe_link_name_t name;
	gxio_mpipe_context_t *context = _gxio_get_link_context();

	if (!context)
		return GXIO_ERR_NO_DEVICE;

	strncpy(name.name, link_name, sizeof(name.name));
	name.name[GXIO_MPIPE_LINK_NAME_LEN - 1] = '\0';

	return gxio_mpipe_info_instance_aux(context, name);
}

int gxio_mpipe_link_enumerate_mac(int idx, char *link_name, uint8_t *link_mac)
{
	int rv;
	_gxio_mpipe_link_name_t name;
	_gxio_mpipe_link_mac_t mac;

	gxio_mpipe_context_t *context = _gxio_get_link_context();
	if (!context)
		return GXIO_ERR_NO_DEVICE;

	rv = gxio_mpipe_info_enumerate_aux(context, idx, &name, &mac);
	if (rv >= 0) {
		strncpy(link_name, name.name, sizeof(name.name));
		memcpy(link_mac, mac.mac, sizeof(mac.mac));
	}

	return rv;
}

EXPORT_SYMBOL_GPL(gxio_mpipe_link_enumerate_mac);

int gxio_mpipe_link_open(gxio_mpipe_link_t *link,
			 gxio_mpipe_context_t *context, const char *link_name,
			 unsigned int flags)
{
	_gxio_mpipe_link_name_t name;
	int rv;

	strncpy(name.name, link_name, sizeof(name.name));
	name.name[GXIO_MPIPE_LINK_NAME_LEN - 1] = '\0';

	rv = gxio_mpipe_link_open_aux(context, name, flags);
	if (rv < 0)
		return rv;

	link->context = context;
	link->channel = rv >> 8;
	link->mac = rv & 0xFF;

	return 0;
}

EXPORT_SYMBOL_GPL(gxio_mpipe_link_open);

int gxio_mpipe_link_close(gxio_mpipe_link_t *link)
{
	return gxio_mpipe_link_close_aux(link->context, link->mac);
}

EXPORT_SYMBOL_GPL(gxio_mpipe_link_close);

int gxio_mpipe_link_set_attr(gxio_mpipe_link_t *link, uint32_t attr,
			     int64_t val)
{
	return gxio_mpipe_link_set_attr_aux(link->context, link->mac, attr,
					    val);
}

EXPORT_SYMBOL_GPL(gxio_mpipe_link_set_attr);
