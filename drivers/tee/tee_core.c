/*
 * Copyright (c) 2015-2016, Linaro Limited
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#define pr_fmt(fmt) "%s: " fmt, __func__

#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/idr.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/tee_drv.h>
#include <linux/uaccess.h>
#include "tee_private.h"

#define TEE_NUM_DEVICES	32

#define TEE_IOCTL_PARAM_SIZE(x) (sizeof(struct tee_param) * (x))

/*
 * Unprivileged devices in the lower half range and privileged devices in
 * the upper half range.
 */
static DECLARE_BITMAP(dev_mask, TEE_NUM_DEVICES);
static DEFINE_SPINLOCK(driver_lock);

static struct class *tee_class;
static dev_t tee_devt;

static struct tee_context *teedev_open(struct tee_device *teedev)
{
	int rc;
	struct tee_context *ctx;

	if (!tee_device_get(teedev))
		return ERR_PTR(-EINVAL);

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx) {
		rc = -ENOMEM;
		goto err;
	}

	ctx->teedev = teedev;
	INIT_LIST_HEAD(&ctx->list_shm);
	rc = teedev->desc->ops->open(ctx);
	if (rc)
		goto err;

	return ctx;
err:
	kfree(ctx);
	tee_device_put(teedev);
	return ERR_PTR(rc);

}

static void teedev_close_context(struct tee_context *ctx)
{
	struct tee_shm *shm;

	ctx->teedev->desc->ops->release(ctx);
	mutex_lock(&ctx->teedev->mutex);
	list_for_each_entry(shm, &ctx->list_shm, link)
		shm->ctx = NULL;
	mutex_unlock(&ctx->teedev->mutex);
	tee_device_put(ctx->teedev);
	kfree(ctx);
}

static int tee_open(struct inode *inode, struct file *filp)
{
	struct tee_context *ctx;

	ctx = teedev_open(container_of(inode->i_cdev, struct tee_device, cdev));
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	filp->private_data = ctx;
	return 0;
}

static int tee_release(struct inode *inode, struct file *filp)
{
	teedev_close_context(filp->private_data);
	return 0;
}

static int tee_ioctl_version(struct tee_context *ctx,
			     struct tee_ioctl_version_data __user *uvers)
{
	struct tee_ioctl_version_data vers;

	ctx->teedev->desc->ops->get_version(ctx->teedev, &vers);
	if (copy_to_user(uvers, &vers, sizeof(vers)))
		return -EFAULT;
	return 0;
}

static int tee_ioctl_shm_alloc(struct tee_context *ctx,
			       struct tee_ioctl_shm_alloc_data __user *udata)
{
	long ret;
	struct tee_ioctl_shm_alloc_data data;
	struct tee_shm *shm;

	if (copy_from_user(&data, udata, sizeof(data)))
		return -EFAULT;

	/* Currently no input flags are supported */
	if (data.flags)
		return -EINVAL;

	data.id = -1;

	shm = tee_shm_alloc(ctx, data.size, TEE_SHM_MAPPED | TEE_SHM_DMA_BUF);
	if (IS_ERR(shm))
		return PTR_ERR(shm);

	data.id = shm->id;
	data.flags = shm->flags;
	data.size = shm->size;

	if (copy_to_user(udata, &data, sizeof(data)))
		ret = -EFAULT;
	else
		ret = tee_shm_get_fd(shm);

	/*
	 * When user space closes the file descriptor the shared memory
	 * should be freed or if tee_shm_get_fd() failed then it will
	 * be freed immediately.
	 */
	tee_shm_put(shm);
	return ret;
}

static int params_from_user(struct tee_context *ctx, struct tee_param *params,
			    size_t num_params,
			    struct tee_ioctl_param __user *uparams)
{
	size_t n;

