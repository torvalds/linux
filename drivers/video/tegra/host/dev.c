/*
 * drivers/video/tegra/host/dev.c
 *
 * Tegra Graphics Host Driver Entrypoint
 *
 * Copyright (c) 2010, NVIDIA Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <linux/nvhost.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/spinlock.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/platform_device.h>
#include <linux/uaccess.h>
#include <linux/file.h>
#include <linux/nvhost.h>

#include <asm/io.h>

#include "dev.h"

#define DRIVER_NAME "tegra_grhost"
#define IFACE_NAME "nvhost"

static int nvhost_major = NVHOST_MAJOR;
static int nvhost_minor = NVHOST_CHANNEL_BASE;

struct nvhost_channel_userctx {
	struct nvhost_channel *ch;
	struct nvhost_hwctx *hwctx;
	struct file *nvmapctx;
	u32 syncpt_id;
	u32 syncpt_incrs;
	u32 cmdbufs_pending;
	u32 relocs_pending;
	struct nvmap_handle *gather_mem;
	struct nvhost_op_pair *gathers;
	int num_gathers;
	int pinarray_size;
	struct nvmap_pinarray_elem pinarray[NVHOST_MAX_HANDLES];
	struct nvmap_handle *unpinarray[NVHOST_MAX_HANDLES];
};

struct nvhost_ctrl_userctx {
	struct nvhost_master *dev;
	u32 mod_locks[NV_HOST1X_NB_MLOCKS];
};

static int nvhost_channelrelease(struct inode *inode, struct file *filp)
{
	struct nvhost_channel_userctx *priv = filp->private_data;
	filp->private_data = NULL;

	nvhost_putchannel(priv->ch, priv->hwctx);
	if (priv->hwctx)
		priv->ch->ctxhandler.put(priv->hwctx);
	if (priv->gather_mem)
		nvmap_free(priv->gather_mem, priv->gathers);
	if (priv->nvmapctx)
		fput(priv->nvmapctx);
	kfree(priv);
	return 0;
}

static int nvhost_channelopen(struct inode *inode, struct file *filp)
{
	struct nvhost_channel_userctx *priv;
	struct nvhost_channel *ch;

	ch = container_of(inode->i_cdev, struct nvhost_channel, cdev);
	ch = nvhost_getchannel(ch);
	if (!ch)
		return -ENOMEM;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		nvhost_putchannel(ch, NULL);
		return -ENOMEM;
	}
	filp->private_data = priv;
	priv->ch = ch;
	priv->gather_mem = nvmap_alloc(
		sizeof(struct nvhost_op_pair) * NVHOST_MAX_GATHERS, 32,
		NVMEM_HANDLE_CACHEABLE, (void**)&priv->gathers);
	if (IS_ERR_OR_NULL(priv->gather_mem))
		goto fail;
	if (ch->ctxhandler.alloc) {
		priv->hwctx = ch->ctxhandler.alloc(ch);
		if (!priv->hwctx)
			goto fail;
	}

	return 0;
fail:
	nvhost_channelrelease(inode, filp);
	return -ENOMEM;
}

static void add_gather(struct nvhost_channel_userctx *ctx, int idx,
		struct nvmap_handle *mem, u32 words, u32 offset)
{
	struct nvmap_pinarray_elem *pin;
	pin = &ctx->pinarray[ctx->pinarray_size++];
	pin->patch_mem = ctx->gather_mem;
	pin->patch_offset = (idx * sizeof(struct nvhost_op_pair)) +
		offsetof(struct nvhost_op_pair, op2);
	pin->pin_mem = mem;
	pin->pin_offset = offset;
	ctx->gathers[idx].op1 = nvhost_opcode_gather(0, words);
}

static void reset_submit(struct nvhost_channel_userctx *ctx)
{
	ctx->cmdbufs_pending = 0;
	ctx->relocs_pending = 0;
}

static ssize_t nvhost_channelwrite(struct file *filp, const char __user *buf,
				size_t count, loff_t *offp)
{
	struct nvhost_channel_userctx *priv = filp->private_data;
	size_t remaining = count;
	int err = 0;

	while (remaining) {
		size_t consumed;
		if (!priv->relocs_pending && !priv->cmdbufs_pending) {
			consumed = sizeof(struct nvhost_submit_hdr);
			if (remaining < consumed)
				break;
			if (copy_from_user(&priv->syncpt_id, buf, consumed)) {
				err = -EFAULT;
				break;
			}
			if (!priv->cmdbufs_pending) {
				err = -EFAULT;
				break;
			}
			/* leave room for ctx switch */
			priv->num_gathers = 2;
			priv->pinarray_size = 0;
		} else if (priv->cmdbufs_pending) {
			struct nvhost_cmdbuf cmdbuf;
			consumed = sizeof(cmdbuf);
			if (remaining < consumed)
				break;
			if (copy_from_user(&cmdbuf, buf, consumed)) {
				err = -EFAULT;
				break;
			}
			add_gather(priv, priv->num_gathers++,
				(struct nvmap_handle *)cmdbuf.mem,
				cmdbuf.words, cmdbuf.offset);
			priv->cmdbufs_pending--;
		} else if (priv->relocs_pending) {
			int numrelocs = remaining / sizeof(struct nvhost_reloc);
			if (!numrelocs)
				break;
			numrelocs = min_t(int, numrelocs, priv->relocs_pending);
			consumed = numrelocs * sizeof(struct nvhost_reloc);
			if (copy_from_user(&priv->pinarray[priv->pinarray_size],
						buf, consumed)) {
				err = -EFAULT;
				break;
			}
			priv->pinarray_size += numrelocs;
			priv->relocs_pending -= numrelocs;
		} else {
			err = -EFAULT;
			break;
		}
		remaining -= consumed;
		buf += consumed;
	}

	if (err < 0) {
		dev_err(&priv->ch->dev->pdev->dev, "channel write error\n");
		reset_submit(priv);
		return err;
	}

	return (count - remaining);
}

