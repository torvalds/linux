/*
 *    Chassis LCD/LED driver for HP-PARISC workstations
 *
 *      (c) Copyright 2000 Red Hat Software
 *      (c) Copyright 2000 Helge Deller <hdeller@redhat.com>
 *      (c) Copyright 2001-2009 Helge Deller <deller@gmx.de>
 *      (c) Copyright 2001 Randolph Chung <tausq@debian.org>
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *
 * TODO:
 *	- speed-up calculations with inlined assembler
 *	- interface to write to second row of LCD from /proc (if technically possible)
 *
 * Changes:
 *      - Audit copy_from_user in led_proc_write.
 *                                Daniele Bellucci <bellucda@tiscali.it>
 *	- Switch from using a tasklet to a work queue, so the led_LCD_driver
 *	  	can sleep.
 *	  			  David Pye <dmp@davidmpye.dyndns.org>
 */

#include <linux/module.h>
#include <linux/stddef.h>	/* for offsetof() */
#include <linux/init.h>
#include <linux/types.h>
#include <linux/ioport.h>
#include <linux/utsname.h>
#include <linux/capability.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <linux/inetdevice.h>
#include <linux/in.h>
#include <linux/interrupt.h>
#include <linux/kernel_stat.h>
#include <linux/reboot.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/ctype.h>
#include <linux/blkdev.h>
#include <linux/workqueue.h>
#include <linux/rcupdate.h>
#include <asm/io.h>
#include <asm/processor.h>
#include <asm/hardware.h>
#include <asm/param.h>		/* HZ */
#include <asm/led.h>
#include <asm/pdc.h>
#include <asm/uaccess.h>

/* The control of the LEDs and LCDs on PARISC-machines have to be done 
   completely in software. The necessary calculations are done in a work queue
   task which is scheduled regularly, and since the calculations may consume a 
   relatively large amount of CPU time, some of the calculations can be 
   turned off with the following variables (controlled via procfs) */

static int led_type __read_mostly = -1;
static unsigned char lastleds;	/* LED state from most recent update */
static unsigned int led_heartbeat __read_mostly = 1;
static unsigned int led_diskio    __read_mostly = 1;
static unsigned int led_lanrxtx   __read_mostly = 1;
static char lcd_text[32]          __read_mostly;
static char lcd_text_default[32]  __read_mostly;


static struct workqueue_struct *led_wq;
static void led_work_func(struct work_struct *);
static DECLARE_DELAYED_WORK(led_task, led_work_func);

#if 0
#define DPRINTK(x)	printk x
#else
#define DPRINTK(x)
#endif

struct lcd_block {
	unsigned char command;	/* stores the command byte      */
	unsigned char on;	/* value for turning LED on     */
	unsigned char off;	/* value for turning LED off    */
};

/* Structure returned by PDC_RETURN_CHASSIS_INFO */
/* NOTE: we use unsigned long:16 two times, since the following member 
   lcd_cmd_reg_addr needs to be 64bit aligned on 64bit PA2.0-machines */
struct pdc_chassis_lcd_info_ret_block {
	unsigned long model:16;		/* DISPLAY_MODEL_XXXX */
	unsigned long lcd_width:16;	/* width of the LCD in chars (DISPLAY_MODEL_LCD only) */
	unsigned long lcd_cmd_reg_addr;	/* ptr to LCD cmd-register & data ptr for LED */
	unsigned long lcd_data_reg_addr; /* ptr to LCD data-register (LCD only) */
	unsigned int min_cmd_delay;	/* delay in uS after cmd-write (LCD only) */
	unsigned char reset_cmd1;	/* command #1 for writing LCD string (LCD only) */
	unsigned char reset_cmd2;	/* command #2 for writing LCD string (LCD only) */
	unsigned char act_enable;	/* 0 = no activity (LCD only) */
	struct lcd_block heartbeat;
	struct lcd_block disk_io;
	struct lcd_block lan_rcv;
	struct lcd_block lan_tx;
	char _pad;
};


/* LCD_CMD and LCD_DATA for KittyHawk machines */
#define KITTYHAWK_LCD_CMD  F_EXTEND(0xf0190000UL) /* 64bit-ready */
#define KITTYHAWK_LCD_DATA (KITTYHAWK_LCD_CMD+1)

