/*
 * (C) Copyright 2008 Intel Corporation
 * Authors:
 * Andy Henroid <andrew.d.henroid@intel.com>
 * Venkatesh Pallipadi <venkatesh.pallipadi@intel.com>
 */

/*
 * Save DIMM power on Intel 7300-based platforms when all CPUs/cores
 * are idle, using the DIMM thermal throttling capability.
 *
 * This driver depends on the Intel integrated DMA controller (I/O AT).
 * If the driver for I/O AT (drivers/dma/ioatdma*) is also enabled,
 * this driver should work cooperatively.
 */

/* #define DEBUG */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/gfp.h>
#include <linux/sched.h>
#include <linux/notifier.h>
#include <linux/cpumask.h>
#include <linux/ktime.h>
#include <linux/delay.h>
#include <linux/debugfs.h>
#include <linux/stop_machine.h>
#include <linux/i7300_idle.h>

#include <asm/idle.h>

#include "../dma/ioat/hw.h"
#include "../dma/ioat/registers.h"

#define I7300_IDLE_DRIVER_VERSION	"1.55"
#define I7300_PRINT			"i7300_idle:"

#define MAX_STOP_RETRIES	10

static int debug;
module_param_named(debug, debug, uint, 0644);
MODULE_PARM_DESC(debug, "Enable debug printks in this driver");

static int forceload;
module_param_named(forceload, forceload, uint, 0644);
MODULE_PARM_DESC(debug, "Enable driver testing on unvalidated i5000");