static int nvhost_ioctl_channel_flush(
	struct nvhost_channel_userctx *ctx,
	struct nvhost_get_param_args *args)
{
	struct nvhost_cpuinterrupt ctxsw;
	int gather_idx = 2;
	int num_intrs = 0;
	u32 syncval;
	int num_unpin;
	int err;

	if (ctx->relocs_pending || ctx->cmdbufs_pending) {
		reset_submit(ctx);
		dev_err(&ctx->ch->dev->pdev->dev, "channel submit out of sync\n");
		return -EFAULT;
	}
	if (!ctx->nvmapctx) {
		dev_err(&ctx->ch->dev->pdev->dev, "no nvmap context set\n");
		return -EFAULT;
	}
	if (ctx->num_gathers <= 2)
		return 0;

	/* keep module powered */
	nvhost_module_busy(&ctx->ch->mod);

	/* pin mem handles and patch physical addresses */
	err = nvmap_pin_array(ctx->nvmapctx, ctx->pinarray, ctx->pinarray_size,
			ctx->unpinarray, &num_unpin, true);
	if (err) {
		dev_warn(&ctx->ch->dev->pdev->dev, "nvmap_pin_array failed: %d\n", err);
		nvhost_module_idle(&ctx->ch->mod);
		return err;
	}

	/* get submit lock */
	err = mutex_lock_interruptible(&ctx->ch->submitlock);
	if (err) {
		nvmap_unpin(ctx->unpinarray, num_unpin);
		nvhost_module_idle(&ctx->ch->mod);
		return err;
	}

	/* context switch */
	if (ctx->ch->cur_ctx != ctx->hwctx) {
		struct nvhost_hwctx *hw = ctx->hwctx;
		if (hw && hw->valid) {
			gather_idx--;
			ctx->gathers[gather_idx].op1 =
				nvhost_opcode_gather(0, hw->restore_size);
			ctx->gathers[gather_idx].op2 = hw->restore_phys;
			ctx->syncpt_incrs += hw->restore_incrs;
		}
		hw = ctx->ch->cur_ctx;
		if (hw) {
			gather_idx--;
			ctx->gathers[gather_idx].op1 =
				nvhost_opcode_gather(0, hw->save_size);
			ctx->gathers[gather_idx].op2 = hw->save_phys;
			ctx->syncpt_incrs += hw->save_incrs;
			num_intrs = 1;
			ctxsw.syncpt_val = hw->save_incrs - 1;
			ctxsw.intr_data = hw;
			hw->valid = true;
			ctx->ch->ctxhandler.get(hw);
		}
		ctx->ch->cur_ctx = ctx->hwctx;
	}

