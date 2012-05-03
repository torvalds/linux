/*
 * RAM Oops/Panic logger
 *
 * Copyright (C) 2010 Marco Stornelli <marco.stornelli@gmail.com>
 * Copyright (C) 2011 Kees Cook <keescook@chromium.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/pstore.h>
#include <linux/time.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/ramoops.h>

#define RAMOOPS_KERNMSG_HDR "===="
#define MIN_MEM_SIZE 4096UL

static ulong record_size = MIN_MEM_SIZE;
module_param(record_size, ulong, 0400);
MODULE_PARM_DESC(record_size,
		"size of each dump done on oops/panic");

static ulong mem_address;
module_param(mem_address, ulong, 0400);
MODULE_PARM_DESC(mem_address,
		"start of reserved RAM used to store oops/panic logs");

static ulong mem_size;
module_param(mem_size, ulong, 0400);
MODULE_PARM_DESC(mem_size,
		"size of reserved RAM used to store oops/panic logs");

static int dump_oops = 1;
module_param(dump_oops, int, 0600);
MODULE_PARM_DESC(dump_oops,
		"set to 1 to dump oopses, 0 to only dump panics (default 1)");

struct ramoops_context {
	void *virt_addr;
	phys_addr_t phys_addr;
	unsigned long size;
	size_t record_size;
	int dump_oops;
	unsigned int count;
	unsigned int max_count;
	unsigned int read_count;
	struct pstore_info pstore;
};

static struct platform_device *dummy;
static struct ramoops_platform_data *dummy_data;

static int ramoops_pstore_open(struct pstore_info *psi)
{
	struct ramoops_context *cxt = psi->data;

	cxt->read_count = 0;
	return 0;
}

static ssize_t ramoops_pstore_read(u64 *id, enum pstore_type_id *type,
				   struct timespec *time,
				   char **buf,
				   struct pstore_info *psi)
{
	ssize_t size;
	char *rambuf;
	struct ramoops_context *cxt = psi->data;

	if (cxt->read_count >= cxt->max_count)
		return -EINVAL;
	*id = cxt->read_count++;
	/* Only supports dmesg output so far. */
	*type = PSTORE_TYPE_DMESG;
	/* TODO(kees): Bogus time for the moment. */
	time->tv_sec = 0;
	time->tv_nsec = 0;

	rambuf = cxt->virt_addr + (*id * cxt->record_size);
	size = strnlen(rambuf, cxt->record_size);
	*buf = kmalloc(size, GFP_KERNEL);
	if (*buf == NULL)
		return -ENOMEM;
	memcpy(*buf, rambuf, size);

	return size;
}

static int ramoops_pstore_write(enum pstore_type_id type,
				enum kmsg_dump_reason reason,
				u64 *id,
				unsigned int part,
				size_t size, struct pstore_info *psi)
{
	char *buf;
	size_t res;
	struct timeval timestamp;
	struct ramoops_context *cxt = psi->data;
	size_t available = cxt->record_size;

	/* Currently ramoops is designed to only store dmesg dumps. */
	if (type != PSTORE_TYPE_DMESG)
		return -EINVAL;

	/* Out of the various dmesg dump types, ramoops is currently designed
	 * to only store crash logs, rather than storing general kernel logs.
	 */
	if (reason != KMSG_DUMP_OOPS &&
	    reason != KMSG_DUMP_PANIC)
		return -EINVAL;

	/* Skip Oopes when configured to do so. */
	if (reason == KMSG_DUMP_OOPS && !cxt->dump_oops)
		return -EINVAL;

	/* Explicitly only take the first part of any new crash.
	 * If our buffer is larger than kmsg_bytes, this can never happen,
	 * and if our buffer is smaller than kmsg_bytes, we don't want the
	 * report split across multiple records.
	 */
	if (part != 1)
		return -ENOSPC;

	buf = cxt->virt_addr + (cxt->count * cxt->record_size);

	res = sprintf(buf, "%s", RAMOOPS_KERNMSG_HDR);
	buf += res;
	available -= res;

	do_gettimeofday(&timestamp);
	res = sprintf(buf, "%lu.%lu\n", (long)timestamp.tv_sec, (long)timestamp.tv_usec);
	buf += res;
	available -= res;

	if (size > available)
		size = available;

	memcpy(buf, cxt->pstore.buf, size);
	memset(buf + size, '\0', available - size);

	cxt->count = (cxt->count + 1) % cxt->max_count;

	return 0;
}

static int ramoops_pstore_erase(enum pstore_type_id type, u64 id,
				struct pstore_info *psi)
{
	char *buf;
	struct ramoops_context *cxt = psi->data;

	if (id >= cxt->max_count)
		return -EINVAL;

	buf = cxt->virt_addr + (id * cxt->record_size);
	memset(buf, '\0', cxt->record_size);

	return 0;
}

static struct ramoops_context oops_cxt = {
	.pstore = {
		.owner	= THIS_MODULE,
		.name	= "ramoops",
		.open	= ramoops_pstore_open,
		.read	= ramoops_pstore_read,
		.write	= ramoops_pstore_write,
		.erase	= ramoops_pstore_erase,
	},
};