	for (n = 0; n < num_params; n++) {
		struct tee_shm *shm;
		struct tee_ioctl_param ip;

		if (copy_from_user(&ip, uparams + n, sizeof(ip)))
			return -EFAULT;

		/* All unused attribute bits has to be zero */
		if (ip.attr & ~TEE_IOCTL_PARAM_ATTR_TYPE_MASK)
			return -EINVAL;

		params[n].attr = ip.attr;
		switch (ip.attr) {
		case TEE_IOCTL_PARAM_ATTR_TYPE_NONE:
		case TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_OUTPUT:
			break;
		case TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INPUT:
		case TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INOUT:
			params[n].u.value.a = ip.u.value.a;
			params[n].u.value.b = ip.u.value.b;
			params[n].u.value.c = ip.u.value.c;
			break;
		case TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_INPUT:
		case TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_OUTPUT:
		case TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_INOUT:
			/*
			 * If we fail to get a pointer to a shared memory
			 * object (and increase the ref count) from an
			 * identifier we return an error. All pointers that
			 * has been added in params have an increased ref
			 * count. It's the callers responibility to do
			 * tee_shm_put() on all resolved pointers.
			 */
			shm = tee_shm_get_from_id(ctx, ip.u.memref.shm_id);
			if (IS_ERR(shm))
				return PTR_ERR(shm);

			params[n].u.memref.shm_offs = ip.u.memref.shm_offs;
			params[n].u.memref.size = ip.u.memref.size;
			params[n].u.memref.shm = shm;
			break;
		default:
			/* Unknown attribute */
			return -EINVAL;
		}
	}
	return 0;
}

static int params_to_user(struct tee_ioctl_param __user *uparams,
			  size_t num_params, struct tee_param *params)
{
	size_t n;

	for (n = 0; n < num_params; n++) {
		struct tee_ioctl_param __user *up = uparams + n;
		struct tee_param *p = params + n;

		switch (p->attr) {
		case TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_OUTPUT:
		case TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INOUT:
			if (put_user(p->u.value.a, &up->u.value.a) ||
			    put_user(p->u.value.b, &up->u.value.b) ||
			    put_user(p->u.value.c, &up->u.value.c))
				return -EFAULT;
			break;
		case TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_OUTPUT:
		case TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_INOUT:
			if (put_user((u64)p->u.memref.size, &up->u.memref.size))
				return -EFAULT;
		default:
			break;
		}
	}
	return 0;
}

static bool param_is_memref(struct tee_param *param)
{
	switch (param->attr & TEE_IOCTL_PARAM_ATTR_TYPE_MASK) {
	case TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_INPUT:
	case TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_OUTPUT:
	case TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_INOUT:
		return true;
	default:
		return false;
	}
}

static int tee_ioctl_open_session(struct tee_context *ctx,
				  struct tee_ioctl_buf_data __user *ubuf)
{
	int rc;
	size_t n;
	struct tee_ioctl_buf_data buf;
	struct tee_ioctl_open_session_arg __user *uarg;
	struct tee_ioctl_open_session_arg arg;
	struct tee_ioctl_param __user *uparams = NULL;
	struct tee_param *params = NULL;
	bool have_session = false;

	if (!ctx->teedev->desc->ops->open_session)
		return -EINVAL;

	if (copy_from_user(&buf, ubuf, sizeof(buf)))
		return -EFAULT;

	if (buf.buf_len > TEE_MAX_ARG_SIZE ||
	    buf.buf_len < sizeof(struct tee_ioctl_open_session_arg))
		return -EINVAL;

	uarg = (struct tee_ioctl_open_session_arg __user *)(unsigned long)
		buf.buf_ptr;
	if (copy_from_user(&arg, uarg, sizeof(arg)))
		return -EFAULT;

	if (sizeof(arg) + TEE_IOCTL_PARAM_SIZE(arg.num_params) != buf.buf_len)
		return -EINVAL;

	if (arg.num_params) {
		params = kcalloc(arg.num_params, sizeof(struct tee_param),
				 GFP_KERNEL);
		if (!params)
			return -ENOMEM;
		uparams = (struct tee_ioctl_param __user *)(uarg + 1);
		rc = params_from_user(ctx, params, arg.num_params, uparams);
		if (rc)
			goto out;
	}

