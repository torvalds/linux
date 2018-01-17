/*
 *
 * (C) COPYRIGHT 2017 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained
 * from Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 */



#include <linux/debugfs.h>
#include <linux/list.h>
#include <linux/mutex.h>

#include "mali_kbase.h"
#include "mali_kbase_ipa.h"
#include "mali_kbase_ipa_debugfs.h"

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 6, 0))
#define DEFINE_DEBUGFS_ATTRIBUTE DEFINE_SIMPLE_ATTRIBUTE
#endif

struct kbase_ipa_model_param {
	char *name;
	union {
		void *voidp;
		s32 *s32p;
		char *str;
	} addr;
	size_t size;
	enum kbase_ipa_model_param_type type;
	struct kbase_ipa_model *model;
	struct list_head link;
};

static int param_int_get(void *data, u64 *val)
{
	struct kbase_ipa_model_param *param = data;

	mutex_lock(&param->model->kbdev->ipa.lock);
	*(s64 *) val = *param->addr.s32p;
	mutex_unlock(&param->model->kbdev->ipa.lock);

	return 0;
}

static int param_int_set(void *data, u64 val)
{
	struct kbase_ipa_model_param *param = data;
	struct kbase_ipa_model *model = param->model;
	s64 sval = (s64) val;
	int err = 0;

	if (sval < S32_MIN || sval > S32_MAX)
		return -ERANGE;

	mutex_lock(&param->model->kbdev->ipa.lock);
	*param->addr.s32p = val;
	err = kbase_ipa_model_recalculate(model);
	mutex_unlock(&param->model->kbdev->ipa.lock);

	return err;
}

DEFINE_DEBUGFS_ATTRIBUTE(fops_s32, param_int_get, param_int_set, "%lld\n");

static ssize_t param_string_get(struct file *file, char __user *user_buf,
				size_t count, loff_t *ppos)
{
	struct kbase_ipa_model_param *param = file->private_data;
	ssize_t ret;
	size_t len;

	mutex_lock(&param->model->kbdev->ipa.lock);
	len = strnlen(param->addr.str, param->size - 1) + 1;
	ret = simple_read_from_buffer(user_buf, count, ppos,
				      param->addr.str, len);
	mutex_unlock(&param->model->kbdev->ipa.lock);

	return ret;
}

static ssize_t param_string_set(struct file *file, const char __user *user_buf,
				size_t count, loff_t *ppos)
{
	struct kbase_ipa_model_param *param = file->private_data;
	struct kbase_ipa_model *model = param->model;
	ssize_t ret = count;
	size_t buf_size;
	int err;

	mutex_lock(&model->kbdev->ipa.lock);

	if (count > param->size) {
		ret = -EINVAL;
		goto end;
	}

	buf_size = min(param->size - 1, count);
	if (copy_from_user(param->addr.str, user_buf, buf_size)) {
		ret = -EFAULT;
		goto end;
	}

	param->addr.str[buf_size] = '\0';

	err = kbase_ipa_model_recalculate(model);
	if (err < 0)
		ret = err;

end:
	mutex_unlock(&model->kbdev->ipa.lock);

	return ret;
}

static const struct file_operations fops_string = {
	.read = param_string_get,
	.write = param_string_set,
	.open = simple_open,
	.llseek = default_llseek,
};

int kbase_ipa_model_param_add(struct kbase_ipa_model *model, const char *name,
			      void *addr, size_t size,
			      enum kbase_ipa_model_param_type type)
{
	struct kbase_ipa_model_param *param;

	param = kzalloc(sizeof(*param), GFP_KERNEL);

	if (!param)
		return -ENOMEM;

	/* 'name' is stack-allocated for array elements, so copy it into
	 * heap-allocated storage */
	param->name = kstrdup(name, GFP_KERNEL);
	param->addr.voidp = addr;
	param->size = size;
	param->type = type;
	param->model = model;

	list_add(&param->link, &model->params);

	return 0;
}

void kbase_ipa_model_param_free_all(struct kbase_ipa_model *model)
{
	struct kbase_ipa_model_param *param_p, *param_n;

	list_for_each_entry_safe(param_p, param_n, &model->params, link) {
		list_del(&param_p->link);
		kfree(param_p->name);
		kfree(param_p);
	}
}

static void kbase_ipa_model_debugfs_init(struct kbase_ipa_model *model)
{
	struct list_head *it;
	struct dentry *dir;

	lockdep_assert_held(&model->kbdev->ipa.lock);

	dir = debugfs_create_dir(model->ops->name,
				 model->kbdev->mali_debugfs_directory);

	if (!dir) {
		dev_err(model->kbdev->dev,
			"Couldn't create mali debugfs %s directory",
			model->ops->name);
		return;
	}

	list_for_each(it, &model->params) {
		struct kbase_ipa_model_param *param =
				list_entry(it,
					   struct kbase_ipa_model_param,
					   link);
		const struct file_operations *fops = NULL;

		switch (param->type) {
		case PARAM_TYPE_S32:
			fops = &fops_s32;
			break;
		case PARAM_TYPE_STRING:
			fops = &fops_string;
			break;
		}

		if (unlikely(!fops)) {
			dev_err(model->kbdev->dev,
				"Type not set for %s parameter %s\n",
				model->ops->name, param->name);
		} else {
			debugfs_create_file(param->name, S_IRUGO | S_IWUSR,
					    dir, param, fops);
		}
	}
}

void kbase_ipa_debugfs_init(struct kbase_device *kbdev)
{
	mutex_lock(&kbdev->ipa.lock);

	if (kbdev->ipa.configured_model != kbdev->ipa.fallback_model)
		kbase_ipa_model_debugfs_init(kbdev->ipa.configured_model);
	kbase_ipa_model_debugfs_init(kbdev->ipa.fallback_model);

	mutex_unlock(&kbdev->ipa.lock);
}
