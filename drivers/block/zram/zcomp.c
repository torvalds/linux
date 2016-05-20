/*
 * Copyright (C) 2014 Sergey Senozhatsky.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/cpu.h>

#include "zcomp.h"
#include "zcomp_lzo.h"
#ifdef CONFIG_ZRAM_LZ4_COMPRESS
#include "zcomp_lz4.h"
#endif

static struct zcomp_backend *backends[] = {
	&zcomp_lzo,
#ifdef CONFIG_ZRAM_LZ4_COMPRESS
	&zcomp_lz4,
#endif
	NULL
};

static struct zcomp_backend *find_backend(const char *compress)
{
	int i = 0;
	while (backends[i]) {
		if (sysfs_streq(compress, backends[i]->name))
			break;
		i++;
	}
	return backends[i];
}

static void zcomp_strm_free(struct zcomp *comp, struct zcomp_strm *zstrm)
{
	if (zstrm->private)
		comp->backend->destroy(zstrm->private);
	free_pages((unsigned long)zstrm->buffer, 1);
	kfree(zstrm);
}

/*
 * allocate new zcomp_strm structure with ->private initialized by
 * backend, return NULL on error
 */
static struct zcomp_strm *zcomp_strm_alloc(struct zcomp *comp, gfp_t flags)
{
	struct zcomp_strm *zstrm = kmalloc(sizeof(*zstrm), flags);
	if (!zstrm)
		return NULL;

	zstrm->private = comp->backend->create(flags);
	/*
	 * allocate 2 pages. 1 for compressed data, plus 1 extra for the
	 * case when compressed size is larger than the original one
	 */
	zstrm->buffer = (void *)__get_free_pages(flags | __GFP_ZERO, 1);
	if (!zstrm->private || !zstrm->buffer) {
		zcomp_strm_free(comp, zstrm);
		zstrm = NULL;
	}
	return zstrm;
}

/* show available compressors */
ssize_t zcomp_available_show(const char *comp, char *buf)
{
	ssize_t sz = 0;
	int i = 0;

	while (backends[i]) {
		if (!strcmp(comp, backends[i]->name))
			sz += scnprintf(buf + sz, PAGE_SIZE - sz - 2,
					"[%s] ", backends[i]->name);
		else
			sz += scnprintf(buf + sz, PAGE_SIZE - sz - 2,
					"%s ", backends[i]->name);
		i++;
	}
	sz += scnprintf(buf + sz, PAGE_SIZE - sz, "\n");
	return sz;
}

bool zcomp_available_algorithm(const char *comp)
{
	return find_backend(comp) != NULL;
}

bool zcomp_set_max_streams(struct zcomp *comp, int num_strm)
{
	return true;
}

struct zcomp_strm *zcomp_strm_find(struct zcomp *comp)
{
	return *get_cpu_ptr(comp->stream);
}

void zcomp_strm_release(struct zcomp *comp, struct zcomp_strm *zstrm)
{
	put_cpu_ptr(comp->stream);
}

int zcomp_compress(struct zcomp *comp, struct zcomp_strm *zstrm,
		const unsigned char *src, size_t *dst_len)
{
	return comp->backend->compress(src, zstrm->buffer, dst_len,
			zstrm->private);
}

int zcomp_decompress(struct zcomp *comp, const unsigned char *src,
		size_t src_len, unsigned char *dst)
{
	return comp->backend->decompress(src, src_len, dst);
}

static int __zcomp_cpu_notifier(struct zcomp *comp,
		unsigned long action, unsigned long cpu)
{
	struct zcomp_strm *zstrm;

	switch (action) {
	case CPU_UP_PREPARE:
		if (WARN_ON(*per_cpu_ptr(comp->stream, cpu)))
			break;
		zstrm = zcomp_strm_alloc(comp, GFP_KERNEL);
		if (IS_ERR_OR_NULL(zstrm)) {
			pr_err("Can't allocate a compression stream\n");
			return NOTIFY_BAD;
		}
		*per_cpu_ptr(comp->stream, cpu) = zstrm;
		break;
	case CPU_DEAD:
	case CPU_UP_CANCELED:
		zstrm = *per_cpu_ptr(comp->stream, cpu);
		if (!IS_ERR_OR_NULL(zstrm))
			zcomp_strm_free(comp, zstrm);
		*per_cpu_ptr(comp->stream, cpu) = NULL;
		break;
	default:
		break;
	}
	return NOTIFY_OK;
}

static int zcomp_cpu_notifier(struct notifier_block *nb,
		unsigned long action, void *pcpu)
{
	unsigned long cpu = (unsigned long)pcpu;
	struct zcomp *comp = container_of(nb, typeof(*comp), notifier);

	return __zcomp_cpu_notifier(comp, action, cpu);
}

static int zcomp_init(struct zcomp *comp)
{
	unsigned long cpu;
	int ret;

	comp->notifier.notifier_call = zcomp_cpu_notifier;

	comp->stream = alloc_percpu(struct zcomp_strm *);
	if (!comp->stream)
		return -ENOMEM;

	cpu_notifier_register_begin();
	for_each_online_cpu(cpu) {
		ret = __zcomp_cpu_notifier(comp, CPU_UP_PREPARE, cpu);
		if (ret == NOTIFY_BAD)
			goto cleanup;
	}
	__register_cpu_notifier(&comp->notifier);
	cpu_notifier_register_done();
	return 0;

cleanup:
	for_each_online_cpu(cpu)
		__zcomp_cpu_notifier(comp, CPU_UP_CANCELED, cpu);
	cpu_notifier_register_done();
	return -ENOMEM;
}

void zcomp_destroy(struct zcomp *comp)
{
	unsigned long cpu;

	cpu_notifier_register_begin();
	for_each_online_cpu(cpu)
		__zcomp_cpu_notifier(comp, CPU_UP_CANCELED, cpu);
	__unregister_cpu_notifier(&comp->notifier);
	cpu_notifier_register_done();

	free_percpu(comp->stream);
	kfree(comp);
}

/*
 * search available compressors for requested algorithm.
 * allocate new zcomp and initialize it. return compressing
 * backend pointer or ERR_PTR if things went bad. ERR_PTR(-EINVAL)
 * if requested algorithm is not supported, ERR_PTR(-ENOMEM) in
 * case of allocation error, or any other error potentially
 * returned by zcomp_init().
 */
struct zcomp *zcomp_create(const char *compress)
{
	struct zcomp *comp;
	struct zcomp_backend *backend;
	int error;

	backend = find_backend(compress);
	if (!backend)
		return ERR_PTR(-EINVAL);

	comp = kzalloc(sizeof(struct zcomp), GFP_KERNEL);
	if (!comp)
		return ERR_PTR(-ENOMEM);

	comp->backend = backend;
	error = zcomp_init(comp);
	if (error) {
		kfree(comp);
		return ERR_PTR(error);
	}
	return comp;
}
