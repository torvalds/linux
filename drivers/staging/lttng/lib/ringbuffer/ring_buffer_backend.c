/*
 * ring_buffer_backend.c
 *
 * Copyright (C) 2005-2010 - Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
 *
 * Dual LGPL v2.1/GPL v2 license.
 */

#include <linux/stddef.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/cpu.h>
#include <linux/mm.h>

#include "../../wrapper/vmalloc.h"	/* for wrapper_vmalloc_sync_all() */
#include "../../wrapper/ringbuffer/config.h"
#include "../../wrapper/ringbuffer/backend.h"
#include "../../wrapper/ringbuffer/frontend.h"

/**
 * lib_ring_buffer_backend_allocate - allocate a channel buffer
 * @config: ring buffer instance configuration
 * @buf: the buffer struct
 * @size: total size of the buffer
 * @num_subbuf: number of subbuffers
 * @extra_reader_sb: need extra subbuffer for reader
 */
static
int lib_ring_buffer_backend_allocate(const struct lib_ring_buffer_config *config,
				     struct lib_ring_buffer_backend *bufb,
				     size_t size, size_t num_subbuf,
				     int extra_reader_sb)
{
	struct channel_backend *chanb = &bufb->chan->backend;
	unsigned long j, num_pages, num_pages_per_subbuf, page_idx = 0;
	unsigned long subbuf_size, mmap_offset = 0;
	unsigned long num_subbuf_alloc;
	struct page **pages;
	void **virt;
	unsigned long i;

	num_pages = size >> PAGE_SHIFT;
	num_pages_per_subbuf = num_pages >> get_count_order(num_subbuf);
	subbuf_size = chanb->subbuf_size;
	num_subbuf_alloc = num_subbuf;

	if (extra_reader_sb) {
		num_pages += num_pages_per_subbuf; /* Add pages for reader */
		num_subbuf_alloc++;
	}

	pages = kmalloc_node(ALIGN(sizeof(*pages) * num_pages,
				   1 << INTERNODE_CACHE_SHIFT),
			GFP_KERNEL, cpu_to_node(max(bufb->cpu, 0)));
	if (unlikely(!pages))
		goto pages_error;

	virt = kmalloc_node(ALIGN(sizeof(*virt) * num_pages,
				  1 << INTERNODE_CACHE_SHIFT),
			GFP_KERNEL, cpu_to_node(max(bufb->cpu, 0)));
	if (unlikely(!virt))
		goto virt_error;

	bufb->array = kmalloc_node(ALIGN(sizeof(*bufb->array)
					 * num_subbuf_alloc,
				  1 << INTERNODE_CACHE_SHIFT),
			GFP_KERNEL, cpu_to_node(max(bufb->cpu, 0)));
	if (unlikely(!bufb->array))
		goto array_error;

	for (i = 0; i < num_pages; i++) {
		pages[i] = alloc_pages_node(cpu_to_node(max(bufb->cpu, 0)),
					    GFP_KERNEL | __GFP_ZERO, 0);
		if (unlikely(!pages[i]))
			goto depopulate;
		virt[i] = page_address(pages[i]);
	}
	bufb->num_pages_per_subbuf = num_pages_per_subbuf;

	/* Allocate backend pages array elements */
	for (i = 0; i < num_subbuf_alloc; i++) {
		bufb->array[i] =
			kzalloc_node(ALIGN(
				sizeof(struct lib_ring_buffer_backend_pages) +
				sizeof(struct lib_ring_buffer_backend_page)
				* num_pages_per_subbuf,
				1 << INTERNODE_CACHE_SHIFT),
				GFP_KERNEL, cpu_to_node(max(bufb->cpu, 0)));
		if (!bufb->array[i])
			goto free_array;
	}

	/* Allocate write-side subbuffer table */
	bufb->buf_wsb = kzalloc_node(ALIGN(
				sizeof(struct lib_ring_buffer_backend_subbuffer)
				* num_subbuf,
				1 << INTERNODE_CACHE_SHIFT),
				GFP_KERNEL, cpu_to_node(max(bufb->cpu, 0)));
	if (unlikely(!bufb->buf_wsb))
		goto free_array;

	for (i = 0; i < num_subbuf; i++)
		bufb->buf_wsb[i].id = subbuffer_id(config, 0, 1, i);

	/* Assign read-side subbuffer table */
	if (extra_reader_sb)
		bufb->buf_rsb.id = subbuffer_id(config, 0, 1,
						num_subbuf_alloc - 1);
	else
		bufb->buf_rsb.id = subbuffer_id(config, 0, 1, 0);

	/* Assign pages to page index */
	for (i = 0; i < num_subbuf_alloc; i++) {
		for (j = 0; j < num_pages_per_subbuf; j++) {
			CHAN_WARN_ON(chanb, page_idx > num_pages);
			bufb->array[i]->p[j].virt = virt[page_idx];
			bufb->array[i]->p[j].page = pages[page_idx];
			page_idx++;
		}
		if (config->output == RING_BUFFER_MMAP) {
			bufb->array[i]->mmap_offset = mmap_offset;
			mmap_offset += subbuf_size;
		}
	}

	/*
	 * If kmalloc ever uses vmalloc underneath, make sure the buffer pages
	 * will not fault.
	 */
	wrapper_vmalloc_sync_all();
	kfree(virt);
	kfree(pages);
	return 0;

free_array:
	for (i = 0; (i < num_subbuf_alloc && bufb->array[i]); i++)
		kfree(bufb->array[i]);
depopulate:
	/* Free all allocated pages */
	for (i = 0; (i < num_pages && pages[i]); i++)
		__free_page(pages[i]);
	kfree(bufb->array);
array_error:
	kfree(virt);
virt_error:
	kfree(pages);
pages_error:
	return -ENOMEM;
}

