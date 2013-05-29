/*
 * Tegra host1x Job
 *
 * Copyright (c) 2010-2013, NVIDIA Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/kref.h>
#include <linux/module.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <trace/events/host1x.h>

#include "channel.h"
#include "dev.h"
#include "host1x_bo.h"
#include "job.h"
#include "syncpt.h"

struct host1x_job *host1x_job_alloc(struct host1x_channel *ch,
				    u32 num_cmdbufs, u32 num_relocs,
				    u32 num_waitchks)
{
	struct host1x_job *job = NULL;
	unsigned int num_unpins = num_cmdbufs + num_relocs;
	u64 total;
	void *mem;

	/* Check that we're not going to overflow */
	total = sizeof(struct host1x_job) +
		num_relocs * sizeof(struct host1x_reloc) +
		num_unpins * sizeof(struct host1x_job_unpin_data) +
		num_waitchks * sizeof(struct host1x_waitchk) +
		num_cmdbufs * sizeof(struct host1x_job_gather) +
		num_unpins * sizeof(dma_addr_t) +
		num_unpins * sizeof(u32 *);
	if (total > ULONG_MAX)
		return NULL;

	mem = job = kzalloc(total, GFP_KERNEL);
	if (!job)
		return NULL;

	kref_init(&job->ref);
	job->channel = ch;

	/* Redistribute memory to the structs  */
	mem += sizeof(struct host1x_job);
	job->relocarray = num_relocs ? mem : NULL;
	mem += num_relocs * sizeof(struct host1x_reloc);
	job->unpins = num_unpins ? mem : NULL;
	mem += num_unpins * sizeof(struct host1x_job_unpin_data);
	job->waitchk = num_waitchks ? mem : NULL;
	mem += num_waitchks * sizeof(struct host1x_waitchk);
	job->gathers = num_cmdbufs ? mem : NULL;
	mem += num_cmdbufs * sizeof(struct host1x_job_gather);
	job->addr_phys = num_unpins ? mem : NULL;

	job->reloc_addr_phys = job->addr_phys;
	job->gather_addr_phys = &job->addr_phys[num_relocs];

	return job;
}

struct host1x_job *host1x_job_get(struct host1x_job *job)
{
	kref_get(&job->ref);
	return job;
}

static void job_free(struct kref *ref)
{
	struct host1x_job *job = container_of(ref, struct host1x_job, ref);

	kfree(job);
}

void host1x_job_put(struct host1x_job *job)
{
	kref_put(&job->ref, job_free);
}

void host1x_job_add_gather(struct host1x_job *job, struct host1x_bo *bo,
			   u32 words, u32 offset)
{
	struct host1x_job_gather *cur_gather = &job->gathers[job->num_gathers];

	cur_gather->words = words;
	cur_gather->bo = bo;
	cur_gather->offset = offset;
	job->num_gathers++;
}

/*
 * NULL an already satisfied WAIT_SYNCPT host method, by patching its
 * args in the command stream. The method data is changed to reference
 * a reserved (never given out or incr) HOST1X_SYNCPT_RESERVED syncpt
 * with a matching threshold value of 0, so is guaranteed to be popped
 * by the host HW.
 */
static void host1x_syncpt_patch_offset(struct host1x_syncpt *sp,
				       struct host1x_bo *h, u32 offset)
{
	void *patch_addr = NULL;

	/* patch the wait */
	patch_addr = host1x_bo_kmap(h, offset >> PAGE_SHIFT);
	if (patch_addr) {
		host1x_syncpt_patch_wait(sp,
					 patch_addr + (offset & ~PAGE_MASK));
		host1x_bo_kunmap(h, offset >> PAGE_SHIFT, patch_addr);
	} else
		pr_err("Could not map cmdbuf for wait check\n");
}

/*
 * Check driver supplied waitchk structs for syncpt thresholds
 * that have already been satisfied and NULL the comparison (to
 * avoid a wrap condition in the HW).
 */
static int do_waitchks(struct host1x_job *job, struct host1x *host,
		       struct host1x_bo *patch)
{
	int i;

