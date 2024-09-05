// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2019-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#include <linux/of_device.h>
#include "hab.h"

unsigned int get_refcnt(struct kref ref)
{
	return kref_read(&ref);
}

static void hab_ctx_free_work_fn(struct work_struct *work)
{
	struct uhab_context *ctx =
		container_of(work, struct uhab_context, destroy_work);

	hab_ctx_free_fn(ctx);
}

/*
 * ctx can only be freed after all the vchan releases the refcnt
 * and hab_release() is called.
 *
 * this function might be called in atomic context in following situations
 * (only applicable to Linux):
 * 1. physical_channel_rx_dispatch()->hab_msg_recv()->hab_vchan_put()
 * ->hab_ctx_put()->hab_ctx_free() in tasklet.
 * 2. hab client holds spin_lock and calls hab_vchan_close()->hab_vchan_put()
 * ->hab_vchan_free()->hab_ctx_free().
 */
void hab_ctx_free_os(struct kref *ref)
{
	struct uhab_context *ctx =
		container_of(ref, struct uhab_context, refcount);

	if (likely(preemptible())) {
		hab_ctx_free_fn(ctx);
	} else {
		pr_info("In non-preemptive context now, ctx owner %d\n",
			ctx->owner);
		INIT_WORK(&ctx->destroy_work, hab_ctx_free_work_fn);
		schedule_work(&ctx->destroy_work);
	}
}

static int hab_open(struct inode *inodep, struct file *filep)
{
	int result = 0;
	struct uhab_context *ctx;

	ctx = hab_ctx_alloc(0);

	if (!ctx) {
		pr_err("hab_ctx_alloc failed\n");
		filep->private_data = NULL;
		return -ENOMEM;
	}

	/*
	 * w/ the /dev/hab node split feature, when one single process
	 * using different threads to call habmm_socket_open() w/ different
	 * MMIDs from different MMID groups, we can know those different
	 * uhab_context are belonging to the same process.
	 *
	 * even if w/o /dev/hab node split feature, there will be a case where
	 * a child thread is responsible for managing habmm_socket_open/close,
	 * and other child threads use vcid for communication.
	 * If ctx->owner is pid, then context_stat will display the pid of the child thread.
	 * This is not what we want. We hope that context_stat will display the
	 * thread group id of the process, not the pid of a child thread.
	 * Because the threa group id often has more information
	 * for us to analyze and debug.
	 */
	ctx->owner = task_tgid_nr(current);
	ctx->mmid_grp_index = MINOR(inodep->i_rdev);

	filep->private_data = ctx;
	pr_debug("ctx owner %d refcnt %d\n", ctx->owner,
			get_refcnt(ctx->refcount));

	return result;
}

static int hab_release(struct inode *inodep, struct file *filep)
{
	struct uhab_context *ctx = filep->private_data;
	struct virtual_channel *vchan, *tmp;
	struct hab_open_node *node;

	if (!ctx)
		return 0;

	pr_debug("inode %pK, filep %pK ctx %pK\n", inodep, filep, ctx);

	/*
	 * This function will only be called for user-space clients,
	 * so no need to disable bottom half here since there is no
	 * potential dead lock issue among these clients racing for
	 * a ctx_lock of any user-space context.
	 */
	write_lock(&ctx->ctx_lock);
	/* notify remote side on vchan closing */
	list_for_each_entry_safe(vchan, tmp, &ctx->vchannels, node) {
		/* local close starts */
		vchan->closed = 1;

		list_del(&vchan->node); /* vchan is not in this ctx anymore */
		ctx->vcnt--;

		write_unlock(&ctx->ctx_lock);
		hab_vchan_stop_notify(vchan);
		hab_vchan_put(vchan); /* there is a lock inside */
		write_lock(&ctx->ctx_lock);
	}

	/* notify remote side on pending open */
	list_for_each_entry(node, &ctx->pending_open, node) {
		/* no touch to the list itself. it is allocated on the stack */
		if (hab_open_cancel_notify(&node->request))
			pr_err("failed to send open cancel vcid %x subid %d openid %d pchan %s\n",
					node->request.xdata.vchan_id,
					node->request.xdata.sub_id,
					node->request.xdata.open_id,
					node->request.pchan->habdev->name);
	}
	write_unlock(&ctx->ctx_lock);

	hab_ctx_put(ctx);
	filep->private_data = NULL;

	return 0;
}