int lib_ring_buffer_backend_create(struct lib_ring_buffer_backend *bufb,
				   struct channel_backend *chanb, int cpu)
{
	const struct lib_ring_buffer_config *config = chanb->config;

	bufb->chan = container_of(chanb, struct channel, backend);
	bufb->cpu = cpu;

	return lib_ring_buffer_backend_allocate(config, bufb, chanb->buf_size,
						chanb->num_subbuf,
						chanb->extra_reader_sb);
}

void lib_ring_buffer_backend_free(struct lib_ring_buffer_backend *bufb)
{
	struct channel_backend *chanb = &bufb->chan->backend;
	unsigned long i, j, num_subbuf_alloc;

	num_subbuf_alloc = chanb->num_subbuf;
	if (chanb->extra_reader_sb)
		num_subbuf_alloc++;

	kfree(bufb->buf_wsb);
	for (i = 0; i < num_subbuf_alloc; i++) {
		for (j = 0; j < bufb->num_pages_per_subbuf; j++)
			__free_page(bufb->array[i]->p[j].page);
		kfree(bufb->array[i]);
	}
	kfree(bufb->array);
	bufb->allocated = 0;
}

void lib_ring_buffer_backend_reset(struct lib_ring_buffer_backend *bufb)
{
	struct channel_backend *chanb = &bufb->chan->backend;
	const struct lib_ring_buffer_config *config = chanb->config;
	unsigned long num_subbuf_alloc;
	unsigned int i;

	num_subbuf_alloc = chanb->num_subbuf;
	if (chanb->extra_reader_sb)
		num_subbuf_alloc++;

	for (i = 0; i < chanb->num_subbuf; i++)
		bufb->buf_wsb[i].id = subbuffer_id(config, 0, 1, i);
	if (chanb->extra_reader_sb)
		bufb->buf_rsb.id = subbuffer_id(config, 0, 1,
						num_subbuf_alloc - 1);
	else
		bufb->buf_rsb.id = subbuffer_id(config, 0, 1, 0);

	for (i = 0; i < num_subbuf_alloc; i++) {
		/* Don't reset mmap_offset */
		v_set(config, &bufb->array[i]->records_commit, 0);
		v_set(config, &bufb->array[i]->records_unread, 0);
		bufb->array[i]->data_size = 0;
		/* Don't reset backend page and virt addresses */
	}
	/* Don't reset num_pages_per_subbuf, cpu, allocated */
	v_set(config, &bufb->records_read, 0);
}

