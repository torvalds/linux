/*
 * Cell Broadband Engine OProfile Support
 *
 * (C) Copyright IBM Corporation 2006
 *
 * Author: Maynard Johnson <maynardj@us.ibm.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

/* The purpose of this file is to handle SPU event task switching
 * and to record SPU context information into the OProfile
 * event buffer.
 *
 * Additionally, the spu_sync_buffer function is provided as a helper
 * for recoding actual SPU program counter samples to the event buffer.
 */
#include <linux/dcookies.h>
#include <linux/kref.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/numa.h>
#include <linux/oprofile.h>
#include <linux/spinlock.h>
#include "pr_util.h"

#define RELEASE_ALL 9999

static DEFINE_SPINLOCK(buffer_lock);
static DEFINE_SPINLOCK(cache_lock);
static int num_spu_nodes;
int spu_prof_num_nodes;
int last_guard_val[MAX_NUMNODES * 8];

/* Container for caching information about an active SPU task. */
struct cached_info {
	struct vma_to_fileoffset_map *map;
	struct spu *the_spu;	/* needed to access pointer to local_store */
	struct kref cache_ref;
};

static struct cached_info *spu_info[MAX_NUMNODES * 8];

static void destroy_cached_info(struct kref *kref)
{
	struct cached_info *info;

	info = container_of(kref, struct cached_info, cache_ref);
	vma_map_free(info->map);
	kfree(info);
	module_put(THIS_MODULE);
}

/* Return the cached_info for the passed SPU number.
 * ATTENTION:  Callers are responsible for obtaining the
 *	       cache_lock if needed prior to invoking this function.
 */
static struct cached_info *get_cached_info(struct spu *the_spu, int spu_num)
{
	struct kref *ref;
	struct cached_info *ret_info;

	if (spu_num >= num_spu_nodes) {
		printk(KERN_ERR "SPU_PROF: "
		       "%s, line %d: Invalid index %d into spu info cache\n",
		       __FUNCTION__, __LINE__, spu_num);
		ret_info = NULL;
		goto out;
	}
	if (!spu_info[spu_num] && the_spu) {
		ref = spu_get_profile_private_kref(the_spu->ctx);
		if (ref) {
			spu_info[spu_num] = container_of(ref, struct cached_info, cache_ref);
			kref_get(&spu_info[spu_num]->cache_ref);
		}
	}

	ret_info = spu_info[spu_num];
 out:
	return ret_info;
}


/* Looks for cached info for the passed spu.  If not found, the
 * cached info is created for the passed spu.
 * Returns 0 for success; otherwise, -1 for error.
 */
static int
prepare_cached_spu_info(struct spu *spu, unsigned long objectId)
{
	unsigned long flags;
	struct vma_to_fileoffset_map *new_map;
	int retval = 0;
	struct cached_info *info;

	/* We won't bother getting cache_lock here since
	 * don't do anything with the cached_info that's returned.
	 */
	info = get_cached_info(spu, spu->number);

	if (info) {
		pr_debug("Found cached SPU info.\n");
		goto out;
	}

	/* Create cached_info and set spu_info[spu->number] to point to it.
	 * spu->number is a system-wide value, not a per-node value.
	 */
	info = kzalloc(sizeof(struct cached_info), GFP_KERNEL);
	if (!info) {
		printk(KERN_ERR "SPU_PROF: "
		       "%s, line %d: create vma_map failed\n",
		       __FUNCTION__, __LINE__);
		retval = -ENOMEM;
		goto err_alloc;
	}
	new_map = create_vma_map(spu, objectId);
	if (!new_map) {
		printk(KERN_ERR "SPU_PROF: "
		       "%s, line %d: create vma_map failed\n",
		       __FUNCTION__, __LINE__);
		retval = -ENOMEM;
		goto err_alloc;
	}

	pr_debug("Created vma_map\n");
	info->map = new_map;
	info->the_spu = spu;
	kref_init(&info->cache_ref);
	spin_lock_irqsave(&cache_lock, flags);
	spu_info[spu->number] = info;
	/* Increment count before passing off ref to SPUFS. */
	kref_get(&info->cache_ref);

	/* We increment the module refcount here since SPUFS is
	 * responsible for the final destruction of the cached_info,
	 * and it must be able to access the destroy_cached_info()
	 * function defined in the OProfile module.  We decrement
	 * the module refcount in destroy_cached_info.
	 */
	try_module_get(THIS_MODULE);
	spu_set_profile_private_kref(spu->ctx, &info->cache_ref,
				destroy_cached_info);
	spin_unlock_irqrestore(&cache_lock, flags);
	goto out;

err_alloc:
	kfree(info);
out:
	return retval;
}

/*
 * NOTE:  The caller is responsible for locking the
 *	  cache_lock prior to calling this function.
 */
