/*
 * System Trace Module (STM) infrastructure
 * Copyright (c) 2014, Intel Corporation.
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
 * STM class implements generic infrastructure for  System Trace Module devices
 * as defined in MIPI STPv2 specification.
 */

#include <linux/pm_runtime.h>
#include <linux/uaccess.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/compat.h>
#include <linux/kdev_t.h>
#include <linux/srcu.h>
#include <linux/slab.h>
#include <linux/stm.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include "stm.h"

#include <uapi/linux/stm.h>

static unsigned int stm_core_up;

/*
 * The SRCU here makes sure that STM device doesn't disappear from under a
 * stm_source_write() caller, which may want to have as little overhead as
 * possible.
 */
static struct srcu_struct stm_source_srcu;

static ssize_t masters_show(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	struct stm_device *stm = to_stm_device(dev);
	int ret;

	ret = sprintf(buf, "%u %u\n", stm->data->sw_start, stm->data->sw_end);

	return ret;
}

static DEVICE_ATTR_RO(masters);

static ssize_t channels_show(struct device *dev,
			     struct device_attribute *attr,
			     char *buf)
{
	struct stm_device *stm = to_stm_device(dev);
	int ret;

	ret = sprintf(buf, "%u\n", stm->data->sw_nchannels);

	return ret;
}

static DEVICE_ATTR_RO(channels);

static ssize_t hw_override_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct stm_device *stm = to_stm_device(dev);
	int ret;

	ret = sprintf(buf, "%u\n", stm->data->hw_override);

	return ret;
}

static DEVICE_ATTR_RO(hw_override);

static struct attribute *stm_attrs[] = {
	&dev_attr_masters.attr,
	&dev_attr_channels.attr,
	&dev_attr_hw_override.attr,
	NULL,
};

ATTRIBUTE_GROUPS(stm);

static struct class stm_class = {
	.name		= "stm",
	.dev_groups	= stm_groups,
};

static int stm_dev_match(struct device *dev, const void *data)
{
	const char *name = data;

	return sysfs_streq(name, dev_name(dev));
}

/**
 * stm_find_device() - find stm device by name
 * @buf:	character buffer containing the name
 *
 * This is called when either policy gets assigned to an stm device or an
 * stm_source device gets linked to an stm device.
 *
 * This grabs device's reference (get_device()) and module reference, both
 * of which the calling path needs to make sure to drop with stm_put_device().
 *
 * Return:	stm device pointer or null if lookup failed.
 */
struct stm_device *stm_find_device(const char *buf)
{
	struct stm_device *stm;
	struct device *dev;

	if (!stm_core_up)
		return NULL;

	dev = class_find_device(&stm_class, NULL, buf, stm_dev_match);
	if (!dev)
		return NULL;

	stm = to_stm_device(dev);
	if (!try_module_get(stm->owner)) {
		/* matches class_find_device() above */
		put_device(dev);
		return NULL;
	}

	return stm;
}

/**
 * stm_put_device() - drop references on the stm device
 * @stm:	stm device, previously acquired by stm_find_device()
 *
 * This drops the module reference and device reference taken by
 * stm_find_device() or stm_char_open().
 */
void stm_put_device(struct stm_device *stm)
{
	module_put(stm->owner);
	put_device(&stm->dev);
}

/*
 * Internally we only care about software-writable masters here, that is the
 * ones in the range [stm_data->sw_start..stm_data..sw_end], however we need
 * original master numbers to be visible externally, since they are the ones
 * that will appear in the STP stream. Thus, the internal bookkeeping uses
 * $master - stm_data->sw_start to reference master descriptors and such.
 */

#define __stm_master(_s, _m)				\
	((_s)->masters[(_m) - (_s)->data->sw_start])

static inline struct stp_master *
stm_master(struct stm_device *stm, unsigned int idx)
{
	if (idx < stm->data->sw_start || idx > stm->data->sw_end)
		return NULL;

	return __stm_master(stm, idx);
}

