/*
 * RAM Oops/Panic logger
 *
 * Copyright (C) 2010 Marco Stornelli <marco.stornelli@gmail.com>
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

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/kmsg_dump.h>
#include <linux/time.h>
#include <linux/io.h>
#include <linux/ioport.h>

#define RAMOOPS_KERNMSG_HDR "===="

#define RECORD_SIZE 4096

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

static struct ramoops_context {
	struct kmsg_dumper dump;
	void *virt_addr;
	phys_addr_t phys_addr;
	unsigned long size;
	int count;
	int max_count;
} oops_cxt;

static void ramoops_do_dump(struct kmsg_dumper *dumper,
		enum kmsg_dump_reason reason, const char *s1, unsigned long l1,
		const char *s2, unsigned long l2)
{
	struct ramoops_context *cxt = container_of(dumper,
			struct ramoops_context, dump);
	unsigned long s1_start, s2_start;
	unsigned long l1_cpy, l2_cpy;
	int res, hdr_size;
	char *buf, *buf_orig;
	struct timeval timestamp;

	/* Only dump oopses if dump_oops is set */
	if (reason == KMSG_DUMP_OOPS && !dump_oops)
		return;

	buf = (char *)(cxt->virt_addr + (cxt->count * RECORD_SIZE));
	buf_orig = buf;

	memset(buf, '\0', RECORD_SIZE);
	res = sprintf(buf, "%s", RAMOOPS_KERNMSG_HDR);
	buf += res;
	do_gettimeofday(&timestamp);
	res = sprintf(buf, "%lu.%lu\n", (long)timestamp.tv_sec, (long)timestamp.tv_usec);
	buf += res;

	hdr_size = buf - buf_orig;
	l2_cpy = min(l2, (unsigned long)(RECORD_SIZE - hdr_size));
	l1_cpy = min(l1, (unsigned long)(RECORD_SIZE - hdr_size) - l2_cpy);

	s2_start = l2 - l2_cpy;
	s1_start = l1 - l1_cpy;

	memcpy(buf, s1 + s1_start, l1_cpy);
	memcpy(buf + l1_cpy, s2 + s2_start, l2_cpy);

	cxt->count = (cxt->count + 1) % cxt->max_count;
}

static int __init ramoops_init(void)
{
	struct ramoops_context *cxt = &oops_cxt;
	int err = -EINVAL;

	if (!mem_size) {
		printk(KERN_ERR "ramoops: invalid size specification");
		goto fail3;
	}

	rounddown_pow_of_two(mem_size);

	if (mem_size < RECORD_SIZE) {
		printk(KERN_ERR "ramoops: size too small");
		goto fail3;
	}

	cxt->max_count = mem_size / RECORD_SIZE;
	cxt->count = 0;
	cxt->size = mem_size;
	cxt->phys_addr = mem_address;

	if (!request_mem_region(cxt->phys_addr, cxt->size, "ramoops")) {
		printk(KERN_ERR "ramoops: request mem region failed");
		err = -EINVAL;
		goto fail3;
	}

	cxt->virt_addr = ioremap(cxt->phys_addr,  cxt->size);
	if (!cxt->virt_addr) {
		printk(KERN_ERR "ramoops: ioremap failed");
		goto fail2;
	}

	cxt->dump.dump = ramoops_do_dump;
	err = kmsg_dump_register(&cxt->dump);
	if (err) {
		printk(KERN_ERR "ramoops: registering kmsg dumper failed");
		goto fail1;
	}

	return 0;

fail1:
	iounmap(cxt->virt_addr);
fail2:
	release_mem_region(cxt->phys_addr, cxt->size);
fail3:
	return err;
}

static void __exit ramoops_exit(void)
{
	struct ramoops_context *cxt = &oops_cxt;

	if (kmsg_dump_unregister(&cxt->dump) < 0)
		printk(KERN_WARNING "ramoops: could not unregister kmsg_dumper");

	iounmap(cxt->virt_addr);
	release_mem_region(cxt->phys_addr, cxt->size);
}


module_init(ramoops_init);
module_exit(ramoops_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Marco Stornelli <marco.stornelli@gmail.com>");
MODULE_DESCRIPTION("RAM Oops/Panic logger/driver");
