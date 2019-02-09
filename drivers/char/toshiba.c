/* toshiba.c -- Linux driver for accessing the SMM on Toshiba laptops
 *
 * Copyright (c) 1996-2001  Jonathan A. Buzzard (jonathan@buzzard.org.uk)
 *
 * Valuable assistance and patches from:
 *     Tom May <tom@you-bastards.com>
 *     Rob Napier <rnapier@employees.org>
 *
 * Fn status port numbers for machine ID's courtesy of
 *     0xfc02: Scott Eisert <scott.e@sky-eye.com>
 *     0xfc04: Steve VanDevender <stevev@efn.org>
 *     0xfc08: Garth Berry <garth@itsbruce.net>
 *     0xfc0a: Egbert Eich <eich@xfree86.org>
 *     0xfc10: Andrew Lofthouse <Andrew.Lofthouse@robins.af.mil>
 *     0xfc11: Spencer Olson <solson@novell.com>
 *     0xfc13: Claudius Frankewitz <kryp@gmx.de>
 *     0xfc15: Tom May <tom@you-bastards.com>
 *     0xfc17: Dave Konrad <konrad@xenia.it>
 *     0xfc1a: George Betzos <betzos@engr.colostate.edu>
 *     0xfc1b: Munemasa Wada <munemasa@jnovel.co.jp>
 *     0xfc1d: Arthur Liu <armie@slap.mine.nu>
 *     0xfc5a: Jacques L'helgoualc'h <lhh@free.fr>
 *     0xfcd1: Mr. Dave Konrad <konrad@xenia.it>
 *
 * WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING
 *
 *   This code is covered by the GNU GPL and you are free to make any
 *   changes you wish to it under the terms of the license. However the
 *   code has the potential to render your computer and/or someone else's
 *   unusable. Please proceed with care when modifying the code.
 *
 * Note: Unfortunately the laptop hardware can close the System Configuration
 *       Interface on it's own accord. It is therefore necessary for *all*
 *       programs using this driver to be aware that *any* SCI call can fail at
 *       *any* time. It is up to any program to be aware of this eventuality
 *       and take appropriate steps.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * The information used to write this driver has been obtained by reverse
 * engineering the software supplied by Toshiba for their portable computers in
 * strict accordance with the European Council Directive 92/250/EEC on the legal
 * protection of computer programs, and it's implementation into English Law by
 * the Copyright (Computer Programs) Regulations 1992 (S.I. 1992 No.3233).
 *
 */

#define TOSH_VERSION "1.11 26/9/2001"
#define TOSH_DEBUG 0

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/miscdevice.h>
#include <linux/ioport.h>
#include <asm/io.h>
#include <linux/uaccess.h>
#include <linux/init.h>
#include <linux/stat.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/mutex.h>
#include <linux/toshiba.h>

#define TOSH_MINOR_DEV 181

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jonathan Buzzard <jonathan@buzzard.org.uk>");
MODULE_DESCRIPTION("Toshiba laptop SMM driver");
MODULE_SUPPORTED_DEVICE("toshiba");

static DEFINE_MUTEX(tosh_mutex);
static int tosh_fn;
module_param_named(fn, tosh_fn, int, 0);
MODULE_PARM_DESC(fn, "User specified Fn key detection port");

static int tosh_id;
static int tosh_bios;
static int tosh_date;
static int tosh_sci;
static int tosh_fan;

static long tosh_ioctl(struct file *, unsigned int,
	unsigned long);


static const struct file_operations tosh_fops = {
	.owner		= THIS_MODULE,
	.unlocked_ioctl	= tosh_ioctl,
	.llseek		= noop_llseek,
};

static struct miscdevice tosh_device = {
	TOSH_MINOR_DEV,
	"toshiba",
	&tosh_fops
};

/*
 * Read the Fn key status
 */
#ifdef CONFIG_PROC_FS
static int tosh_fn_status(void)
{
        unsigned char scan;
	unsigned long flags;

	if (tosh_fn!=0) {
		scan = inb(tosh_fn);
	} else {
		local_irq_save(flags);
		outb(0x8e, 0xe4);
		scan = inb(0xe5);
		local_irq_restore(flags);
	}

        return (int) scan;
}
#endif