	rc = ctx->teedev->desc->ops->open_session(ctx, &arg, params);
	if (rc)
		goto out;
	have_session = true;

	if (put_user(arg.session, &uarg->session) ||
	    put_user(arg.ret, &uarg->ret) ||
	    put_user(arg.ret_origin, &uarg->ret_origin)) {
		rc = -EFAULT;
		goto out;
	}
	rc = params_to_user(uparams, arg.num_params, params);
out:
	/*
	 * If we've succeeded to open the session but failed to communicate
	 * it back to user space, close the session again to avoid leakage.
	 */
	if (rc && have_session && ctx->teedev->desc->ops->close_session)
		ctx->teedev->desc->ops->close_session(ctx, arg.session);

	if (params) {
		/* Decrease ref count for all valid shared memory pointers */
		for (n = 0; n < arg.num_params; n++)
			if (param_is_memref(params + n) &&
			    params[n].u.memref.shm)
				tee_shm_put(params[n].u.memref.shm);
		kfree(params);
	}

	return rc;
}

static int tee_ioctl_invoke(struct tee_context *ctx,
			    struct tee_ioctl_buf_data __user *ubuf)
{
	int rc;
	size_t n;
	struct tee_ioctl_buf_data buf;
	struct tee_ioctl_invoke_arg __user *uarg;
	struct tee_ioctl_invoke_arg arg;
	struct tee_ioctl_param __user *uparams = NULL;
	struct tee_param *params = NULL;

	if (!ctx->teedev->desc->ops->invoke_func)
		return -EINVAL;

	if (copy_from_user(&buf, ubuf, sizeof(buf)))
		return -EFAULT;

	if (buf.buf_len > TEE_MAX_ARG_SIZE ||
	    buf.buf_len < sizeof(struct tee_ioctl_invoke_arg))
		return -EINVAL;

	uarg = (struct tee_ioctl_invoke_arg __user *)(unsigned long)buf.buf_ptr;
	if (copy_from_user(&arg, uarg, sizeof(arg)))
		return -EFAULT;

	if (sizeof(arg) + TEE_IOCTL_PARAM_SIZE(arg.num_params) != buf.buf_len)
		return -EINVAL;

	if (arg.num_params) {
		params = kcalloc(arg.num_params, sizeof(struct tee_param),
				 GFP_KERNEL);
		if (!params)
			return -ENOMEM;
		uparams = (struct tee_ioctl_param __user *)(uarg + 1);
		rc = params_from_user(ctx, params, arg.num_params, uparams);
		if (rc)
			goto out;
	}

	rc = ctx->teedev->desc->ops->invoke_func(ctx, &arg, params);
	if (rc)
		goto out;

	if (put_user(arg.ret, &uarg->ret) ||
	    put_user(arg.ret_origin, &uarg->ret_origin)) {
		rc = -EFAULT;
		goto out;
	}
	rc = params_to_user(uparams, arg.num_params, params);
out:
	if (params) {
		/* Decrease ref count for all valid shared memory pointers */
		for (n = 0; n < arg.num_params; n++)
			if (param_is_memref(params + n) &&
			    params[n].u.memref.shm)
				tee_shm_put(params[n].u.memref.shm);
		kfree(params);
	}
	return rc;
}

static int tee_ioctl_cancel(struct tee_context *ctx,
			    struct tee_ioctl_cancel_arg __user *uarg)
{
	struct tee_ioctl_cancel_arg arg;

	if (!ctx->teedev->desc->ops->cancel_req)
		return -EINVAL;

	if (copy_from_user(&arg, uarg, sizeof(arg)))
		return -EFAULT;

	return ctx->teedev->desc->ops->cancel_req(ctx, arg.cancel_id,
						  arg.session);
}

static int
tee_ioctl_close_session(struct tee_context *ctx,
			struct tee_ioctl_close_session_arg __user *uarg)
{
	struct tee_ioctl_close_session_arg arg;

