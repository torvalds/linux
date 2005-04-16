/*
 * Copyright (C) 2001, 2002, 2003 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#define SBPROF_TB_DEBUG 0

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/reboot.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/sibyte/sb1250.h>
#include <asm/sibyte/sb1250_regs.h>
#include <asm/sibyte/sb1250_scd.h>
#include <asm/sibyte/sb1250_int.h>
#include <asm/sibyte/trace_prof.h>

#define DEVNAME "bcm1250_tbprof"

static struct sbprof_tb sbp;

#define TB_FULL (sbp.next_tb_sample == MAX_TB_SAMPLES)

/************************************************************************
 * Support for ZBbus sampling using the trace buffer
 *
 * We use the SCD performance counter interrupt, caused by a Zclk counter
 * overflow, to trigger the start of tracing.
 *
 * We set the trace buffer to sample everything and freeze on
 * overflow.
 *
 * We map the interrupt for trace_buffer_freeze to handle it on CPU 0.
 *
 ************************************************************************/

static u_int64_t tb_period;

static void arm_tb(void)
{
        u_int64_t scdperfcnt;
	u_int64_t next = (1ULL << 40) - tb_period;
	u_int64_t tb_options = M_SCD_TRACE_CFG_FREEZE_FULL;
	/* Generate an SCD_PERFCNT interrupt in TB_PERIOD Zclks to
	   trigger start of trace.  XXX vary sampling period */
	bus_writeq(0, IOADDR(A_SCD_PERF_CNT_1));
	scdperfcnt = bus_readq(IOADDR(A_SCD_PERF_CNT_CFG));
	/* Unfortunately, in Pass 2 we must clear all counters to knock down
	   a previous interrupt request.  This means that bus profiling
	   requires ALL of the SCD perf counters. */
	bus_writeq((scdperfcnt & ~M_SPC_CFG_SRC1) | // keep counters 0,2,3 as is
		   M_SPC_CFG_ENABLE |		 // enable counting
		   M_SPC_CFG_CLEAR |		 // clear all counters
		   V_SPC_CFG_SRC1(1),		 // counter 1 counts cycles
		   IOADDR(A_SCD_PERF_CNT_CFG));
	bus_writeq(next, IOADDR(A_SCD_PERF_CNT_1));
	/* Reset the trace buffer */
	bus_writeq(M_SCD_TRACE_CFG_RESET, IOADDR(A_SCD_TRACE_CFG));
#if 0 && defined(M_SCD_TRACE_CFG_FORCECNT)
	/* XXXKW may want to expose control to the data-collector */
	tb_options |= M_SCD_TRACE_CFG_FORCECNT;
#endif
	bus_writeq(tb_options, IOADDR(A_SCD_TRACE_CFG));
	sbp.tb_armed = 1;
}