static long hab_copy_data(struct hab_message *msg, struct hab_recv *recv_param)
{
	long ret = 0;
	int i = 0;
	void **scatter_buf = (void **)msg->data;
	uint64_t dest = 0U;

	if (unlikely(msg->scatter)) {
		/* The maximum size of msg is limited in hab_msg_alloc */
		for (i = 0; i < msg->sizebytes / PAGE_SIZE; i++) {
			dest = (uint64_t)(recv_param->data) + (uint64_t)(i * PAGE_SIZE);
			if (copy_to_user((void __user *)dest,
					scatter_buf[i],
					PAGE_SIZE)) {
				pr_err("copy_to_user failed: vc=%x size=%d\n",
				recv_param->vcid, (int)msg->sizebytes);
				recv_param->sizebytes = 0;
				ret = -EFAULT;
				break;
			}
		}
		if ((ret != -EFAULT) && (msg->sizebytes % PAGE_SIZE)) {
			dest = (uint64_t)(recv_param->data) + (uint64_t)(i * PAGE_SIZE);
			if (copy_to_user((void __user *)dest,
					scatter_buf[i],
					msg->sizebytes % PAGE_SIZE)) {
				pr_err("copy_to_user failed: vc=%x size=%d\n",
				recv_param->vcid, (int)msg->sizebytes);
				recv_param->sizebytes = 0;
				ret = -EFAULT;
			}
		}
	} else {
		if (copy_to_user((void __user *)recv_param->data,
				msg->data,
				msg->sizebytes)) {
			pr_err("copy_to_user failed: vc=%x size=%d\n",
			recv_param->vcid, (int)msg->sizebytes);
			recv_param->sizebytes = 0;
			ret = -EFAULT;
		}
	}

	return ret;
}

static inline long hab_check_cmd(unsigned int cmd, unsigned int data_size)
{
	if (!_IOC_SIZE(cmd) || !(cmd & IOC_INOUT) || (_IOC_SIZE(cmd) > data_size))
		return -EINVAL;

	return 0;
}