static int release_cached_info(int spu_index)
{
	int index, end;

	if (spu_index == RELEASE_ALL) {
		end = num_spu_nodes;
		index = 0;
	} else {
		if (spu_index >= num_spu_nodes) {
			printk(KERN_ERR "SPU_PROF: "
				"%s, line %d: "
				"Invalid index %d into spu info cache\n",
				__FUNCTION__, __LINE__, spu_index);
			goto out;
		}
		end = spu_index + 1;
		index = spu_index;
	}
	for (; index < end; index++) {
		if (spu_info[index]) {
			kref_put(&spu_info[index]->cache_ref,
				 destroy_cached_info);
			spu_info[index] = NULL;
		}
	}

out:
	return 0;
}

/* The source code for fast_get_dcookie was "borrowed"
 * from drivers/oprofile/buffer_sync.c.
 */

/* Optimisation. We can manage without taking the dcookie sem
 * because we cannot reach this code without at least one
 * dcookie user still being registered (namely, the reader
 * of the event buffer).
 */
static inline unsigned long fast_get_dcookie(struct dentry *dentry,
					     struct vfsmount *vfsmnt)
{
	unsigned long cookie;

	if (dentry->d_cookie)
		return (unsigned long)dentry;
	get_dcookie(dentry, vfsmnt, &cookie);
	return cookie;
}

/* Look up the dcookie for the task's first VM_EXECUTABLE mapping,
 * which corresponds loosely to "application name". Also, determine
 * the offset for the SPU ELF object.  If computed offset is
 * non-zero, it implies an embedded SPU object; otherwise, it's a
 * separate SPU binary, in which case we retrieve it's dcookie.
 * For the embedded case, we must determine if SPU ELF is embedded
 * in the executable application or another file (i.e., shared lib).
 * If embedded in a shared lib, we must get the dcookie and return
 * that to the caller.
 */
static unsigned long
get_exec_dcookie_and_offset(struct spu *spu, unsigned int *offsetp,
			    unsigned long *spu_bin_dcookie,
			    unsigned long spu_ref)
{
	unsigned long app_cookie = 0;
	unsigned int my_offset = 0;
	struct file *app = NULL;
	struct vm_area_struct *vma;
	struct mm_struct *mm = spu->mm;

	if (!mm)
		goto out;

	down_read(&mm->mmap_sem);

	for (vma = mm->mmap; vma; vma = vma->vm_next) {
		if (!vma->vm_file)
			continue;
		if (!(vma->vm_flags & VM_EXECUTABLE))
			continue;
		app_cookie = fast_get_dcookie(vma->vm_file->f_dentry,
					  vma->vm_file->f_vfsmnt);
		pr_debug("got dcookie for %s\n",
			 vma->vm_file->f_dentry->d_name.name);
		app = vma->vm_file;
		break;
	}

	for (vma = mm->mmap; vma; vma = vma->vm_next) {
		if (vma->vm_start > spu_ref || vma->vm_end <= spu_ref)
			continue;
		my_offset = spu_ref - vma->vm_start;
		if (!vma->vm_file)
			goto fail_no_image_cookie;

		pr_debug("Found spu ELF at %X(object-id:%lx) for file %s\n",
			 my_offset, spu_ref,
			 vma->vm_file->f_dentry->d_name.name);
		*offsetp = my_offset;
		break;
	}

	*spu_bin_dcookie = fast_get_dcookie(vma->vm_file->f_dentry,
						 vma->vm_file->f_vfsmnt);
	pr_debug("got dcookie for %s\n", vma->vm_file->f_dentry->d_name.name);

	up_read(&mm->mmap_sem);

out:
	return app_cookie;

fail_no_image_cookie:
	up_read(&mm->mmap_sem);

	printk(KERN_ERR "SPU_PROF: "
		"%s, line %d: Cannot find dcookie for SPU binary\n",
		__FUNCTION__, __LINE__);
	goto out;
}



/* This function finds or creates cached context information for the
 * passed SPU and records SPU context information into the OProfile
 * event buffer.
 */
static int process_context_switch(struct spu *spu, unsigned long objectId)
{
	unsigned long flags;
	int retval;
	unsigned int offset = 0;
	unsigned long spu_cookie = 0, app_dcookie;

	retval = prepare_cached_spu_info(spu, objectId);
	if (retval)
		goto out;

	/* Get dcookie first because a mutex_lock is taken in that
	 * code path, so interrupts must not be disabled.
	 */
	app_dcookie = get_exec_dcookie_and_offset(spu, &offset, &spu_cookie, objectId);
	if (!app_dcookie || !spu_cookie) {
		retval  = -ENOENT;
		goto out;
	}

	/* Record context info in event buffer */
	spin_lock_irqsave(&buffer_lock, flags);
	add_event_entry(ESCAPE_CODE);
	add_event_entry(SPU_CTX_SWITCH_CODE);
	add_event_entry(spu->number);
	add_event_entry(spu->pid);
	add_event_entry(spu->tgid);
	add_event_entry(app_dcookie);
	add_event_entry(spu_cookie);
	add_event_entry(offset);
	spin_unlock_irqrestore(&buffer_lock, flags);
	smp_wmb();	/* insure spu event buffer updates are written */
			/* don't want entries intermingled... */
out:
	return retval;
}