static irqreturn_t sbprof_tb_intr(int irq, void *dev_id, struct pt_regs *regs)
{
	int i;
	DBG(printk(DEVNAME ": tb_intr\n"));
	if (sbp.next_tb_sample < MAX_TB_SAMPLES) {
		/* XXX should use XKPHYS to make writes bypass L2 */
		u_int64_t *p = sbp.sbprof_tbbuf[sbp.next_tb_sample++];
		/* Read out trace */
		bus_writeq(M_SCD_TRACE_CFG_START_READ, IOADDR(A_SCD_TRACE_CFG));
		__asm__ __volatile__ ("sync" : : : "memory");
		/* Loop runs backwards because bundles are read out in reverse order */
		for (i = 256 * 6; i > 0; i -= 6) {
			// Subscripts decrease to put bundle in the order
			//   t0 lo, t0 hi, t1 lo, t1 hi, t2 lo, t2 hi
			p[i-1] = bus_readq(IOADDR(A_SCD_TRACE_READ)); // read t2 hi
			p[i-2] = bus_readq(IOADDR(A_SCD_TRACE_READ)); // read t2 lo
			p[i-3] = bus_readq(IOADDR(A_SCD_TRACE_READ)); // read t1 hi
			p[i-4] = bus_readq(IOADDR(A_SCD_TRACE_READ)); // read t1 lo
			p[i-5] = bus_readq(IOADDR(A_SCD_TRACE_READ)); // read t0 hi
			p[i-6] = bus_readq(IOADDR(A_SCD_TRACE_READ)); // read t0 lo
		}
		if (!sbp.tb_enable) {
			DBG(printk(DEVNAME ": tb_intr shutdown\n"));
			bus_writeq(M_SCD_TRACE_CFG_RESET,
				   IOADDR(A_SCD_TRACE_CFG));
			sbp.tb_armed = 0;
			wake_up(&sbp.tb_sync);
		} else {
			arm_tb();	// knock down current interrupt and get another one later
		}
	} else {
		/* No more trace buffer samples */
		DBG(printk(DEVNAME ": tb_intr full\n"));
		bus_writeq(M_SCD_TRACE_CFG_RESET, IOADDR(A_SCD_TRACE_CFG));
		sbp.tb_armed = 0;
		if (!sbp.tb_enable) {
			wake_up(&sbp.tb_sync);
		}
		wake_up(&sbp.tb_read);
	}
	return IRQ_HANDLED;
}

static irqreturn_t sbprof_pc_intr(int irq, void *dev_id, struct pt_regs *regs)
{
	printk(DEVNAME ": unexpected pc_intr");
	return IRQ_NONE;
}