static long hab_ioctl(struct file *filep, unsigned int cmd, unsigned long arg)
{
	struct uhab_context *ctx = (struct uhab_context *)filep->private_data;
	struct hab_open *open_param;
	struct hab_close *close_param;
	struct hab_recv *recv_param;
	struct hab_send *send_param;
	struct hab_info *info_param;
	struct hab_message *msg = NULL;
	void *send_data;
	unsigned char data[256] = { 0 };
	long ret = 0;
	char names[30] = { 0 };
	int mmid_grp_index = ctx->mmid_grp_index;

	ret = hab_check_cmd(cmd, sizeof(data));
	if (ret)
		return ret;

	if ((cmd & IOC_IN) &&
	    (copy_from_user(data, (void __user *)arg, _IOC_SIZE(cmd)))) {
		pr_err("copy_from_user failed cmd=%x size=%d\n",
			cmd, _IOC_SIZE(cmd));
		return -EFAULT;
	}

	switch (cmd) {
	case IOCTL_HAB_VC_OPEN:
		open_param = (struct hab_open *)data;
		/*
		 * each hab group node(/dev/hab-*) only serves mmid(s) of the corresponding group
		 * but the super node /dev/hab(mmid_grp_index is 0) serves all mmids.
		 */
		if (mmid_grp_index &&
			(mmid_grp_index != (HAB_MMID_GET_MAJOR(open_param->mmid) / 100))) {
			pr_err("current node is %s, not for mmid %d (major %d)\n",
				HAB_MMID_MAP_NODE(mmid_grp_index * 100),
				open_param->mmid,
				HAB_MMID_GET_MAJOR(open_param->mmid));
			return -EINVAL;
		}
		ret = hab_vchan_open(ctx, open_param->mmid,
			&open_param->vcid,
			open_param->timeout,
			open_param->flags);
		break;
	case IOCTL_HAB_VC_CLOSE:
		close_param = (struct hab_close *)data;
		ret = hab_vchan_close(ctx, close_param->vcid);
		break;
	case IOCTL_HAB_SEND:
		send_param = (struct hab_send *)data;
		if (send_param->sizebytes > (uint32_t)(HAB_HEADER_SIZE_MAX)) {
			ret = -EINVAL;
			break;
		}

		send_data = kzalloc(send_param->sizebytes, GFP_KERNEL);
		if (!send_data) {
			ret = -ENOMEM;
			break;
		}

		if (copy_from_user(send_data, (void __user *)send_param->data,
				send_param->sizebytes)) {
			ret = -EFAULT;
		} else {
			ret = hab_vchan_send(ctx, send_param->vcid,
						send_param->sizebytes,
						send_data,
						send_param->flags);
		}
		kfree(send_data);
		break;
	case IOCTL_HAB_RECV:
		recv_param = (struct hab_recv *)data;
		if (!recv_param->data) {
			ret = -EINVAL;
			break;
		}

		ret = hab_vchan_recv(ctx, &msg, recv_param->vcid,
				&recv_param->sizebytes, recv_param->timeout,
				recv_param->flags);

		if (msg) {
			if (ret == 0)
				ret = hab_copy_data(msg, recv_param);
			else
				pr_warn("vcid %X recv failed %d and msg is still of %zd bytes\n",
					recv_param->vcid, (int)ret, msg->sizebytes);

			hab_msg_free(msg);
		}

		break;
	case IOCTL_HAB_VC_EXPORT:
		ret = hab_mem_export(ctx, (struct hab_export *)data, 0);
		break;
	case IOCTL_HAB_VC_IMPORT:
		ret = hab_mem_import(ctx, (struct hab_import *)data, 0);
		break;
	case IOCTL_HAB_VC_UNEXPORT:
		ret = hab_mem_unexport(ctx, (struct hab_unexport *)data, 0);
		break;
	case IOCTL_HAB_VC_UNIMPORT:
		ret = hab_mem_unimport(ctx, (struct hab_unimport *)data, 0);
		break;
	case IOCTL_HAB_VC_QUERY:
		info_param = (struct hab_info *)data;
		if (!info_param->names || !info_param->namesize ||
			info_param->namesize > sizeof(names)) {
			pr_err("wrong param for vm info vcid %X, names %llX, sz %d\n",
					info_param->vcid, info_param->names,
					info_param->namesize);
			ret = -EINVAL;
			break;
		}
		ret = hab_vchan_query(ctx, info_param->vcid,
				(uint64_t *)&info_param->ids,
				 names, info_param->namesize, 0);
		if (!ret) {
			if (copy_to_user((void __user *)info_param->names,
						 names,
						 info_param->namesize)) {
				pr_err("copy_to_user failed: vc=%x size=%d\n",
						info_param->vcid,
						info_param->namesize*2);
				info_param->namesize = 0;
				ret = -EFAULT;
			}
		}
		break;
	default:
		ret = -ENOIOCTLCMD;
	}

	if ((ret != -ENOIOCTLCMD) &&
	    (cmd & IOC_OUT) &&
	    (copy_to_user((void __user *) arg, data, _IOC_SIZE(cmd)))) {
		pr_err("copy_to_user failed: cmd=%x\n", cmd);
		ret = -EFAULT;
	}

	return ret;
}

static long hab_compat_ioctl(struct file *filep, unsigned int cmd,
	unsigned long arg)
{
	return hab_ioctl(filep, cmd, arg);
}

static const struct file_operations hab_fops = {
	.owner = THIS_MODULE,
	.open = hab_open,
	.release = hab_release,
	.mmap = habmem_imp_hyp_mmap,
	.unlocked_ioctl = hab_ioctl,
	.compat_ioctl = hab_compat_ioctl
};

/*
 * These map sg functions are pass through because the memory backing the
 * sg list is already accessible to the kernel as they come from a the
 * dedicated shared vm pool
 */

static int hab_map_sg(struct device *dev, struct scatterlist *sgl,
	int nelems, enum dma_data_direction dir,
	unsigned long attrs)
{
	/* return nelems directly */
	return nelems;
}

static void hab_unmap_sg(struct device *dev,
	struct scatterlist *sgl, int nelems,
	enum dma_data_direction dir,
	unsigned long attrs)
{
	/*Do nothing */
}

static const struct dma_map_ops hab_dma_ops = {
	.map_sg		= hab_map_sg,
	.unmap_sg	= hab_unmap_sg,
};

static int hab_power_down_callback(
		struct notifier_block *nfb, unsigned long action, void *data)
{

	switch (action) {
	case SYS_DOWN:
	case SYS_HALT:
	case SYS_POWER_OFF:
		pr_debug("reboot called %ld\n", action);
		break;
	}
	pr_info("reboot called %ld done\n", action);
	return NOTIFY_DONE;
}

