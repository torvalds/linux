/*
 * IBM ASM Service Processor Device Driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Copyright (C) IBM Corporation, 2004
 *
 * Author: Max Asböck <amax@us.ibm.com>
 *
 */

/*
 * Parts of this code are based on an article by Jonathan Corbet
 * that appeared in Linux Weekly News.
 */


/*
 * The IBMASM file virtual filesystem. It creates the following hierarchy
 * dynamically when mounted from user space:
 *
 *    /ibmasm
 *    |-- 0
 *    |   |-- command
 *    |   |-- event
 *    |   |-- reverse_heartbeat
 *    |   `-- remote_video
 *    |       |-- depth
 *    |       |-- height
 *    |       `-- width
 *    .
 *    .
 *    .
 *    `-- n
 *        |-- command
 *        |-- event
 *        |-- reverse_heartbeat
 *        `-- remote_video
 *            |-- depth
 *            |-- height
 *            `-- width
 *
 * For each service processor the following files are created:
 *
 * command: execute dot commands
 *	write: execute a dot command on the service processor
 *	read: return the result of a previously executed dot command
 *
 * events: listen for service processor events
 *	read: sleep (interruptible) until an event occurs
 *      write: wakeup sleeping event listener
 *
 * reverse_heartbeat: send a heartbeat to the service processor
 *	read: sleep (interruptible) until the reverse heartbeat fails
 *      write: wakeup sleeping heartbeat listener
 *
 * remote_video/width
 * remote_video/height
 * remote_video/width: control remote display settings
 *	write: set value
 *	read: read value
 */

#include <linux/fs.h>
#include <linux/pagemap.h>
#include <linux/slab.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include "ibmasm.h"
#include "remote.h"
#include "dot_command.h"

#define IBMASMFS_MAGIC 0x66726f67

static LIST_HEAD(service_processors);

static struct inode *ibmasmfs_make_inode(struct super_block *sb, int mode);
static void ibmasmfs_create_files (struct super_block *sb);
static int ibmasmfs_fill_super (struct super_block *sb, void *data, int silent);


static struct dentry *ibmasmfs_mount(struct file_system_type *fst,
			int flags, const char *name, void *data)
{
	return mount_single(fst, flags, data, ibmasmfs_fill_super);
}

static const struct super_operations ibmasmfs_s_ops = {
	.statfs		= simple_statfs,
	.drop_inode	= generic_delete_inode,
};

static const struct file_operations *ibmasmfs_dir_ops = &simple_dir_operations;

static struct file_system_type ibmasmfs_type = {
	.owner          = THIS_MODULE,
	.name           = "ibmasmfs",
	.mount          = ibmasmfs_mount,
	.kill_sb        = kill_litter_super,
};
MODULE_ALIAS_FS("ibmasmfs");

static int ibmasmfs_fill_super (struct super_block *sb, void *data, int silent)
{
	struct inode *root;

	sb->s_blocksize = PAGE_CACHE_SIZE;
	sb->s_blocksize_bits = PAGE_CACHE_SHIFT;
	sb->s_magic = IBMASMFS_MAGIC;
	sb->s_op = &ibmasmfs_s_ops;
	sb->s_time_gran = 1;

	root = ibmasmfs_make_inode (sb, S_IFDIR | 0500);
	if (!root)
		return -ENOMEM;

	root->i_op = &simple_dir_inode_operations;
	root->i_fop = ibmasmfs_dir_ops;

	sb->s_root = d_make_root(root);
	if (!sb->s_root)
		return -ENOMEM;

	ibmasmfs_create_files(sb);
	return 0;
}

static struct inode *ibmasmfs_make_inode(struct super_block *sb, int mode)
{
	struct inode *ret = new_inode(sb);

	if (ret) {
		ret->i_ino = get_next_ino();
		ret->i_mode = mode;
		ret->i_atime = ret->i_mtime = ret->i_ctime = CURRENT_TIME;
	}
	return ret;
}

static struct dentry *ibmasmfs_create_file(struct dentry *parent,
			const char *name,
			const struct file_operations *fops,
			void *data,
			int mode)
{
	struct dentry *dentry;
	struct inode *inode;

	dentry = d_alloc_name(parent, name);
	if (!dentry)
		return NULL;

	inode = ibmasmfs_make_inode(parent->d_sb, S_IFREG | mode);
	if (!inode) {
		dput(dentry);
		return NULL;
	}

	inode->i_fop = fops;
	inode->i_private = data;

	d_add(dentry, inode);
	return dentry;
}