	if (!ctx->teedev->desc->ops->close_session)
		return -EINVAL;

	if (copy_from_user(&arg, uarg, sizeof(arg)))
		return -EFAULT;

	return ctx->teedev->desc->ops->close_session(ctx, arg.session);
}

static int params_to_supp(struct tee_context *ctx,
			  struct tee_ioctl_param __user *uparams,
			  size_t num_params, struct tee_param *params)
{
	size_t n;

	for (n = 0; n < num_params; n++) {
		struct tee_ioctl_param ip;
		struct tee_param *p = params + n;

		ip.attr = p->attr & TEE_IOCTL_PARAM_ATTR_TYPE_MASK;
		switch (p->attr) {
		case TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INPUT:
		case TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INOUT:
			ip.u.value.a = p->u.value.a;
			ip.u.value.b = p->u.value.b;
			ip.u.value.c = p->u.value.c;
			break;
		case TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_INPUT:
		case TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_OUTPUT:
		case TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_INOUT:
			ip.u.memref.size = p->u.memref.size;
			if (!p->u.memref.shm) {
				ip.u.memref.shm_offs = 0;
				ip.u.memref.shm_id = -1;
				break;
			}
			ip.u.memref.shm_offs = p->u.memref.shm_offs;
			ip.u.memref.shm_id = p->u.memref.shm->id;
			break;
		default:
			memset(&ip.u, 0, sizeof(ip.u));
			break;
		}

		if (copy_to_user(uparams + n, &ip, sizeof(ip)))
			return -EFAULT;
	}

	return 0;
}

static int tee_ioctl_supp_recv(struct tee_context *ctx,
			       struct tee_ioctl_buf_data __user *ubuf)
{
	int rc;
	struct tee_ioctl_buf_data buf;
	struct tee_iocl_supp_recv_arg __user *uarg;
	struct tee_param *params;
	struct tee_ioctl_param __user *uparams;
	u32 num_params;
	u32 func;

	if (!ctx->teedev->desc->ops->supp_recv)
		return -EINVAL;

	if (copy_from_user(&buf, ubuf, sizeof(buf)))
		return -EFAULT;

	if (buf.buf_len > TEE_MAX_ARG_SIZE ||
	    buf.buf_len < sizeof(struct tee_iocl_supp_recv_arg))
		return -EINVAL;

	uarg = (struct tee_iocl_supp_recv_arg __user *)(unsigned long)
		buf.buf_ptr;
	if (get_user(num_params, &uarg->num_params))
		return -EFAULT;

	if (sizeof(*uarg) + TEE_IOCTL_PARAM_SIZE(num_params) != buf.buf_len)
		return -EINVAL;

	params = kcalloc(num_params, sizeof(struct tee_param), GFP_KERNEL);
	if (!params)
		return -ENOMEM;

	rc = ctx->teedev->desc->ops->supp_recv(ctx, &func, &num_params, params);
	if (rc)
		goto out;

	if (put_user(func, &uarg->func) ||
	    put_user(num_params, &uarg->num_params)) {
		rc = -EFAULT;
		goto out;
	}

	uparams = (struct tee_ioctl_param __user *)(uarg + 1);
	rc = params_to_supp(ctx, uparams, num_params, params);
out:
	kfree(params);
	return rc;
}

static int params_from_supp(struct tee_param *params, size_t num_params,
			    struct tee_ioctl_param __user *uparams)
{
	size_t n;

