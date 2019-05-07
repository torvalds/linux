// SPDX-License-Identifier: GPL-2.0+
/*
 * Zoran zr36057/zr36067 PCI controller driver, for the
 * Pinnacle/Miro DC10/DC10+/DC30/DC30+, Iomega Buz, Linux
 * Media Labs LML33/LML33R10.
 *
 * This part handles the procFS entries (/proc/ZORAN[%d])
 *
 * Copyright (C) 2000 Serguei Miridonov <mirsev@cicese.mx>
 *
 * Currently maintained by:
 *   Ronald Bultje    <rbultje@ronald.bitfreak.net>
 *   Laurent Pinchart <laurent.pinchart@skynet.be>
 *   Mailinglist      <mjpeg-users@lists.sf.net>
 */
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/vmalloc.h>

#include <linux/proc_fs.h>
#include <linux/pci.h>
#include <linux/i2c.h>
#include <linux/i2c-algo-bit.h>
#include <linux/videodev2.h>
#include <linux/spinlock.h>
#include <linux/sem.h>
#include <linux/seq_file.h>

#include <linux/ctype.h>
#include <linux/poll.h>
#include <asm/io.h>

#include "videocodec.h"
#include "zoran.h"
#include "zoran_procfs.h"
#include "zoran_card.h"

#ifdef CONFIG_PROC_FS
struct procfs_params_zr36067 {
	char *name;
	short reg;
	u32 mask;
	short bit;
};

static const struct procfs_params_zr36067 zr67[] = {
	{"HSPol", 0x000, 1, 30},
	{"HStart", 0x000, 0x3ff, 10},
	{"HEnd", 0x000, 0x3ff, 0},

	{"VSPol", 0x004, 1, 30},
	{"VStart", 0x004, 0x3ff, 10},
	{"VEnd", 0x004, 0x3ff, 0},

	{"ExtFl", 0x008, 1, 26},
	{"TopField", 0x008, 1, 25},
	{"VCLKPol", 0x008, 1, 24},
	{"DupFld", 0x008, 1, 20},
	{"LittleEndian", 0x008, 1, 0},

	{"HsyncStart", 0x10c, 0xffff, 16},
	{"LineTot", 0x10c, 0xffff, 0},

	{"NAX", 0x110, 0xffff, 16},
	{"PAX", 0x110, 0xffff, 0},

	{"NAY", 0x114, 0xffff, 16},
	{"PAY", 0x114, 0xffff, 0},

	/* {"",,,}, */

	{NULL, 0, 0, 0},
};

static void
setparam (struct zoran *zr,
	  char         *name,
	  char         *sval)
{
	int i = 0, reg0, reg, val;

	while (zr67[i].name != NULL) {
		if (!strncmp(name, zr67[i].name, strlen(zr67[i].name))) {
			reg = reg0 = btread(zr67[i].reg);
			reg &= ~(zr67[i].mask << zr67[i].bit);
			if (!isdigit(sval[0]))
				break;
			val = simple_strtoul(sval, NULL, 0);
			if ((val & ~zr67[i].mask))
				break;
			reg |= (val & zr67[i].mask) << zr67[i].bit;
			dprintk(4,
				KERN_INFO
				"%s: setparam: setting ZR36067 register 0x%03x: 0x%08x=>0x%08x %s=%d\n",
				ZR_DEVNAME(zr), zr67[i].reg, reg0, reg,
				zr67[i].name, val);
			btwrite(reg, zr67[i].reg);
			break;
		}
		i++;
	}
}

static int zoran_show(struct seq_file *p, void *v)
{
	struct zoran *zr = p->private;
	int i;

	seq_printf(p, "ZR36067 registers:\n");
	for (i = 0; i < 0x130; i += 16)
		seq_printf(p, "%03X %08X  %08X  %08X  %08X \n", i,
			   btread(i), btread(i+4), btread(i+8), btread(i+12));
	return 0;
}

static int zoran_open(struct inode *inode, struct file *file)
{
	struct zoran *data = PDE_DATA(inode);
	return single_open(file, zoran_show, data);
}

static ssize_t zoran_write(struct file *file, const char __user *buffer,
			size_t count, loff_t *ppos)
{
	struct zoran *zr = PDE_DATA(file_inode(file));
	char *string, *sp;
	char *line, *ldelim, *varname, *svar, *tdelim;

	if (count > 32768)	/* Stupidity filter */
		return -EINVAL;

	string = sp = vmalloc(count + 1);
	if (!string) {
		dprintk(1,
			KERN_ERR
			"%s: write_proc: can not allocate memory\n",
			ZR_DEVNAME(zr));
		return -ENOMEM;
	}
	if (copy_from_user(string, buffer, count)) {
		vfree (string);
		return -EFAULT;
	}
	string[count] = 0;
	dprintk(4, KERN_INFO "%s: write_proc: name=%pD count=%zu zr=%p\n",
		ZR_DEVNAME(zr), file, count, zr);
	ldelim = " \t\n";
	tdelim = "=";
	line = strpbrk(sp, ldelim);
	while (line) {
		*line = 0;
		svar = strpbrk(sp, tdelim);
		if (svar) {
			*svar = 0;
			varname = sp;
			svar++;
			setparam(zr, varname, svar);
		}
		sp = line + 1;
		line = strpbrk(sp, ldelim);
	}
	vfree(string);

	return count;
}

static const struct file_operations zoran_operations = {
	.owner		= THIS_MODULE,
	.open		= zoran_open,
	.read		= seq_read,
	.write		= zoran_write,
	.llseek		= seq_lseek,
	.release	= single_release,
};
#endif

int
zoran_proc_init (struct zoran *zr)
{
#ifdef CONFIG_PROC_FS
	char name[8];

	snprintf(name, 7, "zoran%d", zr->id);
	zr->zoran_proc = proc_create_data(name, 0, NULL, &zoran_operations, zr);
	if (zr->zoran_proc != NULL) {
		dprintk(2,
			KERN_INFO
			"%s: procfs entry /proc/%s allocated. data=%p\n",
			ZR_DEVNAME(zr), name, zr);
	} else {
		dprintk(1, KERN_ERR "%s: Unable to initialise /proc/%s\n",
			ZR_DEVNAME(zr), name);
		return 1;
	}
#endif
	return 0;
}

void
zoran_proc_cleanup (struct zoran *zr)
{
#ifdef CONFIG_PROC_FS
	char name[8];

	snprintf(name, 7, "zoran%d", zr->id);
	if (zr->zoran_proc)
		remove_proc_entry(name, NULL);
	zr->zoran_proc = NULL;
#endif
}
