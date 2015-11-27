/*
 * Copyright (C) 2015 IT University of Copenhagen. All rights reserved.
 * Initial release: Matias Bjorling <m@bjorling.me>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139,
 * USA.
 *
 */

#include <linux/blkdev.h>
#include <linux/blk-mq.h>
#include <linux/list.h>
#include <linux/types.h>
#include <linux/sem.h>
#include <linux/bitmap.h>
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/lightnvm.h>
#include <uapi/linux/lightnvm.h>

static LIST_HEAD(nvm_targets);
static LIST_HEAD(nvm_mgrs);
static LIST_HEAD(nvm_devices);
static DECLARE_RWSEM(nvm_lock);

static struct nvm_tgt_type *nvm_find_target_type(const char *name)
{
	struct nvm_tgt_type *tt;

	list_for_each_entry(tt, &nvm_targets, list)
		if (!strcmp(name, tt->name))
			return tt;

	return NULL;
}

int nvm_register_target(struct nvm_tgt_type *tt)
{
	int ret = 0;

	down_write(&nvm_lock);
	if (nvm_find_target_type(tt->name))
		ret = -EEXIST;
	else
		list_add(&tt->list, &nvm_targets);
	up_write(&nvm_lock);

	return ret;
}
EXPORT_SYMBOL(nvm_register_target);

void nvm_unregister_target(struct nvm_tgt_type *tt)
{
	if (!tt)
		return;

	down_write(&nvm_lock);
	list_del(&tt->list);
	up_write(&nvm_lock);
}
EXPORT_SYMBOL(nvm_unregister_target);

void *nvm_dev_dma_alloc(struct nvm_dev *dev, gfp_t mem_flags,
							dma_addr_t *dma_handler)
{
	return dev->ops->dev_dma_alloc(dev->q, dev->ppalist_pool, mem_flags,
								dma_handler);
}
EXPORT_SYMBOL(nvm_dev_dma_alloc);

void nvm_dev_dma_free(struct nvm_dev *dev, void *ppa_list,
							dma_addr_t dma_handler)
{
	dev->ops->dev_dma_free(dev->ppalist_pool, ppa_list, dma_handler);
}
EXPORT_SYMBOL(nvm_dev_dma_free);

static struct nvmm_type *nvm_find_mgr_type(const char *name)
{
	struct nvmm_type *mt;

	list_for_each_entry(mt, &nvm_mgrs, list)
		if (!strcmp(name, mt->name))
			return mt;

	return NULL;
}

int nvm_register_mgr(struct nvmm_type *mt)
{
	int ret = 0;

	down_write(&nvm_lock);
	if (nvm_find_mgr_type(mt->name))
		ret = -EEXIST;
	else
		list_add(&mt->list, &nvm_mgrs);
	up_write(&nvm_lock);

	return ret;
}
EXPORT_SYMBOL(nvm_register_mgr);

void nvm_unregister_mgr(struct nvmm_type *mt)
{
	if (!mt)
		return;

	down_write(&nvm_lock);
	list_del(&mt->list);
	up_write(&nvm_lock);
}
EXPORT_SYMBOL(nvm_unregister_mgr);

static struct nvm_dev *nvm_find_nvm_dev(const char *name)
{
	struct nvm_dev *dev;

	list_for_each_entry(dev, &nvm_devices, devices)
		if (!strcmp(name, dev->name))
			return dev;

	return NULL;
}

struct nvm_block *nvm_get_blk(struct nvm_dev *dev, struct nvm_lun *lun,
							unsigned long flags)
{
	return dev->mt->get_blk(dev, lun, flags);
}
EXPORT_SYMBOL(nvm_get_blk);

/* Assumes that all valid pages have already been moved on release to bm */
void nvm_put_blk(struct nvm_dev *dev, struct nvm_block *blk)
{
	return dev->mt->put_blk(dev, blk);
}
EXPORT_SYMBOL(nvm_put_blk);