	for (n = 0; n < num_params; n++) {
		struct tee_param *p = params + n;
		struct tee_ioctl_param ip;

		if (copy_from_user(&ip, uparams + n, sizeof(ip)))
			return -EFAULT;

		/* All unused attribute bits has to be zero */
		if (ip.attr & ~TEE_IOCTL_PARAM_ATTR_TYPE_MASK)
			return -EINVAL;

		p->attr = ip.attr;
		switch (ip.attr) {
		case TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_OUTPUT:
		case TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INOUT:
			/* Only out and in/out values can be updated */
			p->u.value.a = ip.u.value.a;
			p->u.value.b = ip.u.value.b;
			p->u.value.c = ip.u.value.c;
			break;
		case TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_OUTPUT:
		case TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_INOUT:
			/*
			 * Only the size of the memref can be updated.
			 * Since we don't have access to the original
			 * parameters here, only store the supplied size.
			 * The driver will copy the updated size into the
			 * original parameters.
			 */
			p->u.memref.shm = NULL;
			p->u.memref.shm_offs = 0;
			p->u.memref.size = ip.u.memref.size;
			break;
		default:
			memset(&p->u, 0, sizeof(p->u));
			break;
		}
	}
	return 0;
}

static int tee_ioctl_supp_send(struct tee_context *ctx,
			       struct tee_ioctl_buf_data __user *ubuf)
{
	long rc;
	struct tee_ioctl_buf_data buf;
	struct tee_iocl_supp_send_arg __user *uarg;
	struct tee_param *params;
	struct tee_ioctl_param __user *uparams;
	u32 num_params;
	u32 ret;

	/* Not valid for this driver */
	if (!ctx->teedev->desc->ops->supp_send)
		return -EINVAL;

	if (copy_from_user(&buf, ubuf, sizeof(buf)))
		return -EFAULT;

	if (buf.buf_len > TEE_MAX_ARG_SIZE ||
	    buf.buf_len < sizeof(struct tee_iocl_supp_send_arg))
		return -EINVAL;

	uarg = (struct tee_iocl_supp_send_arg __user *)(unsigned long)
		buf.buf_ptr;
	if (get_user(ret, &uarg->ret) ||
	    get_user(num_params, &uarg->num_params))
		return -EFAULT;

	if (sizeof(*uarg) + TEE_IOCTL_PARAM_SIZE(num_params) > buf.buf_len)
		return -EINVAL;

	params = kcalloc(num_params, sizeof(struct tee_param), GFP_KERNEL);
	if (!params)
		return -ENOMEM;

	uparams = (struct tee_ioctl_param __user *)(uarg + 1);
	rc = params_from_supp(params, num_params, uparams);
	if (rc)
		goto out;

	rc = ctx->teedev->desc->ops->supp_send(ctx, ret, num_params, params);
out:
	kfree(params);
	return rc;
}

static long tee_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct tee_context *ctx = filp->private_data;
	void __user *uarg = (void __user *)arg;

	switch (cmd) {
	case TEE_IOC_VERSION:
		return tee_ioctl_version(ctx, uarg);
	case TEE_IOC_SHM_ALLOC:
		return tee_ioctl_shm_alloc(ctx, uarg);
	case TEE_IOC_OPEN_SESSION:
		return tee_ioctl_open_session(ctx, uarg);
	case TEE_IOC_INVOKE:
		return tee_ioctl_invoke(ctx, uarg);
	case TEE_IOC_CANCEL:
		return tee_ioctl_cancel(ctx, uarg);
	case TEE_IOC_CLOSE_SESSION:
		return tee_ioctl_close_session(ctx, uarg);
	case TEE_IOC_SUPPL_RECV:
		return tee_ioctl_supp_recv(ctx, uarg);
	case TEE_IOC_SUPPL_SEND:
		return tee_ioctl_supp_send(ctx, uarg);
	default:
		return -EINVAL;
	}
}

static const struct file_operations tee_fops = {
	.owner = THIS_MODULE,
	.open = tee_open,
	.release = tee_release,
	.unlocked_ioctl = tee_ioctl,
	.compat_ioctl = tee_ioctl,
};

static void tee_release_device(struct device *dev)
{
	struct tee_device *teedev = container_of(dev, struct tee_device, dev);

	spin_lock(&driver_lock);
	clear_bit(teedev->id, dev_mask);
	spin_unlock(&driver_lock);
	mutex_destroy(&teedev->mutex);
	kfree(teedev);
}