/*
 * For the Portage 610CT and the Tecra 700CS/700CDT emulate the HCI fan function
 */
static int tosh_emulate_fan(SMMRegisters *regs)
{
	unsigned long eax,ecx,flags;
	unsigned char al;

	eax = regs->eax & 0xff00;
	ecx = regs->ecx & 0xffff;

	/* Portage 610CT */

	if (tosh_id==0xfccb) {
		if (eax==0xfe00) {
			/* fan status */
			local_irq_save(flags);
			outb(0xbe, 0xe4);
			al = inb(0xe5);
			local_irq_restore(flags);
			regs->eax = 0x00;
			regs->ecx = (unsigned int) (al & 0x01);
		}
		if ((eax==0xff00) && (ecx==0x0000)) {
			/* fan off */
			local_irq_save(flags);
			outb(0xbe, 0xe4);
			al = inb(0xe5);
			outb(0xbe, 0xe4);
			outb (al | 0x01, 0xe5);
			local_irq_restore(flags);
			regs->eax = 0x00;
			regs->ecx = 0x00;
		}
		if ((eax==0xff00) && (ecx==0x0001)) {
			/* fan on */
			local_irq_save(flags);
			outb(0xbe, 0xe4);
			al = inb(0xe5);
			outb(0xbe, 0xe4);
			outb(al & 0xfe, 0xe5);
			local_irq_restore(flags);
			regs->eax = 0x00;
			regs->ecx = 0x01;
		}
	}

	/* Tecra 700CS/CDT */

	if (tosh_id==0xfccc) {
		if (eax==0xfe00) {
			/* fan status */
			local_irq_save(flags);
			outb(0xe0, 0xe4);
			al = inb(0xe5);
			local_irq_restore(flags);
			regs->eax = 0x00;
			regs->ecx = al & 0x01;
		}
		if ((eax==0xff00) && (ecx==0x0000)) {
			/* fan off */
			local_irq_save(flags);
			outb(0xe0, 0xe4);
			al = inb(0xe5);
			outw(0xe0 | ((al & 0xfe) << 8), 0xe4);
			local_irq_restore(flags);
			regs->eax = 0x00;
			regs->ecx = 0x00;
		}
		if ((eax==0xff00) && (ecx==0x0001)) {
			/* fan on */
			local_irq_save(flags);
			outb(0xe0, 0xe4);
			al = inb(0xe5);
			outw(0xe0 | ((al | 0x01) << 8), 0xe4);
			local_irq_restore(flags);
			regs->eax = 0x00;
			regs->ecx = 0x01;
		}
	}

	return 0;
}


/*
 * Put the laptop into System Management Mode
 */
int tosh_smm(SMMRegisters *regs)
{
	int eax;

	asm ("# load the values into the registers\n\t" \
		"pushl %%eax\n\t" \
		"movl 0(%%eax),%%edx\n\t" \
		"push %%edx\n\t" \
		"movl 4(%%eax),%%ebx\n\t" \
		"movl 8(%%eax),%%ecx\n\t" \
		"movl 12(%%eax),%%edx\n\t" \
		"movl 16(%%eax),%%esi\n\t" \
		"movl 20(%%eax),%%edi\n\t" \
		"popl %%eax\n\t" \
		"# call the System Management mode\n\t" \
		"inb $0xb2,%%al\n\t"
		"# fill out the memory with the values in the registers\n\t" \
		"xchgl %%eax,(%%esp)\n\t"
		"movl %%ebx,4(%%eax)\n\t" \
		"movl %%ecx,8(%%eax)\n\t" \
		"movl %%edx,12(%%eax)\n\t" \
		"movl %%esi,16(%%eax)\n\t" \
		"movl %%edi,20(%%eax)\n\t" \
		"popl %%edx\n\t" \
		"movl %%edx,0(%%eax)\n\t" \
		"# setup the return value to the carry flag\n\t" \
		"lahf\n\t" \
		"shrl $8,%%eax\n\t" \
		"andl $1,%%eax\n" \
		: "=a" (eax)
		: "a" (regs)
		: "%ebx", "%ecx", "%edx", "%esi", "%edi", "memory");

	return eax;
}
EXPORT_SYMBOL(tosh_smm);