static struct notifier_block hab_reboot_notifier = {
	.notifier_call = hab_power_down_callback,
};

static void reclaim_cleanup(struct work_struct *reclaim_work)
{
	struct export_desc *exp = NULL, *exp_tmp = NULL;
	struct export_desc_super *exp_super = NULL;
	struct physical_channel *pchan = NULL;
	LIST_HEAD(free_list);

	pr_debug("reclaim worker called\n");
	spin_lock(&hab_driver.reclaim_lock);
	list_for_each_entry_safe(exp, exp_tmp, &hab_driver.reclaim_list, node) {
		exp_super = container_of(exp, struct export_desc_super, exp);
		if (exp_super->remote_imported == 0)
			list_move(&exp->node, &free_list);
	}
	spin_unlock(&hab_driver.reclaim_lock);

	list_for_each_entry_safe(exp, exp_tmp, &free_list, node) {
		list_del(&exp->node);
		exp_super = container_of(exp, struct export_desc_super, exp);
		pchan = exp->pchan;
		spin_lock_bh(&pchan->expid_lock);
		idr_remove(&pchan->expid_idr, exp->export_id);
		spin_unlock_bh(&pchan->expid_lock);
		pr_info("cleanup exp id %u from %s\n", exp->export_id, pchan->name);
		habmem_export_put(exp_super);
	}
}

void hab_rb_init(struct rb_root *root)
{
	*root = RB_ROOT;
}

struct export_desc_super *hab_rb_exp_find(struct rb_root *root, struct export_desc_super *key)
{
	struct rb_node *node = root->rb_node;
	struct export_desc_super *exp_super;

	while (node) {
		exp_super = rb_entry(node, struct export_desc_super, node);
		if (key->exp.export_id < exp_super->exp.export_id)
			node = node->rb_left;
		else if (key->exp.export_id > exp_super->exp.export_id)
			node = node->rb_right;
		else {
			if (key->exp.pchan < exp_super->exp.pchan)
				node = node->rb_left;
			else if (key->exp.pchan > exp_super->exp.pchan)
				node = node->rb_right;
			else
				return exp_super;
		}
	}

	return NULL;
}

struct export_desc_super *hab_rb_exp_insert(struct rb_root *root, struct export_desc_super *exp_s)
{
	struct rb_node **new = &(root->rb_node), *parent = NULL;

	while (*new) {
		struct export_desc_super *this = rb_entry(*new, struct export_desc_super, node);

		parent = *new;
		if (exp_s->exp.export_id < this->exp.export_id)
			new = &((*new)->rb_left);
		else if (exp_s->exp.export_id > this->exp.export_id)
			new = &((*new)->rb_right);
		else {
			if (exp_s->exp.pchan < this->exp.pchan)
				new = &((*new)->rb_left);
			else if (exp_s->exp.pchan > this->exp.pchan)
				new = &((*new)->rb_right);
			else
				/* should not found the target key before insert */
				return this;
		}
	}

	rb_link_node(&exp_s->node, parent, new);
	rb_insert_color(&exp_s->node, root);

	return NULL;
}

/* create one more char device for /dev/hab */
#define CDEV_NUM_MAX (MM_ID_MAX / 100 + 1)

int hab_create_cdev_node(int mmid_grp_index)
{
	int result;
	const char *node_name;
	dev_t dev_no;

	node_name = HAB_MMID_MAP_NODE(mmid_grp_index * 100);
	if (!node_name) {
		pr_err("err hab group id %d\n", mmid_grp_index);
		return -ENOENT;
	}

	cdev_init(&(hab_driver.cdev[mmid_grp_index]), &hab_fops);
	hab_driver.cdev[mmid_grp_index].owner = THIS_MODULE;

	dev_no = MKDEV(hab_driver.major, mmid_grp_index);

	result = cdev_add(&(hab_driver.cdev[mmid_grp_index]), dev_no, 1);
	if (result) {
		pr_err("cdev_add failed: %d\n", result);
		return result;
	}

	hab_driver.dev[mmid_grp_index] = device_create(hab_driver.class, NULL,
					dev_no, &hab_driver, node_name);

	if (IS_ERR_OR_NULL(hab_driver.dev[mmid_grp_index])) {
		result = PTR_ERR(hab_driver.dev[mmid_grp_index]);
		pr_err("mmid_grp_index %d device_create %s failed: %d\n",
				mmid_grp_index, node_name, result);
		cdev_del(&hab_driver.cdev[mmid_grp_index]);
		hab_driver.dev[mmid_grp_index] = NULL;
		return result;
	}

	/* First, try to configure system dma_ops */
	result = dma_coerce_mask_and_coherent(
			hab_driver.dev[mmid_grp_index],
			DMA_BIT_MASK(64));
	/* System dma_ops failed, fallback to dma_ops of hab */
	if (result) {
		pr_warn("config system dma_ops failed %d, fallback to hab\n",
				result);
		hab_driver.dev[mmid_grp_index]->bus = NULL;
		set_dma_ops(hab_driver.dev[mmid_grp_index], &hab_dma_ops);
	}

	pr_debug("create char device for /dev/%s successful\n", node_name);
	return 0;
}