static int stp_master_alloc(struct stm_device *stm, unsigned int idx)
{
	struct stp_master *master;
	size_t size;

	size = ALIGN(stm->data->sw_nchannels, 8) / 8;
	size += sizeof(struct stp_master);
	master = kzalloc(size, GFP_ATOMIC);
	if (!master)
		return -ENOMEM;

	master->nr_free = stm->data->sw_nchannels;
	__stm_master(stm, idx) = master;

	return 0;
}

static void stp_master_free(struct stm_device *stm, unsigned int idx)
{
	struct stp_master *master = stm_master(stm, idx);

	if (!master)
		return;

	__stm_master(stm, idx) = NULL;
	kfree(master);
}

static void stm_output_claim(struct stm_device *stm, struct stm_output *output)
{
	struct stp_master *master = stm_master(stm, output->master);

	lockdep_assert_held(&stm->mc_lock);
	lockdep_assert_held(&output->lock);

	if (WARN_ON_ONCE(master->nr_free < output->nr_chans))
		return;

	bitmap_allocate_region(&master->chan_map[0], output->channel,
			       ilog2(output->nr_chans));

	master->nr_free -= output->nr_chans;
}

static void
stm_output_disclaim(struct stm_device *stm, struct stm_output *output)
{
	struct stp_master *master = stm_master(stm, output->master);

	lockdep_assert_held(&stm->mc_lock);
	lockdep_assert_held(&output->lock);

	bitmap_release_region(&master->chan_map[0], output->channel,
			      ilog2(output->nr_chans));

	output->nr_chans = 0;
	master->nr_free += output->nr_chans;
}

/*
 * This is like bitmap_find_free_region(), except it can ignore @start bits
 * at the beginning.
 */
static int find_free_channels(unsigned long *bitmap, unsigned int start,
			      unsigned int end, unsigned int width)
{
	unsigned int pos;
	int i;

	for (pos = start; pos < end + 1; pos = ALIGN(pos, width)) {
		pos = find_next_zero_bit(bitmap, end + 1, pos);
		if (pos + width > end + 1)
			break;

		if (pos & (width - 1))
			continue;

		for (i = 1; i < width && !test_bit(pos + i, bitmap); i++)
			;
		if (i == width)
			return pos;
	}

	return -1;
}

static int
stm_find_master_chan(struct stm_device *stm, unsigned int width,
		     unsigned int *mstart, unsigned int mend,
		     unsigned int *cstart, unsigned int cend)
{
	struct stp_master *master;
	unsigned int midx;
	int pos, err;

	for (midx = *mstart; midx <= mend; midx++) {
		if (!stm_master(stm, midx)) {
			err = stp_master_alloc(stm, midx);
			if (err)
				return err;
		}

		master = stm_master(stm, midx);

		if (!master->nr_free)
			continue;

		pos = find_free_channels(master->chan_map, *cstart, cend,
					 width);
		if (pos < 0)
			continue;

		*mstart = midx;
		*cstart = pos;
		return 0;
	}

	return -ENOSPC;
}

static int stm_output_assign(struct stm_device *stm, unsigned int width,
			     struct stp_policy_node *policy_node,
			     struct stm_output *output)
{
	unsigned int midx, cidx, mend, cend;
	int ret = -EINVAL;

	if (width > stm->data->sw_nchannels)
		return -EINVAL;

	if (policy_node) {
		stp_policy_node_get_ranges(policy_node,
					   &midx, &mend, &cidx, &cend);
	} else {
		midx = stm->data->sw_start;
		cidx = 0;
		mend = stm->data->sw_end;
		cend = stm->data->sw_nchannels - 1;
	}

	spin_lock(&stm->mc_lock);
	spin_lock(&output->lock);
	/* output is already assigned -- shouldn't happen */
	if (WARN_ON_ONCE(output->nr_chans))
		goto unlock;

	ret = stm_find_master_chan(stm, width, &midx, mend, &cidx, cend);
	if (ret < 0)
		goto unlock;