/**
 * tee_device_alloc() - Allocate a new struct tee_device instance
 * @teedesc:	Descriptor for this driver
 * @dev:	Parent device for this device
 * @pool:	Shared memory pool, NULL if not used
 * @driver_data: Private driver data for this device
 *
 * Allocates a new struct tee_device instance. The device is
 * removed by tee_device_unregister().
 *
 * @returns a pointer to a 'struct tee_device' or an ERR_PTR on failure
 */
struct tee_device *tee_device_alloc(const struct tee_desc *teedesc,
				    struct device *dev,
				    struct tee_shm_pool *pool,
				    void *driver_data)
{
	struct tee_device *teedev;
	void *ret;
	int rc;
	int offs = 0;

	if (!teedesc || !teedesc->name || !teedesc->ops ||
	    !teedesc->ops->get_version || !teedesc->ops->open ||
	    !teedesc->ops->release || !dev || !pool)
		return ERR_PTR(-EINVAL);

	teedev = kzalloc(sizeof(*teedev), GFP_KERNEL);
	if (!teedev) {
		ret = ERR_PTR(-ENOMEM);
		goto err;
	}

	if (teedesc->flags & TEE_DESC_PRIVILEGED)
		offs = TEE_NUM_DEVICES / 2;

	spin_lock(&driver_lock);
	teedev->id = find_next_zero_bit(dev_mask, TEE_NUM_DEVICES, offs);
	if (teedev->id < TEE_NUM_DEVICES)
		set_bit(teedev->id, dev_mask);
	spin_unlock(&driver_lock);

	if (teedev->id >= TEE_NUM_DEVICES) {
		ret = ERR_PTR(-ENOMEM);
		goto err;
	}

	snprintf(teedev->name, sizeof(teedev->name), "tee%s%d",
		 teedesc->flags & TEE_DESC_PRIVILEGED ? "priv" : "",
		 teedev->id - offs);

	teedev->dev.class = tee_class;
	teedev->dev.release = tee_release_device;
	teedev->dev.parent = dev;

	teedev->dev.devt = MKDEV(MAJOR(tee_devt), teedev->id);

	rc = dev_set_name(&teedev->dev, "%s", teedev->name);
	if (rc) {
		ret = ERR_PTR(rc);
		goto err_devt;
	}

	cdev_init(&teedev->cdev, &tee_fops);
	teedev->cdev.owner = teedesc->owner;
	teedev->cdev.kobj.parent = &teedev->dev.kobj;

	dev_set_drvdata(&teedev->dev, driver_data);
	device_initialize(&teedev->dev);

	/* 1 as tee_device_unregister() does one final tee_device_put() */
	teedev->num_users = 1;
	init_completion(&teedev->c_no_users);
	mutex_init(&teedev->mutex);

	teedev->desc = teedesc;
	teedev->pool = pool;

	return teedev;
err_devt:
	unregister_chrdev_region(teedev->dev.devt, 1);
err:
	dev_err(dev, "could not register %s driver\n",
		teedesc->flags & TEE_DESC_PRIVILEGED ? "privileged" : "client");
	if (teedev && teedev->id < TEE_NUM_DEVICES) {
		spin_lock(&driver_lock);
		clear_bit(teedev->id, dev_mask);
		spin_unlock(&driver_lock);
	}
	kfree(teedev);
	return ret;
}
EXPORT_SYMBOL_GPL(tee_device_alloc);

static ssize_t implementation_id_show(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct tee_device *teedev = container_of(dev, struct tee_device, dev);
	struct tee_ioctl_version_data vers;

	teedev->desc->ops->get_version(teedev, &vers);
	return scnprintf(buf, PAGE_SIZE, "%d\n", vers.impl_id);
}
static DEVICE_ATTR_RO(implementation_id);

static struct attribute *tee_dev_attrs[] = {
	&dev_attr_implementation_id.attr,
	NULL
};

