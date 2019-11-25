// SPDX-License-Identifier: GPL-2.0-only
/**
 * Copyright (c) ????		Jochen Schäuble <psionic@psionic.de>
 * Copyright (c) 2003-2004	Joern Engel <joern@wh.fh-wedel.de>
 *
 * Usage:
 *
 * one commend line parameter per device, each in the form:
 *   phram=<name>,<start>,<len>
 * <name> may be up to 63 characters.
 * <start> and <len> can be octal, decimal or hexadecimal.  If followed
 * by "ki", "Mi" or "Gi", the numbers will be interpreted as kilo, mega or
 * gigabytes.
 *
 * Example:
 *	phram=swap,64Mi,128Mi phram=test,900Mi,1Mi
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/io.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/slab.h>
#include <linux/mtd/mtd.h>

struct phram_mtd_list {
	struct mtd_info mtd;
	struct list_head list;
};

static LIST_HEAD(phram_list);

static int phram_erase(struct mtd_info *mtd, struct erase_info *instr)
{
	u_char *start = mtd->priv;

	memset(start + instr->addr, 0xff, instr->len);

	return 0;
}

static int phram_point(struct mtd_info *mtd, loff_t from, size_t len,
		size_t *retlen, void **virt, resource_size_t *phys)
{
	*virt = mtd->priv + from;
	*retlen = len;
	return 0;
}

static int phram_unpoint(struct mtd_info *mtd, loff_t from, size_t len)
{
	return 0;
}

static int phram_read(struct mtd_info *mtd, loff_t from, size_t len,
		size_t *retlen, u_char *buf)
{
	u_char *start = mtd->priv;

	memcpy(buf, start + from, len);
	*retlen = len;
	return 0;
}

static int phram_write(struct mtd_info *mtd, loff_t to, size_t len,
		size_t *retlen, const u_char *buf)
{
	u_char *start = mtd->priv;

	memcpy(start + to, buf, len);
	*retlen = len;
	return 0;
}

static void unregister_devices(void)
{
	struct phram_mtd_list *this, *safe;

	list_for_each_entry_safe(this, safe, &phram_list, list) {
		mtd_device_unregister(&this->mtd);
		iounmap(this->mtd.priv);
		kfree(this->mtd.name);
		kfree(this);
	}
}

static int register_device(char *name, phys_addr_t start, size_t len)
{
	struct phram_mtd_list *new;
	int ret = -ENOMEM;

	new = kzalloc(sizeof(*new), GFP_KERNEL);
	if (!new)
		goto out0;

	ret = -EIO;
	new->mtd.priv = ioremap(start, len);
	if (!new->mtd.priv) {
		pr_err("ioremap failed\n");
		goto out1;
	}


	new->mtd.name = name;
	new->mtd.size = len;
	new->mtd.flags = MTD_CAP_RAM;
	new->mtd._erase = phram_erase;
	new->mtd._point = phram_point;
	new->mtd._unpoint = phram_unpoint;
	new->mtd._read = phram_read;
	new->mtd._write = phram_write;
	new->mtd.owner = THIS_MODULE;
	new->mtd.type = MTD_RAM;
	new->mtd.erasesize = PAGE_SIZE;
	new->mtd.writesize = 1;

	ret = -EAGAIN;
	if (mtd_device_register(&new->mtd, NULL, 0)) {
		pr_err("Failed to register new device\n");
		goto out2;
	}

	list_add_tail(&new->list, &phram_list);
	return 0;

out2:
	iounmap(new->mtd.priv);
out1:
	kfree(new);
out0:
	return ret;
}

static int parse_num64(uint64_t *num64, char *token)
{
	size_t len;
	int shift = 0;
	int ret;

	len = strlen(token);
	/* By dwmw2 editorial decree, "ki", "Mi" or "Gi" are to be used. */
	if (len > 2) {
		if (token[len - 1] == 'i') {
			switch (token[len - 2]) {
			case 'G':
				shift += 10;
				/* fall through */
			case 'M':
				shift += 10;
				/* fall through */
			case 'k':
				shift += 10;
				token[len - 2] = 0;
				break;
			default:
				return -EINVAL;
			}
		}
	}

	ret = kstrtou64(token, 0, num64);
	*num64 <<= shift;

	return ret;
}