/*
 * The frontend is responsible for also calling ring_buffer_backend_reset for
 * each buffer when calling channel_backend_reset.
 */
void channel_backend_reset(struct channel_backend *chanb)
{
	struct channel *chan = container_of(chanb, struct channel, backend);
	const struct lib_ring_buffer_config *config = chanb->config;

	/*
	 * Don't reset buf_size, subbuf_size, subbuf_size_order,
	 * num_subbuf_order, buf_size_order, extra_reader_sb, num_subbuf,
	 * priv, notifiers, config, cpumask and name.
	 */
	chanb->start_tsc = config->cb.ring_buffer_clock_read(chan);
}

#ifdef CONFIG_HOTPLUG_CPU
/**
 *	lib_ring_buffer_cpu_hp_callback - CPU hotplug callback
 *	@nb: notifier block
 *	@action: hotplug action to take
 *	@hcpu: CPU number
 *
 *	Returns the success/failure of the operation. (%NOTIFY_OK, %NOTIFY_BAD)
 */
static
int __cpuinit lib_ring_buffer_cpu_hp_callback(struct notifier_block *nb,
					      unsigned long action,
					      void *hcpu)
{
	unsigned int cpu = (unsigned long)hcpu;
	struct channel_backend *chanb = container_of(nb, struct channel_backend,
						     cpu_hp_notifier);
	const struct lib_ring_buffer_config *config = chanb->config;
	struct lib_ring_buffer *buf;
	int ret;

	CHAN_WARN_ON(chanb, config->alloc == RING_BUFFER_ALLOC_GLOBAL);

	switch (action) {
	case CPU_UP_PREPARE:
	case CPU_UP_PREPARE_FROZEN:
		buf = per_cpu_ptr(chanb->buf, cpu);
		ret = lib_ring_buffer_create(buf, chanb, cpu);
		if (ret) {
			printk(KERN_ERR
			  "ring_buffer_cpu_hp_callback: cpu %d "
			  "buffer creation failed\n", cpu);
			return NOTIFY_BAD;
		}
		break;
	case CPU_DEAD:
	case CPU_DEAD_FROZEN:
		/* No need to do a buffer switch here, because it will happen
		 * when tracing is stopped, or will be done by switch timer CPU
		 * DEAD callback. */
		break;
	}
	return NOTIFY_OK;
}
#endif

/**
 * channel_backend_init - initialize a channel backend
 * @chanb: channel backend
 * @name: channel name
 * @config: client ring buffer configuration
 * @priv: client private data
 * @parent: dentry of parent directory, %NULL for root directory
 * @subbuf_size: size of sub-buffers (> PAGE_SIZE, power of 2)
 * @num_subbuf: number of sub-buffers (power of 2)
 *
 * Returns channel pointer if successful, %NULL otherwise.
 *
 * Creates per-cpu channel buffers using the sizes and attributes
 * specified.  The created channel buffer files will be named
 * name_0...name_N-1.  File permissions will be %S_IRUSR.
 *
 * Called with CPU hotplug disabled.
 */