	/* compare syncpt vs wait threshold */
	for (i = 0; i < job->num_waitchk; i++) {
		struct host1x_waitchk *wait = &job->waitchk[i];
		struct host1x_syncpt *sp =
			host1x_syncpt_get(host, wait->syncpt_id);

		/* validate syncpt id */
		if (wait->syncpt_id > host1x_syncpt_nb_pts(host))
			continue;

		/* skip all other gathers */
		if (patch != wait->bo)
			continue;

		trace_host1x_syncpt_wait_check(wait->bo, wait->offset,
					       wait->syncpt_id, wait->thresh,
					       host1x_syncpt_read_min(sp));

		if (host1x_syncpt_is_expired(sp, wait->thresh)) {
			dev_dbg(host->dev,
				"drop WAIT id %d (%s) thresh 0x%x, min 0x%x\n",
				wait->syncpt_id, sp->name, wait->thresh,
				host1x_syncpt_read_min(sp));

			host1x_syncpt_patch_offset(sp, patch, wait->offset);
		}

		wait->bo = NULL;
	}

	return 0;
}

static unsigned int pin_job(struct host1x_job *job)
{
	unsigned int i;

	job->num_unpins = 0;

	for (i = 0; i < job->num_relocs; i++) {
		struct host1x_reloc *reloc = &job->relocarray[i];
		struct sg_table *sgt;
		dma_addr_t phys_addr;

		reloc->target = host1x_bo_get(reloc->target);
		if (!reloc->target)
			goto unpin;

		phys_addr = host1x_bo_pin(reloc->target, &sgt);
		if (!phys_addr)
			goto unpin;

		job->addr_phys[job->num_unpins] = phys_addr;
		job->unpins[job->num_unpins].bo = reloc->target;
		job->unpins[job->num_unpins].sgt = sgt;
		job->num_unpins++;
	}

	for (i = 0; i < job->num_gathers; i++) {
		struct host1x_job_gather *g = &job->gathers[i];
		struct sg_table *sgt;
		dma_addr_t phys_addr;

		g->bo = host1x_bo_get(g->bo);
		if (!g->bo)
			goto unpin;

		phys_addr = host1x_bo_pin(g->bo, &sgt);
		if (!phys_addr)
			goto unpin;

		job->addr_phys[job->num_unpins] = phys_addr;
		job->unpins[job->num_unpins].bo = g->bo;
		job->unpins[job->num_unpins].sgt = sgt;
		job->num_unpins++;
	}

	return job->num_unpins;

unpin:
	host1x_job_unpin(job);
	return 0;
}

static unsigned int do_relocs(struct host1x_job *job, struct host1x_bo *cmdbuf)
{
	int i = 0;
	u32 last_page = ~0;
	void *cmdbuf_page_addr = NULL;

	/* pin & patch the relocs for one gather */
	while (i < job->num_relocs) {
		struct host1x_reloc *reloc = &job->relocarray[i];
		u32 reloc_addr = (job->reloc_addr_phys[i] +
			reloc->target_offset) >> reloc->shift;
		u32 *target;

		/* skip all other gathers */
		if (!(reloc->cmdbuf && cmdbuf == reloc->cmdbuf)) {
			i++;
			continue;
		}

		if (last_page != reloc->cmdbuf_offset >> PAGE_SHIFT) {
			if (cmdbuf_page_addr)
				host1x_bo_kunmap(cmdbuf, last_page,
						 cmdbuf_page_addr);

			cmdbuf_page_addr = host1x_bo_kmap(cmdbuf,
					reloc->cmdbuf_offset >> PAGE_SHIFT);
			last_page = reloc->cmdbuf_offset >> PAGE_SHIFT;

			if (unlikely(!cmdbuf_page_addr)) {
				pr_err("Could not map cmdbuf for relocation\n");
				return -ENOMEM;
			}
		}

		target = cmdbuf_page_addr + (reloc->cmdbuf_offset & ~PAGE_MASK);
		*target = reloc_addr;

		/* mark this gather as handled */
		reloc->cmdbuf = 0;
	}

	if (cmdbuf_page_addr)
		host1x_bo_kunmap(cmdbuf, last_page, cmdbuf_page_addr);

	return 0;
}

static int check_reloc(struct host1x_reloc *reloc, struct host1x_bo *cmdbuf,
		       unsigned int offset)
{
	offset *= sizeof(u32);

	if (reloc->cmdbuf != cmdbuf || reloc->cmdbuf_offset != offset)
		return -EINVAL;

	return 0;
}

struct host1x_firewall {
	struct host1x_job *job;
	struct device *dev;