static int __init ramoops_probe(struct platform_device *pdev)
{
	struct ramoops_platform_data *pdata = pdev->dev.platform_data;
	struct ramoops_context *cxt = &oops_cxt;
	int err = -EINVAL;

	/* Only a single ramoops area allowed at a time, so fail extra
	 * probes.
	 */
	if (cxt->max_count)
		goto fail_out;

	if (!pdata->mem_size || !pdata->record_size) {
		pr_err("The memory size and the record size must be "
			"non-zero\n");
		goto fail_out;
	}

	pdata->mem_size = rounddown_pow_of_two(pdata->mem_size);
	pdata->record_size = rounddown_pow_of_two(pdata->record_size);

	/* Check for the minimum memory size */
	if (pdata->mem_size < MIN_MEM_SIZE &&
			pdata->record_size < MIN_MEM_SIZE) {
		pr_err("memory size too small, minimum is %lu\n",
			MIN_MEM_SIZE);
		goto fail_out;
	}

	if (pdata->mem_size < pdata->record_size) {
		pr_err("The memory size must be larger than the "
			"records size\n");
		goto fail_out;
	}

	cxt->max_count = pdata->mem_size / pdata->record_size;
	cxt->count = 0;
	cxt->size = pdata->mem_size;
	cxt->phys_addr = pdata->mem_address;
	cxt->record_size = pdata->record_size;
	cxt->dump_oops = pdata->dump_oops;

	cxt->pstore.data = cxt;
	cxt->pstore.bufsize = cxt->record_size;
	cxt->pstore.buf = kmalloc(cxt->pstore.bufsize, GFP_KERNEL);
	spin_lock_init(&cxt->pstore.buf_lock);
	if (!cxt->pstore.buf) {
		pr_err("cannot allocate pstore buffer\n");
		goto fail_clear;
	}

	if (!request_mem_region(cxt->phys_addr, cxt->size, "ramoops")) {
		pr_err("request mem region (0x%lx@0x%llx) failed\n",
			cxt->size, (unsigned long long)cxt->phys_addr);
		err = -EINVAL;
		goto fail_buf;
	}

	cxt->virt_addr = ioremap(cxt->phys_addr,  cxt->size);
	if (!cxt->virt_addr) {
		pr_err("ioremap failed\n");
		goto fail_mem_region;
	}

	err = pstore_register(&cxt->pstore);
	if (err) {
		pr_err("registering with pstore failed\n");
		goto fail_iounmap;
	}

	/*
	 * Update the module parameter variables as well so they are visible
	 * through /sys/module/ramoops/parameters/
	 */
	mem_size = pdata->mem_size;
	mem_address = pdata->mem_address;
	record_size = pdata->record_size;
	dump_oops = pdata->dump_oops;

	pr_info("attached 0x%lx@0x%llx (%ux0x%zx)\n",
		cxt->size, (unsigned long long)cxt->phys_addr,
		cxt->max_count, cxt->record_size);

	return 0;

fail_iounmap:
	iounmap(cxt->virt_addr);
fail_mem_region:
	release_mem_region(cxt->phys_addr, cxt->size);
fail_buf:
	kfree(cxt->pstore.buf);
fail_clear:
	cxt->pstore.bufsize = 0;
	cxt->max_count = 0;
fail_out:
	return err;
}

static int __exit ramoops_remove(struct platform_device *pdev)
{
#if 0
	/* TODO(kees): We cannot unload ramoops since pstore doesn't support
	 * unregistering yet.
	 */
	struct ramoops_context *cxt = &oops_cxt;

	iounmap(cxt->virt_addr);
	release_mem_region(cxt->phys_addr, cxt->size);
	cxt->max_count = 0;

	/* TODO(kees): When pstore supports unregistering, call it here. */
	kfree(cxt->pstore.buf);
	cxt->pstore.bufsize = 0;

	return 0;
#endif
	return -EBUSY;
}

static struct platform_driver ramoops_driver = {
	.remove		= __exit_p(ramoops_remove),
	.driver		= {
		.name	= "ramoops",
		.owner	= THIS_MODULE,
	},
};

static int __init ramoops_init(void)
{
	int ret;
	ret = platform_driver_probe(&ramoops_driver, ramoops_probe);
	if (ret == -ENODEV) {
		/*
		 * If we didn't find a platform device, we use module parameters
		 * building platform data on the fly.
		 */
		pr_info("platform device not found, using module parameters\n");
		dummy_data = kzalloc(sizeof(struct ramoops_platform_data),
				     GFP_KERNEL);
		if (!dummy_data)
			return -ENOMEM;
		dummy_data->mem_size = mem_size;
		dummy_data->mem_address = mem_address;
		dummy_data->record_size = record_size;
		dummy_data->dump_oops = dump_oops;
		dummy = platform_create_bundle(&ramoops_driver, ramoops_probe,
			NULL, 0, dummy_data,
			sizeof(struct ramoops_platform_data));

		if (IS_ERR(dummy))
			ret = PTR_ERR(dummy);
		else
			ret = 0;
	}

	return ret;
}

static void __exit ramoops_exit(void)
{
	platform_driver_unregister(&ramoops_driver);
	kfree(dummy_data);
}

module_init(ramoops_init);
module_exit(ramoops_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Marco Stornelli <marco.stornelli@gmail.com>");
MODULE_DESCRIPTION("RAM Oops/Panic logger/driver");