int channel_backend_init(struct channel_backend *chanb,
			 const char *name,
			 const struct lib_ring_buffer_config *config,
			 void *priv, size_t subbuf_size, size_t num_subbuf)
{
	struct channel *chan = container_of(chanb, struct channel, backend);
	unsigned int i;
	int ret;

	if (!name)
		return -EPERM;

	if (!(subbuf_size && num_subbuf))
		return -EPERM;

	/* Check that the subbuffer size is larger than a page. */
	if (subbuf_size < PAGE_SIZE)
		return -EINVAL;

	/*
	 * Make sure the number of subbuffers and subbuffer size are power of 2.
	 */
	CHAN_WARN_ON(chanb, hweight32(subbuf_size) != 1);
	CHAN_WARN_ON(chanb, hweight32(num_subbuf) != 1);

	ret = subbuffer_id_check_index(config, num_subbuf);
	if (ret)
		return ret;

	chanb->priv = priv;
	chanb->buf_size = num_subbuf * subbuf_size;
	chanb->subbuf_size = subbuf_size;
	chanb->buf_size_order = get_count_order(chanb->buf_size);
	chanb->subbuf_size_order = get_count_order(subbuf_size);
	chanb->num_subbuf_order = get_count_order(num_subbuf);
	chanb->extra_reader_sb =
			(config->mode == RING_BUFFER_OVERWRITE) ? 1 : 0;
	chanb->num_subbuf = num_subbuf;
	strlcpy(chanb->name, name, NAME_MAX);
	chanb->config = config;

	if (config->alloc == RING_BUFFER_ALLOC_PER_CPU) {
		if (!zalloc_cpumask_var(&chanb->cpumask, GFP_KERNEL))
			return -ENOMEM;
	}

	if (config->alloc == RING_BUFFER_ALLOC_PER_CPU) {
		/* Allocating the buffer per-cpu structures */
		chanb->buf = alloc_percpu(struct lib_ring_buffer);
		if (!chanb->buf)
			goto free_cpumask;

		/*
		 * In case of non-hotplug cpu, if the ring-buffer is allocated
		 * in early initcall, it will not be notified of secondary cpus.
		 * In that off case, we need to allocate for all possible cpus.
		 */
#ifdef CONFIG_HOTPLUG_CPU
		/*
		 * buf->backend.allocated test takes care of concurrent CPU
		 * hotplug.
		 * Priority higher than frontend, so we create the ring buffer
		 * before we start the timer.
		 */
		chanb->cpu_hp_notifier.notifier_call =
				lib_ring_buffer_cpu_hp_callback;
		chanb->cpu_hp_notifier.priority = 5;
		register_hotcpu_notifier(&chanb->cpu_hp_notifier);

		get_online_cpus();
		for_each_online_cpu(i) {
			ret = lib_ring_buffer_create(per_cpu_ptr(chanb->buf, i),
						 chanb, i);
			if (ret)
				goto free_bufs;	/* cpu hotplug locked */
		}
		put_online_cpus();
#else
		for_each_possible_cpu(i) {
			ret = lib_ring_buffer_create(per_cpu_ptr(chanb->buf, i),
						 chanb, i);
			if (ret)
				goto free_bufs;	/* cpu hotplug locked */
		}
#endif
	} else {
		chanb->buf = kzalloc(sizeof(struct lib_ring_buffer), GFP_KERNEL);
		if (!chanb->buf)
			goto free_cpumask;
		ret = lib_ring_buffer_create(chanb->buf, chanb, -1);
		if (ret)
			goto free_bufs;
	}
	chanb->start_tsc = config->cb.ring_buffer_clock_read(chan);

	return 0;

free_bufs:
	if (config->alloc == RING_BUFFER_ALLOC_PER_CPU) {
		for_each_possible_cpu(i) {
			struct lib_ring_buffer *buf = per_cpu_ptr(chanb->buf, i);

			if (!buf->backend.allocated)
				continue;
			lib_ring_buffer_free(buf);
		}
#ifdef CONFIG_HOTPLUG_CPU
		put_online_cpus();
#endif
		free_percpu(chanb->buf);
	} else
		kfree(chanb->buf);
free_cpumask:
	if (config->alloc == RING_BUFFER_ALLOC_PER_CPU)
		free_cpumask_var(chanb->cpumask);
	return -ENOMEM;
}

/**
 * channel_backend_unregister_notifiers - unregister notifiers
 * @chan: the channel
 *
 * Holds CPU hotplug.
 */
void channel_backend_unregister_notifiers(struct channel_backend *chanb)
{
	const struct lib_ring_buffer_config *config = chanb->config;

	if (config->alloc == RING_BUFFER_ALLOC_PER_CPU)
		unregister_hotcpu_notifier(&chanb->cpu_hp_notifier);
}

/**
 * channel_backend_free - destroy the channel
 * @chan: the channel
 *
 * Destroy all channel buffers and frees the channel.
 */