	/* add a setclass for modules that require it */
	if (gather_idx == 2 && ctx->ch->desc->class) {
		gather_idx--;
		ctx->gathers[gather_idx].op1 =
			nvhost_opcode_setclass(ctx->ch->desc->class, 0, 0);
		ctx->gathers[gather_idx].op2 = NVHOST_OPCODE_NOOP;
	}

	/* get absolute sync value */
	if (BIT(ctx->syncpt_id) & NVSYNCPTS_CLIENT_MANAGED)
		syncval = nvhost_syncpt_set_max(&ctx->ch->dev->syncpt,
						ctx->syncpt_id, ctx->syncpt_incrs);
	else
		syncval = nvhost_syncpt_incr_max(&ctx->ch->dev->syncpt,
						ctx->syncpt_id, ctx->syncpt_incrs);

	/* patch absolute syncpt value into interrupt triggers */
	ctxsw.syncpt_val += syncval - ctx->syncpt_incrs;

	nvhost_channel_submit(ctx->ch, &ctx->gathers[gather_idx],
			ctx->num_gathers - gather_idx, &ctxsw, num_intrs,
			ctx->unpinarray, num_unpin, ctx->syncpt_id, syncval);

	/* schedule a submit complete interrupt */
	nvhost_intr_add_action(&ctx->ch->dev->intr, ctx->syncpt_id, syncval,
			NVHOST_INTR_ACTION_SUBMIT_COMPLETE, ctx->ch, NULL);

	mutex_unlock(&ctx->ch->submitlock);
	args->value = syncval;
	return 0;
}

static long nvhost_channelctl(struct file *filp,
	unsigned int cmd, unsigned long arg)
{
	struct nvhost_channel_userctx *priv = filp->private_data;
	u8 buf[NVHOST_IOCTL_CHANNEL_MAX_ARG_SIZE];
	int err = 0;

	if ((_IOC_TYPE(cmd) != NVHOST_IOCTL_MAGIC) ||
		(_IOC_NR(cmd) == 0) ||
		(_IOC_NR(cmd) > NVHOST_IOCTL_CHANNEL_LAST))
		return -EFAULT;

	BUG_ON(_IOC_SIZE(cmd) > NVHOST_IOCTL_CHANNEL_MAX_ARG_SIZE);

	if (_IOC_DIR(cmd) & _IOC_WRITE) {
		if (copy_from_user(buf, (void __user *)arg, _IOC_SIZE(cmd)))
			return -EFAULT;
	}

	switch (cmd) {
	case NVHOST_IOCTL_CHANNEL_FLUSH:
		err = nvhost_ioctl_channel_flush(priv, (void *)buf);
		break;
	case NVHOST_IOCTL_CHANNEL_GET_SYNCPOINTS:
		((struct nvhost_get_param_args *)buf)->value =
			priv->ch->desc->syncpts;
		break;
	case NVHOST_IOCTL_CHANNEL_GET_WAITBASES:
		((struct nvhost_get_param_args *)buf)->value =
			priv->ch->desc->waitbases;
		break;
	case NVHOST_IOCTL_CHANNEL_GET_MODMUTEXES:
		((struct nvhost_get_param_args *)buf)->value =
			priv->ch->desc->modulemutexes;
		break;
	case NVHOST_IOCTL_CHANNEL_SET_NVMAP_FD:
	{
		int fd = (int)((struct nvhost_set_nvmap_fd_args *)buf)->fd;
		struct file *newctx = NULL;
		if (fd) {
			newctx = fget(fd);
			if (!newctx) {
				err = -EFAULT;
				break;
			}
			err = nvmap_validate_file(newctx);
			if (err) {
				fput(newctx);
				break;
			}
		}
		if (priv->nvmapctx)
			fput(priv->nvmapctx);
		priv->nvmapctx = newctx;
		break;
	}
	default:
		err = -ENOTTY;
		break;
	}

	if ((err == 0) && (_IOC_DIR(cmd) & _IOC_READ))
		err = copy_to_user((void __user *)arg, buf, _IOC_SIZE(cmd));

	return err;
}

static struct file_operations nvhost_channelops = {
	.owner = THIS_MODULE,
	.release = nvhost_channelrelease,
	.open = nvhost_channelopen,
	.write = nvhost_channelwrite,
	.unlocked_ioctl = nvhost_channelctl
};