static struct dentry *ibmasmfs_create_dir(struct dentry *parent,
				const char *name)
{
	struct dentry *dentry;
	struct inode *inode;

	dentry = d_alloc_name(parent, name);
	if (!dentry)
		return NULL;

	inode = ibmasmfs_make_inode(parent->d_sb, S_IFDIR | 0500);
	if (!inode) {
		dput(dentry);
		return NULL;
	}

	inode->i_op = &simple_dir_inode_operations;
	inode->i_fop = ibmasmfs_dir_ops;

	d_add(dentry, inode);
	return dentry;
}

int ibmasmfs_register(void)
{
	return register_filesystem(&ibmasmfs_type);
}

void ibmasmfs_unregister(void)
{
	unregister_filesystem(&ibmasmfs_type);
}

void ibmasmfs_add_sp(struct service_processor *sp)
{
	list_add(&sp->node, &service_processors);
}

/* struct to save state between command file operations */
struct ibmasmfs_command_data {
	struct service_processor	*sp;
	struct command			*command;
};

/* struct to save state between event file operations */
struct ibmasmfs_event_data {
	struct service_processor	*sp;
	struct event_reader		reader;
	int				active;
};

/* struct to save state between reverse heartbeat file operations */
struct ibmasmfs_heartbeat_data {
	struct service_processor	*sp;
	struct reverse_heartbeat	heartbeat;
	int				active;
};

static int command_file_open(struct inode *inode, struct file *file)
{
	struct ibmasmfs_command_data *command_data;

	if (!inode->i_private)
		return -ENODEV;

	command_data = kmalloc(sizeof(struct ibmasmfs_command_data), GFP_KERNEL);
	if (!command_data)
		return -ENOMEM;

	command_data->command = NULL;
	command_data->sp = inode->i_private;
	file->private_data = command_data;
	return 0;
}

static int command_file_close(struct inode *inode, struct file *file)
{
	struct ibmasmfs_command_data *command_data = file->private_data;

	if (command_data->command)
		command_put(command_data->command);

	kfree(command_data);
	return 0;
}

static ssize_t command_file_read(struct file *file, char __user *buf, size_t count, loff_t *offset)
{
	struct ibmasmfs_command_data *command_data = file->private_data;
	struct command *cmd;
	int len;
	unsigned long flags;

	if (*offset < 0)
		return -EINVAL;
	if (count == 0 || count > IBMASM_CMD_MAX_BUFFER_SIZE)
		return 0;
	if (*offset != 0)
		return 0;

	spin_lock_irqsave(&command_data->sp->lock, flags);
	cmd = command_data->command;
	if (cmd == NULL) {
		spin_unlock_irqrestore(&command_data->sp->lock, flags);
		return 0;
	}
	command_data->command = NULL;
	spin_unlock_irqrestore(&command_data->sp->lock, flags);

	if (cmd->status != IBMASM_CMD_COMPLETE) {
		command_put(cmd);
		return -EIO;
	}
	len = min(count, cmd->buffer_size);
	if (copy_to_user(buf, cmd->buffer, len)) {
		command_put(cmd);
		return -EFAULT;
	}
	command_put(cmd);

	return len;
}

static ssize_t command_file_write(struct file *file, const char __user *ubuff, size_t count, loff_t *offset)
{
	struct ibmasmfs_command_data *command_data = file->private_data;
	struct command *cmd;
	unsigned long flags;

	if (*offset < 0)
		return -EINVAL;
	if (count == 0 || count > IBMASM_CMD_MAX_BUFFER_SIZE)
		return 0;
	if (*offset != 0)
		return 0;

	/* commands are executed sequentially, only one command at a time */
	if (command_data->command)
		return -EAGAIN;

	cmd = ibmasm_new_command(command_data->sp, count);
	if (!cmd)
		return -ENOMEM;

	if (copy_from_user(cmd->buffer, ubuff, count)) {
		command_put(cmd);
		return -EFAULT;
	}

	spin_lock_irqsave(&command_data->sp->lock, flags);
	if (command_data->command) {
		spin_unlock_irqrestore(&command_data->sp->lock, flags);
		command_put(cmd);
		return -EAGAIN;
	}
	command_data->command = cmd;
	spin_unlock_irqrestore(&command_data->sp->lock, flags);

	ibmasm_exec_command(command_data->sp, cmd);
	ibmasm_wait_for_response(cmd, get_dot_command_timeout(cmd->buffer));

	return count;
}