/* lcd_info is pre-initialized to the values needed to program KittyHawk LCD's 
 * HP seems to have used Sharp/Hitachi HD44780 LCDs most of the time. */
static struct pdc_chassis_lcd_info_ret_block
lcd_info __attribute__((aligned(8))) __read_mostly =
{
	.model =		DISPLAY_MODEL_LCD,
	.lcd_width =		16,
	.lcd_cmd_reg_addr =	KITTYHAWK_LCD_CMD,
	.lcd_data_reg_addr =	KITTYHAWK_LCD_DATA,
	.min_cmd_delay =	40,
	.reset_cmd1 =		0x80,
	.reset_cmd2 =		0xc0,
};


/* direct access to some of the lcd_info variables */
#define LCD_CMD_REG	lcd_info.lcd_cmd_reg_addr	 
#define LCD_DATA_REG	lcd_info.lcd_data_reg_addr	 
#define LED_DATA_REG	lcd_info.lcd_cmd_reg_addr	/* LASI & ASP only */

#define LED_HASLCD 1
#define LED_NOLCD  0

/* The workqueue must be created at init-time */
static int start_task(void) 
{	
	/* Display the default text now */
	if (led_type == LED_HASLCD) lcd_print( lcd_text_default );

	/* Create the work queue and queue the LED task */
	led_wq = create_singlethread_workqueue("led_wq");	
	queue_delayed_work(led_wq, &led_task, 0);

	return 0;
}

device_initcall(start_task);

/* ptr to LCD/LED-specific function */
static void (*led_func_ptr) (unsigned char) __read_mostly;

#ifdef CONFIG_PROC_FS
static int led_proc_show(struct seq_file *m, void *v)
{
	switch ((long)m->private)
	{
	case LED_NOLCD:
		seq_printf(m, "Heartbeat: %d\n", led_heartbeat);
		seq_printf(m, "Disk IO: %d\n", led_diskio);
		seq_printf(m, "LAN Rx/Tx: %d\n", led_lanrxtx);
		break;
	case LED_HASLCD:
		seq_printf(m, "%s\n", lcd_text);
		break;
	default:
		return 0;
	}
	return 0;
}

static int led_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, led_proc_show, PDE(inode)->data);
}


static ssize_t led_proc_write(struct file *file, const char *buf,
	size_t count, loff_t *pos)
{
	void *data = PDE(file->f_path.dentry->d_inode)->data;
	char *cur, lbuf[count + 1];
	int d;

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;

	memset(lbuf, 0, count + 1);

	if (copy_from_user(lbuf, buf, count))
		return -EFAULT;

	cur = lbuf;

	switch ((long)data)
	{
	case LED_NOLCD:
		d = *cur++ - '0';
		if (d != 0 && d != 1) goto parse_error;
		led_heartbeat = d;

		if (*cur++ != ' ') goto parse_error;

		d = *cur++ - '0';
		if (d != 0 && d != 1) goto parse_error;
		led_diskio = d;

		if (*cur++ != ' ') goto parse_error;

		d = *cur++ - '0';
		if (d != 0 && d != 1) goto parse_error;
		led_lanrxtx = d;

		break;
	case LED_HASLCD:
		if (*cur && cur[strlen(cur)-1] == '\n')
			cur[strlen(cur)-1] = 0;
		if (*cur == 0) 
			cur = lcd_text_default;
		lcd_print(cur);
		break;
	default:
		return 0;
	}
	
	return count;

parse_error:
	if ((long)data == LED_NOLCD)
		printk(KERN_CRIT "Parse error: expect \"n n n\" (n == 0 or 1) for heartbeat,\ndisk io and lan tx/rx indicators\n");
	return -EINVAL;
}

static const struct file_operations led_proc_fops = {
	.owner		= THIS_MODULE,
	.open		= led_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
	.write		= led_proc_write,
};