void channel_backend_free(struct channel_backend *chanb)
{
	const struct lib_ring_buffer_config *config = chanb->config;
	unsigned int i;

	if (config->alloc == RING_BUFFER_ALLOC_PER_CPU) {
		for_each_possible_cpu(i) {
			struct lib_ring_buffer *buf = per_cpu_ptr(chanb->buf, i);

			if (!buf->backend.allocated)
				continue;
			lib_ring_buffer_free(buf);
		}
		free_cpumask_var(chanb->cpumask);
		free_percpu(chanb->buf);
	} else {
		struct lib_ring_buffer *buf = chanb->buf;

		CHAN_WARN_ON(chanb, !buf->backend.allocated);
		lib_ring_buffer_free(buf);
		kfree(buf);
	}
}

/**
 * lib_ring_buffer_write - write data to a ring_buffer buffer.
 * @bufb : buffer backend
 * @offset : offset within the buffer
 * @src : source address
 * @len : length to write
 * @pagecpy : page size copied so far
 */
void _lib_ring_buffer_write(struct lib_ring_buffer_backend *bufb, size_t offset,
			    const void *src, size_t len, ssize_t pagecpy)
{
	struct channel_backend *chanb = &bufb->chan->backend;
	const struct lib_ring_buffer_config *config = chanb->config;
	size_t sbidx, index;
	struct lib_ring_buffer_backend_pages *rpages;
	unsigned long sb_bindex, id;

	do {
		len -= pagecpy;
		src += pagecpy;
		offset += pagecpy;
		sbidx = offset >> chanb->subbuf_size_order;
		index = (offset & (chanb->subbuf_size - 1)) >> PAGE_SHIFT;

		/*
		 * Underlying layer should never ask for writes across
		 * subbuffers.
		 */
		CHAN_WARN_ON(chanb, offset >= chanb->buf_size);

		pagecpy = min_t(size_t, len, PAGE_SIZE - (offset & ~PAGE_MASK));
		id = bufb->buf_wsb[sbidx].id;
		sb_bindex = subbuffer_id_get_index(config, id);
		rpages = bufb->array[sb_bindex];
		CHAN_WARN_ON(chanb, config->mode == RING_BUFFER_OVERWRITE
			     && subbuffer_id_is_noref(config, id));
		lib_ring_buffer_do_copy(config,
					rpages->p[index].virt
						+ (offset & ~PAGE_MASK),
					src, pagecpy);
	} while (unlikely(len != pagecpy));
}
EXPORT_SYMBOL_GPL(_lib_ring_buffer_write);


/**
 * lib_ring_buffer_memset - write len bytes of c to a ring_buffer buffer.
 * @bufb : buffer backend
 * @offset : offset within the buffer
 * @c : the byte to write
 * @len : length to write
 * @pagecpy : page size copied so far
 */
void _lib_ring_buffer_memset(struct lib_ring_buffer_backend *bufb,
			     size_t offset,
			     int c, size_t len, ssize_t pagecpy)
{
	struct channel_backend *chanb = &bufb->chan->backend;
	const struct lib_ring_buffer_config *config = chanb->config;
	size_t sbidx, index;
	struct lib_ring_buffer_backend_pages *rpages;
	unsigned long sb_bindex, id;

	do {
		len -= pagecpy;
		offset += pagecpy;
		sbidx = offset >> chanb->subbuf_size_order;
		index = (offset & (chanb->subbuf_size - 1)) >> PAGE_SHIFT;

		/*
		 * Underlying layer should never ask for writes across
		 * subbuffers.
		 */
		CHAN_WARN_ON(chanb, offset >= chanb->buf_size);

		pagecpy = min_t(size_t, len, PAGE_SIZE - (offset & ~PAGE_MASK));
		id = bufb->buf_wsb[sbidx].id;
		sb_bindex = subbuffer_id_get_index(config, id);
		rpages = bufb->array[sb_bindex];
		CHAN_WARN_ON(chanb, config->mode == RING_BUFFER_OVERWRITE
			     && subbuffer_id_is_noref(config, id));
		lib_ring_buffer_do_memset(rpages->p[index].virt
					  + (offset & ~PAGE_MASK),
					  c, pagecpy);
	} while (unlikely(len != pagecpy));
}
EXPORT_SYMBOL_GPL(_lib_ring_buffer_memset);


/**
 * lib_ring_buffer_copy_from_user - write user data to a ring_buffer buffer.
 * @bufb : buffer backend
 * @offset : offset within the buffer
 * @src : source address
 * @len : length to write
 * @pagecpy : page size copied so far
 *
 * This function deals with userspace pointers, it should never be called
 * directly without having the src pointer checked with access_ok()
 * previously.
 */