static int parse_name(char **pname, const char *token)
{
	size_t len;
	char *name;

	len = strlen(token) + 1;
	if (len > 64)
		return -ENOSPC;

	name = kstrdup(token, GFP_KERNEL);
	if (!name)
		return -ENOMEM;

	*pname = name;
	return 0;
}


static inline void kill_final_newline(char *str)
{
	char *newline = strrchr(str, '\n');

	if (newline && !newline[1])
		*newline = 0;
}


#define parse_err(fmt, args...) do {	\
	pr_err(fmt , ## args);	\
	return 1;		\
} while (0)

#ifndef MODULE
static int phram_init_called;
/*
 * This shall contain the module parameter if any. It is of the form:
 * - phram=<device>,<address>,<size> for module case
 * - phram.phram=<device>,<address>,<size> for built-in case
 * We leave 64 bytes for the device name, 20 for the address and 20 for the
 * size.
 * Example: phram.phram=rootfs,0xa0000000,512Mi
 */
static char phram_paramline[64 + 20 + 20];
#endif

static int phram_setup(const char *val)
{
	char buf[64 + 20 + 20], *str = buf;
	char *token[3];
	char *name;
	uint64_t start;
	uint64_t len;
	int i, ret;

	if (strnlen(val, sizeof(buf)) >= sizeof(buf))
		parse_err("parameter too long\n");

	strcpy(str, val);
	kill_final_newline(str);

	for (i = 0; i < 3; i++)
		token[i] = strsep(&str, ",");

	if (str)
		parse_err("too many arguments\n");

	if (!token[2])
		parse_err("not enough arguments\n");

	ret = parse_name(&name, token[0]);
	if (ret)
		return ret;

	ret = parse_num64(&start, token[1]);
	if (ret) {
		kfree(name);
		parse_err("illegal start address\n");
	}

	ret = parse_num64(&len, token[2]);
	if (ret) {
		kfree(name);
		parse_err("illegal device length\n");
	}

	ret = register_device(name, start, len);
	if (!ret)
		pr_info("%s device: %#llx at %#llx\n", name, len, start);
	else
		kfree(name);

	return ret;
}

static int phram_param_call(const char *val, const struct kernel_param *kp)
{
#ifdef MODULE
	return phram_setup(val);
#else
	/*
	 * If more parameters are later passed in via
	 * /sys/module/phram/parameters/phram
	 * and init_phram() has already been called,
	 * we can parse the argument now.
	 */

	if (phram_init_called)
		return phram_setup(val);

	/*
	 * During early boot stage, we only save the parameters
	 * here. We must parse them later: if the param passed
	 * from kernel boot command line, phram_param_call() is
	 * called so early that it is not possible to resolve
	 * the device (even kmalloc() fails). Defer that work to
	 * phram_setup().
	 */

	if (strlen(val) >= sizeof(phram_paramline))
		return -ENOSPC;
	strcpy(phram_paramline, val);

	return 0;
#endif
}

module_param_call(phram, phram_param_call, NULL, NULL, 0200);
MODULE_PARM_DESC(phram, "Memory region to map. \"phram=<name>,<start>,<length>\"");


static int __init init_phram(void)
{
	int ret = 0;

#ifndef MODULE
	if (phram_paramline[0])
		ret = phram_setup(phram_paramline);
	phram_init_called = 1;
#endif

	return ret;
}

static void __exit cleanup_phram(void)
{
	unregister_devices();
}

module_init(init_phram);
module_exit(cleanup_phram);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Joern Engel <joern@wh.fh-wedel.de>");
MODULE_DESCRIPTION("MTD driver for physical RAM");