	output->master = midx;
	output->channel = cidx;
	output->nr_chans = width;
	stm_output_claim(stm, output);
	dev_dbg(&stm->dev, "assigned %u:%u (+%u)\n", midx, cidx, width);

	ret = 0;
unlock:
	spin_unlock(&output->lock);
	spin_unlock(&stm->mc_lock);

	return ret;
}

static void stm_output_free(struct stm_device *stm, struct stm_output *output)
{
	spin_lock(&stm->mc_lock);
	spin_lock(&output->lock);
	if (output->nr_chans)
		stm_output_disclaim(stm, output);
	spin_unlock(&output->lock);
	spin_unlock(&stm->mc_lock);
}

static void stm_output_init(struct stm_output *output)
{
	spin_lock_init(&output->lock);
}

static int major_match(struct device *dev, const void *data)
{
	unsigned int major = *(unsigned int *)data;

	return MAJOR(dev->devt) == major;
}

static int stm_char_open(struct inode *inode, struct file *file)
{
	struct stm_file *stmf;
	struct device *dev;
	unsigned int major = imajor(inode);
	int err = -ENOMEM;

	dev = class_find_device(&stm_class, NULL, &major, major_match);
	if (!dev)
		return -ENODEV;

	stmf = kzalloc(sizeof(*stmf), GFP_KERNEL);
	if (!stmf)
		goto err_put_device;

	err = -ENODEV;
	stm_output_init(&stmf->output);
	stmf->stm = to_stm_device(dev);

	if (!try_module_get(stmf->stm->owner))
		goto err_free;

	file->private_data = stmf;

	return nonseekable_open(inode, file);

err_free:
	kfree(stmf);
err_put_device:
	/* matches class_find_device() above */
	put_device(dev);

	return err;
}

static int stm_char_release(struct inode *inode, struct file *file)
{
	struct stm_file *stmf = file->private_data;
	struct stm_device *stm = stmf->stm;

	if (stm->data->unlink)
		stm->data->unlink(stm->data, stmf->output.master,
				  stmf->output.channel);

	stm_output_free(stm, &stmf->output);

	/*
	 * matches the stm_char_open()'s
	 * class_find_device() + try_module_get()
	 */
	stm_put_device(stm);
	kfree(stmf);

	return 0;
}

static int stm_file_assign(struct stm_file *stmf, char *id, unsigned int width)
{
	struct stm_device *stm = stmf->stm;
	int ret;

	stmf->policy_node = stp_policy_node_lookup(stm, id);

	ret = stm_output_assign(stm, width, stmf->policy_node, &stmf->output);

	if (stmf->policy_node)
		stp_policy_node_put(stmf->policy_node);

	return ret;
}

static ssize_t notrace stm_write(struct stm_data *data, unsigned int master,
			  unsigned int channel, const char *buf, size_t count)
{
	unsigned int flags = STP_PACKET_TIMESTAMPED;
	const unsigned char *p = buf, nil = 0;
	size_t pos;
	ssize_t sz;

	for (pos = 0, p = buf; count > pos; pos += sz, p += sz) {
		sz = min_t(unsigned int, count - pos, 8);
		sz = data->packet(data, master, channel, STP_PACKET_DATA, flags,
				  sz, p);
		flags = 0;

		if (sz < 0)
			break;
	}

	data->packet(data, master, channel, STP_PACKET_FLAG, 0, 0, &nil);

	return pos;
}