void _lib_ring_buffer_copy_from_user(struct lib_ring_buffer_backend *bufb,
				      size_t offset,
				      const void __user *src, size_t len,
				      ssize_t pagecpy)
{
	struct channel_backend *chanb = &bufb->chan->backend;
	const struct lib_ring_buffer_config *config = chanb->config;
	size_t sbidx, index;
	struct lib_ring_buffer_backend_pages *rpages;
	unsigned long sb_bindex, id;
	int ret;

	do {
		len -= pagecpy;
		src += pagecpy;
		offset += pagecpy;
		sbidx = offset >> chanb->subbuf_size_order;
		index = (offset & (chanb->subbuf_size - 1)) >> PAGE_SHIFT;

		/*
		 * Underlying layer should never ask for writes across
		 * subbuffers.
		 */
		CHAN_WARN_ON(chanb, offset >= chanb->buf_size);

		pagecpy = min_t(size_t, len, PAGE_SIZE - (offset & ~PAGE_MASK));
		id = bufb->buf_wsb[sbidx].id;
		sb_bindex = subbuffer_id_get_index(config, id);
		rpages = bufb->array[sb_bindex];
		CHAN_WARN_ON(chanb, config->mode == RING_BUFFER_OVERWRITE
				&& subbuffer_id_is_noref(config, id));
		ret = lib_ring_buffer_do_copy_from_user(rpages->p[index].virt
							+ (offset & ~PAGE_MASK),
							src, pagecpy) != 0;
		if (ret > 0) {
			offset += (pagecpy - ret);
			len -= (pagecpy - ret);
			_lib_ring_buffer_memset(bufb, offset, 0, len, 0);
			break; /* stop copy */
		}
	} while (unlikely(len != pagecpy));
}
EXPORT_SYMBOL_GPL(_lib_ring_buffer_copy_from_user);

/**
 * lib_ring_buffer_read - read data from ring_buffer_buffer.
 * @bufb : buffer backend
 * @offset : offset within the buffer
 * @dest : destination address
 * @len : length to copy to destination
 *
 * Should be protected by get_subbuf/put_subbuf.
 * Returns the length copied.
 */
size_t lib_ring_buffer_read(struct lib_ring_buffer_backend *bufb, size_t offset,
			    void *dest, size_t len)
{
	struct channel_backend *chanb = &bufb->chan->backend;
	const struct lib_ring_buffer_config *config = chanb->config;
	size_t index;
	ssize_t pagecpy, orig_len;
	struct lib_ring_buffer_backend_pages *rpages;
	unsigned long sb_bindex, id;

	orig_len = len;
	offset &= chanb->buf_size - 1;
	index = (offset & (chanb->subbuf_size - 1)) >> PAGE_SHIFT;
	if (unlikely(!len))
		return 0;
	for (;;) {
		pagecpy = min_t(size_t, len, PAGE_SIZE - (offset & ~PAGE_MASK));
		id = bufb->buf_rsb.id;
		sb_bindex = subbuffer_id_get_index(config, id);
		rpages = bufb->array[sb_bindex];
		CHAN_WARN_ON(chanb, config->mode == RING_BUFFER_OVERWRITE
			     && subbuffer_id_is_noref(config, id));
		memcpy(dest, rpages->p[index].virt + (offset & ~PAGE_MASK),
		       pagecpy);
		len -= pagecpy;
		if (likely(!len))
			break;
		dest += pagecpy;
		offset += pagecpy;
		index = (offset & (chanb->subbuf_size - 1)) >> PAGE_SHIFT;
		/*
		 * Underlying layer should never ask for reads across
		 * subbuffers.
		 */
		CHAN_WARN_ON(chanb, offset >= chanb->buf_size);
	}
	return orig_len;
}
EXPORT_SYMBOL_GPL(lib_ring_buffer_read);