int nvm_submit_io(struct nvm_dev *dev, struct nvm_rq *rqd)
{
	return dev->mt->submit_io(dev, rqd);
}
EXPORT_SYMBOL(nvm_submit_io);

int nvm_erase_blk(struct nvm_dev *dev, struct nvm_block *blk)
{
	return dev->mt->erase_blk(dev, blk, 0);
}
EXPORT_SYMBOL(nvm_erase_blk);

static void nvm_core_free(struct nvm_dev *dev)
{
	kfree(dev);
}

static int nvm_core_init(struct nvm_dev *dev)
{
	struct nvm_id *id = &dev->identity;
	struct nvm_id_group *grp = &id->groups[0];

	/* device values */
	dev->nr_chnls = grp->num_ch;
	dev->luns_per_chnl = grp->num_lun;
	dev->pgs_per_blk = grp->num_pg;
	dev->blks_per_lun = grp->num_blk;
	dev->nr_planes = grp->num_pln;
	dev->sec_size = grp->csecs;
	dev->oob_size = grp->sos;
	dev->sec_per_pg = grp->fpg_sz / grp->csecs;
	dev->addr_mode = id->ppat;
	dev->addr_format = id->ppaf;

	dev->plane_mode = NVM_PLANE_SINGLE;
	dev->max_rq_size = dev->ops->max_phys_sect * dev->sec_size;

	if (grp->mpos & 0x020202)
		dev->plane_mode = NVM_PLANE_DOUBLE;
	if (grp->mpos & 0x040404)
		dev->plane_mode = NVM_PLANE_QUAD;

	/* calculated values */
	dev->sec_per_pl = dev->sec_per_pg * dev->nr_planes;
	dev->sec_per_blk = dev->sec_per_pl * dev->pgs_per_blk;
	dev->sec_per_lun = dev->sec_per_blk * dev->blks_per_lun;
	dev->nr_luns = dev->luns_per_chnl * dev->nr_chnls;

	dev->total_blocks = dev->nr_planes *
				dev->blks_per_lun *
				dev->luns_per_chnl *
				dev->nr_chnls;
	dev->total_pages = dev->total_blocks * dev->pgs_per_blk;
	INIT_LIST_HEAD(&dev->online_targets);

	return 0;
}

static void nvm_free(struct nvm_dev *dev)
{
	if (!dev)
		return;

	if (dev->mt)
		dev->mt->unregister_mgr(dev);

	nvm_core_free(dev);
}

static int nvm_init(struct nvm_dev *dev)
{
	struct nvmm_type *mt;
	int ret = 0;

	if (!dev->q || !dev->ops)
		return -EINVAL;

	if (dev->ops->identity(dev->q, &dev->identity)) {
		pr_err("nvm: device could not be identified\n");
		ret = -EINVAL;
		goto err;
	}

	pr_debug("nvm: ver:%x nvm_vendor:%x groups:%u\n",
			dev->identity.ver_id, dev->identity.vmnt,
							dev->identity.cgrps);

	if (dev->identity.ver_id != 1) {
		pr_err("nvm: device not supported by kernel.");
		goto err;
	}

	if (dev->identity.cgrps != 1) {
		pr_err("nvm: only one group configuration supported.");
		goto err;
	}

	ret = nvm_core_init(dev);
	if (ret) {
		pr_err("nvm: could not initialize core structures.\n");
		goto err;
	}

	/* register with device with a supported manager */
	list_for_each_entry(mt, &nvm_mgrs, list) {
		ret = mt->register_mgr(dev);
		if (ret < 0)
			goto err; /* initialization failed */
		if (ret > 0) {
			dev->mt = mt;
			break; /* successfully initialized */
		}
	}

	if (!ret) {
		pr_info("nvm: no compatible manager found.\n");
		return 0;
	}

	pr_info("nvm: registered %s [%u/%u/%u/%u/%u/%u]\n",
			dev->name, dev->sec_per_pg, dev->nr_planes,
			dev->pgs_per_blk, dev->blks_per_lun, dev->nr_luns,
			dev->nr_chnls);
	return 0;
err:
	nvm_free(dev);
	pr_err("nvm: failed to initialize nvm\n");
	return ret;
}