static ssize_t stm_char_write(struct file *file, const char __user *buf,
			      size_t count, loff_t *ppos)
{
	struct stm_file *stmf = file->private_data;
	struct stm_device *stm = stmf->stm;
	char *kbuf;
	int err;

	if (count + 1 > PAGE_SIZE)
		count = PAGE_SIZE - 1;

	/*
	 * if no m/c have been assigned to this writer up to this
	 * point, use "default" policy entry
	 */
	if (!stmf->output.nr_chans) {
		err = stm_file_assign(stmf, "default", 1);
		/*
		 * EBUSY means that somebody else just assigned this
		 * output, which is just fine for write()
		 */
		if (err && err != -EBUSY)
			return err;
	}

	kbuf = kmalloc(count + 1, GFP_KERNEL);
	if (!kbuf)
		return -ENOMEM;

	err = copy_from_user(kbuf, buf, count);
	if (err) {
		kfree(kbuf);
		return -EFAULT;
	}

	pm_runtime_get_sync(&stm->dev);

	count = stm_write(stm->data, stmf->output.master, stmf->output.channel,
			  kbuf, count);

	pm_runtime_mark_last_busy(&stm->dev);
	pm_runtime_put_autosuspend(&stm->dev);
	kfree(kbuf);

	return count;
}

static void stm_mmap_open(struct vm_area_struct *vma)
{
	struct stm_file *stmf = vma->vm_file->private_data;
	struct stm_device *stm = stmf->stm;

	pm_runtime_get(&stm->dev);
}

static void stm_mmap_close(struct vm_area_struct *vma)
{
	struct stm_file *stmf = vma->vm_file->private_data;
	struct stm_device *stm = stmf->stm;

	pm_runtime_mark_last_busy(&stm->dev);
	pm_runtime_put_autosuspend(&stm->dev);
}

static const struct vm_operations_struct stm_mmap_vmops = {
	.open	= stm_mmap_open,
	.close	= stm_mmap_close,
};

static int stm_char_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct stm_file *stmf = file->private_data;
	struct stm_device *stm = stmf->stm;
	unsigned long size, phys;

	if (!stm->data->mmio_addr)
		return -EOPNOTSUPP;

	if (vma->vm_pgoff)
		return -EINVAL;

	size = vma->vm_end - vma->vm_start;

	if (stmf->output.nr_chans * stm->data->sw_mmiosz != size)
		return -EINVAL;

	phys = stm->data->mmio_addr(stm->data, stmf->output.master,
				    stmf->output.channel,
				    stmf->output.nr_chans);

	if (!phys)
		return -EINVAL;

	pm_runtime_get_sync(&stm->dev);

	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	vma->vm_flags |= VM_IO | VM_DONTEXPAND | VM_DONTDUMP;
	vma->vm_ops = &stm_mmap_vmops;
	vm_iomap_memory(vma, phys, size);

	return 0;
}

static int stm_char_policy_set_ioctl(struct stm_file *stmf, void __user *arg)
{
	struct stm_device *stm = stmf->stm;
	struct stp_policy_id *id;
	int ret = -EINVAL;
	u32 size;

	if (stmf->output.nr_chans)
		return -EBUSY;

	if (copy_from_user(&size, arg, sizeof(size)))
		return -EFAULT;

	if (size < sizeof(*id) || size >= PATH_MAX + sizeof(*id))
		return -EINVAL;

	/*
	 * size + 1 to make sure the .id string at the bottom is terminated,
	 * which is also why memdup_user() is not useful here
	 */
	id = kzalloc(size + 1, GFP_KERNEL);
	if (!id)
		return -ENOMEM;

	if (copy_from_user(id, arg, size)) {
		ret = -EFAULT;
		goto err_free;
	}

	if (id->__reserved_0 || id->__reserved_1)
		goto err_free;

	if (id->width < 1 ||
	    id->width > PAGE_SIZE / stm->data->sw_mmiosz)
		goto err_free;

	ret = stm_file_assign(stmf, id->id, id->width);
	if (ret)
		goto err_free;

	if (stm->data->link)
		ret = stm->data->link(stm->data, stmf->output.master,
				      stmf->output.channel);

	if (ret)
		stm_output_free(stmf->stm, &stmf->output);

err_free:
	kfree(id);

	return ret;
}