static int __init hab_init(void)
{
	int result;
	dev_t dev_no;

	pr_debug("init start, ver %X\n", HAB_API_VER);

	/* prepare resources for creating hab char devices */
	result = alloc_chrdev_region(&dev_no, 0, CDEV_NUM_MAX, "hab");

	if (result < 0) {
		pr_err("alloc_chrdev_region failed: %d\n", result);
		return result;
	}

	hab_driver.major = MAJOR(dev_no);

	hab_driver.dev = kzalloc(sizeof(struct device *) * CDEV_NUM_MAX, GFP_KERNEL);
	if (!hab_driver.dev)
		goto dev_alloc_fail;

	hab_driver.cdev = kzalloc(sizeof(struct cdev) * CDEV_NUM_MAX, GFP_KERNEL);
	if (!hab_driver.cdev)
		goto cdev_alloc_fail;

	hab_driver.class = class_create(THIS_MODULE, "hab");

	if (IS_ERR(hab_driver.class)) {
		result = PTR_ERR(hab_driver.class);
		pr_err("class_create failed: %d\n", result);
		goto err_class_create;
	}

	result = register_reboot_notifier(&hab_reboot_notifier);
	if (result)
		pr_err("failed to register reboot notifier %d\n", result);

	INIT_WORK(&hab_driver.reclaim_work, reclaim_cleanup);

	/* read in hab config, then configure pchans */
	result = do_hab_parse();

	if (result)
		goto err_hab_parse;

	hab_driver.kctx = hab_ctx_alloc(1);
	if (!hab_driver.kctx) {
		pr_err("hab_ctx_alloc failed\n");
		result = -ENOMEM;
		goto err_hab_parse;
	}

	/* create the super char device node /dev/hab */
	result = hab_create_cdev_node(0);
	if (result)
		goto err;

	hab_hypervisor_register_post();
	hab_stat_init(&hab_driver);

	WRITE_ONCE(hab_driver.hab_init_success, 1);

	pr_info("succeeds\n");

	return 0;

err:
	hab_ctx_put(hab_driver.kctx);
err_hab_parse:
	if (!IS_ERR_OR_NULL(hab_driver.class))
		class_destroy(hab_driver.class);
err_class_create:
	kfree(hab_driver.cdev);
cdev_alloc_fail:
	kfree(hab_driver.dev);
dev_alloc_fail:
	unregister_chrdev_region(hab_driver.major, CDEV_NUM_MAX);

	pr_err("Error in hab init, result %d\n", result);
	return result;
}

static void __exit hab_exit(void)
{
	int i;

	hab_hypervisor_unregister();
	hab_stat_deinit(&hab_driver);
	hab_ctx_put(hab_driver.kctx);
	for (i = 0; i < CDEV_NUM_MAX; i++) {
		if (!IS_ERR_OR_NULL(hab_driver.dev[i])) {
			device_destroy(hab_driver.class, MKDEV(hab_driver.major, i));
			cdev_del(&hab_driver.cdev[i]);
		}
	}
	class_destroy(hab_driver.class);
	unregister_chrdev_region(hab_driver.major, CDEV_NUM_MAX);

	unregister_reboot_notifier(&hab_reboot_notifier);
	pr_debug("hab exit called\n");
}

#if IS_MODULE(CONFIG_MSM_HAB)
module_init(hab_init);
#else
subsys_initcall(hab_init);
#endif
module_exit(hab_exit);

MODULE_DESCRIPTION("Hypervisor abstraction layer");
MODULE_LICENSE("GPL");
