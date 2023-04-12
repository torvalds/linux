// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) Rockchip Electronics Co.Ltd
 * Author: Felix Zeng <felix.zeng@rock-chips.com>
 */

#include "rknpu_debugger.h"
#include "rknpu_mm.h"

int rknpu_mm_create(unsigned int mem_size, unsigned int chunk_size,
		    struct rknpu_mm **mm)
{
	unsigned int num_of_longs;
	int ret = -EINVAL;

	if (WARN_ON(mem_size < chunk_size))
		return -EINVAL;
	if (WARN_ON(mem_size == 0))
		return -EINVAL;
	if (WARN_ON(chunk_size == 0))
		return -EINVAL;

	*mm = kzalloc(sizeof(struct rknpu_mm), GFP_KERNEL);
	if (!(*mm))
		return -ENOMEM;

	(*mm)->chunk_size = chunk_size;
	(*mm)->total_chunks = mem_size / chunk_size;
	(*mm)->free_chunks = (*mm)->total_chunks;

	num_of_longs =
		((*mm)->total_chunks + BITS_PER_LONG - 1) / BITS_PER_LONG;

	(*mm)->bitmap = kcalloc(num_of_longs, sizeof(long), GFP_KERNEL);
	if (!(*mm)->bitmap) {
		ret = -ENOMEM;
		goto free_mm;
	}

	mutex_init(&(*mm)->lock);

	LOG_DEBUG("total_chunks: %d, bitmap: %p\n", (*mm)->total_chunks,
		  (*mm)->bitmap);

	return 0;

free_mm:
	kfree(mm);
	return ret;
}

void rknpu_mm_destroy(struct rknpu_mm *mm)
{
	if (mm != NULL) {
		mutex_destroy(&mm->lock);
		kfree(mm->bitmap);
		kfree(mm);
	}
}

int rknpu_mm_alloc(struct rknpu_mm *mm, unsigned int size,
		   struct rknpu_mm_obj **mm_obj)
{
	unsigned int found, start_search, cur_size;

	if (size == 0)
		return -EINVAL;

	if (size > mm->total_chunks * mm->chunk_size)
		return -ENOMEM;

	*mm_obj = kzalloc(sizeof(struct rknpu_mm_obj), GFP_KERNEL);
	if (!(*mm_obj))
		return -ENOMEM;

	start_search = 0;

	mutex_lock(&mm->lock);

mm_restart_search:
	/* Find the first chunk that is free */
	found = find_next_zero_bit(mm->bitmap, mm->total_chunks, start_search);

	/* If there wasn't any free chunk, bail out */
	if (found == mm->total_chunks)
		goto mm_no_free_chunk;

	/* Update fields of mm_obj */
	(*mm_obj)->range_start = found;
	(*mm_obj)->range_end = found;

	/* If we need only one chunk, mark it as allocated and get out */
	if (size <= mm->chunk_size) {
		set_bit(found, mm->bitmap);
		goto mm_out;
	}

	/* Otherwise, try to see if we have enough contiguous chunks */
	cur_size = size - mm->chunk_size;
	do {
		(*mm_obj)->range_end = find_next_zero_bit(
			mm->bitmap, mm->total_chunks, ++found);
		/*
		 * If next free chunk is not contiguous than we need to
		 * restart our search from the last free chunk we found (which
		 * wasn't contiguous to the previous ones
		 */
		if ((*mm_obj)->range_end != found) {
			start_search = found;
			goto mm_restart_search;
		}

		/*
		 * If we reached end of buffer, bail out with error
		 */
		if (found == mm->total_chunks)
			goto mm_no_free_chunk;

		/* Check if we don't need another chunk */
		if (cur_size <= mm->chunk_size)
			cur_size = 0;
		else
			cur_size -= mm->chunk_size;

	} while (cur_size > 0);

	/* Mark the chunks as allocated */
	for (found = (*mm_obj)->range_start; found <= (*mm_obj)->range_end;
	     found++)
		set_bit(found, mm->bitmap);

mm_out:
	mm->free_chunks -= ((*mm_obj)->range_end - (*mm_obj)->range_start + 1);
	mutex_unlock(&mm->lock);

	LOG_DEBUG("mm allocate, mm_obj: %p, range_start: %d, range_end: %d\n",
		  *mm_obj, (*mm_obj)->range_start, (*mm_obj)->range_end);

	return 0;

mm_no_free_chunk:
	mutex_unlock(&mm->lock);
	kfree(*mm_obj);

	return -ENOMEM;
}

int rknpu_mm_free(struct rknpu_mm *mm, struct rknpu_mm_obj *mm_obj)
{
	unsigned int bit;

	/* Act like kfree when trying to free a NULL object */
	if (!mm_obj)
		return 0;

	LOG_DEBUG("mm free, mem_obj: %p, range_start: %d, range_end: %d\n",
		  mm_obj, mm_obj->range_start, mm_obj->range_end);

	mutex_lock(&mm->lock);

	/* Mark the chunks as free */
	for (bit = mm_obj->range_start; bit <= mm_obj->range_end; bit++)
		clear_bit(bit, mm->bitmap);

	mm->free_chunks += (mm_obj->range_end - mm_obj->range_start + 1);

	mutex_unlock(&mm->lock);

	kfree(mm_obj);

	return 0;
}

int rknpu_mm_dump(struct seq_file *m, void *data)
{
	struct rknpu_debugger_node *node = m->private;
	struct rknpu_debugger *debugger = node->debugger;
	struct rknpu_device *rknpu_dev =
		container_of(debugger, struct rknpu_device, debugger);
	struct rknpu_mm *mm = NULL;
	int cur = 0, rbot = 0, rtop = 0;
	size_t ret = 0;
	char buf[64];
	size_t size = sizeof(buf);
	int seg_chunks = 32, seg_id = 0;
	int free_size = 0;
	int i = 0;

	mm = rknpu_dev->sram_mm;
	if (mm == NULL)
		return 0;

	seq_printf(m, "SRAM bitmap: \"*\" - used, \".\" - free (1bit = %dKB)\n",
		   mm->chunk_size / 1024);

	rbot = cur = find_first_bit(mm->bitmap, mm->total_chunks);
	for (i = 0; i < cur; ++i) {
		ret += scnprintf(buf + ret, size - ret, ".");
		if (ret >= seg_chunks) {
			seq_printf(m, "[%03d] [%s]\n", seg_id++, buf);
			ret = 0;
		}
	}
	while (cur < mm->total_chunks) {
		rtop = cur;
		cur = find_next_bit(mm->bitmap, mm->total_chunks, cur + 1);
		if (cur < mm->total_chunks && cur <= rtop + 1)
			continue;

		for (i = rbot; i <= rtop; ++i) {
			ret += scnprintf(buf + ret, size - ret, "*");
			if (ret >= seg_chunks) {
				seq_printf(m, "[%03d] [%s]\n", seg_id++, buf);
				ret = 0;
			}
		}

		for (i = rtop + 1; i < cur; ++i) {
			ret += scnprintf(buf + ret, size - ret, ".");
			if (ret >= seg_chunks) {
				seq_printf(m, "[%03d] [%s]\n", seg_id++, buf);
				ret = 0;
			}
		}

		rbot = cur;
	}

	if (ret > 0)
		seq_printf(m, "[%03d] [%s]\n", seg_id++, buf);

	free_size = mm->free_chunks * mm->chunk_size;
	seq_printf(m, "SRAM total size: %d, used: %d, free: %d\n",
		   rknpu_dev->sram_size, rknpu_dev->sram_size - free_size,
		   free_size);

	return 0;
}