static int __init led_create_procfs(void)
{
	struct proc_dir_entry *proc_pdc_root = NULL;
	struct proc_dir_entry *ent;

	if (led_type == -1) return -1;

	proc_pdc_root = proc_mkdir("pdc", 0);
	if (!proc_pdc_root) return -1;
	ent = proc_create_data("led", S_IRUGO|S_IWUSR, proc_pdc_root,
				&led_proc_fops, (void *)LED_NOLCD); /* LED */
	if (!ent) return -1;

	if (led_type == LED_HASLCD)
	{
		ent = proc_create_data("lcd", S_IRUGO|S_IWUSR, proc_pdc_root,
					&led_proc_fops, (void *)LED_HASLCD); /* LCD */
		if (!ent) return -1;
	}

	return 0;
}
#endif

/*
   ** 
   ** led_ASP_driver()
   ** 
 */
#define	LED_DATA	0x01	/* data to shift (0:on 1:off) */
#define	LED_STROBE	0x02	/* strobe to clock data */
static void led_ASP_driver(unsigned char leds)
{
	int i;

	leds = ~leds;
	for (i = 0; i < 8; i++) {
		unsigned char value;
		value = (leds & 0x80) >> 7;
		gsc_writeb( value,		 LED_DATA_REG );
		gsc_writeb( value | LED_STROBE,	 LED_DATA_REG );
		leds <<= 1;
	}
}


/*
   ** 
   ** led_LASI_driver()
   ** 
 */
static void led_LASI_driver(unsigned char leds)
{
	leds = ~leds;
	gsc_writeb( leds, LED_DATA_REG );
}


/*
   ** 
   ** led_LCD_driver()
   **   
 */
static void led_LCD_driver(unsigned char leds)
{
	static int i;
	static unsigned char mask[4] = { LED_HEARTBEAT, LED_DISK_IO,
		LED_LAN_RCV, LED_LAN_TX };
	
	static struct lcd_block * blockp[4] = {
		&lcd_info.heartbeat,
		&lcd_info.disk_io,
		&lcd_info.lan_rcv,
		&lcd_info.lan_tx
	};

	/* Convert min_cmd_delay to milliseconds */
	unsigned int msec_cmd_delay = 1 + (lcd_info.min_cmd_delay / 1000);
	
	for (i=0; i<4; ++i) 
	{
		if ((leds & mask[i]) != (lastleds & mask[i])) 
		{
			gsc_writeb( blockp[i]->command, LCD_CMD_REG );
			msleep(msec_cmd_delay);
			
			gsc_writeb( leds & mask[i] ? blockp[i]->on : 
					blockp[i]->off, LCD_DATA_REG );
			msleep(msec_cmd_delay);
		}
	}
}


/*
   ** 
   ** led_get_net_activity()
   ** 
   ** calculate if there was TX- or RX-throughput on the network interfaces
   ** (analog to dev_get_info() from net/core/dev.c)
   **   
 */
static __inline__ int led_get_net_activity(void)
{ 
#ifndef CONFIG_NET
	return 0;
#else
	static unsigned long rx_total_last, tx_total_last;
	unsigned long rx_total, tx_total;
	struct net_device *dev;
	int retval;

	rx_total = tx_total = 0;
	
	/* we are running as a workqueue task, so we can use an RCU lookup */
	rcu_read_lock();
	for_each_netdev_rcu(&init_net, dev) {
	    const struct net_device_stats *stats;
	    struct in_device *in_dev = __in_dev_get_rcu(dev);
	    if (!in_dev || !in_dev->ifa_list)
		continue;
	    if (ipv4_is_loopback(in_dev->ifa_list->ifa_local))
		continue;
	    stats = dev_get_stats(dev);
	    rx_total += stats->rx_packets;
	    tx_total += stats->tx_packets;
	}
	rcu_read_unlock();

	retval = 0;

	if (rx_total != rx_total_last) {
		rx_total_last = rx_total;
		retval |= LED_LAN_RCV;
	}

	if (tx_total != tx_total_last) {
		tx_total_last = tx_total;
		retval |= LED_LAN_TX;
	}

	return retval;
#endif
}


/*
   ** 
   ** led_get_diskio_activity()
   ** 
   ** calculate if there was disk-io in the system
   **   
 */
static __inline__ int led_get_diskio_activity(void)
{	
	static unsigned long last_pgpgin, last_pgpgout;
	unsigned long events[NR_VM_EVENT_ITEMS];
	int changed;

	all_vm_events(events);

	/* Just use a very simple calculation here. Do not care about overflow,
	   since we only want to know if there was activity or not. */
	changed = (events[PGPGIN] != last_pgpgin) ||
		  (events[PGPGOUT] != last_pgpgout);
	last_pgpgin  = events[PGPGIN];
	last_pgpgout = events[PGPGOUT];

	return (changed ? LED_DISK_IO : 0);
}



