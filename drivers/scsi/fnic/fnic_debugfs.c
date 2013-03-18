/*
 * Copyright 2012 Cisco Systems, Inc.  All rights reserved.
 *
 * This program is free software; you may redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/debugfs.h>
#include "fnic.h"

static struct dentry *fnic_trace_debugfs_root;
static struct dentry *fnic_trace_debugfs_file;
static struct dentry *fnic_trace_enable;

/*
 * fnic_trace_ctrl_open - Open the trace_enable file
 * @inode: The inode pointer.
 * @file: The file pointer to attach the trace enable/disable flag.
 *
 * Description:
 * This routine opens a debugsfs file trace_enable.
 *
 * Returns:
 * This function returns zero if successful.
 */
static int fnic_trace_ctrl_open(struct inode *inode, struct file *filp)
{
	filp->private_data = inode->i_private;
	return 0;
}

/*
 * fnic_trace_ctrl_read - Read a trace_enable debugfs file
 * @filp: The file pointer to read from.
 * @ubuf: The buffer to copy the data to.
 * @cnt: The number of bytes to read.
 * @ppos: The position in the file to start reading from.
 *
 * Description:
 * This routine reads value of variable fnic_tracing_enabled
 * and stores into local @buf. It will start reading file at @ppos and
 * copy up to @cnt of data to @ubuf from @buf.
 *
 * Returns:
 * This function returns the amount of data that was read.
 */
static ssize_t fnic_trace_ctrl_read(struct file *filp,
				  char __user *ubuf,
				  size_t cnt, loff_t *ppos)
{
	char buf[64];
	int len;
	len = sprintf(buf, "%u\n", fnic_tracing_enabled);

	return simple_read_from_buffer(ubuf, cnt, ppos, buf, len);
}

/*
 * fnic_trace_ctrl_write - Write to trace_enable debugfs file
 * @filp: The file pointer to write from.
 * @ubuf: The buffer to copy the data from.
 * @cnt: The number of bytes to write.
 * @ppos: The position in the file to start writing to.
 *
 * Description:
 * This routine writes data from user buffer @ubuf to buffer @buf and
 * sets fnic_tracing_enabled value as per user input.
 *
 * Returns:
 * This function returns the amount of data that was written.
 */
static ssize_t fnic_trace_ctrl_write(struct file *filp,
				  const char __user *ubuf,
				  size_t cnt, loff_t *ppos)
{
	char buf[64];
	unsigned long val;
	int ret;

	if (cnt >= sizeof(buf))
		return -EINVAL;

	if (copy_from_user(&buf, ubuf, cnt))
		return -EFAULT;

	buf[cnt] = 0;

	ret = kstrtoul(buf, 10, &val);
	if (ret < 0)
		return ret;

	fnic_tracing_enabled = val;
	(*ppos)++;

	return cnt;
}

/*
 * fnic_trace_debugfs_open - Open the fnic trace log
 * @inode: The inode pointer
 * @file: The file pointer to attach the log output
 *
 * Description:
 * This routine is the entry point for the debugfs open file operation.
 * It allocates the necessary buffer for the log, fills the buffer from
 * the in-memory log and then returns a pointer to that log in
 * the private_data field in @file.
 *
 * Returns:
 * This function returns zero if successful. On error it will return
 * a negative error value.
 */
static int fnic_trace_debugfs_open(struct inode *inode,
				  struct file *file)
{
	fnic_dbgfs_t *fnic_dbg_prt;
	fnic_dbg_prt = kzalloc(sizeof(fnic_dbgfs_t), GFP_KERNEL);
	if (!fnic_dbg_prt)
		return -ENOMEM;

	fnic_dbg_prt->buffer = vmalloc((3*(trace_max_pages * PAGE_SIZE)));
	if (!fnic_dbg_prt->buffer) {
		kfree(fnic_dbg_prt);
		return -ENOMEM;
	}
	memset((void *)fnic_dbg_prt->buffer, 0,
			  (3*(trace_max_pages * PAGE_SIZE)));
	fnic_dbg_prt->buffer_len = fnic_get_trace_data(fnic_dbg_prt);
	file->private_data = fnic_dbg_prt;
	return 0;
}

/*
 * fnic_trace_debugfs_lseek - Seek through a debugfs file
 * @file: The file pointer to seek through.
 * @offset: The offset to seek to or the amount to seek by.
 * @howto: Indicates how to seek.
 *
 * Description:
 * This routine is the entry point for the debugfs lseek file operation.
 * The @howto parameter indicates whether @offset is the offset to directly
 * seek to, or if it is a value to seek forward or reverse by. This function
 * figures out what the new offset of the debugfs file will be and assigns
 * that value to the f_pos field of @file.
 *
 * Returns:
 * This function returns the new offset if successful and returns a negative
 * error if unable to process the seek.
 */
