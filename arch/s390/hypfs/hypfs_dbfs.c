/*
 * Hypervisor filesystem for Linux on s390 - debugfs interface
 *
 * Copyright IBM Corp. 2010
 * Author(s): Michael Holzheu <holzheu@linux.vnet.ibm.com>
 */

#include <linux/slab.h>
#include "hypfs.h"

static struct dentry *dbfs_dir;

static struct hypfs_dbfs_data *hypfs_dbfs_data_alloc(struct hypfs_dbfs_file *f)
{
	struct hypfs_dbfs_data *data;

	data = kmalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return NULL;
	kref_init(&data->kref);
	data->dbfs_file = f;
	return data;
}

static void hypfs_dbfs_data_free(struct kref *kref)
{
	struct hypfs_dbfs_data *data;

	data = container_of(kref, struct hypfs_dbfs_data, kref);
	data->dbfs_file->data_free(data->buf_free_ptr);
	kfree(data);
}

static void data_free_delayed(struct work_struct *work)
{
	struct hypfs_dbfs_data *data;
	struct hypfs_dbfs_file *df;

	df = container_of(work, struct hypfs_dbfs_file, data_free_work.work);
	mutex_lock(&df->lock);
	data = df->data;
	df->data = NULL;
	mutex_unlock(&df->lock);
	kref_put(&data->kref, hypfs_dbfs_data_free);
}

static ssize_t dbfs_read(struct file *file, char __user *buf,
			 size_t size, loff_t *ppos)
{
	struct hypfs_dbfs_data *data;
	struct hypfs_dbfs_file *df;
	ssize_t rc;

	if (*ppos != 0)
		return 0;

	df = file_inode(file)->i_private;
	mutex_lock(&df->lock);
	if (!df->data) {
		data = hypfs_dbfs_data_alloc(df);
		if (!data) {
			mutex_unlock(&df->lock);
			return -ENOMEM;
		}
		rc = df->data_create(&data->buf, &data->buf_free_ptr,
				     &data->size);
		if (rc) {
			mutex_unlock(&df->lock);
			kfree(data);
			return rc;
		}
		df->data = data;
		schedule_delayed_work(&df->data_free_work, HZ);
	}
	data = df->data;
	kref_get(&data->kref);
	mutex_unlock(&df->lock);

	rc = simple_read_from_buffer(buf, size, ppos, data->buf, data->size);
	kref_put(&data->kref, hypfs_dbfs_data_free);
	return rc;
}

static const struct file_operations dbfs_ops = {
	.read		= dbfs_read,
	.llseek		= no_llseek,
};

int hypfs_dbfs_create_file(struct hypfs_dbfs_file *df)
{
	df->dentry = debugfs_create_file(df->name, 0400, dbfs_dir, df,
					 &dbfs_ops);
	if (IS_ERR(df->dentry))
		return PTR_ERR(df->dentry);
	mutex_init(&df->lock);
	INIT_DELAYED_WORK(&df->data_free_work, data_free_delayed);
	return 0;
}

void hypfs_dbfs_remove_file(struct hypfs_dbfs_file *df)
{
	debugfs_remove(df->dentry);
}

int hypfs_dbfs_init(void)
{
	dbfs_dir = debugfs_create_dir("s390_hypfs", NULL);
	return PTR_ERR_OR_ZERO(dbfs_dir);
}

void hypfs_dbfs_exit(void)
{
	debugfs_remove(dbfs_dir);
}