/*
   ** led_work_func()
   ** 
   ** manages when and which chassis LCD/LED gets updated

    TODO:
    - display load average (older machines like 715/64 have 4 "free" LED's for that)
    - optimizations
 */

#define HEARTBEAT_LEN (HZ*10/100)
#define HEARTBEAT_2ND_RANGE_START (HZ*28/100)
#define HEARTBEAT_2ND_RANGE_END   (HEARTBEAT_2ND_RANGE_START + HEARTBEAT_LEN)

#define LED_UPDATE_INTERVAL (1 + (HZ*19/1000))

static void led_work_func (struct work_struct *unused)
{
	static unsigned long last_jiffies;
	static unsigned long count_HZ; /* counter in range 0..HZ */
	unsigned char currentleds = 0; /* stores current value of the LEDs */

	/* exit if not initialized */
	if (!led_func_ptr)
	    return;

	/* increment the heartbeat timekeeper */
	count_HZ += jiffies - last_jiffies;
	last_jiffies = jiffies;
	if (count_HZ >= HZ)
	    count_HZ = 0;

	if (likely(led_heartbeat))
	{
		/* flash heartbeat-LED like a real heart
		 * (2 x short then a long delay)
		 */
		if (count_HZ < HEARTBEAT_LEN || 
				(count_HZ >= HEARTBEAT_2ND_RANGE_START &&
				count_HZ < HEARTBEAT_2ND_RANGE_END)) 
			currentleds |= LED_HEARTBEAT;
	}

	if (likely(led_lanrxtx))  currentleds |= led_get_net_activity();
	if (likely(led_diskio))   currentleds |= led_get_diskio_activity();

	/* blink LEDs if we got an Oops (HPMC) */
	if (unlikely(oops_in_progress)) {
		if (boot_cpu_data.cpu_type >= pcxl2) {
			/* newer machines don't have loadavg. LEDs, so we
			 * let all LEDs blink twice per second instead */
			currentleds = (count_HZ <= (HZ/2)) ? 0 : 0xff;
		} else {
			/* old machines: blink loadavg. LEDs twice per second */
			if (count_HZ <= (HZ/2))
				currentleds &= ~(LED4|LED5|LED6|LED7);
			else
				currentleds |= (LED4|LED5|LED6|LED7);
		}
	}

	if (currentleds != lastleds)
	{
		led_func_ptr(currentleds);	/* Update the LCD/LEDs */
		lastleds = currentleds;
	}

	queue_delayed_work(led_wq, &led_task, LED_UPDATE_INTERVAL);
}

/*
   ** led_halt()
   ** 
   ** called by the reboot notifier chain at shutdown and stops all
   ** LED/LCD activities.
   ** 
 */

static int led_halt(struct notifier_block *, unsigned long, void *);

static struct notifier_block led_notifier = {
	.notifier_call = led_halt,
};
static int notifier_disabled = 0;

static int led_halt(struct notifier_block *nb, unsigned long event, void *buf) 
{
	char *txt;

	if (notifier_disabled)
		return NOTIFY_OK;

	notifier_disabled = 1;
	switch (event) {
	case SYS_RESTART:	txt = "SYSTEM RESTART";
				break;
	case SYS_HALT:		txt = "SYSTEM HALT";
				break;
	case SYS_POWER_OFF:	txt = "SYSTEM POWER OFF";
				break;
	default:		return NOTIFY_DONE;
	}
	
	/* Cancel the work item and delete the queue */
	if (led_wq) {
		cancel_delayed_work_sync(&led_task);
		destroy_workqueue(led_wq);
		led_wq = NULL;
	}
 
	if (lcd_info.model == DISPLAY_MODEL_LCD)
		lcd_print(txt);
	else
		if (led_func_ptr)
			led_func_ptr(0xff); /* turn all LEDs ON */
	
	return NOTIFY_OK;
}

/*
   ** register_led_driver()
   ** 
   ** registers an external LED or LCD for usage by this driver.
   ** currently only LCD-, LASI- and ASP-style LCD/LED's are supported.
   ** 
 */