static int nvhost_ctrlrelease(struct inode *inode, struct file *filp)
{
	struct nvhost_ctrl_userctx *priv = filp->private_data;
	int i;

	filp->private_data = NULL;
	if (priv->mod_locks[0])
		nvhost_module_idle(&priv->dev->mod);
	for (i = 1; i < NV_HOST1X_NB_MLOCKS; i++)
		if (priv->mod_locks[i])
			nvhost_mutex_unlock(&priv->dev->cpuaccess, i);
	kfree(priv);
	return 0;
}

static int nvhost_ctrlopen(struct inode *inode, struct file *filp)
{
	struct nvhost_master *host = container_of(inode->i_cdev, struct nvhost_master, cdev);
	struct nvhost_ctrl_userctx *priv;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = host;
	filp->private_data = priv;
	return 0;
}

static int nvhost_ioctl_ctrl_syncpt_read(
	struct nvhost_ctrl_userctx *ctx,
	struct nvhost_ctrl_syncpt_read_args *args)
{
	if (args->id >= NV_HOST1X_SYNCPT_NB_PTS)
		return -EINVAL;
	args->value = nvhost_syncpt_read(&ctx->dev->syncpt, args->id);
	return 0;
}

static int nvhost_ioctl_ctrl_syncpt_incr(
	struct nvhost_ctrl_userctx *ctx,
	struct nvhost_ctrl_syncpt_incr_args *args)
{
	if (args->id >= NV_HOST1X_SYNCPT_NB_PTS)
		return -EINVAL;
	nvhost_syncpt_incr(&ctx->dev->syncpt, args->id);
	return 0;
}

static int nvhost_ioctl_ctrl_syncpt_wait(
	struct nvhost_ctrl_userctx *ctx,
	struct nvhost_ctrl_syncpt_wait_args *args)
{
	u32 timeout;
	if (args->id >= NV_HOST1X_SYNCPT_NB_PTS)
		return -EINVAL;
	if (args->timeout == NVHOST_NO_TIMEOUT)
		timeout = MAX_SCHEDULE_TIMEOUT;
	else
		timeout = (u32)msecs_to_jiffies(args->timeout);

	return nvhost_syncpt_wait_timeout(&ctx->dev->syncpt, args->id,
					args->thresh, timeout);
}

static int nvhost_ioctl_ctrl_module_mutex(
	struct nvhost_ctrl_userctx *ctx,
	struct nvhost_ctrl_module_mutex_args *args)
{
	int err = 0;
	if (args->id >= NV_HOST1X_SYNCPT_NB_PTS ||
	    args->lock > 1)
		return -EINVAL;

	if (args->lock && !ctx->mod_locks[args->id]) {
		if (args->id == 0)
			nvhost_module_busy(&ctx->dev->mod);
		else
			err = nvhost_mutex_try_lock(&ctx->dev->cpuaccess, args->id);
		if (!err)
			ctx->mod_locks[args->id] = 1;
	}
	else if (!args->lock && ctx->mod_locks[args->id]) {
		if (args->id == 0)
			nvhost_module_idle(&ctx->dev->mod);
		else
			nvhost_mutex_unlock(&ctx->dev->cpuaccess, args->id);
		ctx->mod_locks[args->id] = 0;
	}
	return err;
}

static int nvhost_ioctl_ctrl_module_regrdwr(
	struct nvhost_ctrl_userctx *ctx,
	struct nvhost_ctrl_module_regrdwr_args *args)
{
	u32 num_offsets = args->num_offsets;
	u32 *offsets = args->offsets;
	void *values = args->values;
	u32 vals[64];

	if (!nvhost_access_module_regs(&ctx->dev->cpuaccess, args->id) ||
	    (num_offsets == 0))
		return -EINVAL;

	while (num_offsets--) {
		u32 remaining = args->block_size;
		u32 offs;
		if (get_user(offs, offsets))
			return -EFAULT;
		offsets++;
		while (remaining) {
			u32 batch = min(remaining, 64*sizeof(u32));
			if (args->write) {
				if (copy_from_user(vals, values, batch))
					return -EFAULT;
				nvhost_write_module_regs(&ctx->dev->cpuaccess,
							args->id, offs, batch, vals);
			} else {
				nvhost_read_module_regs(&ctx->dev->cpuaccess,
							args->id, offs, batch, vals);
				if (copy_to_user(values, vals, batch))
					return -EFAULT;
			}
			remaining -= batch;
			offs += batch;
			values += batch;
		}
	}

	return 0;
}