static int event_file_open(struct inode *inode, struct file *file)
{
	struct ibmasmfs_event_data *event_data;
	struct service_processor *sp;

	if (!inode->i_private)
		return -ENODEV;

	sp = inode->i_private;

	event_data = kmalloc(sizeof(struct ibmasmfs_event_data), GFP_KERNEL);
	if (!event_data)
		return -ENOMEM;

	ibmasm_event_reader_register(sp, &event_data->reader);

	event_data->sp = sp;
	event_data->active = 0;
	file->private_data = event_data;
	return 0;
}

static int event_file_close(struct inode *inode, struct file *file)
{
	struct ibmasmfs_event_data *event_data = file->private_data;

	ibmasm_event_reader_unregister(event_data->sp, &event_data->reader);
	kfree(event_data);
	return 0;
}

static ssize_t event_file_read(struct file *file, char __user *buf, size_t count, loff_t *offset)
{
	struct ibmasmfs_event_data *event_data = file->private_data;
	struct event_reader *reader = &event_data->reader;
	struct service_processor *sp = event_data->sp;
	int ret;
	unsigned long flags;

	if (*offset < 0)
		return -EINVAL;
	if (count == 0 || count > IBMASM_EVENT_MAX_SIZE)
		return 0;
	if (*offset != 0)
		return 0;

	spin_lock_irqsave(&sp->lock, flags);
	if (event_data->active) {
		spin_unlock_irqrestore(&sp->lock, flags);
		return -EBUSY;
	}
	event_data->active = 1;
	spin_unlock_irqrestore(&sp->lock, flags);

	ret = ibmasm_get_next_event(sp, reader);
	if (ret <= 0)
		goto out;

	if (count < reader->data_size) {
		ret = -EINVAL;
		goto out;
	}

        if (copy_to_user(buf, reader->data, reader->data_size)) {
		ret = -EFAULT;
		goto out;
	}
	ret = reader->data_size;

out:
	event_data->active = 0;
	return ret;
}

static ssize_t event_file_write(struct file *file, const char __user *buf, size_t count, loff_t *offset)
{
	struct ibmasmfs_event_data *event_data = file->private_data;

	if (*offset < 0)
		return -EINVAL;
	if (count != 1)
		return 0;
	if (*offset != 0)
		return 0;

	ibmasm_cancel_next_event(&event_data->reader);
	return 0;
}

static int r_heartbeat_file_open(struct inode *inode, struct file *file)
{
	struct ibmasmfs_heartbeat_data *rhbeat;

	if (!inode->i_private)
		return -ENODEV;

	rhbeat = kmalloc(sizeof(struct ibmasmfs_heartbeat_data), GFP_KERNEL);
	if (!rhbeat)
		return -ENOMEM;

	rhbeat->sp = inode->i_private;
	rhbeat->active = 0;
	ibmasm_init_reverse_heartbeat(rhbeat->sp, &rhbeat->heartbeat);
	file->private_data = rhbeat;
	return 0;
}

static int r_heartbeat_file_close(struct inode *inode, struct file *file)
{
	struct ibmasmfs_heartbeat_data *rhbeat = file->private_data;

	kfree(rhbeat);
	return 0;
}

static ssize_t r_heartbeat_file_read(struct file *file, char __user *buf, size_t count, loff_t *offset)
{
	struct ibmasmfs_heartbeat_data *rhbeat = file->private_data;
	unsigned long flags;
	int result;

	if (*offset < 0)
		return -EINVAL;
	if (count == 0 || count > 1024)
		return 0;
	if (*offset != 0)
		return 0;

	/* allow only one reverse heartbeat per process */
	spin_lock_irqsave(&rhbeat->sp->lock, flags);
	if (rhbeat->active) {
		spin_unlock_irqrestore(&rhbeat->sp->lock, flags);
		return -EBUSY;
	}
	rhbeat->active = 1;
	spin_unlock_irqrestore(&rhbeat->sp->lock, flags);

	result = ibmasm_start_reverse_heartbeat(rhbeat->sp, &rhbeat->heartbeat);
	rhbeat->active = 0;

	return result;
}