int __init register_led_driver(int model, unsigned long cmd_reg, unsigned long data_reg)
{
	static int initialized;
	
	if (initialized || !data_reg)
		return 1;
	
	lcd_info.model = model;		/* store the values */
	LCD_CMD_REG = (cmd_reg == LED_CMD_REG_NONE) ? 0 : cmd_reg;

	switch (lcd_info.model) {
	case DISPLAY_MODEL_LCD:
		LCD_DATA_REG = data_reg;
		printk(KERN_INFO "LCD display at %lx,%lx registered\n", 
			LCD_CMD_REG , LCD_DATA_REG);
		led_func_ptr = led_LCD_driver;
		led_type = LED_HASLCD;
		break;

	case DISPLAY_MODEL_LASI:
		LED_DATA_REG = data_reg;
		led_func_ptr = led_LASI_driver;
		printk(KERN_INFO "LED display at %lx registered\n", LED_DATA_REG);
		led_type = LED_NOLCD;
		break;

	case DISPLAY_MODEL_OLD_ASP:
		LED_DATA_REG = data_reg;
		led_func_ptr = led_ASP_driver;
		printk(KERN_INFO "LED (ASP-style) display at %lx registered\n", 
		    LED_DATA_REG);
		led_type = LED_NOLCD;
		break;

	default:
		printk(KERN_ERR "%s: Wrong LCD/LED model %d !\n",
		       __func__, lcd_info.model);
		return 1;
	}
	
	/* mark the LCD/LED driver now as initialized and 
	 * register to the reboot notifier chain */
	initialized++;
	register_reboot_notifier(&led_notifier);

	/* Ensure the work is queued */
	if (led_wq) {
		queue_delayed_work(led_wq, &led_task, 0);
	}

	return 0;
}

/*
   ** register_led_regions()
   ** 
   ** register_led_regions() registers the LCD/LED regions for /procfs.
   ** At bootup - where the initialisation of the LCD/LED normally happens - 
   ** not all internal structures of request_region() are properly set up,
   ** so that we delay the led-registration until after busdevices_init() 
   ** has been executed.
   **
 */

void __init register_led_regions(void)
{
	switch (lcd_info.model) {
	case DISPLAY_MODEL_LCD:
		request_mem_region((unsigned long)LCD_CMD_REG,  1, "lcd_cmd");
		request_mem_region((unsigned long)LCD_DATA_REG, 1, "lcd_data");
		break;
	case DISPLAY_MODEL_LASI:
	case DISPLAY_MODEL_OLD_ASP:
		request_mem_region((unsigned long)LED_DATA_REG, 1, "led_data");
		break;
	}
}


/*
   ** 
   ** lcd_print()
   ** 
   ** Displays the given string on the LCD-Display of newer machines.
   ** lcd_print() disables/enables the timer-based led work queue to
   ** avoid a race condition while writing the CMD/DATA register pair.
   **
 */
int lcd_print( const char *str )
{
	int i;

	if (!led_func_ptr || lcd_info.model != DISPLAY_MODEL_LCD)
	    return 0;
	
	/* temporarily disable the led work task */
	if (led_wq)
		cancel_delayed_work_sync(&led_task);

	/* copy display string to buffer for procfs */
	strlcpy(lcd_text, str, sizeof(lcd_text));

	/* Set LCD Cursor to 1st character */
	gsc_writeb(lcd_info.reset_cmd1, LCD_CMD_REG);
	udelay(lcd_info.min_cmd_delay);

	/* Print the string */
	for (i=0; i < lcd_info.lcd_width; i++) {
	    if (str && *str)
		gsc_writeb(*str++, LCD_DATA_REG);
	    else
		gsc_writeb(' ', LCD_DATA_REG);
	    udelay(lcd_info.min_cmd_delay);
	}
	
	/* re-queue the work */
	if (led_wq) {
		queue_delayed_work(led_wq, &led_task, 0);
	}

	return lcd_info.lcd_width;
}