static long tosh_ioctl(struct file *fp, unsigned int cmd, unsigned long arg)
{
	SMMRegisters regs;
	SMMRegisters __user *argp = (SMMRegisters __user *)arg;
	unsigned short ax,bx;
	int err;

	if (!argp)
		return -EINVAL;

	if (copy_from_user(&regs, argp, sizeof(SMMRegisters)))
		return -EFAULT;

	switch (cmd) {
		case TOSH_SMM:
			ax = regs.eax & 0xff00;
			bx = regs.ebx & 0xffff;
			/* block HCI calls to read/write memory & PCI devices */
			if (((ax==0xff00) || (ax==0xfe00)) && (bx>0x0069))
				return -EINVAL;

			/* do we need to emulate the fan ? */
			mutex_lock(&tosh_mutex);
			if (tosh_fan==1) {
				if (((ax==0xf300) || (ax==0xf400)) && (bx==0x0004)) {
					err = tosh_emulate_fan(&regs);
					mutex_unlock(&tosh_mutex);
					break;
				}
			}
			err = tosh_smm(&regs);
			mutex_unlock(&tosh_mutex);
			break;
		default:
			return -EINVAL;
	}

        if (copy_to_user(argp, &regs, sizeof(SMMRegisters)))
        	return -EFAULT;

	return (err==0) ? 0:-EINVAL;
}


/*
 * Print the information for /proc/toshiba
 */
#ifdef CONFIG_PROC_FS
static int proc_toshiba_show(struct seq_file *m, void *v)
{
	int key;

	key = tosh_fn_status();

	/* Arguments
	     0) Linux driver version (this will change if format changes)
	     1) Machine ID
	     2) SCI version
	     3) BIOS version (major, minor)
	     4) BIOS date (in SCI date format)
	     5) Fn Key status
	*/
	seq_printf(m, "1.1 0x%04x %d.%d %d.%d 0x%04x 0x%02x\n",
		tosh_id,
		(tosh_sci & 0xff00)>>8,
		tosh_sci & 0xff,
		(tosh_bios & 0xff00)>>8,
		tosh_bios & 0xff,
		tosh_date,
		key);
	return 0;
}
#endif


/*
 * Determine which port to use for the Fn key status
 */
static void tosh_set_fn_port(void)
{
	switch (tosh_id) {
		case 0xfc02: case 0xfc04: case 0xfc09: case 0xfc0a: case 0xfc10:
		case 0xfc11: case 0xfc13: case 0xfc15: case 0xfc1a: case 0xfc1b:
		case 0xfc5a:
			tosh_fn = 0x62;
			break;
		case 0xfc08: case 0xfc17: case 0xfc1d: case 0xfcd1: case 0xfce0:
		case 0xfce2:
			tosh_fn = 0x68;
			break;
		default:
			tosh_fn = 0x00;
			break;
	}

	return;
}


/*
 * Get the machine identification number of the current model
 */
static int tosh_get_machine_id(void __iomem *bios)
{
	int id;
	SMMRegisters regs;
	unsigned short bx,cx;
	unsigned long address;

	id = (0x100*(int) readb(bios+0xfffe))+((int) readb(bios+0xfffa));

	/* do we have a SCTTable machine identication number on our hands */

	if (id==0xfc2f) {

		/* start by getting a pointer into the BIOS */

		regs.eax = 0xc000;
		regs.ebx = 0x0000;
		regs.ecx = 0x0000;
		tosh_smm(&regs);
		bx = (unsigned short) (regs.ebx & 0xffff);

		/* At this point in the Toshiba routines under MS Windows
		   the bx register holds 0xe6f5. However my code is producing
		   a different value! For the time being I will just fudge the
		   value. This has been verified on a Satellite Pro 430CDT,
		   Tecra 750CDT, Tecra 780DVD and Satellite 310CDT. */
#if TOSH_DEBUG
		printk("toshiba: debugging ID ebx=0x%04x\n", regs.ebx);
#endif
		bx = 0xe6f5;

		/* now twiddle with our pointer a bit */

		address = bx;
		cx = readw(bios + address);
		address = 9+bx+cx;
		cx = readw(bios + address);
		address = 0xa+cx;
		cx = readw(bios + address);

		/* now construct our machine identification number */

		id = ((cx & 0xff)<<8)+((cx & 0xff00)>>8);
	}

	return id;
}