	unsigned int num_relocs;
	struct host1x_reloc *reloc;

	struct host1x_bo *cmdbuf_id;
	unsigned int offset;

	u32 words;
	u32 class;
	u32 reg;
	u32 mask;
	u32 count;
};

static int check_mask(struct host1x_firewall *fw)
{
	u32 mask = fw->mask;
	u32 reg = fw->reg;

	while (mask) {
		if (fw->words == 0)
			return -EINVAL;

		if (mask & 1) {
			if (fw->job->is_addr_reg(fw->dev, fw->class, reg)) {
				bool bad_reloc = check_reloc(fw->reloc,
							     fw->cmdbuf_id,
							     fw->offset);
				if (!fw->num_relocs || bad_reloc)
					return -EINVAL;
				fw->reloc++;
				fw->num_relocs--;
			}
			fw->words--;
			fw->offset++;
		}
		mask >>= 1;
		reg++;
	}

	return 0;
}

static int check_incr(struct host1x_firewall *fw)
{
	u32 count = fw->count;
	u32 reg = fw->reg;

	while (count) {
		if (fw->words == 0)
			return -EINVAL;

		if (fw->job->is_addr_reg(fw->dev, fw->class, reg)) {
			bool bad_reloc = check_reloc(fw->reloc, fw->cmdbuf_id,
						     fw->offset);
			if (!fw->num_relocs || bad_reloc)
				return -EINVAL;
			fw->reloc++;
			fw->num_relocs--;
		}
		reg++;
		fw->words--;
		fw->offset++;
		count--;
	}

	return 0;
}

static int check_nonincr(struct host1x_firewall *fw)
{
	int is_addr_reg = fw->job->is_addr_reg(fw->dev, fw->class, fw->reg);
	u32 count = fw->count;

	while (count) {
		if (fw->words == 0)
			return -EINVAL;

		if (is_addr_reg) {
			bool bad_reloc = check_reloc(fw->reloc, fw->cmdbuf_id,
						     fw->offset);
			if (!fw->num_relocs || bad_reloc)
				return -EINVAL;
			fw->reloc++;
			fw->num_relocs--;
		}
		fw->words--;
		fw->offset++;
		count--;
	}

	return 0;
}

static int validate(struct host1x_job *job, struct device *dev,
		    struct host1x_job_gather *g)
{
	u32 *cmdbuf_base;
	int err = 0;
	struct host1x_firewall fw;

	fw.job = job;
	fw.dev = dev;
	fw.reloc = job->relocarray;
	fw.num_relocs = job->num_relocs;
	fw.cmdbuf_id = g->bo;

	fw.offset = 0;
	fw.class = 0;

	if (!job->is_addr_reg)
		return 0;

	cmdbuf_base = host1x_bo_mmap(g->bo);
	if (!cmdbuf_base)
		return -ENOMEM;

	fw.words = g->words;
	while (fw.words && !err) {
		u32 word = cmdbuf_base[fw.offset];
		u32 opcode = (word & 0xf0000000) >> 28;

		fw.mask = 0;
		fw.reg = 0;
		fw.count = 0;
		fw.words--;
		fw.offset++;

		switch (opcode) {
		case 0:
			fw.class = word >> 6 & 0x3ff;
			fw.mask = word & 0x3f;
			fw.reg = word >> 16 & 0xfff;
			err = check_mask(&fw);
			if (err)
				goto out;
			break;
		case 1:
			fw.reg = word >> 16 & 0xfff;
			fw.count = word & 0xffff;
			err = check_incr(&fw);
			if (err)
				goto out;
			break;

		case 2:
			fw.reg = word >> 16 & 0xfff;
			fw.count = word & 0xffff;
			err = check_nonincr(&fw);
			if (err)
				goto out;
			break;

		case 3:
			fw.mask = word & 0xffff;
			fw.reg = word >> 16 & 0xfff;
			err = check_mask(&fw);
			if (err)
				goto out;
			break;
		case 4:
		case 5:
		case 14:
			break;
		default:
			err = -EINVAL;
			break;
		}
	}

	/* No relocs should remain at this point */
	if (fw.num_relocs)
		err = -EINVAL;

out:
	host1x_bo_munmap(g->bo, cmdbuf_base);

	return err;
}