/*
   ** led_init()
   ** 
   ** led_init() is called very early in the bootup-process from setup.c 
   ** and asks the PDC for an usable chassis LCD or LED.
   ** If the PDC doesn't return any info, then the LED
   ** is detected by lasi.c or asp.c and registered with the
   ** above functions lasi_led_init() or asp_led_init().
   ** KittyHawk machines have often a buggy PDC, so that
   ** we explicitly check for those machines here.
 */

int __init led_init(void)
{
	struct pdc_chassis_info chassis_info;
	int ret;

	snprintf(lcd_text_default, sizeof(lcd_text_default),
		"Linux %s", init_utsname()->release);

	/* Work around the buggy PDC of KittyHawk-machines */
	switch (CPU_HVERSION) {
	case 0x580:		/* KittyHawk DC2-100 (K100) */
	case 0x581:		/* KittyHawk DC3-120 (K210) */
	case 0x582:		/* KittyHawk DC3 100 (K400) */
	case 0x583:		/* KittyHawk DC3 120 (K410) */
	case 0x58B:		/* KittyHawk DC2 100 (K200) */
		printk(KERN_INFO "%s: KittyHawk-Machine (hversion 0x%x) found, "
				"LED detection skipped.\n", __FILE__, CPU_HVERSION);
		goto found;	/* use the preinitialized values of lcd_info */
	}

	/* initialize the struct, so that we can check for valid return values */
	lcd_info.model = DISPLAY_MODEL_NONE;
	chassis_info.actcnt = chassis_info.maxcnt = 0;

	ret = pdc_chassis_info(&chassis_info, &lcd_info, sizeof(lcd_info));
	if (ret == PDC_OK) {
		DPRINTK((KERN_INFO "%s: chassis info: model=%d (%s), "
			 "lcd_width=%d, cmd_delay=%u,\n"
			 "%s: sizecnt=%d, actcnt=%ld, maxcnt=%ld\n",
		         __FILE__, lcd_info.model,
			 (lcd_info.model==DISPLAY_MODEL_LCD) ? "LCD" :
			  (lcd_info.model==DISPLAY_MODEL_LASI) ? "LED" : "unknown",
			 lcd_info.lcd_width, lcd_info.min_cmd_delay,
			 __FILE__, sizeof(lcd_info), 
			 chassis_info.actcnt, chassis_info.maxcnt));
		DPRINTK((KERN_INFO "%s: cmd=%p, data=%p, reset1=%x, reset2=%x, act_enable=%d\n",
			__FILE__, lcd_info.lcd_cmd_reg_addr, 
			lcd_info.lcd_data_reg_addr, lcd_info.reset_cmd1,  
			lcd_info.reset_cmd2, lcd_info.act_enable ));
	
		/* check the results. Some machines have a buggy PDC */
		if (chassis_info.actcnt <= 0 || chassis_info.actcnt != chassis_info.maxcnt)
			goto not_found;

		switch (lcd_info.model) {
		case DISPLAY_MODEL_LCD:		/* LCD display */
			if (chassis_info.actcnt < 
				offsetof(struct pdc_chassis_lcd_info_ret_block, _pad)-1)
				goto not_found;
			if (!lcd_info.act_enable) {
				DPRINTK((KERN_INFO "PDC prohibited usage of the LCD.\n"));
				goto not_found;
			}
			break;

		case DISPLAY_MODEL_NONE:	/* no LED or LCD available */
			printk(KERN_INFO "PDC reported no LCD or LED.\n");
			goto not_found;

		case DISPLAY_MODEL_LASI:	/* Lasi style 8 bit LED display */
			if (chassis_info.actcnt != 8 && chassis_info.actcnt != 32)
				goto not_found;
			break;

		default:
			printk(KERN_WARNING "PDC reported unknown LCD/LED model %d\n",
			       lcd_info.model);
			goto not_found;
		} /* switch() */

found:
		/* register the LCD/LED driver */
		register_led_driver(lcd_info.model, LCD_CMD_REG, LCD_DATA_REG);
		return 0;

	} else { /* if() */
		DPRINTK((KERN_INFO "pdc_chassis_info call failed with retval = %d\n", ret));
	}

not_found:
	lcd_info.model = DISPLAY_MODEL_NONE;
	return 1;
}

static void __exit led_exit(void)
{
	unregister_reboot_notifier(&led_notifier);
	return;
}

#ifdef CONFIG_PROC_FS
module_init(led_create_procfs)
#endif