static loff_t fnic_trace_debugfs_lseek(struct file *file,
					loff_t offset,
					int howto)
{
	fnic_dbgfs_t *fnic_dbg_prt = file->private_data;
	loff_t pos = -1;

	switch (howto) {
	case 0:
		pos = offset;
		break;
	case 1:
		pos = file->f_pos + offset;
		break;
	case 2:
		pos = fnic_dbg_prt->buffer_len - offset;
	}
	return (pos < 0 || pos > fnic_dbg_prt->buffer_len) ?
			  -EINVAL : (file->f_pos = pos);
}

/*
 * fnic_trace_debugfs_read - Read a debugfs file
 * @file: The file pointer to read from.
 * @ubuf: The buffer to copy the data to.
 * @nbytes: The number of bytes to read.
 * @pos: The position in the file to start reading from.
 *
 * Description:
 * This routine reads data from the buffer indicated in the private_data
 * field of @file. It will start reading at @pos and copy up to @nbytes of
 * data to @ubuf.
 *
 * Returns:
 * This function returns the amount of data that was read (this could be
 * less than @nbytes if the end of the file was reached).
 */
static ssize_t fnic_trace_debugfs_read(struct file *file,
					char __user *ubuf,
					size_t nbytes,
					loff_t *pos)
{
	fnic_dbgfs_t *fnic_dbg_prt = file->private_data;
	int rc = 0;
	rc = simple_read_from_buffer(ubuf, nbytes, pos,
				  fnic_dbg_prt->buffer,
				  fnic_dbg_prt->buffer_len);
	return rc;
}

/*
 * fnic_trace_debugfs_release - Release the buffer used to store
 * debugfs file data
 * @inode: The inode pointer
 * @file: The file pointer that contains the buffer to release
 *
 * Description:
 * This routine frees the buffer that was allocated when the debugfs
 * file was opened.
 *
 * Returns:
 * This function returns zero.
 */
static int fnic_trace_debugfs_release(struct inode *inode,
					  struct file *file)
{
	fnic_dbgfs_t *fnic_dbg_prt = file->private_data;

	vfree(fnic_dbg_prt->buffer);
	kfree(fnic_dbg_prt);
	return 0;
}

static const struct file_operations fnic_trace_ctrl_fops = {
	.owner = THIS_MODULE,
	.open = fnic_trace_ctrl_open,
	.read = fnic_trace_ctrl_read,
	.write = fnic_trace_ctrl_write,
};

static const struct file_operations fnic_trace_debugfs_fops = {
	.owner = THIS_MODULE,
	.open = fnic_trace_debugfs_open,
	.llseek = fnic_trace_debugfs_lseek,
	.read = fnic_trace_debugfs_read,
	.release = fnic_trace_debugfs_release,
};

/*
 * fnic_trace_debugfs_init - Initialize debugfs for fnic trace logging
 *
 * Description:
 * When Debugfs is configured this routine sets up the fnic debugfs
 * file system. If not already created, this routine will create the
 * fnic directory. It will create file trace to log fnic trace buffer
 * output into debugfs and it will also create file trace_enable to
 * control enable/disable of trace logging into trace buffer.
 */
int fnic_trace_debugfs_init(void)
{
	int rc = -1;
	fnic_trace_debugfs_root = debugfs_create_dir("fnic", NULL);
	if (!fnic_trace_debugfs_root) {
		printk(KERN_DEBUG "Cannot create debugfs root\n");
		return rc;
	}
	fnic_trace_enable = debugfs_create_file("tracing_enable",
					  S_IFREG|S_IRUGO|S_IWUSR,
					  fnic_trace_debugfs_root,
					  NULL, &fnic_trace_ctrl_fops);

	if (!fnic_trace_enable) {
		printk(KERN_DEBUG "Cannot create trace_enable file"
				  " under debugfs");
		return rc;
	}

	fnic_trace_debugfs_file = debugfs_create_file("trace",
						  S_IFREG|S_IRUGO|S_IWUSR,
						  fnic_trace_debugfs_root,
						  NULL,
						  &fnic_trace_debugfs_fops);

	if (!fnic_trace_debugfs_file) {
		printk(KERN_DEBUG "Cannot create trace file under debugfs");
		return rc;
	}
	rc = 0;
	return rc;
}

/*
 * fnic_trace_debugfs_terminate - Tear down debugfs infrastructure
 *
 * Description:
 * When Debugfs is configured this routine removes debugfs file system
 * elements that are specific to fnic trace logging.
 */
void fnic_trace_debugfs_terminate(void)
{
	if (fnic_trace_debugfs_file) {
		debugfs_remove(fnic_trace_debugfs_file);
		fnic_trace_debugfs_file = NULL;
	}
	if (fnic_trace_enable) {
		debugfs_remove(fnic_trace_enable);
		fnic_trace_enable = NULL;
	}
	if (fnic_trace_debugfs_root) {
		debugfs_remove(fnic_trace_debugfs_root);
		fnic_trace_debugfs_root = NULL;
	}
}