static inline int copy_gathers(struct host1x_job *job, struct device *dev)
{
	size_t size = 0;
	size_t offset = 0;
	int i;

	for (i = 0; i < job->num_gathers; i++) {
		struct host1x_job_gather *g = &job->gathers[i];
		size += g->words * sizeof(u32);
	}

	job->gather_copy_mapped = dma_alloc_writecombine(dev, size,
							 &job->gather_copy,
							 GFP_KERNEL);
	if (!job->gather_copy_mapped) {
		int err = PTR_ERR(job->gather_copy_mapped);
		job->gather_copy_mapped = NULL;
		return err;
	}

	job->gather_copy_size = size;

	for (i = 0; i < job->num_gathers; i++) {
		struct host1x_job_gather *g = &job->gathers[i];
		void *gather;

		gather = host1x_bo_mmap(g->bo);
		memcpy(job->gather_copy_mapped + offset, gather + g->offset,
		       g->words * sizeof(u32));
		host1x_bo_munmap(g->bo, gather);

		g->base = job->gather_copy;
		g->offset = offset;
		g->bo = NULL;

		offset += g->words * sizeof(u32);
	}

	return 0;
}

int host1x_job_pin(struct host1x_job *job, struct device *dev)
{
	int err;
	unsigned int i, j;
	struct host1x *host = dev_get_drvdata(dev->parent);
	DECLARE_BITMAP(waitchk_mask, host1x_syncpt_nb_pts(host));

	bitmap_zero(waitchk_mask, host1x_syncpt_nb_pts(host));
	for (i = 0; i < job->num_waitchk; i++) {
		u32 syncpt_id = job->waitchk[i].syncpt_id;
		if (syncpt_id < host1x_syncpt_nb_pts(host))
			set_bit(syncpt_id, waitchk_mask);
	}

	/* get current syncpt values for waitchk */
	for_each_set_bit(i, waitchk_mask, host1x_syncpt_nb_pts(host))
		host1x_syncpt_load(host->syncpt + i);

	/* pin memory */
	err = pin_job(job);
	if (!err)
		goto out;

	/* patch gathers */
	for (i = 0; i < job->num_gathers; i++) {
		struct host1x_job_gather *g = &job->gathers[i];

		/* process each gather mem only once */
		if (g->handled)
			continue;

		g->base = job->gather_addr_phys[i];

		for (j = 0; j < job->num_gathers; j++)
			if (job->gathers[j].bo == g->bo)
				job->gathers[j].handled = true;

		err = 0;

		if (IS_ENABLED(CONFIG_TEGRA_HOST1X_FIREWALL))
			err = validate(job, dev, g);

		if (err)
			dev_err(dev, "Job invalid (err=%d)\n", err);

		if (!err)
			err = do_relocs(job, g->bo);

		if (!err)
			err = do_waitchks(job, host, g->bo);

		if (err)
			break;
	}

	if (IS_ENABLED(CONFIG_TEGRA_HOST1X_FIREWALL) && !err) {
		err = copy_gathers(job, dev);
		if (err) {
			host1x_job_unpin(job);
			return err;
		}
	}

out:
	wmb();

	return err;
}

void host1x_job_unpin(struct host1x_job *job)
{
	unsigned int i;

	for (i = 0; i < job->num_unpins; i++) {
		struct host1x_job_unpin_data *unpin = &job->unpins[i];
		host1x_bo_unpin(unpin->bo, unpin->sgt);
		host1x_bo_put(unpin->bo);
	}
	job->num_unpins = 0;

	if (job->gather_copy_size)
		dma_free_writecombine(job->channel->dev, job->gather_copy_size,
				      job->gather_copy_mapped,
				      job->gather_copy);
}

/*
 * Debug routine used to dump job entries
 */
void host1x_job_dump(struct device *dev, struct host1x_job *job)
{
	dev_dbg(dev, "    SYNCPT_ID   %d\n", job->syncpt_id);
	dev_dbg(dev, "    SYNCPT_VAL  %d\n", job->syncpt_end);
	dev_dbg(dev, "    FIRST_GET   0x%x\n", job->first_get);
	dev_dbg(dev, "    TIMEOUT     %d\n", job->timeout);
	dev_dbg(dev, "    NUM_SLOTS   %d\n", job->num_slots);
	dev_dbg(dev, "    NUM_HANDLES %d\n", job->num_unpins);
}