static long nvhost_ctrlctl(struct file *filp,
	unsigned int cmd, unsigned long arg)
{
	struct nvhost_ctrl_userctx *priv = filp->private_data;
	u8 buf[NVHOST_IOCTL_CTRL_MAX_ARG_SIZE];
	int err = 0;

	if ((_IOC_TYPE(cmd) != NVHOST_IOCTL_MAGIC) ||
		(_IOC_NR(cmd) == 0) ||
		(_IOC_NR(cmd) > NVHOST_IOCTL_CTRL_LAST))
		return -EFAULT;

	BUG_ON(_IOC_SIZE(cmd) > NVHOST_IOCTL_CTRL_MAX_ARG_SIZE);

	if (_IOC_DIR(cmd) & _IOC_WRITE) {
		if (copy_from_user(buf, (void __user *)arg, _IOC_SIZE(cmd)))
			return -EFAULT;
	}

	switch (cmd) {
	case NVHOST_IOCTL_CTRL_SYNCPT_READ:
		err = nvhost_ioctl_ctrl_syncpt_read(priv, (void *)buf);
		break;
	case NVHOST_IOCTL_CTRL_SYNCPT_INCR:
		err = nvhost_ioctl_ctrl_syncpt_incr(priv, (void *)buf);
		break;
	case NVHOST_IOCTL_CTRL_SYNCPT_WAIT:
		err = nvhost_ioctl_ctrl_syncpt_wait(priv, (void *)buf);
		break;
	case NVHOST_IOCTL_CTRL_MODULE_MUTEX:
		err = nvhost_ioctl_ctrl_module_mutex(priv, (void *)buf);
		break;
	case NVHOST_IOCTL_CTRL_MODULE_REGRDWR:
		err = nvhost_ioctl_ctrl_module_regrdwr(priv, (void *)buf);
		break;
	default:
		err = -ENOTTY;
		break;
	}

	if ((err == 0) && (_IOC_DIR(cmd) & _IOC_READ))
		err = copy_to_user((void __user *)arg, buf, _IOC_SIZE(cmd));

	return err;
}

static struct file_operations nvhost_ctrlops = {
	.owner = THIS_MODULE,
	.release = nvhost_ctrlrelease,
	.open = nvhost_ctrlopen,
	.unlocked_ioctl = nvhost_ctrlctl
};

static void power_host(struct nvhost_module *mod, enum nvhost_power_action action)
{
	struct nvhost_master *dev = container_of(mod, struct nvhost_master, mod);

	if (action == NVHOST_POWER_ACTION_ON) {
		nvhost_intr_configure(&dev->intr, clk_get_rate(mod->clk[0]));
		nvhost_syncpt_reset(&dev->syncpt);
	}
	else if (action == NVHOST_POWER_ACTION_OFF) {
		int i;
		for (i = 0; i < NVHOST_NUMCHANNELS; i++)
			nvhost_channel_suspend(&dev->channels[i]);
		nvhost_syncpt_save(&dev->syncpt);
	}
}

static int __init nvhost_user_init(struct nvhost_master *host)
{
	int i, err, devno;

	host->nvhost_class = class_create(THIS_MODULE, IFACE_NAME);
	if (IS_ERR(host->nvhost_class)) {
		err = PTR_ERR(host->nvhost_class);
		dev_err(&host->pdev->dev, "failed to create class\n");
		goto fail;
	}

	if (nvhost_major) {
		devno = MKDEV(nvhost_major, nvhost_minor);
		err = register_chrdev_region(devno, NVHOST_NUMCHANNELS + 1, IFACE_NAME);
	} else {
		err = alloc_chrdev_region(&devno, nvhost_minor,
					NVHOST_NUMCHANNELS + 1, IFACE_NAME);
		nvhost_major = MAJOR(devno);
	}
	if (err < 0) {
		dev_err(&host->pdev->dev, "failed to reserve chrdev region\n");
		goto fail;
	}

	for (i = 0; i < NVHOST_NUMCHANNELS; i++) {
		struct nvhost_channel *ch = &host->channels[i];

		cdev_init(&ch->cdev, &nvhost_channelops);
		ch->cdev.owner = THIS_MODULE;

		devno = MKDEV(nvhost_major, nvhost_minor + i);
		err = cdev_add(&ch->cdev, devno, 1);
		if (err < 0) {
			dev_err(&host->pdev->dev, "failed to add chan %i cdev\n", i);
			goto fail;
		}
		ch->node = device_create(host->nvhost_class, NULL, devno, NULL,
				IFACE_NAME "-%s", ch->desc->name);
		if (IS_ERR(ch->node)) {
			err = PTR_ERR(ch->node);
			dev_err(&host->pdev->dev, "failed to create chan %i device\n", i);
			goto fail;
		}
	}

	cdev_init(&host->cdev, &nvhost_ctrlops);
	host->cdev.owner = THIS_MODULE;
	devno = MKDEV(nvhost_major, nvhost_minor + NVHOST_NUMCHANNELS);
	err = cdev_add(&host->cdev, devno, 1);
	if (err < 0)
		goto fail;
	host->ctrl = device_create(host->nvhost_class, NULL, devno, NULL,
			IFACE_NAME "-ctrl");
	if (IS_ERR(host->ctrl)) {
		err = PTR_ERR(host->ctrl);
		dev_err(&host->pdev->dev, "failed to create ctrl device\n");
		goto fail;
	}

	return 0;
fail:
	return err;
}

