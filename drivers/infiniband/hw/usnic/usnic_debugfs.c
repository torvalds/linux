/*
 * Copyright (c) 2013, Cisco Systems, Inc. All rights reserved.
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
 *
 */

#include <linux/debugfs.h>
#include <linux/module.h>

#include "usnic.h"
#include "usnic_log.h"
#include "usnic_debugfs.h"

static struct dentry *debugfs_root;

static ssize_t usnic_debugfs_buildinfo_read(struct file *f, char __user *data,
						size_t count, loff_t *ppos)
{
	char buf[500];
	int res;

	if (*ppos > 0)
		return 0;

	res = scnprintf(buf, sizeof(buf),
			"version:       %s\n"
			"build date:    %s\n",
			DRV_VERSION, DRV_RELDATE);

	return simple_read_from_buffer(data, count, ppos, buf, res);
}

static const struct file_operations usnic_debugfs_buildinfo_ops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = usnic_debugfs_buildinfo_read
};

void usnic_debugfs_init(void)
{
	debugfs_root = debugfs_create_dir(DRV_NAME, NULL);
	if (IS_ERR(debugfs_root)) {
		usnic_err("Failed to create debugfs root dir, check if debugfs is enabled in kernel configuration\n");
		debugfs_root = NULL;
		return;
	}

	debugfs_create_file("build-info", S_IRUGO, debugfs_root,
				NULL, &usnic_debugfs_buildinfo_ops);
}

void usnic_debugfs_exit(void)
{
	if (!debugfs_root)
		return;

	debugfs_remove_recursive(debugfs_root);
	debugfs_root = NULL;
}