static void nvm_exit(struct nvm_dev *dev)
{
	if (dev->ppalist_pool)
		dev->ops->destroy_dma_pool(dev->ppalist_pool);
	nvm_free(dev);

	pr_info("nvm: successfully unloaded\n");
}

int nvm_register(struct request_queue *q, char *disk_name,
							struct nvm_dev_ops *ops)
{
	struct nvm_dev *dev;
	int ret;

	if (!ops->identity)
		return -EINVAL;

	dev = kzalloc(sizeof(struct nvm_dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	dev->q = q;
	dev->ops = ops;
	strncpy(dev->name, disk_name, DISK_NAME_LEN);

	ret = nvm_init(dev);
	if (ret)
		goto err_init;

	down_write(&nvm_lock);
	list_add(&dev->devices, &nvm_devices);
	up_write(&nvm_lock);

	if (dev->ops->max_phys_sect > 1) {
		dev->ppalist_pool = dev->ops->create_dma_pool(dev->q,
								"ppalist");
		if (!dev->ppalist_pool) {
			pr_err("nvm: could not create ppa pool\n");
			return -ENOMEM;
		}
	} else if (dev->ops->max_phys_sect > 256) {
		pr_info("nvm: max sectors supported is 256.\n");
		return -EINVAL;
	}

	return 0;
err_init:
	kfree(dev);
	return ret;
}
EXPORT_SYMBOL(nvm_register);

void nvm_unregister(char *disk_name)
{
	struct nvm_dev *dev = nvm_find_nvm_dev(disk_name);

	if (!dev) {
		pr_err("nvm: could not find device %s to unregister\n",
								disk_name);
		return;
	}

	nvm_exit(dev);

	down_write(&nvm_lock);
	list_del(&dev->devices);
	up_write(&nvm_lock);
}
EXPORT_SYMBOL(nvm_unregister);

static const struct block_device_operations nvm_fops = {
	.owner		= THIS_MODULE,
};

static int nvm_create_target(struct nvm_dev *dev,
						struct nvm_ioctl_create *create)
{
	struct nvm_ioctl_create_simple *s = &create->conf.s;
	struct request_queue *tqueue;
	struct nvmm_type *mt;
	struct gendisk *tdisk;
	struct nvm_tgt_type *tt;
	struct nvm_target *t;
	void *targetdata;
	int ret = 0;

	if (!dev->mt) {
		/* register with device with a supported NVM manager */
		list_for_each_entry(mt, &nvm_mgrs, list) {
			ret = mt->register_mgr(dev);
			if (ret < 0)
				return ret; /* initialization failed */
			if (ret > 0) {
				dev->mt = mt;
				break; /* successfully initialized */
			}
		}

		if (!ret) {
			pr_info("nvm: no compatible nvm manager found.\n");
			return -ENODEV;
		}
	}

	tt = nvm_find_target_type(create->tgttype);
	if (!tt) {
		pr_err("nvm: target type %s not found\n", create->tgttype);
		return -EINVAL;
	}

	down_write(&nvm_lock);
	list_for_each_entry(t, &dev->online_targets, list) {
		if (!strcmp(create->tgtname, t->disk->disk_name)) {
			pr_err("nvm: target name already exists.\n");
			up_write(&nvm_lock);
			return -EINVAL;
		}
	}
	up_write(&nvm_lock);

	t = kmalloc(sizeof(struct nvm_target), GFP_KERNEL);
	if (!t)
		return -ENOMEM;

	tqueue = blk_alloc_queue_node(GFP_KERNEL, dev->q->node);
	if (!tqueue)
		goto err_t;
	blk_queue_make_request(tqueue, tt->make_rq);

	tdisk = alloc_disk(0);
	if (!tdisk)
		goto err_queue;

	sprintf(tdisk->disk_name, "%s", create->tgtname);
	tdisk->flags = GENHD_FL_EXT_DEVT;
	tdisk->major = 0;
	tdisk->first_minor = 0;
	tdisk->fops = &nvm_fops;
	tdisk->queue = tqueue;

	targetdata = tt->init(dev, tdisk, s->lun_begin, s->lun_end);
	if (IS_ERR(targetdata))
		goto err_init;

	tdisk->private_data = targetdata;
	tqueue->queuedata = targetdata;

	blk_queue_max_hw_sectors(tqueue, 8 * dev->ops->max_phys_sect);

	set_capacity(tdisk, tt->capacity(targetdata));
	add_disk(tdisk);

	t->type = tt;
	t->disk = tdisk;

	down_write(&nvm_lock);
	list_add_tail(&t->list, &dev->online_targets);
	up_write(&nvm_lock);

	return 0;
err_init:
	put_disk(tdisk);
err_queue:
	blk_cleanup_queue(tqueue);
err_t:
	kfree(t);
	return -ENOMEM;
}

static void nvm_remove_target(struct nvm_target *t)
{
	struct nvm_tgt_type *tt = t->type;
	struct gendisk *tdisk = t->disk;
	struct request_queue *q = tdisk->queue;

	lockdep_assert_held(&nvm_lock);

	del_gendisk(tdisk);
	if (tt->exit)
		tt->exit(tdisk->private_data);

	blk_cleanup_queue(q);

	put_disk(tdisk);

	list_del(&t->list);
	kfree(t);
}

static int __nvm_configure_create(struct nvm_ioctl_create *create)
{
	struct nvm_dev *dev;
	struct nvm_ioctl_create_simple *s;

	dev = nvm_find_nvm_dev(create->dev);
	if (!dev) {
		pr_err("nvm: device not found\n");
		return -EINVAL;
	}

	if (create->conf.type != NVM_CONFIG_TYPE_SIMPLE) {
		pr_err("nvm: config type not valid\n");
		return -EINVAL;
	}
	s = &create->conf.s;

	if (s->lun_begin > s->lun_end || s->lun_end > dev->nr_luns) {
		pr_err("nvm: lun out of bound (%u:%u > %u)\n",
			s->lun_begin, s->lun_end, dev->nr_luns);
		return -EINVAL;
	}

	return nvm_create_target(dev, create);
}

static int __nvm_configure_remove(struct nvm_ioctl_remove *remove)
{
	struct nvm_target *t = NULL;
	struct nvm_dev *dev;
	int ret = -1;

	down_write(&nvm_lock);
	list_for_each_entry(dev, &nvm_devices, devices)
		list_for_each_entry(t, &dev->online_targets, list) {
			if (!strcmp(remove->tgtname, t->disk->disk_name)) {
				nvm_remove_target(t);
				ret = 0;
				break;
			}
		}
	up_write(&nvm_lock);

	if (ret) {
		pr_err("nvm: target \"%s\" doesn't exist.\n", remove->tgtname);
		return -EINVAL;
	}

	return 0;
}

#ifdef CONFIG_NVM_DEBUG
static int nvm_configure_show(const char *val)
{
	struct nvm_dev *dev;
	char opcode, devname[DISK_NAME_LEN];
	int ret;

	ret = sscanf(val, "%c %32s", &opcode, devname);
	if (ret != 2) {
		pr_err("nvm: invalid command. Use \"opcode devicename\".\n");
		return -EINVAL;
	}

	dev = nvm_find_nvm_dev(devname);
	if (!dev) {
		pr_err("nvm: device not found\n");
		return -EINVAL;
	}

	if (!dev->mt)
		return 0;

	dev->mt->free_blocks_print(dev);

	return 0;
}

static int nvm_configure_remove(const char *val)
{
	struct nvm_ioctl_remove remove;
	char opcode;
	int ret;

	ret = sscanf(val, "%c %256s", &opcode, remove.tgtname);
	if (ret != 2) {
		pr_err("nvm: invalid command. Use \"d targetname\".\n");
		return -EINVAL;
	}

	remove.flags = 0;

	return __nvm_configure_remove(&remove);
}

static int nvm_configure_create(const char *val)
{
	struct nvm_ioctl_create create;
	char opcode;
	int lun_begin, lun_end, ret;

	ret = sscanf(val, "%c %256s %256s %48s %u:%u", &opcode, create.dev,
						create.tgtname, create.tgttype,
						&lun_begin, &lun_end);
	if (ret != 6) {
		pr_err("nvm: invalid command. Use \"opcode device name tgttype lun_begin:lun_end\".\n");
		return -EINVAL;
	}

	create.flags = 0;
	create.conf.type = NVM_CONFIG_TYPE_SIMPLE;
	create.conf.s.lun_begin = lun_begin;
	create.conf.s.lun_end = lun_end;

	return __nvm_configure_create(&create);
}


/* Exposes administrative interface through /sys/module/lnvm/configure_by_str */
static int nvm_configure_by_str_event(const char *val,
					const struct kernel_param *kp)
{
	char opcode;
	int ret;

	ret = sscanf(val, "%c", &opcode);
	if (ret != 1) {
		pr_err("nvm: string must have the format of \"cmd ...\"\n");
		return -EINVAL;
	}

	switch (opcode) {
	case 'a':
		return nvm_configure_create(val);
	case 'd':
		return nvm_configure_remove(val);
	case 's':
		return nvm_configure_show(val);
	default:
		pr_err("nvm: invalid command\n");
		return -EINVAL;
	}

	return 0;
}

static int nvm_configure_get(char *buf, const struct kernel_param *kp)
{
	int sz = 0;
	char *buf_start = buf;
	struct nvm_dev *dev;

	buf += sprintf(buf, "available devices:\n");
	down_write(&nvm_lock);
	list_for_each_entry(dev, &nvm_devices, devices) {
		if (sz > 4095 - DISK_NAME_LEN)
			break;
		buf += sprintf(buf, " %32s\n", dev->name);
	}
	up_write(&nvm_lock);

	return buf - buf_start - 1;
}

static const struct kernel_param_ops nvm_configure_by_str_event_param_ops = {
	.set	= nvm_configure_by_str_event,
	.get	= nvm_configure_get,
};

#undef MODULE_PARAM_PREFIX
#define MODULE_PARAM_PREFIX	"lnvm."

module_param_cb(configure_debug, &nvm_configure_by_str_event_param_ops, NULL,
									0644);

#endif /* CONFIG_NVM_DEBUG */

static long nvm_ioctl_info(struct file *file, void __user *arg)
{
	struct nvm_ioctl_info *info;
	struct nvm_tgt_type *tt;
	int tgt_iter = 0;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	info = memdup_user(arg, sizeof(struct nvm_ioctl_info));
	if (IS_ERR(info))
		return -EFAULT;

	info->version[0] = NVM_VERSION_MAJOR;
	info->version[1] = NVM_VERSION_MINOR;
	info->version[2] = NVM_VERSION_PATCH;

	down_write(&nvm_lock);
	list_for_each_entry(tt, &nvm_targets, list) {
		struct nvm_ioctl_info_tgt *tgt = &info->tgts[tgt_iter];

		tgt->version[0] = tt->version[0];
		tgt->version[1] = tt->version[1];
		tgt->version[2] = tt->version[2];
		strncpy(tgt->tgtname, tt->name, NVM_TTYPE_NAME_MAX);

		tgt_iter++;
	}

	info->tgtsize = tgt_iter;
	up_write(&nvm_lock);

	if (copy_to_user(arg, info, sizeof(struct nvm_ioctl_info)))
		return -EFAULT;

	kfree(info);
	return 0;
}

static long nvm_ioctl_get_devices(struct file *file, void __user *arg)
{
	struct nvm_ioctl_get_devices *devices;
	struct nvm_dev *dev;
	int i = 0;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	devices = kzalloc(sizeof(struct nvm_ioctl_get_devices), GFP_KERNEL);
	if (!devices)
		return -ENOMEM;

	down_write(&nvm_lock);
	list_for_each_entry(dev, &nvm_devices, devices) {
		struct nvm_ioctl_device_info *info = &devices->info[i];

		sprintf(info->devname, "%s", dev->name);
		if (dev->mt) {
			info->bmversion[0] = dev->mt->version[0];
			info->bmversion[1] = dev->mt->version[1];
			info->bmversion[2] = dev->mt->version[2];
			sprintf(info->bmname, "%s", dev->mt->name);
		} else {
			sprintf(info->bmname, "none");
		}

		i++;
		if (i > 31) {
			pr_err("nvm: max 31 devices can be reported.\n");
			break;
		}
	}
	up_write(&nvm_lock);

	devices->nr_devices = i;

	if (copy_to_user(arg, devices, sizeof(struct nvm_ioctl_get_devices)))
		return -EFAULT;

	kfree(devices);
	return 0;
}

static long nvm_ioctl_dev_create(struct file *file, void __user *arg)
{
	struct nvm_ioctl_create create;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (copy_from_user(&create, arg, sizeof(struct nvm_ioctl_create)))
		return -EFAULT;

	create.dev[DISK_NAME_LEN - 1] = '\0';
	create.tgttype[NVM_TTYPE_NAME_MAX - 1] = '\0';
	create.tgtname[DISK_NAME_LEN - 1] = '\0';

	if (create.flags != 0) {
		pr_err("nvm: no flags supported\n");
		return -EINVAL;
	}

	return __nvm_configure_create(&create);
}

static long nvm_ioctl_dev_remove(struct file *file, void __user *arg)
{
	struct nvm_ioctl_remove remove;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (copy_from_user(&remove, arg, sizeof(struct nvm_ioctl_remove)))
		return -EFAULT;

	remove.tgtname[DISK_NAME_LEN - 1] = '\0';

	if (remove.flags != 0) {
		pr_err("nvm: no flags supported\n");
		return -EINVAL;
	}

	return __nvm_configure_remove(&remove);
}

static long nvm_ctl_ioctl(struct file *file, uint cmd, unsigned long arg)
{
	void __user *argp = (void __user *)arg;

	switch (cmd) {
	case NVM_INFO:
		return nvm_ioctl_info(file, argp);
	case NVM_GET_DEVICES:
		return nvm_ioctl_get_devices(file, argp);
	case NVM_DEV_CREATE:
		return nvm_ioctl_dev_create(file, argp);
	case NVM_DEV_REMOVE:
		return nvm_ioctl_dev_remove(file, argp);
	}
	return 0;
}

static const struct file_operations _ctl_fops = {
	.open = nonseekable_open,
	.unlocked_ioctl = nvm_ctl_ioctl,
	.owner = THIS_MODULE,
	.llseek  = noop_llseek,
};

static struct miscdevice _nvm_misc = {
	.minor		= MISC_DYNAMIC_MINOR,
	.name		= "lightnvm",
	.nodename	= "lightnvm/control",
	.fops		= &_ctl_fops,
};

MODULE_ALIAS_MISCDEV(MISC_DYNAMIC_MINOR);

static int __init nvm_mod_init(void)
{
	int ret;

	ret = misc_register(&_nvm_misc);
	if (ret)
		pr_err("nvm: misc_register failed for control device");

	return ret;
}

static void __exit nvm_mod_exit(void)
{
	misc_deregister(&_nvm_misc);
}

MODULE_AUTHOR("Matias Bjorling <m@bjorling.me>");
MODULE_LICENSE("GPL v2");
MODULE_VERSION("0.1");
module_init(nvm_mod_init);
module_exit(nvm_mod_exit);
