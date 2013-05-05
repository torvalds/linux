/* 
 *    interfaces to Chassis Codes via PDC (firmware)
 *
 *    Copyright (C) 2002 Laurent Canet <canetl@esiee.fr>
 *    Copyright (C) 2002-2006 Thibaut VARENE <varenet@parisc-linux.org>
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License, version 2, as
 *    published by the Free Software Foundation.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 *    TODO: poll chassis warns, trigger (configurable) machine shutdown when
 *    		needed.
 *    	    Find out how to get Chassis warnings out of PAT boxes?
 */

#undef PDC_CHASSIS_DEBUG
#ifdef PDC_CHASSIS_DEBUG
#define DPRINTK(fmt, args...)	printk(fmt, ## args)
#else
#define DPRINTK(fmt, args...)
#endif

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/reboot.h>
#include <linux/notifier.h>
#include <linux/cache.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#include <asm/pdc_chassis.h>
#include <asm/processor.h>
#include <asm/pdc.h>
#include <asm/pdcpat.h>

#define PDC_CHASSIS_VER	"0.05"

#ifdef CONFIG_PDC_CHASSIS
static unsigned int pdc_chassis_enabled __read_mostly = 1;


/**
 * pdc_chassis_setup() - Enable/disable pdc_chassis code at boot time.
 * @str configuration param: 0 to disable chassis log
 * @return 1
 */
 
static int __init pdc_chassis_setup(char *str)
{
	/*panic_timeout = simple_strtoul(str, NULL, 0);*/
	get_option(&str, &pdc_chassis_enabled);
	return 1;
}
__setup("pdcchassis=", pdc_chassis_setup);


/** 
 * pdc_chassis_checkold() - Checks for old PDC_CHASSIS compatibility
 * @pdc_chassis_old: 1 if old pdc chassis style
 * 
 * Currently, only E class and A180 are known to work with this.
 * Inspired by Christoph Plattner
 */
#if 0
static void __init pdc_chassis_checkold(void)
{
	switch(CPU_HVERSION) {
		case 0x480:		/* E25 */
		case 0x481:		/* E35 */
		case 0x482:		/* E45 */
		case 0x483:		/* E55 */
		case 0x516:		/* A180 */
			break;

		default:
			break;
	}
	DPRINTK(KERN_DEBUG "%s: pdc_chassis_checkold(); pdc_chassis_old = %d\n", __FILE__, pdc_chassis_old);
}
#endif

/**
 * pdc_chassis_panic_event() - Called by the panic handler.
 *
 * As soon as a panic occurs, we should inform the PDC.
 */

static int pdc_chassis_panic_event(struct notifier_block *this,
		        unsigned long event, void *ptr)
{
	pdc_chassis_send_status(PDC_CHASSIS_DIRECT_PANIC);
		return NOTIFY_DONE;
}   


static struct notifier_block pdc_chassis_panic_block = {
	.notifier_call = pdc_chassis_panic_event,
	.priority = INT_MAX,
};


/**
 * parisc_reboot_event() - Called by the reboot handler.
 *
 * As soon as a reboot occurs, we should inform the PDC.
 */

static int pdc_chassis_reboot_event(struct notifier_block *this,
		        unsigned long event, void *ptr)
{
	pdc_chassis_send_status(PDC_CHASSIS_DIRECT_SHUTDOWN);
		return NOTIFY_DONE;
}   


static struct notifier_block pdc_chassis_reboot_block = {
	.notifier_call = pdc_chassis_reboot_event,
	.priority = INT_MAX,
};
#endif /* CONFIG_PDC_CHASSIS */


/**
 * parisc_pdc_chassis_init() - Called at boot time.
 */

void __init parisc_pdc_chassis_init(void)
{
#ifdef CONFIG_PDC_CHASSIS
	if (likely(pdc_chassis_enabled)) {
		DPRINTK(KERN_DEBUG "%s: parisc_pdc_chassis_init()\n", __FILE__);

		/* Let see if we have something to handle... */
		printk(KERN_INFO "Enabling %s chassis codes support v%s\n",
				is_pdc_pat() ? "PDC_PAT" : "regular",
				PDC_CHASSIS_VER);

		/* initialize panic notifier chain */
		atomic_notifier_chain_register(&panic_notifier_list,
				&pdc_chassis_panic_block);

		/* initialize reboot notifier chain */
		register_reboot_notifier(&pdc_chassis_reboot_block);
	}
#endif /* CONFIG_PDC_CHASSIS */
}


/** 
 * pdc_chassis_send_status() - Sends a predefined message to the chassis,
 * and changes the front panel LEDs according to the new system state
 * @retval: PDC call return value.
 *
 * Only machines with 64 bits PDC PAT and those reported in
 * pdc_chassis_checkold() are supported atm.
 * 
 * returns 0 if no error, -1 if no supported PDC is present or invalid message,
 * else returns the appropriate PDC error code.
 * 
 * For a list of predefined messages, see asm-parisc/pdc_chassis.h
 */