static int stm_char_policy_get_ioctl(struct stm_file *stmf, void __user *arg)
{
	struct stp_policy_id id = {
		.size		= sizeof(id),
		.master		= stmf->output.master,
		.channel	= stmf->output.channel,
		.width		= stmf->output.nr_chans,
		.__reserved_0	= 0,
		.__reserved_1	= 0,
	};

	return copy_to_user(arg, &id, id.size) ? -EFAULT : 0;
}

static long
stm_char_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct stm_file *stmf = file->private_data;
	struct stm_data *stm_data = stmf->stm->data;
	int err = -ENOTTY;
	u64 options;

	switch (cmd) {
	case STP_POLICY_ID_SET:
		err = stm_char_policy_set_ioctl(stmf, (void __user *)arg);
		if (err)
			return err;

		return stm_char_policy_get_ioctl(stmf, (void __user *)arg);

	case STP_POLICY_ID_GET:
		return stm_char_policy_get_ioctl(stmf, (void __user *)arg);

	case STP_SET_OPTIONS:
		if (copy_from_user(&options, (u64 __user *)arg, sizeof(u64)))
			return -EFAULT;

		if (stm_data->set_options)
			err = stm_data->set_options(stm_data,
						    stmf->output.master,
						    stmf->output.channel,
						    stmf->output.nr_chans,
						    options);

		break;
	default:
		break;
	}

	return err;
}

#ifdef CONFIG_COMPAT
static long
stm_char_compat_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	return stm_char_ioctl(file, cmd, (unsigned long)compat_ptr(arg));
}
#else
#define stm_char_compat_ioctl	NULL
#endif

static const struct file_operations stm_fops = {
	.open		= stm_char_open,
	.release	= stm_char_release,
	.write		= stm_char_write,
	.mmap		= stm_char_mmap,
	.unlocked_ioctl	= stm_char_ioctl,
	.compat_ioctl	= stm_char_compat_ioctl,
	.llseek		= no_llseek,
};

static void stm_device_release(struct device *dev)
{
	struct stm_device *stm = to_stm_device(dev);

	kfree(stm);
}

int stm_register_device(struct device *parent, struct stm_data *stm_data,
			struct module *owner)
{
	struct stm_device *stm;
	unsigned int nmasters;
	int err = -ENOMEM;

	if (!stm_core_up)
		return -EPROBE_DEFER;

	if (!stm_data->packet || !stm_data->sw_nchannels)
		return -EINVAL;

	nmasters = stm_data->sw_end - stm_data->sw_start + 1;
	stm = kzalloc(sizeof(*stm) + nmasters * sizeof(void *), GFP_KERNEL);
	if (!stm)
		return -ENOMEM;

	stm->major = register_chrdev(0, stm_data->name, &stm_fops);
	if (stm->major < 0)
		goto err_free;

	device_initialize(&stm->dev);
	stm->dev.devt = MKDEV(stm->major, 0);
	stm->dev.class = &stm_class;
	stm->dev.parent = parent;
	stm->dev.release = stm_device_release;

	mutex_init(&stm->link_mutex);
	spin_lock_init(&stm->link_lock);
	INIT_LIST_HEAD(&stm->link_list);

	/* initialize the object before it is accessible via sysfs */
	spin_lock_init(&stm->mc_lock);
	mutex_init(&stm->policy_mutex);
	stm->sw_nmasters = nmasters;
	stm->owner = owner;
	stm->data = stm_data;
	stm_data->stm = stm;

	err = kobject_set_name(&stm->dev.kobj, "%s", stm_data->name);
	if (err)
		goto err_device;

	err = device_add(&stm->dev);
	if (err)
		goto err_device;

	/*
	 * Use delayed autosuspend to avoid bouncing back and forth
	 * on recurring character device writes, with the initial
	 * delay time of 2 seconds.
	 */
	pm_runtime_no_callbacks(&stm->dev);
	pm_runtime_use_autosuspend(&stm->dev);
	pm_runtime_set_autosuspend_delay(&stm->dev, 2000);
	pm_runtime_set_suspended(&stm->dev);
	pm_runtime_enable(&stm->dev);

	return 0;

err_device:
	unregister_chrdev(stm->major, stm_data->name);