static ssize_t r_heartbeat_file_write(struct file *file, const char __user *buf, size_t count, loff_t *offset)
{
	struct ibmasmfs_heartbeat_data *rhbeat = file->private_data;

	if (*offset < 0)
		return -EINVAL;
	if (count != 1)
		return 0;
	if (*offset != 0)
		return 0;

	if (rhbeat->active)
		ibmasm_stop_reverse_heartbeat(&rhbeat->heartbeat);

	return 1;
}

static int remote_settings_file_close(struct inode *inode, struct file *file)
{
	return 0;
}

static ssize_t remote_settings_file_read(struct file *file, char __user *buf, size_t count, loff_t *offset)
{
	void __iomem *address = (void __iomem *)file->private_data;
	unsigned char *page;
	int retval;
	int len = 0;
	unsigned int value;

	if (*offset < 0)
		return -EINVAL;
	if (count == 0 || count > 1024)
		return 0;
	if (*offset != 0)
		return 0;

	page = (unsigned char *)__get_free_page(GFP_KERNEL);
	if (!page)
		return -ENOMEM;

	value = readl(address);
	len = sprintf(page, "%d\n", value);

	if (copy_to_user(buf, page, len)) {
		retval = -EFAULT;
		goto exit;
	}
	*offset += len;
	retval = len;

exit:
	free_page((unsigned long)page);
	return retval;
}

static ssize_t remote_settings_file_write(struct file *file, const char __user *ubuff, size_t count, loff_t *offset)
{
	void __iomem *address = (void __iomem *)file->private_data;
	char *buff;
	unsigned int value;

	if (*offset < 0)
		return -EINVAL;
	if (count == 0 || count > 1024)
		return 0;
	if (*offset != 0)
		return 0;

	buff = kzalloc (count + 1, GFP_KERNEL);
	if (!buff)
		return -ENOMEM;


	if (copy_from_user(buff, ubuff, count)) {
		kfree(buff);
		return -EFAULT;
	}

	value = simple_strtoul(buff, NULL, 10);
	writel(value, address);
	kfree(buff);

	return count;
}

static const struct file_operations command_fops = {
	.open =		command_file_open,
	.release =	command_file_close,
	.read =		command_file_read,
	.write =	command_file_write,
	.llseek =	generic_file_llseek,
};

static const struct file_operations event_fops = {
	.open =		event_file_open,
	.release =	event_file_close,
	.read =		event_file_read,
	.write =	event_file_write,
	.llseek =	generic_file_llseek,
};

static const struct file_operations r_heartbeat_fops = {
	.open =		r_heartbeat_file_open,
	.release =	r_heartbeat_file_close,
	.read =		r_heartbeat_file_read,
	.write =	r_heartbeat_file_write,
	.llseek =	generic_file_llseek,
};

static const struct file_operations remote_settings_fops = {
	.open =		simple_open,
	.release =	remote_settings_file_close,
	.read =		remote_settings_file_read,
	.write =	remote_settings_file_write,
	.llseek =	generic_file_llseek,
};


static void ibmasmfs_create_files (struct super_block *sb)
{
	struct list_head *entry;
	struct service_processor *sp;

	list_for_each(entry, &service_processors) {
		struct dentry *dir;
		struct dentry *remote_dir;
		sp = list_entry(entry, struct service_processor, node);
		dir = ibmasmfs_create_dir(sb->s_root, sp->dirname);
		if (!dir)
			continue;

		ibmasmfs_create_file(dir, "command", &command_fops, sp, S_IRUSR|S_IWUSR);
		ibmasmfs_create_file(dir, "event", &event_fops, sp, S_IRUSR|S_IWUSR);
		ibmasmfs_create_file(dir, "reverse_heartbeat", &r_heartbeat_fops, sp, S_IRUSR|S_IWUSR);

		remote_dir = ibmasmfs_create_dir(dir, "remote_video");
		if (!remote_dir)
			continue;

		ibmasmfs_create_file(remote_dir, "width", &remote_settings_fops, (void *)display_width(sp), S_IRUSR|S_IWUSR);
		ibmasmfs_create_file(remote_dir, "height", &remote_settings_fops, (void *)display_height(sp), S_IRUSR|S_IWUSR);
		ibmasmfs_create_file(remote_dir, "depth", &remote_settings_fops, (void *)display_depth(sp), S_IRUSR|S_IWUSR);
	}
}