#define dprintk(fmt, arg...) \
	do { if (debug) printk(KERN_INFO I7300_PRINT fmt, ##arg); } while (0)

/*
 * Value to set THRTLOW to when initiating throttling
 *  0 = No throttling
 *  1 = Throttle when > 4 activations per eval window (Maximum throttling)
 *  2 = Throttle when > 8 activations
 *  168 = Throttle when > 672 activations (Minimum throttling)
 */
#define MAX_THROTTLE_LOW_LIMIT		168
static uint throttle_low_limit = 1;
module_param_named(throttle_low_limit, throttle_low_limit, uint, 0644);
MODULE_PARM_DESC(throttle_low_limit,
		"Value for THRTLOWLM activation field "
		"(0 = disable throttle, 1 = Max throttle, 168 = Min throttle)");

/*
 * simple invocation and duration statistics
 */
static unsigned long total_starts;
static unsigned long total_us;

#ifdef DEBUG
static unsigned long past_skip;
#endif

static struct pci_dev *fbd_dev;

static spinlock_t i7300_idle_lock;
static int i7300_idle_active;

static u8 i7300_idle_thrtctl_saved;
static u8 i7300_idle_thrtlow_saved;
static u32 i7300_idle_mc_saved;

static cpumask_var_t idle_cpumask;
static ktime_t start_ktime;
static unsigned long avg_idle_us;

static struct dentry *debugfs_dir;

/* Begin: I/O AT Helper routines */

#define IOAT_CHANBASE(ioat_ctl, chan) (ioat_ctl + 0x80 + 0x80 * chan)
/* Snoop control (disable snoops when coherency is not important) */
#define IOAT_DESC_SADDR_SNP_CTL (1UL << 1)
#define IOAT_DESC_DADDR_SNP_CTL (1UL << 2)

static struct pci_dev *ioat_dev;
static struct ioat_dma_descriptor *ioat_desc; /* I/O AT desc & data (1 page) */
static unsigned long ioat_desc_phys;
static u8 *ioat_iomap; /* I/O AT memory-mapped control regs (aka CB_BAR) */
static u8 *ioat_chanbase;

/* Start I/O AT memory copy */
static int i7300_idle_ioat_start(void)
{
	u32 err;
	/* Clear error (due to circular descriptor pointer) */
	err = readl(ioat_chanbase + IOAT_CHANERR_OFFSET);
	if (err)
		writel(err, ioat_chanbase + IOAT_CHANERR_OFFSET);

	writeb(IOAT_CHANCMD_START, ioat_chanbase + IOAT1_CHANCMD_OFFSET);
	return 0;
}

/* Stop I/O AT memory copy */
static void i7300_idle_ioat_stop(void)
{
	int i;
	u64 sts;

	for (i = 0; i < MAX_STOP_RETRIES; i++) {
		writeb(IOAT_CHANCMD_RESET,
			ioat_chanbase + IOAT1_CHANCMD_OFFSET);

		udelay(10);

		sts = readq(ioat_chanbase + IOAT1_CHANSTS_OFFSET) &
			IOAT_CHANSTS_STATUS;

		if (sts != IOAT_CHANSTS_ACTIVE)
			break;

	}

	if (i == MAX_STOP_RETRIES) {
		dprintk("failed to stop I/O AT after %d retries\n",
			MAX_STOP_RETRIES);
	}
}

/* Test I/O AT by copying 1024 byte from 2k to 1k */
static int __init i7300_idle_ioat_selftest(u8 *ctl,
		struct ioat_dma_descriptor *desc, unsigned long desc_phys)
{
	u64 chan_sts;

	memset(desc, 0, 2048);
	memset((u8 *) desc + 2048, 0xab, 1024);

	desc[0].size = 1024;
	desc[0].ctl = 0;
	desc[0].src_addr = desc_phys + 2048;
	desc[0].dst_addr = desc_phys + 1024;
	desc[0].next = 0;

	writeb(IOAT_CHANCMD_RESET, ioat_chanbase + IOAT1_CHANCMD_OFFSET);
	writeb(IOAT_CHANCMD_START, ioat_chanbase + IOAT1_CHANCMD_OFFSET);

	udelay(1000);

	chan_sts = readq(ioat_chanbase + IOAT1_CHANSTS_OFFSET) &
			IOAT_CHANSTS_STATUS;

	if (chan_sts != IOAT_CHANSTS_DONE) {
		/* Not complete, reset the channel */
		writeb(IOAT_CHANCMD_RESET,
		       ioat_chanbase + IOAT1_CHANCMD_OFFSET);
		return -1;
	}

	if (*(u32 *) ((u8 *) desc + 3068) != 0xabababab ||
	    *(u32 *) ((u8 *) desc + 2044) != 0xabababab) {
		dprintk("Data values src 0x%x, dest 0x%x, memset 0x%x\n",
			*(u32 *) ((u8 *) desc + 2048),
			*(u32 *) ((u8 *) desc + 1024),
			*(u32 *) ((u8 *) desc + 3072));
		return -1;
	}
	return 0;
}

static struct device dummy_dma_dev = {
	.init_name = "fallback device",
	.coherent_dma_mask = DMA_BIT_MASK(64),
	.dma_mask = &dummy_dma_dev.coherent_dma_mask,
};

/* Setup and initialize I/O AT */
/* This driver needs I/O AT as the throttling takes effect only when there is
 * some memory activity. We use I/O AT to set up a dummy copy, while all CPUs
 * go idle and memory is throttled.
 */
static int __init i7300_idle_ioat_init(void)
{
	u8 ver, chan_count, ioat_chan;
	u16 chan_ctl;

	ioat_iomap = (u8 *) ioremap_nocache(pci_resource_start(ioat_dev, 0),
					    pci_resource_len(ioat_dev, 0));

	if (!ioat_iomap) {
		printk(KERN_ERR I7300_PRINT "failed to map I/O AT registers\n");
		goto err_ret;
	}

	ver = readb(ioat_iomap + IOAT_VER_OFFSET);
	if (ver != IOAT_VER_1_2) {
		printk(KERN_ERR I7300_PRINT "unknown I/O AT version (%u.%u)\n",
			ver >> 4, ver & 0xf);
		goto err_unmap;
	}

	chan_count = readb(ioat_iomap + IOAT_CHANCNT_OFFSET);
	if (!chan_count) {
		printk(KERN_ERR I7300_PRINT "unexpected # of I/O AT channels "
			"(%u)\n",
			chan_count);
		goto err_unmap;
	}

	ioat_chan = chan_count - 1;
	ioat_chanbase = IOAT_CHANBASE(ioat_iomap, ioat_chan);

	chan_ctl = readw(ioat_chanbase + IOAT_CHANCTRL_OFFSET);
	if (chan_ctl & IOAT_CHANCTRL_CHANNEL_IN_USE) {
		printk(KERN_ERR I7300_PRINT "channel %d in use\n", ioat_chan);
		goto err_unmap;
	}

	writew(IOAT_CHANCTRL_CHANNEL_IN_USE,
		ioat_chanbase + IOAT_CHANCTRL_OFFSET);

	ioat_desc = (struct ioat_dma_descriptor *)dma_alloc_coherent(
			&dummy_dma_dev, 4096,
			(dma_addr_t *)&ioat_desc_phys, GFP_KERNEL);
	if (!ioat_desc) {
		printk(KERN_ERR I7300_PRINT "failed to allocate I/O AT desc\n");
		goto err_mark_unused;
	}

	writel(ioat_desc_phys & 0xffffffffUL,
	       ioat_chanbase + IOAT1_CHAINADDR_OFFSET_LOW);
	writel(ioat_desc_phys >> 32,
	       ioat_chanbase + IOAT1_CHAINADDR_OFFSET_HIGH);

	if (i7300_idle_ioat_selftest(ioat_iomap, ioat_desc, ioat_desc_phys)) {
		printk(KERN_ERR I7300_PRINT "I/O AT self-test failed\n");
		goto err_free;
	}

	/* Setup circular I/O AT descriptor chain */
	ioat_desc[0].ctl = IOAT_DESC_SADDR_SNP_CTL | IOAT_DESC_DADDR_SNP_CTL;
	ioat_desc[0].src_addr = ioat_desc_phys + 2048;
	ioat_desc[0].dst_addr = ioat_desc_phys + 3072;
	ioat_desc[0].size = 128;
	ioat_desc[0].next = ioat_desc_phys + sizeof(struct ioat_dma_descriptor);

	ioat_desc[1].ctl = ioat_desc[0].ctl;
	ioat_desc[1].src_addr = ioat_desc[0].src_addr;
	ioat_desc[1].dst_addr = ioat_desc[0].dst_addr;
	ioat_desc[1].size = ioat_desc[0].size;
	ioat_desc[1].next = ioat_desc_phys;

	return 0;

err_free:
	dma_free_coherent(&dummy_dma_dev, 4096, (void *)ioat_desc, 0);
err_mark_unused:
	writew(0, ioat_chanbase + IOAT_CHANCTRL_OFFSET);
err_unmap:
	iounmap(ioat_iomap);
err_ret:
	return -ENODEV;
}

/* Cleanup I/O AT */
static void __exit i7300_idle_ioat_exit(void)
{
	int i;
	u64 chan_sts;

	i7300_idle_ioat_stop();

	/* Wait for a while for the channel to halt before releasing */
	for (i = 0; i < MAX_STOP_RETRIES; i++) {
		writeb(IOAT_CHANCMD_RESET,
		       ioat_chanbase + IOAT1_CHANCMD_OFFSET);

		chan_sts = readq(ioat_chanbase + IOAT1_CHANSTS_OFFSET) &
			IOAT_CHANSTS_STATUS;

		if (chan_sts != IOAT_CHANSTS_ACTIVE) {
			writew(0, ioat_chanbase + IOAT_CHANCTRL_OFFSET);
			break;
		}
		udelay(1000);
	}

	chan_sts = readq(ioat_chanbase + IOAT1_CHANSTS_OFFSET) &
			IOAT_CHANSTS_STATUS;

	/*
	 * We tried to reset multiple times. If IO A/T channel is still active
	 * flag an error and return without cleanup. Memory leak is better
	 * than random corruption in that extreme error situation.
	 */
	if (chan_sts == IOAT_CHANSTS_ACTIVE) {
		printk(KERN_ERR I7300_PRINT "Unable to stop IO A/T channels."
			" Not freeing resources\n");
		return;
	}

	dma_free_coherent(&dummy_dma_dev, 4096, (void *)ioat_desc, 0);
	iounmap(ioat_iomap);
}

/* End: I/O AT Helper routines */

#define DIMM_THRTLOW 0x64
#define DIMM_THRTCTL 0x67
#define DIMM_THRTCTL_THRMHUNT (1UL << 0)
#define DIMM_MC 0x40
#define DIMM_GTW_MODE (1UL << 17)
#define DIMM_GBLACT 0x60

/*
 * Keep track of an exponential-decaying average of recent idle durations.
 * The latest duration gets DURATION_WEIGHT_PCT percentage weight
 * in this average, with the old average getting the remaining weight.
 *
 * High weights emphasize recent history, low weights include long history.
 */
#define DURATION_WEIGHT_PCT 55

/*
 * When the decaying average of recent durations or the predicted duration
 * of the next timer interrupt is shorter than duration_threshold, the
 * driver will decline to throttle.
 */
#define DURATION_THRESHOLD_US 100


/* Store DIMM thermal throttle configuration */
static int i7300_idle_thrt_save(void)
{
	u32 new_mc_val;
	u8 gblactlm;

	pci_read_config_byte(fbd_dev, DIMM_THRTCTL, &i7300_idle_thrtctl_saved);
	pci_read_config_byte(fbd_dev, DIMM_THRTLOW, &i7300_idle_thrtlow_saved);
	pci_read_config_dword(fbd_dev, DIMM_MC, &i7300_idle_mc_saved);
	/*
	 * Make sure we have Global Throttling Window Mode set to have a
	 * "short" window. This (mostly) works around an issue where
	 * throttling persists until the end of the global throttling window
	 * size. On the tested system, this was resulting in a maximum of
	 * 64 ms to exit throttling (average 32 ms). The actual numbers
	 * depends on system frequencies. Setting the short window reduces
	 * this by a factor of 4096.
	 *
	 * We will only do this only if the system is set for
	 * unlimited-activations while in open-loop throttling (i.e., when
	 * Global Activation Throttle Limit is zero).
	 */
	pci_read_config_byte(fbd_dev, DIMM_GBLACT, &gblactlm);
	dprintk("thrtctl_saved = 0x%02x, thrtlow_saved = 0x%02x\n",
		i7300_idle_thrtctl_saved,
		i7300_idle_thrtlow_saved);
	dprintk("mc_saved = 0x%08x, gblactlm = 0x%02x\n",
		i7300_idle_mc_saved,
		gblactlm);
	if (gblactlm == 0) {
		new_mc_val = i7300_idle_mc_saved | DIMM_GTW_MODE;
		pci_write_config_dword(fbd_dev, DIMM_MC, new_mc_val);
		return 0;
	} else {
		dprintk("could not set GTW_MODE = 1 (OLTT enabled)\n");
		return -ENODEV;
	}
}

/* Restore DIMM thermal throttle configuration */
static void i7300_idle_thrt_restore(void)
{
	pci_write_config_dword(fbd_dev, DIMM_MC, i7300_idle_mc_saved);
	pci_write_config_byte(fbd_dev, DIMM_THRTLOW, i7300_idle_thrtlow_saved);
	pci_write_config_byte(fbd_dev, DIMM_THRTCTL, i7300_idle_thrtctl_saved);
}

/* Enable DIMM thermal throttling */
static void i7300_idle_start(void)
{
	u8 new_ctl;
	u8 limit;

	new_ctl = i7300_idle_thrtctl_saved & ~DIMM_THRTCTL_THRMHUNT;
	pci_write_config_byte(fbd_dev, DIMM_THRTCTL, new_ctl);

	limit = throttle_low_limit;
	if (unlikely(limit > MAX_THROTTLE_LOW_LIMIT))
		limit = MAX_THROTTLE_LOW_LIMIT;

	pci_write_config_byte(fbd_dev, DIMM_THRTLOW, limit);

	new_ctl = i7300_idle_thrtctl_saved | DIMM_THRTCTL_THRMHUNT;
	pci_write_config_byte(fbd_dev, DIMM_THRTCTL, new_ctl);
}

/* Disable DIMM thermal throttling */
static void i7300_idle_stop(void)
{
	u8 new_ctl;
	u8 got_ctl;

	new_ctl = i7300_idle_thrtctl_saved & ~DIMM_THRTCTL_THRMHUNT;
	pci_write_config_byte(fbd_dev, DIMM_THRTCTL, new_ctl);

	pci_write_config_byte(fbd_dev, DIMM_THRTLOW, i7300_idle_thrtlow_saved);
	pci_write_config_byte(fbd_dev, DIMM_THRTCTL, i7300_idle_thrtctl_saved);
	pci_read_config_byte(fbd_dev, DIMM_THRTCTL, &got_ctl);
	WARN_ON_ONCE(got_ctl != i7300_idle_thrtctl_saved);
}


/*
 * i7300_avg_duration_check()
 * return 0 if the decaying average of recent idle durations is
 * more than DURATION_THRESHOLD_US
 */
static int i7300_avg_duration_check(void)
{
	if (avg_idle_us >= DURATION_THRESHOLD_US)
		return 0;

#ifdef DEBUG
	past_skip++;
#endif
	return 1;
}

/* Idle notifier to look at idle CPUs */
static int i7300_idle_notifier(struct notifier_block *nb, unsigned long val,
				void *data)
{
	unsigned long flags;
	ktime_t now_ktime;
	static ktime_t idle_begin_time;
	static int time_init = 1;

	if (!throttle_low_limit)
		return 0;

	if (unlikely(time_init)) {
		time_init = 0;
		idle_begin_time = ktime_get();
	}

	spin_lock_irqsave(&i7300_idle_lock, flags);
	if (val == IDLE_START) {

		cpumask_set_cpu(smp_processor_id(), idle_cpumask);

		if (cpumask_weight(idle_cpumask) != num_online_cpus())
			goto end;

		now_ktime = ktime_get();
		idle_begin_time = now_ktime;

		if (i7300_avg_duration_check())
			goto end;

		i7300_idle_active = 1;
		total_starts++;
		start_ktime = now_ktime;

		i7300_idle_start();
		i7300_idle_ioat_start();

	} else if (val == IDLE_END) {
		cpumask_clear_cpu(smp_processor_id(), idle_cpumask);
		if (cpumask_weight(idle_cpumask) == (num_online_cpus() - 1)) {
			/* First CPU coming out of idle */
			u64 idle_duration_us;

			now_ktime = ktime_get();

			idle_duration_us = ktime_to_us(ktime_sub
						(now_ktime, idle_begin_time));

			avg_idle_us =
				((100 - DURATION_WEIGHT_PCT) * avg_idle_us +
				 DURATION_WEIGHT_PCT * idle_duration_us) / 100;

			if (i7300_idle_active) {
				ktime_t idle_ktime;

				idle_ktime = ktime_sub(now_ktime, start_ktime);
				total_us += ktime_to_us(idle_ktime);

				i7300_idle_ioat_stop();
				i7300_idle_stop();
				i7300_idle_active = 0;
			}
		}
	}
end:
	spin_unlock_irqrestore(&i7300_idle_lock, flags);
	return 0;
}

static struct notifier_block i7300_idle_nb = {
	.notifier_call = i7300_idle_notifier,
};

MODULE_DEVICE_TABLE(pci, pci_tbl);

static ssize_t stats_read_ul(struct file *fp, char __user *ubuf, size_t count,
				loff_t *off)
{
	unsigned long *p = fp->private_data;
	char buf[32];
	int len;

	len = snprintf(buf, 32, "%lu\n", *p);
	return simple_read_from_buffer(ubuf, count, off, buf, len);
}

static const struct file_operations idle_fops = {
	.open	= simple_open,
	.read	= stats_read_ul,
	.llseek = default_llseek,
};

struct debugfs_file_info {
	void *ptr;
	char name[32];
	struct dentry *file;
} debugfs_file_list[] = {
				{&total_starts, "total_starts", NULL},
				{&total_us, "total_us", NULL},
#ifdef DEBUG
				{&past_skip, "past_skip", NULL},
#endif
				{NULL, "", NULL}
			};

static int __init i7300_idle_init(void)
{
	spin_lock_init(&i7300_idle_lock);
	total_us = 0;

	if (i7300_idle_platform_probe(&fbd_dev, &ioat_dev, forceload))
		return -ENODEV;

	if (i7300_idle_thrt_save())
		return -ENODEV;

	if (i7300_idle_ioat_init())
		return -ENODEV;

	if (!zalloc_cpumask_var(&idle_cpumask, GFP_KERNEL))
		return -ENOMEM;

	debugfs_dir = debugfs_create_dir("i7300_idle", NULL);
	if (debugfs_dir) {
		int i = 0;

		while (debugfs_file_list[i].ptr != NULL) {
			debugfs_file_list[i].file = debugfs_create_file(
					debugfs_file_list[i].name,
					S_IRUSR,
					debugfs_dir,
					debugfs_file_list[i].ptr,
					&idle_fops);
			i++;
		}
	}

	idle_notifier_register(&i7300_idle_nb);

	printk(KERN_INFO "i7300_idle: loaded v%s\n", I7300_IDLE_DRIVER_VERSION);
	return 0;
}

static void __exit i7300_idle_exit(void)
{
	idle_notifier_unregister(&i7300_idle_nb);
	free_cpumask_var(idle_cpumask);

	if (debugfs_dir) {
		int i = 0;

		while (debugfs_file_list[i].file != NULL) {
			debugfs_remove(debugfs_file_list[i].file);
			i++;
		}

		debugfs_remove(debugfs_dir);
	}
	i7300_idle_thrt_restore();
	i7300_idle_ioat_exit();
}

module_init(i7300_idle_init);
module_exit(i7300_idle_exit);

MODULE_AUTHOR("Andy Henroid <andrew.d.henroid@intel.com>");
MODULE_DESCRIPTION("Intel Chipset DIMM Idle Power Saving Driver v"
			I7300_IDLE_DRIVER_VERSION);
MODULE_LICENSE("GPL");