	/* matches device_initialize() above */
	put_device(&stm->dev);
err_free:
	kfree(stm);

	return err;
}
EXPORT_SYMBOL_GPL(stm_register_device);

static int __stm_source_link_drop(struct stm_source_device *src,
				  struct stm_device *stm);

void stm_unregister_device(struct stm_data *stm_data)
{
	struct stm_device *stm = stm_data->stm;
	struct stm_source_device *src, *iter;
	int i, ret;

	pm_runtime_dont_use_autosuspend(&stm->dev);
	pm_runtime_disable(&stm->dev);

	mutex_lock(&stm->link_mutex);
	list_for_each_entry_safe(src, iter, &stm->link_list, link_entry) {
		ret = __stm_source_link_drop(src, stm);
		/*
		 * src <-> stm link must not change under the same
		 * stm::link_mutex, so complain loudly if it has;
		 * also in this situation ret!=0 means this src is
		 * not connected to this stm and it should be otherwise
		 * safe to proceed with the tear-down of stm.
		 */
		WARN_ON_ONCE(ret);
	}
	mutex_unlock(&stm->link_mutex);

	synchronize_srcu(&stm_source_srcu);

	unregister_chrdev(stm->major, stm_data->name);

	mutex_lock(&stm->policy_mutex);
	if (stm->policy)
		stp_policy_unbind(stm->policy);
	mutex_unlock(&stm->policy_mutex);

	for (i = stm->data->sw_start; i <= stm->data->sw_end; i++)
		stp_master_free(stm, i);

	device_unregister(&stm->dev);
	stm_data->stm = NULL;
}
EXPORT_SYMBOL_GPL(stm_unregister_device);

/*
 * stm::link_list access serialization uses a spinlock and a mutex; holding
 * either of them guarantees that the list is stable; modification requires
 * holding both of them.
 *
 * Lock ordering is as follows:
 *   stm::link_mutex
 *     stm::link_lock
 *       src::link_lock
 */

/**
 * stm_source_link_add() - connect an stm_source device to an stm device
 * @src:	stm_source device
 * @stm:	stm device
 *
 * This function establishes a link from stm_source to an stm device so that
 * the former can send out trace data to the latter.
 *
 * Return:	0 on success, -errno otherwise.
 */
static int stm_source_link_add(struct stm_source_device *src,
			       struct stm_device *stm)
{
	char *id;
	int err;

	mutex_lock(&stm->link_mutex);
	spin_lock(&stm->link_lock);
	spin_lock(&src->link_lock);

	/* src->link is dereferenced under stm_source_srcu but not the list */
	rcu_assign_pointer(src->link, stm);
	list_add_tail(&src->link_entry, &stm->link_list);

	spin_unlock(&src->link_lock);
	spin_unlock(&stm->link_lock);
	mutex_unlock(&stm->link_mutex);

	id = kstrdup(src->data->name, GFP_KERNEL);
	if (id) {
		src->policy_node =
			stp_policy_node_lookup(stm, id);

		kfree(id);
	}

	err = stm_output_assign(stm, src->data->nr_chans,
				src->policy_node, &src->output);

	if (src->policy_node)
		stp_policy_node_put(src->policy_node);

	if (err)
		goto fail_detach;

	/* this is to notify the STM device that a new link has been made */
	if (stm->data->link)
		err = stm->data->link(stm->data, src->output.master,
				      src->output.channel);

	if (err)
		goto fail_free_output;

	/* this is to let the source carry out all necessary preparations */
	if (src->data->link)
		src->data->link(src->data);

	return 0;

fail_free_output:
	stm_output_free(stm, &src->output);

fail_detach:
	mutex_lock(&stm->link_mutex);
	spin_lock(&stm->link_lock);
	spin_lock(&src->link_lock);

	rcu_assign_pointer(src->link, NULL);
	list_del_init(&src->link_entry);

	spin_unlock(&src->link_lock);
	spin_unlock(&stm->link_lock);
	mutex_unlock(&stm->link_mutex);