int sbprof_zbprof_start(struct file *filp)
{
	u_int64_t scdperfcnt;

	if (sbp.tb_enable)
		return -EBUSY;

	DBG(printk(DEVNAME ": starting\n"));

	sbp.tb_enable = 1;
	sbp.next_tb_sample = 0;
	filp->f_pos = 0;

	if (request_irq
	    (K_INT_TRACE_FREEZE, sbprof_tb_intr, 0, DEVNAME " trace freeze", &sbp)) {
		return -EBUSY;
	}
	/* Make sure there isn't a perf-cnt interrupt waiting */
	scdperfcnt = bus_readq(IOADDR(A_SCD_PERF_CNT_CFG));
	/* Disable and clear counters, override SRC_1 */
	bus_writeq((scdperfcnt & ~(M_SPC_CFG_SRC1 | M_SPC_CFG_ENABLE)) |
		   M_SPC_CFG_ENABLE |
		   M_SPC_CFG_CLEAR |
		   V_SPC_CFG_SRC1(1),
		   IOADDR(A_SCD_PERF_CNT_CFG));

	/* We grab this interrupt to prevent others from trying to use
           it, even though we don't want to service the interrupts
           (they only feed into the trace-on-interrupt mechanism) */
	if (request_irq
	    (K_INT_PERF_CNT, sbprof_pc_intr, 0, DEVNAME " scd perfcnt", &sbp)) {
		free_irq(K_INT_TRACE_FREEZE, &sbp);
		return -EBUSY;
	}

	/* I need the core to mask these, but the interrupt mapper to
	   pass them through.  I am exploiting my knowledge that
	   cp0_status masks out IP[5]. krw */
	bus_writeq(K_INT_MAP_I3,
		   IOADDR(A_IMR_REGISTER(0, R_IMR_INTERRUPT_MAP_BASE) +
			  (K_INT_PERF_CNT << 3)));

	/* Initialize address traps */
	bus_writeq(0, IOADDR(A_ADDR_TRAP_UP_0));
	bus_writeq(0, IOADDR(A_ADDR_TRAP_UP_1));
	bus_writeq(0, IOADDR(A_ADDR_TRAP_UP_2));
	bus_writeq(0, IOADDR(A_ADDR_TRAP_UP_3));

	bus_writeq(0, IOADDR(A_ADDR_TRAP_DOWN_0));
	bus_writeq(0, IOADDR(A_ADDR_TRAP_DOWN_1));
	bus_writeq(0, IOADDR(A_ADDR_TRAP_DOWN_2));
	bus_writeq(0, IOADDR(A_ADDR_TRAP_DOWN_3));

	bus_writeq(0, IOADDR(A_ADDR_TRAP_CFG_0));
	bus_writeq(0, IOADDR(A_ADDR_TRAP_CFG_1));
	bus_writeq(0, IOADDR(A_ADDR_TRAP_CFG_2));
	bus_writeq(0, IOADDR(A_ADDR_TRAP_CFG_3));

	/* Initialize Trace Event 0-7 */
	//				when interrupt
	bus_writeq(M_SCD_TREVT_INTERRUPT, IOADDR(A_SCD_TRACE_EVENT_0));
	bus_writeq(0, IOADDR(A_SCD_TRACE_EVENT_1));
	bus_writeq(0, IOADDR(A_SCD_TRACE_EVENT_2));
	bus_writeq(0, IOADDR(A_SCD_TRACE_EVENT_3));
	bus_writeq(0, IOADDR(A_SCD_TRACE_EVENT_4));
	bus_writeq(0, IOADDR(A_SCD_TRACE_EVENT_5));
	bus_writeq(0, IOADDR(A_SCD_TRACE_EVENT_6));
	bus_writeq(0, IOADDR(A_SCD_TRACE_EVENT_7));

	/* Initialize Trace Sequence 0-7 */
	//				     Start on event 0 (interrupt)
	bus_writeq(V_SCD_TRSEQ_FUNC_START | 0x0fff,
		   IOADDR(A_SCD_TRACE_SEQUENCE_0));
	//			  dsamp when d used | asamp when a used
	bus_writeq(M_SCD_TRSEQ_ASAMPLE | M_SCD_TRSEQ_DSAMPLE |
		   K_SCD_TRSEQ_TRIGGER_ALL,
		   IOADDR(A_SCD_TRACE_SEQUENCE_1));
	bus_writeq(0, IOADDR(A_SCD_TRACE_SEQUENCE_2));
	bus_writeq(0, IOADDR(A_SCD_TRACE_SEQUENCE_3));
	bus_writeq(0, IOADDR(A_SCD_TRACE_SEQUENCE_4));
	bus_writeq(0, IOADDR(A_SCD_TRACE_SEQUENCE_5));
	bus_writeq(0, IOADDR(A_SCD_TRACE_SEQUENCE_6));
	bus_writeq(0, IOADDR(A_SCD_TRACE_SEQUENCE_7));

	/* Now indicate the PERF_CNT interrupt as a trace-relevant interrupt */
	bus_writeq((1ULL << K_INT_PERF_CNT),
		   IOADDR(A_IMR_REGISTER(0, R_IMR_INTERRUPT_TRACE)));

	arm_tb();

	DBG(printk(DEVNAME ": done starting\n"));

	return 0;
}

int sbprof_zbprof_stop(void)
{
	DBG(printk(DEVNAME ": stopping\n"));

	if (sbp.tb_enable) {
		sbp.tb_enable = 0;
		/* XXXKW there is a window here where the intr handler
		   may run, see the disable, and do the wake_up before
		   this sleep happens. */
		if (sbp.tb_armed) {
			DBG(printk(DEVNAME ": wait for disarm\n"));
			interruptible_sleep_on(&sbp.tb_sync);
			DBG(printk(DEVNAME ": disarm complete\n"));
		}
		free_irq(K_INT_TRACE_FREEZE, &sbp);
		free_irq(K_INT_PERF_CNT, &sbp);
	}

	DBG(printk(DEVNAME ": done stopping\n"));

	return 0;
}