/**
 * __lib_ring_buffer_copy_to_user - read data from ring_buffer to userspace
 * @bufb : buffer backend
 * @offset : offset within the buffer
 * @dest : destination userspace address
 * @len : length to copy to destination
 *
 * Should be protected by get_subbuf/put_subbuf.
 * access_ok() must have been performed on dest addresses prior to call this
 * function.
 * Returns -EFAULT on error, 0 if ok.
 */
int __lib_ring_buffer_copy_to_user(struct lib_ring_buffer_backend *bufb,
				   size_t offset, void __user *dest, size_t len)
{
	struct channel_backend *chanb = &bufb->chan->backend;
	const struct lib_ring_buffer_config *config = chanb->config;
	size_t index;
	ssize_t pagecpy;
	struct lib_ring_buffer_backend_pages *rpages;
	unsigned long sb_bindex, id;

	offset &= chanb->buf_size - 1;
	index = (offset & (chanb->subbuf_size - 1)) >> PAGE_SHIFT;
	if (unlikely(!len))
		return 0;
	for (;;) {
		pagecpy = min_t(size_t, len, PAGE_SIZE - (offset & ~PAGE_MASK));
		id = bufb->buf_rsb.id;
		sb_bindex = subbuffer_id_get_index(config, id);
		rpages = bufb->array[sb_bindex];
		CHAN_WARN_ON(chanb, config->mode == RING_BUFFER_OVERWRITE
			     && subbuffer_id_is_noref(config, id));
		if (__copy_to_user(dest,
			       rpages->p[index].virt + (offset & ~PAGE_MASK),
			       pagecpy))
			return -EFAULT;
		len -= pagecpy;
		if (likely(!len))
			break;
		dest += pagecpy;
		offset += pagecpy;
		index = (offset & (chanb->subbuf_size - 1)) >> PAGE_SHIFT;
		/*
		 * Underlying layer should never ask for reads across
		 * subbuffers.
		 */
		CHAN_WARN_ON(chanb, offset >= chanb->buf_size);
	}
	return 0;
}
EXPORT_SYMBOL_GPL(__lib_ring_buffer_copy_to_user);

/**
 * lib_ring_buffer_read_cstr - read a C-style string from ring_buffer.
 * @bufb : buffer backend
 * @offset : offset within the buffer
 * @dest : destination address
 * @len : destination's length
 *
 * return string's length
 * Should be protected by get_subbuf/put_subbuf.
 */
int lib_ring_buffer_read_cstr(struct lib_ring_buffer_backend *bufb, size_t offset,
			      void *dest, size_t len)
{
	struct channel_backend *chanb = &bufb->chan->backend;
	const struct lib_ring_buffer_config *config = chanb->config;
	size_t index;
	ssize_t pagecpy, pagelen, strpagelen, orig_offset;
	char *str;
	struct lib_ring_buffer_backend_pages *rpages;
	unsigned long sb_bindex, id;

	offset &= chanb->buf_size - 1;
	index = (offset & (chanb->subbuf_size - 1)) >> PAGE_SHIFT;
	orig_offset = offset;
	for (;;) {
		id = bufb->buf_rsb.id;
		sb_bindex = subbuffer_id_get_index(config, id);
		rpages = bufb->array[sb_bindex];
		CHAN_WARN_ON(chanb, config->mode == RING_BUFFER_OVERWRITE
			     && subbuffer_id_is_noref(config, id));
		str = (char *)rpages->p[index].virt + (offset & ~PAGE_MASK);
		pagelen = PAGE_SIZE - (offset & ~PAGE_MASK);
		strpagelen = strnlen(str, pagelen);
		if (len) {
			pagecpy = min_t(size_t, len, strpagelen);
			if (dest) {
				memcpy(dest, str, pagecpy);
				dest += pagecpy;
			}
			len -= pagecpy;
		}
		offset += strpagelen;
		index = (offset & (chanb->subbuf_size - 1)) >> PAGE_SHIFT;
		if (strpagelen < pagelen)
			break;
		/*
		 * Underlying layer should never ask for reads across
		 * subbuffers.
		 */
		CHAN_WARN_ON(chanb, offset >= chanb->buf_size);
	}
	if (dest && len)
		((char *)dest)[0] = 0;
	return offset - orig_offset;
}
EXPORT_SYMBOL_GPL(lib_ring_buffer_read_cstr);