static const struct attribute_group tee_dev_group = {
	.attrs = tee_dev_attrs,
};

/**
 * tee_device_register() - Registers a TEE device
 * @teedev:	Device to register
 *
 * tee_device_unregister() need to be called to remove the @teedev if
 * this function fails.
 *
 * @returns < 0 on failure
 */
int tee_device_register(struct tee_device *teedev)
{
	int rc;

	/*
	 * If the teedev already is registered, don't do it again. It's
	 * obviously an error to try to register twice, but if we return
	 * an error we'll force the driver to remove the teedev.
	 */
	if (teedev->flags & TEE_DEVICE_FLAG_REGISTERED) {
		dev_err(&teedev->dev, "attempt to register twice\n");
		return 0;
	}

	rc = cdev_add(&teedev->cdev, teedev->dev.devt, 1);
	if (rc) {
		dev_err(&teedev->dev,
			"unable to cdev_add() %s, major %d, minor %d, err=%d\n",
			teedev->name, MAJOR(teedev->dev.devt),
			MINOR(teedev->dev.devt), rc);
		return rc;
	}

	rc = device_add(&teedev->dev);
	if (rc) {
		dev_err(&teedev->dev,
			"unable to device_add() %s, major %d, minor %d, err=%d\n",
			teedev->name, MAJOR(teedev->dev.devt),
			MINOR(teedev->dev.devt), rc);
		goto err_device_add;
	}

	rc = sysfs_create_group(&teedev->dev.kobj, &tee_dev_group);
	if (rc) {
		dev_err(&teedev->dev,
			"failed to create sysfs attributes, err=%d\n", rc);
		goto err_sysfs_create_group;
	}

	teedev->flags |= TEE_DEVICE_FLAG_REGISTERED;
	return 0;

err_sysfs_create_group:
	device_del(&teedev->dev);
err_device_add:
	cdev_del(&teedev->cdev);
	return rc;
}
EXPORT_SYMBOL_GPL(tee_device_register);

void tee_device_put(struct tee_device *teedev)
{
	mutex_lock(&teedev->mutex);
	/* Shouldn't put in this state */
	if (!WARN_ON(!teedev->desc)) {
		teedev->num_users--;
		if (!teedev->num_users) {
			teedev->desc = NULL;
			complete(&teedev->c_no_users);
		}
	}
	mutex_unlock(&teedev->mutex);
}

bool tee_device_get(struct tee_device *teedev)
{
	mutex_lock(&teedev->mutex);
	if (!teedev->desc) {
		mutex_unlock(&teedev->mutex);
		return false;
	}
	teedev->num_users++;
	mutex_unlock(&teedev->mutex);
	return true;
}

/**
 * tee_device_unregister() - Removes a TEE device
 * @teedev:	Device to unregister
 *
 * This function should be called to remove the @teedev even if
 * tee_device_register() hasn't been called yet. Does nothing if
 * @teedev is NULL.
 */
void tee_device_unregister(struct tee_device *teedev)
{
	if (!teedev)
		return;

	if (teedev->flags & TEE_DEVICE_FLAG_REGISTERED) {
		sysfs_remove_group(&teedev->dev.kobj, &tee_dev_group);
		cdev_del(&teedev->cdev);
		device_del(&teedev->dev);
	}

	tee_device_put(teedev);
	wait_for_completion(&teedev->c_no_users);

	/*
	 * No need to take a mutex any longer now since teedev->desc was
	 * set to NULL before teedev->c_no_users was completed.
	 */

	teedev->pool = NULL;

	put_device(&teedev->dev);
}
EXPORT_SYMBOL_GPL(tee_device_unregister);

/**
 * tee_get_drvdata() - Return driver_data pointer
 * @teedev:	Device containing the driver_data pointer
 * @returns the driver_data pointer supplied to tee_register().
 */
void *tee_get_drvdata(struct tee_device *teedev)
{
	return dev_get_drvdata(&teedev->dev);
}
EXPORT_SYMBOL_GPL(tee_get_drvdata);