static int sbprof_tb_open(struct inode *inode, struct file *filp)
{
	int minor;

	minor = iminor(inode);
	if (minor != 0) {
		return -ENODEV;
	}
	if (sbp.open) {
		return -EBUSY;
	}

	memset(&sbp, 0, sizeof(struct sbprof_tb));
	sbp.sbprof_tbbuf = vmalloc(MAX_TBSAMPLE_BYTES);
	if (!sbp.sbprof_tbbuf) {
		return -ENOMEM;
	}
	memset(sbp.sbprof_tbbuf, 0, MAX_TBSAMPLE_BYTES);
	init_waitqueue_head(&sbp.tb_sync);
	init_waitqueue_head(&sbp.tb_read);
	sbp.open = 1;

	return 0;
}

static int sbprof_tb_release(struct inode *inode, struct file *filp)
{
	int minor;

	minor = iminor(inode);
	if (minor != 0 || !sbp.open) {
		return -ENODEV;
	}

	if (sbp.tb_armed || sbp.tb_enable) {
		sbprof_zbprof_stop();
	}

	vfree(sbp.sbprof_tbbuf);
	sbp.open = 0;

	return 0;
}

static ssize_t sbprof_tb_read(struct file *filp, char *buf,
			      size_t size, loff_t *offp)
{
	int cur_sample, sample_off, cur_count, sample_left;
	char *src;
	int   count   =	 0;
	char *dest    =	 buf;
	long  cur_off = *offp;

	count = 0;
	cur_sample = cur_off / TB_SAMPLE_SIZE;
	sample_off = cur_off % TB_SAMPLE_SIZE;
	sample_left = TB_SAMPLE_SIZE - sample_off;
	while (size && (cur_sample < sbp.next_tb_sample)) {
		cur_count = size < sample_left ? size : sample_left;
		src = (char *)(((long)sbp.sbprof_tbbuf[cur_sample])+sample_off);
		copy_to_user(dest, src, cur_count);
		DBG(printk(DEVNAME ": read from sample %d, %d bytes\n",
			   cur_sample, cur_count));
		size -= cur_count;
		sample_left -= cur_count;
		if (!sample_left) {
			cur_sample++;
			sample_off = 0;
			sample_left = TB_SAMPLE_SIZE;
		} else {
			sample_off += cur_count;
		}
		cur_off += cur_count;
		dest += cur_count;
		count += cur_count;
	}
	*offp = cur_off;

	return count;
}

static int sbprof_tb_ioctl(struct inode *inode,
			   struct file *filp,
			   unsigned int command,
			   unsigned long arg)
{
	int error = 0;

	switch (command) {
	case SBPROF_ZBSTART:
		error = sbprof_zbprof_start(filp);
		break;
	case SBPROF_ZBSTOP:
		error = sbprof_zbprof_stop();
		break;
	case SBPROF_ZBWAITFULL:
		interruptible_sleep_on(&sbp.tb_read);
		/* XXXKW check if interrupted? */
		return put_user(TB_FULL, (int *) arg);
	default:
		error = -EINVAL;
		break;
	}

	return error;
}

static struct file_operations sbprof_tb_fops = {
	.owner		= THIS_MODULE,
	.open		= sbprof_tb_open,
	.release	= sbprof_tb_release,
	.read		= sbprof_tb_read,
	.ioctl		= sbprof_tb_ioctl,
	.mmap		= NULL,
};

static int __init sbprof_tb_init(void)
{
	if (register_chrdev(SBPROF_TB_MAJOR, DEVNAME, &sbprof_tb_fops)) {
		printk(KERN_WARNING DEVNAME ": initialization failed (dev %d)\n",
		       SBPROF_TB_MAJOR);
		return -EIO;
	}
	sbp.open = 0;
	tb_period = zbbus_mhz * 10000LL;
	printk(KERN_INFO DEVNAME ": initialized - tb_period = %lld\n", tb_period);
	return 0;
}

static void __exit sbprof_tb_cleanup(void)
{
	unregister_chrdev(SBPROF_TB_MAJOR, DEVNAME);
}

module_init(sbprof_tb_init);
module_exit(sbprof_tb_cleanup);