/*
 * This function is invoked on either a bind_context or unbind_context.
 * If called for an unbind_context, the val arg is 0; otherwise,
 * it is the object-id value for the spu context.
 * The data arg is of type 'struct spu *'.
 */
static int spu_active_notify(struct notifier_block *self, unsigned long val,
				void *data)
{
	int retval;
	unsigned long flags;
	struct spu *the_spu = data;

	pr_debug("SPU event notification arrived\n");
	if (!val) {
		spin_lock_irqsave(&cache_lock, flags);
		retval = release_cached_info(the_spu->number);
		spin_unlock_irqrestore(&cache_lock, flags);
	} else {
		retval = process_context_switch(the_spu, val);
	}
	return retval;
}

static struct notifier_block spu_active = {
	.notifier_call = spu_active_notify,
};

static int number_of_online_nodes(void)
{
        u32 cpu; u32 tmp;
        int nodes = 0;
        for_each_online_cpu(cpu) {
                tmp = cbe_cpu_to_node(cpu) + 1;
                if (tmp > nodes)
                        nodes++;
        }
        return nodes;
}

/* The main purpose of this function is to synchronize
 * OProfile with SPUFS by registering to be notified of
 * SPU task switches.
 *
 * NOTE: When profiling SPUs, we must ensure that only
 * spu_sync_start is invoked and not the generic sync_start
 * in drivers/oprofile/oprof.c.	 A return value of
 * SKIP_GENERIC_SYNC or SYNC_START_ERROR will
 * accomplish this.
 */
int spu_sync_start(void)
{
	int k;
	int ret = SKIP_GENERIC_SYNC;
	int register_ret;
	unsigned long flags = 0;

	spu_prof_num_nodes = number_of_online_nodes();
	num_spu_nodes = spu_prof_num_nodes * 8;

	spin_lock_irqsave(&buffer_lock, flags);
	add_event_entry(ESCAPE_CODE);
	add_event_entry(SPU_PROFILING_CODE);
	add_event_entry(num_spu_nodes);
	spin_unlock_irqrestore(&buffer_lock, flags);

	/* Register for SPU events  */
	register_ret = spu_switch_event_register(&spu_active);
	if (register_ret) {
		ret = SYNC_START_ERROR;
		goto out;
	}

	for (k = 0; k < (MAX_NUMNODES * 8); k++)
		last_guard_val[k] = 0;
	pr_debug("spu_sync_start -- running.\n");
out:
	return ret;
}

/* Record SPU program counter samples to the oprofile event buffer. */
void spu_sync_buffer(int spu_num, unsigned int *samples,
		     int num_samples)
{
	unsigned long long file_offset;
	unsigned long flags;
	int i;
	struct vma_to_fileoffset_map *map;
	struct spu *the_spu;
	unsigned long long spu_num_ll = spu_num;
	unsigned long long spu_num_shifted = spu_num_ll << 32;
	struct cached_info *c_info;

	/* We need to obtain the cache_lock here because it's
	 * possible that after getting the cached_info, the SPU job
	 * corresponding to this cached_info may end, thus resulting
	 * in the destruction of the cached_info.
	 */
	spin_lock_irqsave(&cache_lock, flags);
	c_info = get_cached_info(NULL, spu_num);
	if (!c_info) {
		/* This legitimately happens when the SPU task ends before all
		 * samples are recorded.
		 * No big deal -- so we just drop a few samples.
		 */
		pr_debug("SPU_PROF: No cached SPU contex "
			  "for SPU #%d. Dropping samples.\n", spu_num);
		goto out;
	}

	map = c_info->map;
	the_spu = c_info->the_spu;
	spin_lock(&buffer_lock);
	for (i = 0; i < num_samples; i++) {
		unsigned int sample = *(samples+i);
		int grd_val = 0;
		file_offset = 0;
		if (sample == 0)
			continue;
		file_offset = vma_map_lookup( map, sample, the_spu, &grd_val);

		/* If overlays are used by this SPU application, the guard
		 * value is non-zero, indicating which overlay section is in
		 * use.	 We need to discard samples taken during the time
		 * period which an overlay occurs (i.e., guard value changes).
		 */
		if (grd_val && grd_val != last_guard_val[spu_num]) {
			last_guard_val[spu_num] = grd_val;
			/* Drop the rest of the samples. */
			break;
		}

		add_event_entry(file_offset | spu_num_shifted);
	}
	spin_unlock(&buffer_lock);
out:
	spin_unlock_irqrestore(&cache_lock, flags);
}


int spu_sync_stop(void)
{
	unsigned long flags = 0;
	int ret = spu_switch_event_unregister(&spu_active);
	if (ret) {
		printk(KERN_ERR "SPU_PROF: "
			"%s, line %d: spu_switch_event_unregister returned %d\n",
			__FUNCTION__, __LINE__, ret);
		goto out;
	}

	spin_lock_irqsave(&cache_lock, flags);
	ret = release_cached_info(RELEASE_ALL);
	spin_unlock_irqrestore(&cache_lock, flags);
out:
	pr_debug("spu_sync_stop -- done.\n");
	return ret;
}