int pdc_chassis_send_status(int message)
{
	/* Maybe we should do that in an other way ? */
	int retval = 0;
#ifdef CONFIG_PDC_CHASSIS
	if (likely(pdc_chassis_enabled)) {

		DPRINTK(KERN_DEBUG "%s: pdc_chassis_send_status(%d)\n", __FILE__, message);

#ifdef CONFIG_64BIT
		if (is_pdc_pat()) {
			switch(message) {
				case PDC_CHASSIS_DIRECT_BSTART:
					retval = pdc_pat_chassis_send_log(PDC_CHASSIS_PMSG_BSTART, PDC_CHASSIS_LSTATE_RUN_NORMAL);
					break;

				case PDC_CHASSIS_DIRECT_BCOMPLETE:
					retval = pdc_pat_chassis_send_log(PDC_CHASSIS_PMSG_BCOMPLETE, PDC_CHASSIS_LSTATE_RUN_NORMAL);
					break;

				case PDC_CHASSIS_DIRECT_SHUTDOWN:
					retval = pdc_pat_chassis_send_log(PDC_CHASSIS_PMSG_SHUTDOWN, PDC_CHASSIS_LSTATE_NONOS);
					break;

				case PDC_CHASSIS_DIRECT_PANIC:
					retval = pdc_pat_chassis_send_log(PDC_CHASSIS_PMSG_PANIC, PDC_CHASSIS_LSTATE_RUN_CRASHREC);
					break;

				case PDC_CHASSIS_DIRECT_LPMC:
					retval = pdc_pat_chassis_send_log(PDC_CHASSIS_PMSG_LPMC, PDC_CHASSIS_LSTATE_RUN_SYSINT);
					break;

				case PDC_CHASSIS_DIRECT_HPMC:
					retval = pdc_pat_chassis_send_log(PDC_CHASSIS_PMSG_HPMC, PDC_CHASSIS_LSTATE_RUN_NCRIT);
					break;

				default:
					retval = -1;
			}
		} else retval = -1;
#else
		if (1) {
			switch (message) {
				case PDC_CHASSIS_DIRECT_BSTART:
					retval = pdc_chassis_disp(PDC_CHASSIS_DISP_DATA(OSTAT_INIT));
					break;

				case PDC_CHASSIS_DIRECT_BCOMPLETE:
					retval = pdc_chassis_disp(PDC_CHASSIS_DISP_DATA(OSTAT_RUN));
					break;

				case PDC_CHASSIS_DIRECT_SHUTDOWN:
					retval = pdc_chassis_disp(PDC_CHASSIS_DISP_DATA(OSTAT_SHUT));
					break;

				case PDC_CHASSIS_DIRECT_HPMC:
				case PDC_CHASSIS_DIRECT_PANIC:
					retval = pdc_chassis_disp(PDC_CHASSIS_DISP_DATA(OSTAT_FLT));
					break;

				case PDC_CHASSIS_DIRECT_LPMC:
					retval = pdc_chassis_disp(PDC_CHASSIS_DISP_DATA(OSTAT_WARN));
					break;

				default:
					retval = -1;
			}
		} else retval = -1;
#endif /* CONFIG_64BIT */
	}	/* if (pdc_chassis_enabled) */
#endif /* CONFIG_PDC_CHASSIS */
	return retval;
}

#ifdef CONFIG_PDC_CHASSIS_WARN
#ifdef CONFIG_PROC_FS
static int pdc_chassis_warn_show(struct seq_file *m, void *v)
{
	unsigned long warn;
	u32 warnreg;

	if (pdc_chassis_warn(&warn) != PDC_OK)
		return -EIO;

	warnreg = (warn & 0xFFFFFFFF);

	if ((warnreg >> 24) & 0xFF)
		seq_printf(m, "Chassis component failure! (eg fan or PSU): 0x%.2x\n",
			   (warnreg >> 24) & 0xFF);

	seq_printf(m, "Battery: %s\n", (warnreg & 0x04) ? "Low!" : "OK");
	seq_printf(m, "Temp low: %s\n", (warnreg & 0x02) ? "Exceeded!" : "OK");
	seq_printf(m, "Temp mid: %s\n", (warnreg & 0x01) ? "Exceeded!" : "OK");
	return 0;
}

static int pdc_chassis_warn_open(struct inode *inode, struct file *file)
{
	return single_open(file, pdc_chassis_warn_show, NULL);
}

static const struct file_operations pdc_chassis_warn_fops = {
	.open		= pdc_chassis_warn_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

static int __init pdc_chassis_create_procfs(void)
{
	unsigned long test;
	int ret;

	ret = pdc_chassis_warn(&test);
	if ((ret == PDC_BAD_PROC) || (ret == PDC_BAD_OPTION)) {
		/* seems that some boxes (eg L1000) do not implement this */
		printk(KERN_INFO "Chassis warnings not supported.\n");
		return 0;
	}

	printk(KERN_INFO "Enabling PDC chassis warnings support v%s\n",
			PDC_CHASSIS_VER);
	proc_create("chassis", 0400, NULL, &pdc_chassis_warn_fops);
	return 0;
}

__initcall(pdc_chassis_create_procfs);

#endif /* CONFIG_PROC_FS */
#endif /* CONFIG_PDC_CHASSIS_WARN */