	return err;
}

/**
 * __stm_source_link_drop() - detach stm_source from an stm device
 * @src:	stm_source device
 * @stm:	stm device
 *
 * If @stm is @src::link, disconnect them from one another and put the
 * reference on the @stm device.
 *
 * Caller must hold stm::link_mutex.
 */
static int __stm_source_link_drop(struct stm_source_device *src,
				  struct stm_device *stm)
{
	struct stm_device *link;
	int ret = 0;

	lockdep_assert_held(&stm->link_mutex);

	/* for stm::link_list modification, we hold both mutex and spinlock */
	spin_lock(&stm->link_lock);
	spin_lock(&src->link_lock);
	link = srcu_dereference_check(src->link, &stm_source_srcu, 1);

	/*
	 * The linked device may have changed since we last looked, because
	 * we weren't holding the src::link_lock back then; if this is the
	 * case, tell the caller to retry.
	 */
	if (link != stm) {
		ret = -EAGAIN;
		goto unlock;
	}

	stm_output_free(link, &src->output);
	list_del_init(&src->link_entry);
	pm_runtime_mark_last_busy(&link->dev);
	pm_runtime_put_autosuspend(&link->dev);
	/* matches stm_find_device() from stm_source_link_store() */
	stm_put_device(link);
	rcu_assign_pointer(src->link, NULL);

unlock:
	spin_unlock(&src->link_lock);
	spin_unlock(&stm->link_lock);

	/*
	 * Call the unlink callbacks for both source and stm, when we know
	 * that we have actually performed the unlinking.
	 */
	if (!ret) {
		if (src->data->unlink)
			src->data->unlink(src->data);

		if (stm->data->unlink)
			stm->data->unlink(stm->data, src->output.master,
					  src->output.channel);
	}

	return ret;
}

/**
 * stm_source_link_drop() - detach stm_source from its stm device
 * @src:	stm_source device
 *
 * Unlinking means disconnecting from source's STM device; after this
 * writes will be unsuccessful until it is linked to a new STM device.
 *
 * This will happen on "stm_source_link" sysfs attribute write to undo
 * the existing link (if any), or on linked STM device's de-registration.
 */
static void stm_source_link_drop(struct stm_source_device *src)
{
	struct stm_device *stm;
	int idx, ret;

retry:
	idx = srcu_read_lock(&stm_source_srcu);
	/*
	 * The stm device will be valid for the duration of this
	 * read section, but the link may change before we grab
	 * the src::link_lock in __stm_source_link_drop().
	 */
	stm = srcu_dereference(src->link, &stm_source_srcu);

	ret = 0;
	if (stm) {
		mutex_lock(&stm->link_mutex);
		ret = __stm_source_link_drop(src, stm);
		mutex_unlock(&stm->link_mutex);
	}

	srcu_read_unlock(&stm_source_srcu, idx);

	/* if it did change, retry */
	if (ret == -EAGAIN)
		goto retry;
}

static ssize_t stm_source_link_show(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	struct stm_source_device *src = to_stm_source_device(dev);
	struct stm_device *stm;
	int idx, ret;

	idx = srcu_read_lock(&stm_source_srcu);
	stm = srcu_dereference(src->link, &stm_source_srcu);
	ret = sprintf(buf, "%s\n",
		      stm ? dev_name(&stm->dev) : "<none>");
	srcu_read_unlock(&stm_source_srcu, idx);

	return ret;
}

static ssize_t stm_source_link_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct stm_source_device *src = to_stm_source_device(dev);
	struct stm_device *link;
	int err;

	stm_source_link_drop(src);

	link = stm_find_device(buf);
	if (!link)
		return -EINVAL;

	pm_runtime_get(&link->dev);

	err = stm_source_link_add(src, link);
	if (err) {
		pm_runtime_put_autosuspend(&link->dev);
		/* matches the stm_find_device() above */
		stm_put_device(link);
	}

	return err ? : count;
}