/**
 * lib_ring_buffer_read_get_page - Get a whole page to read from
 * @bufb : buffer backend
 * @offset : offset within the buffer
 * @virt : pointer to page address (output)
 *
 * Should be protected by get_subbuf/put_subbuf.
 * Returns the pointer to the page struct pointer.
 */
struct page **lib_ring_buffer_read_get_page(struct lib_ring_buffer_backend *bufb,
					    size_t offset, void ***virt)
{
	size_t index;
	struct lib_ring_buffer_backend_pages *rpages;
	struct channel_backend *chanb = &bufb->chan->backend;
	const struct lib_ring_buffer_config *config = chanb->config;
	unsigned long sb_bindex, id;

	offset &= chanb->buf_size - 1;
	index = (offset & (chanb->subbuf_size - 1)) >> PAGE_SHIFT;
	id = bufb->buf_rsb.id;
	sb_bindex = subbuffer_id_get_index(config, id);
	rpages = bufb->array[sb_bindex];
	CHAN_WARN_ON(chanb, config->mode == RING_BUFFER_OVERWRITE
		     && subbuffer_id_is_noref(config, id));
	*virt = &rpages->p[index].virt;
	return &rpages->p[index].page;
}
EXPORT_SYMBOL_GPL(lib_ring_buffer_read_get_page);

/**
 * lib_ring_buffer_read_offset_address - get address of a buffer location
 * @bufb : buffer backend
 * @offset : offset within the buffer.
 *
 * Return the address where a given offset is located (for read).
 * Should be used to get the current subbuffer header pointer. Given we know
 * it's never on a page boundary, it's safe to write directly to this address,
 * as long as the write is never bigger than a page size.
 */
void *lib_ring_buffer_read_offset_address(struct lib_ring_buffer_backend *bufb,
					  size_t offset)
{
	size_t index;
	struct lib_ring_buffer_backend_pages *rpages;
	struct channel_backend *chanb = &bufb->chan->backend;
	const struct lib_ring_buffer_config *config = chanb->config;
	unsigned long sb_bindex, id;

	offset &= chanb->buf_size - 1;
	index = (offset & (chanb->subbuf_size - 1)) >> PAGE_SHIFT;
	id = bufb->buf_rsb.id;
	sb_bindex = subbuffer_id_get_index(config, id);
	rpages = bufb->array[sb_bindex];
	CHAN_WARN_ON(chanb, config->mode == RING_BUFFER_OVERWRITE
		     && subbuffer_id_is_noref(config, id));
	return rpages->p[index].virt + (offset & ~PAGE_MASK);
}
EXPORT_SYMBOL_GPL(lib_ring_buffer_read_offset_address);

/**
 * lib_ring_buffer_offset_address - get address of a location within the buffer
 * @bufb : buffer backend
 * @offset : offset within the buffer.
 *
 * Return the address where a given offset is located.
 * Should be used to get the current subbuffer header pointer. Given we know
 * it's always at the beginning of a page, it's safe to write directly to this
 * address, as long as the write is never bigger than a page size.
 */
void *lib_ring_buffer_offset_address(struct lib_ring_buffer_backend *bufb,
				     size_t offset)
{
	size_t sbidx, index;
	struct lib_ring_buffer_backend_pages *rpages;
	struct channel_backend *chanb = &bufb->chan->backend;
	const struct lib_ring_buffer_config *config = chanb->config;
	unsigned long sb_bindex, id;

	offset &= chanb->buf_size - 1;
	sbidx = offset >> chanb->subbuf_size_order;
	index = (offset & (chanb->subbuf_size - 1)) >> PAGE_SHIFT;
	id = bufb->buf_wsb[sbidx].id;
	sb_bindex = subbuffer_id_get_index(config, id);
	rpages = bufb->array[sb_bindex];
	CHAN_WARN_ON(chanb, config->mode == RING_BUFFER_OVERWRITE
		     && subbuffer_id_is_noref(config, id));
	return rpages->p[index].virt + (offset & ~PAGE_MASK);
}
EXPORT_SYMBOL_GPL(lib_ring_buffer_offset_address);