struct match_dev_data {
	struct tee_ioctl_version_data *vers;
	const void *data;
	int (*match)(struct tee_ioctl_version_data *, const void *);
};

static int match_dev(struct device *dev, const void *data)
{
	const struct match_dev_data *match_data = data;
	struct tee_device *teedev = container_of(dev, struct tee_device, dev);

	teedev->desc->ops->get_version(teedev, match_data->vers);
	return match_data->match(match_data->vers, match_data->data);
}

struct tee_context *tee_client_open_context(struct tee_context *start,
			int (*match)(struct tee_ioctl_version_data *,
				const void *),
			const void *data, struct tee_ioctl_version_data *vers)
{
	struct device *dev = NULL;
	struct device *put_dev = NULL;
	struct tee_context *ctx = NULL;
	struct tee_ioctl_version_data v;
	struct match_dev_data match_data = { vers ? vers : &v, data, match };

	if (start)
		dev = &start->teedev->dev;

	do {
		dev = class_find_device(tee_class, dev, &match_data, match_dev);
		if (!dev) {
			ctx = ERR_PTR(-ENOENT);
			break;
		}

		put_device(put_dev);
		put_dev = dev;

		ctx = teedev_open(container_of(dev, struct tee_device, dev));
	} while (IS_ERR(ctx) && PTR_ERR(ctx) != -ENOMEM);

	put_device(put_dev);
	return ctx;
}
EXPORT_SYMBOL_GPL(tee_client_open_context);

void tee_client_close_context(struct tee_context *ctx)
{
	teedev_close_context(ctx);
}

EXPORT_SYMBOL_GPL(tee_client_close_context);

void tee_client_get_version(struct tee_context *ctx,
			struct tee_ioctl_version_data *vers)
{
	ctx->teedev->desc->ops->get_version(ctx->teedev, vers);
}
EXPORT_SYMBOL_GPL(tee_client_get_version);


int tee_client_open_session(struct tee_context *ctx,
			struct tee_ioctl_open_session_arg *arg,
			struct tee_param *param)
{
	if (!ctx->teedev->desc->ops->open_session)
		return -EINVAL;
	return ctx->teedev->desc->ops->open_session(ctx, arg, param);
}
EXPORT_SYMBOL_GPL(tee_client_open_session);

int tee_client_close_session(struct tee_context *ctx, u32 session)
{
	if (!ctx->teedev->desc->ops->close_session)
		return -EINVAL;
	return ctx->teedev->desc->ops->close_session(ctx, session);
}
EXPORT_SYMBOL_GPL(tee_client_close_session);

int tee_client_invoke_func(struct tee_context *ctx,
			struct tee_ioctl_invoke_arg *arg,
			struct tee_param *param)
{
	if (!ctx->teedev->desc->ops->invoke_func)
		return -EINVAL;
	return ctx->teedev->desc->ops->invoke_func(ctx, arg, param);
}
EXPORT_SYMBOL_GPL(tee_client_invoke_func);

static int __init tee_init(void)
{
	int rc;

	tee_class = class_create(THIS_MODULE, "tee");
	if (IS_ERR(tee_class)) {
		pr_err("couldn't create class\n");
		return PTR_ERR(tee_class);
	}

	rc = alloc_chrdev_region(&tee_devt, 0, TEE_NUM_DEVICES, "tee");
	if (rc) {
		pr_err("failed to allocate char dev region\n");
		class_destroy(tee_class);
		tee_class = NULL;
	}

	return rc;
}

static void __exit tee_exit(void)
{
	class_destroy(tee_class);
	tee_class = NULL;
	unregister_chrdev_region(tee_devt, TEE_NUM_DEVICES);
}

subsys_initcall(tee_init);
module_exit(tee_exit);

MODULE_AUTHOR("Linaro");
MODULE_DESCRIPTION("TEE Driver");
MODULE_VERSION("1.0");
MODULE_LICENSE("GPL v2");