static DEVICE_ATTR_RW(stm_source_link);

static struct attribute *stm_source_attrs[] = {
	&dev_attr_stm_source_link.attr,
	NULL,
};

ATTRIBUTE_GROUPS(stm_source);

static struct class stm_source_class = {
	.name		= "stm_source",
	.dev_groups	= stm_source_groups,
};

static void stm_source_device_release(struct device *dev)
{
	struct stm_source_device *src = to_stm_source_device(dev);

	kfree(src);
}

/**
 * stm_source_register_device() - register an stm_source device
 * @parent:	parent device
 * @data:	device description structure
 *
 * This will create a device of stm_source class that can write
 * data to an stm device once linked.
 *
 * Return:	0 on success, -errno otherwise.
 */
int stm_source_register_device(struct device *parent,
			       struct stm_source_data *data)
{
	struct stm_source_device *src;
	int err;

	if (!stm_core_up)
		return -EPROBE_DEFER;

	src = kzalloc(sizeof(*src), GFP_KERNEL);
	if (!src)
		return -ENOMEM;

	device_initialize(&src->dev);
	src->dev.class = &stm_source_class;
	src->dev.parent = parent;
	src->dev.release = stm_source_device_release;

	err = kobject_set_name(&src->dev.kobj, "%s", data->name);
	if (err)
		goto err;

	pm_runtime_no_callbacks(&src->dev);
	pm_runtime_forbid(&src->dev);

	err = device_add(&src->dev);
	if (err)
		goto err;

	stm_output_init(&src->output);
	spin_lock_init(&src->link_lock);
	INIT_LIST_HEAD(&src->link_entry);
	src->data = data;
	data->src = src;

	return 0;

err:
	put_device(&src->dev);
	kfree(src);

	return err;
}
EXPORT_SYMBOL_GPL(stm_source_register_device);

/**
 * stm_source_unregister_device() - unregister an stm_source device
 * @data:	device description that was used to register the device
 *
 * This will remove a previously created stm_source device from the system.
 */
void stm_source_unregister_device(struct stm_source_data *data)
{
	struct stm_source_device *src = data->src;

	stm_source_link_drop(src);

	device_destroy(&stm_source_class, src->dev.devt);
}
EXPORT_SYMBOL_GPL(stm_source_unregister_device);

int notrace stm_source_write(struct stm_source_data *data,
			     unsigned int chan,
			     const char *buf, size_t count)
{
	struct stm_source_device *src = data->src;
	struct stm_device *stm;
	int idx;

	if (!src->output.nr_chans)
		return -ENODEV;

	if (chan >= src->output.nr_chans)
		return -EINVAL;

	idx = srcu_read_lock(&stm_source_srcu);

	stm = srcu_dereference(src->link, &stm_source_srcu);
	if (stm)
		count = stm_write(stm->data, src->output.master,
				  src->output.channel + chan,
				  buf, count);
	else
		count = -ENODEV;

	srcu_read_unlock(&stm_source_srcu, idx);

	return count;
}
EXPORT_SYMBOL_GPL(stm_source_write);

static int __init stm_core_init(void)
{
	int err;

	err = class_register(&stm_class);
	if (err)
		return err;

	err = class_register(&stm_source_class);
	if (err)
		goto err_stm;

	err = stp_configfs_init();
	if (err)
		goto err_src;

	init_srcu_struct(&stm_source_srcu);

	stm_core_up++;

	return 0;

err_src:
	class_unregister(&stm_source_class);
err_stm:
	class_unregister(&stm_class);

	return err;
}

module_init(stm_core_init);

static void __exit stm_core_exit(void)
{
	cleanup_srcu_struct(&stm_source_srcu);
	class_unregister(&stm_source_class);
	class_unregister(&stm_class);
	stp_configfs_exit();
}

module_exit(stm_core_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("System Trace Module device class");
MODULE_AUTHOR("Alexander Shishkin <alexander.shishkin@linux.intel.com>");