static int __devinit nvhost_probe(struct platform_device *pdev)
{
	struct nvhost_master *host;
	struct resource *regs, *intr0, *intr1;
	int i, err;

	regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	intr0 = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	intr1 = platform_get_resource(pdev, IORESOURCE_IRQ, 1);

	if (!regs || !intr0 || !intr1) {
		dev_err(&pdev->dev, "missing required platform resources\n");
		return -ENXIO;
	}

	host = kzalloc(sizeof(*host), GFP_KERNEL);
	if (!host)
		return -ENOMEM;

	host->pdev = pdev;

	host->reg_mem = request_mem_region(regs->start,
					resource_size(regs), pdev->name);
	if (!host->reg_mem) {
		dev_err(&pdev->dev, "failed to get host register memory\n");
		err = -ENXIO;
		goto fail;
	}
	host->aperture = ioremap(regs->start, resource_size(regs));
	if (!host->aperture) {
		dev_err(&pdev->dev, "failed to remap host registers\n");
		err = -ENXIO;
		goto fail;
	}
	host->sync_aperture = host->aperture +
		(NV_HOST1X_CHANNEL0_BASE +
			HOST1X_CHANNEL_SYNC_REG_BASE);

	for (i = 0; i < NVHOST_NUMCHANNELS; i++) {
		struct nvhost_channel *ch = &host->channels[i];
		err = nvhost_channel_init(ch, host, i);
		if (err < 0) {
			dev_err(&pdev->dev, "failed to init channel %d\n", i);
			goto fail;
		}
	}

	err = nvhost_cpuaccess_init(&host->cpuaccess, pdev);
	if (err) goto fail;
	err = nvhost_intr_init(&host->intr, intr1->start, intr0->start);
	if (err) goto fail;
	err = nvhost_user_init(host);
	if (err) goto fail;
	err = nvhost_module_init(&host->mod, "host1x", power_host, NULL, &pdev->dev);
	if (err) goto fail;

	platform_set_drvdata(pdev, host);

	nvhost_bus_register(host);

	dev_info(&pdev->dev, "initialized\n");
	return 0;

fail:
	/* TODO: [ahatala 2010-05-04] */
	kfree(host);
	return err;
}

static int __exit nvhost_remove(struct platform_device *pdev)
{
	return 0;
}

static int nvhost_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct nvhost_master *host = platform_get_drvdata(pdev);
	dev_info(&pdev->dev, "suspending\n");
	nvhost_module_suspend(&host->mod);
	dev_info(&pdev->dev, "suspended\n");
	return 0;
}

static struct platform_driver nvhost_driver = {
	.remove = __exit_p(nvhost_remove),
	.suspend = nvhost_suspend,
	.driver = {
		.owner = THIS_MODULE,
		.name = DRIVER_NAME
	}
};

static int __init nvhost_mod_init(void)
{
	return platform_driver_probe(&nvhost_driver, nvhost_probe);
}

static void __exit nvhost_mod_exit(void)
{
	platform_driver_unregister(&nvhost_driver);
}

module_init(nvhost_mod_init);
module_exit(nvhost_mod_exit);

MODULE_AUTHOR("NVIDIA");
MODULE_DESCRIPTION("Graphics host driver for Tegra products");
MODULE_VERSION("1.0");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_ALIAS("platform-nvhost");