/*
 * Probe for the presence of a Toshiba laptop
 *
 *   returns and non-zero if unable to detect the presence of a Toshiba
 *   laptop, otherwise zero and determines the Machine ID, BIOS version and
 *   date, and SCI version.
 */
static int tosh_probe(void)
{
	int i,major,minor,day,year,month,flag;
	unsigned char signature[7] = { 0x54,0x4f,0x53,0x48,0x49,0x42,0x41 };
	SMMRegisters regs;
	void __iomem *bios = ioremap(0xf0000, 0x10000);

	if (!bios)
		return -ENOMEM;

	/* extra sanity check for the string "TOSHIBA" in the BIOS because
	   some machines that are not Toshiba's pass the next test */

	for (i=0;i<7;i++) {
		if (readb(bios+0xe010+i)!=signature[i]) {
			printk("toshiba: not a supported Toshiba laptop\n");
			iounmap(bios);
			return -ENODEV;
		}
	}

	/* call the Toshiba SCI support check routine */

	regs.eax = 0xf0f0;
	regs.ebx = 0x0000;
	regs.ecx = 0x0000;
	flag = tosh_smm(&regs);

	/* if this is not a Toshiba laptop carry flag is set and ah=0x86 */

	if ((flag==1) || ((regs.eax & 0xff00)==0x8600)) {
		printk("toshiba: not a supported Toshiba laptop\n");
		iounmap(bios);
		return -ENODEV;
	}

	/* if we get this far then we are running on a Toshiba (probably)! */

	tosh_sci = regs.edx & 0xffff;

	/* next get the machine ID of the current laptop */

	tosh_id = tosh_get_machine_id(bios);

	/* get the BIOS version */

	major = readb(bios+0xe009)-'0';
	minor = ((readb(bios+0xe00b)-'0')*10)+(readb(bios+0xe00c)-'0');
	tosh_bios = (major*0x100)+minor;

	/* get the BIOS date */

	day = ((readb(bios+0xfff5)-'0')*10)+(readb(bios+0xfff6)-'0');
	month = ((readb(bios+0xfff8)-'0')*10)+(readb(bios+0xfff9)-'0');
	year = ((readb(bios+0xfffb)-'0')*10)+(readb(bios+0xfffc)-'0');
	tosh_date = (((year-90) & 0x1f)<<10) | ((month & 0xf)<<6)
		| ((day & 0x1f)<<1);


	/* in theory we should check the ports we are going to use for the
	   fn key detection (and the fan on the Portage 610/Tecra700), and
	   then request them to stop other drivers using them. However as
	   the keyboard driver grabs 0x60-0x6f and the pic driver grabs
	   0xa0-0xbf we can't. We just have to live dangerously and use the
	   ports anyway, oh boy! */

	/* do we need to emulate the fan? */

	if ((tosh_id==0xfccb) || (tosh_id==0xfccc))
		tosh_fan = 1;

	iounmap(bios);

	return 0;
}

static int __init toshiba_init(void)
{
	int retval;
	/* are we running on a Toshiba laptop */

	if (tosh_probe())
		return -ENODEV;

	printk(KERN_INFO "Toshiba System Management Mode driver v" TOSH_VERSION "\n");

	/* set the port to use for Fn status if not specified as a parameter */
	if (tosh_fn==0x00)
		tosh_set_fn_port();

	/* register the device file */
	retval = misc_register(&tosh_device);
	if (retval < 0)
		return retval;

#ifdef CONFIG_PROC_FS
	{
		struct proc_dir_entry *pde;

		pde = proc_create_single("toshiba", 0, NULL, proc_toshiba_show);
		if (!pde) {
			misc_deregister(&tosh_device);
			return -ENOMEM;
		}
	}
#endif

	return 0;
}

static void __exit toshiba_exit(void)
{
	remove_proc_entry("toshiba", NULL);
	misc_deregister(&tosh_device);
}

module_init(toshiba_init);
module_exit(toshiba_exit);

