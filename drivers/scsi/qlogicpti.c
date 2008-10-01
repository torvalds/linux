/* qlogicpti.c: Performance Technologies QlogicISP sbus card driver.
 *
 * Copyright (C) 1996, 2006 David S. Miller (davem@davemloft.net)
 *
 * A lot of this driver was directly stolen from Erik H. Moe's PCI
 * Qlogic ISP driver.  Mucho kudos to him for this code.
 *
 * An even bigger kudos to John Grana at Performance Technologies
 * for providing me with the hardware to write this driver, you rule
 * John you really do.
 *
 * May, 2, 1997: Added support for QLGC,isp --jj
 */

#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/blkdev.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/jiffies.h>

#include <asm/byteorder.h>

#include "qlogicpti.h"

#include <asm/sbus.h>
#include <asm/dma.h>
#include <asm/system.h>
#include <asm/ptrace.h>
#include <asm/pgtable.h>
#include <asm/oplib.h>
#include <asm/io.h>
#include <asm/irq.h>

#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_eh.h>
#include <scsi/scsi_tcq.h>
#include <scsi/scsi_host.h>

#define MAX_TARGETS	16
#define MAX_LUNS	8	/* 32 for 1.31 F/W */

#define DEFAULT_LOOP_COUNT	10000

#include "qlogicpti_asm.c"

static struct qlogicpti *qptichain = NULL;
static DEFINE_SPINLOCK(qptichain_lock);

#define PACKB(a, b)			(((a)<<4)|(b))

static const u_char mbox_param[] = {
	PACKB(1, 1),	/* MBOX_NO_OP */
	PACKB(5, 5),	/* MBOX_LOAD_RAM */
	PACKB(2, 0),	/* MBOX_EXEC_FIRMWARE */
	PACKB(5, 5),	/* MBOX_DUMP_RAM */
	PACKB(3, 3),	/* MBOX_WRITE_RAM_WORD */
	PACKB(2, 3),	/* MBOX_READ_RAM_WORD */
	PACKB(6, 6),	/* MBOX_MAILBOX_REG_TEST */
	PACKB(2, 3),	/* MBOX_VERIFY_CHECKSUM	*/
	PACKB(1, 3),	/* MBOX_ABOUT_FIRMWARE */
	PACKB(0, 0),	/* 0x0009 */
	PACKB(0, 0),	/* 0x000a */
	PACKB(0, 0),	/* 0x000b */
	PACKB(0, 0),	/* 0x000c */
	PACKB(0, 0),	/* 0x000d */
	PACKB(1, 2),	/* MBOX_CHECK_FIRMWARE */
	PACKB(0, 0),	/* 0x000f */
	PACKB(5, 5),	/* MBOX_INIT_REQ_QUEUE */
	PACKB(6, 6),	/* MBOX_INIT_RES_QUEUE */
	PACKB(4, 4),	/* MBOX_EXECUTE_IOCB */
	PACKB(2, 2),	/* MBOX_WAKE_UP	*/
	PACKB(1, 6),	/* MBOX_STOP_FIRMWARE */
	PACKB(4, 4),	/* MBOX_ABORT */
	PACKB(2, 2),	/* MBOX_ABORT_DEVICE */
	PACKB(3, 3),	/* MBOX_ABORT_TARGET */
	PACKB(2, 2),	/* MBOX_BUS_RESET */
	PACKB(2, 3),	/* MBOX_STOP_QUEUE */
	PACKB(2, 3),	/* MBOX_START_QUEUE */
	PACKB(2, 3),	/* MBOX_SINGLE_STEP_QUEUE */
	PACKB(2, 3),	/* MBOX_ABORT_QUEUE */
	PACKB(2, 4),	/* MBOX_GET_DEV_QUEUE_STATUS */
	PACKB(0, 0),	/* 0x001e */
	PACKB(1, 3),	/* MBOX_GET_FIRMWARE_STATUS */
	PACKB(1, 2),	/* MBOX_GET_INIT_SCSI_ID */
	PACKB(1, 2),	/* MBOX_GET_SELECT_TIMEOUT */
	PACKB(1, 3),	/* MBOX_GET_RETRY_COUNT	*/
	PACKB(1, 2),	/* MBOX_GET_TAG_AGE_LIMIT */
	PACKB(1, 2),	/* MBOX_GET_CLOCK_RATE */
	PACKB(1, 2),	/* MBOX_GET_ACT_NEG_STATE */
	PACKB(1, 2),	/* MBOX_GET_ASYNC_DATA_SETUP_TIME */
	PACKB(1, 3),	/* MBOX_GET_SBUS_PARAMS */
	PACKB(2, 4),	/* MBOX_GET_TARGET_PARAMS */
	PACKB(2, 4),	/* MBOX_GET_DEV_QUEUE_PARAMS */
	PACKB(0, 0),	/* 0x002a */
	PACKB(0, 0),	/* 0x002b */
	PACKB(0, 0),	/* 0x002c */
	PACKB(0, 0),	/* 0x002d */
	PACKB(0, 0),	/* 0x002e */
	PACKB(0, 0),	/* 0x002f */
	PACKB(2, 2),	/* MBOX_SET_INIT_SCSI_ID */
	PACKB(2, 2),	/* MBOX_SET_SELECT_TIMEOUT */
	PACKB(3, 3),	/* MBOX_SET_RETRY_COUNT	*/
	PACKB(2, 2),	/* MBOX_SET_TAG_AGE_LIMIT */
	PACKB(2, 2),	/* MBOX_SET_CLOCK_RATE */
	PACKB(2, 2),	/* MBOX_SET_ACTIVE_NEG_STATE */
	PACKB(2, 2),	/* MBOX_SET_ASYNC_DATA_SETUP_TIME */
	PACKB(3, 3),	/* MBOX_SET_SBUS_CONTROL_PARAMS */
	PACKB(4, 4),	/* MBOX_SET_TARGET_PARAMS */
	PACKB(4, 4),	/* MBOX_SET_DEV_QUEUE_PARAMS */
	PACKB(0, 0),	/* 0x003a */
	PACKB(0, 0),	/* 0x003b */
	PACKB(0, 0),	/* 0x003c */
	PACKB(0, 0),	/* 0x003d */
	PACKB(0, 0),	/* 0x003e */
	PACKB(0, 0),	/* 0x003f */
	PACKB(0, 0),	/* 0x0040 */
	PACKB(0, 0),	/* 0x0041 */
	PACKB(0, 0)	/* 0x0042 */
};

#define MAX_MBOX_COMMAND	ARRAY_SIZE(mbox_param)

/* queue length's _must_ be power of two: */
#define QUEUE_DEPTH(in, out, ql)	((in - out) & (ql))
#define REQ_QUEUE_DEPTH(in, out)	QUEUE_DEPTH(in, out, 		     \
						    QLOGICPTI_REQ_QUEUE_LEN)
#define RES_QUEUE_DEPTH(in, out)	QUEUE_DEPTH(in, out, RES_QUEUE_LEN)

static inline void qlogicpti_enable_irqs(struct qlogicpti *qpti)
{
	sbus_writew(SBUS_CTRL_ERIRQ | SBUS_CTRL_GENAB,
		    qpti->qregs + SBUS_CTRL);
}

static inline void qlogicpti_disable_irqs(struct qlogicpti *qpti)
{
	sbus_writew(0, qpti->qregs + SBUS_CTRL);
}

static inline void set_sbus_cfg1(struct qlogicpti *qpti)
{
	u16 val;
	u8 bursts = qpti->bursts;

#if 0	/* It appears that at least PTI cards do not support
	 * 64-byte bursts and that setting the B64 bit actually
	 * is a nop and the chip ends up using the smallest burst
	 * size. -DaveM
	 */
	if (sbus_can_burst64(qpti->sdev) && (bursts & DMA_BURST64)) {
		val = (SBUS_CFG1_BENAB | SBUS_CFG1_B64);
	} else
#endif
	if (bursts & DMA_BURST32) {
		val = (SBUS_CFG1_BENAB | SBUS_CFG1_B32);
	} else if (bursts & DMA_BURST16) {
		val = (SBUS_CFG1_BENAB | SBUS_CFG1_B16);
	} else if (bursts & DMA_BURST8) {
		val = (SBUS_CFG1_BENAB | SBUS_CFG1_B8);
	} else {
		val = 0; /* No sbus bursts for you... */
	}
	sbus_writew(val, qpti->qregs + SBUS_CFG1);
}

static int qlogicpti_mbox_command(struct qlogicpti *qpti, u_short param[], int force)
{
	int loop_count;
	u16 tmp;

	if (mbox_param[param[0]] == 0)
		return 1;

	/* Set SBUS semaphore. */
	tmp = sbus_readw(qpti->qregs + SBUS_SEMAPHORE);
	tmp |= SBUS_SEMAPHORE_LCK;
	sbus_writew(tmp, qpti->qregs + SBUS_SEMAPHORE);

	/* Wait for host IRQ bit to clear. */
	loop_count = DEFAULT_LOOP_COUNT;
	while (--loop_count && (sbus_readw(qpti->qregs + HCCTRL) & HCCTRL_HIRQ)) {
		barrier();
		cpu_relax();
	}
	if (!loop_count)
		printk(KERN_EMERG "qlogicpti%d: mbox_command loop timeout #1\n",
		       qpti->qpti_id);

	/* Write mailbox command registers. */
	switch (mbox_param[param[0]] >> 4) {
	case 6: sbus_writew(param[5], qpti->qregs + MBOX5);
	case 5: sbus_writew(param[4], qpti->qregs + MBOX4);
	case 4: sbus_writew(param[3], qpti->qregs + MBOX3);
	case 3: sbus_writew(param[2], qpti->qregs + MBOX2);
	case 2: sbus_writew(param[1], qpti->qregs + MBOX1);
	case 1: sbus_writew(param[0], qpti->qregs + MBOX0);
	}

	/* Clear RISC interrupt. */
	tmp = sbus_readw(qpti->qregs + HCCTRL);
	tmp |= HCCTRL_CRIRQ;
	sbus_writew(tmp, qpti->qregs + HCCTRL);

	/* Clear SBUS semaphore. */
	sbus_writew(0, qpti->qregs + SBUS_SEMAPHORE);

	/* Set HOST interrupt. */
	tmp = sbus_readw(qpti->qregs + HCCTRL);
	tmp |= HCCTRL_SHIRQ;
	sbus_writew(tmp, qpti->qregs + HCCTRL);

	/* Wait for HOST interrupt clears. */
	loop_count = DEFAULT_LOOP_COUNT;
	while (--loop_count &&
	       (sbus_readw(qpti->qregs + HCCTRL) & HCCTRL_CRIRQ))
		udelay(20);
	if (!loop_count)
		printk(KERN_EMERG "qlogicpti%d: mbox_command[%04x] loop timeout #2\n",
		       qpti->qpti_id, param[0]);

	/* Wait for SBUS semaphore to get set. */
	loop_count = DEFAULT_LOOP_COUNT;
	while (--loop_count &&
	       !(sbus_readw(qpti->qregs + SBUS_SEMAPHORE) & SBUS_SEMAPHORE_LCK)) {
		udelay(20);

		/* Workaround for some buggy chips. */
		if (sbus_readw(qpti->qregs + MBOX0) & 0x4000)
			break;
	}
	if (!loop_count)
		printk(KERN_EMERG "qlogicpti%d: mbox_command[%04x] loop timeout #3\n",
		       qpti->qpti_id, param[0]);

	/* Wait for MBOX busy condition to go away. */
	loop_count = DEFAULT_LOOP_COUNT;
	while (--loop_count && (sbus_readw(qpti->qregs + MBOX0) == 0x04))
		udelay(20);
	if (!loop_count)
		printk(KERN_EMERG "qlogicpti%d: mbox_command[%04x] loop timeout #4\n",
		       qpti->qpti_id, param[0]);

	/* Read back output parameters. */
	switch (mbox_param[param[0]] & 0xf) {
	case 6: param[5] = sbus_readw(qpti->qregs + MBOX5);
	case 5: param[4] = sbus_readw(qpti->qregs + MBOX4);
	case 4: param[3] = sbus_readw(qpti->qregs + MBOX3);
	case 3: param[2] = sbus_readw(qpti->qregs + MBOX2);
	case 2: param[1] = sbus_readw(qpti->qregs + MBOX1);
	case 1: param[0] = sbus_readw(qpti->qregs + MBOX0);
	}

	/* Clear RISC interrupt. */
	tmp = sbus_readw(qpti->qregs + HCCTRL);
	tmp |= HCCTRL_CRIRQ;
	sbus_writew(tmp, qpti->qregs + HCCTRL);

	/* Release SBUS semaphore. */
	tmp = sbus_readw(qpti->qregs + SBUS_SEMAPHORE);
	tmp &= ~(SBUS_SEMAPHORE_LCK);
	sbus_writew(tmp, qpti->qregs + SBUS_SEMAPHORE);

	/* We're done. */
	return 0;
}

static inline void qlogicpti_set_hostdev_defaults(struct qlogicpti *qpti)
{
	int i;

	qpti->host_param.initiator_scsi_id = qpti->scsi_id;
	qpti->host_param.bus_reset_delay = 3;
	qpti->host_param.retry_count = 0;
	qpti->host_param.retry_delay = 5;
	qpti->host_param.async_data_setup_time = 3;
	qpti->host_param.req_ack_active_negation = 1;
	qpti->host_param.data_line_active_negation = 1;
	qpti->host_param.data_dma_burst_enable = 1;
	qpti->host_param.command_dma_burst_enable = 1;
	qpti->host_param.tag_aging = 8;
	qpti->host_param.selection_timeout = 250;
	qpti->host_param.max_queue_depth = 256;

	for(i = 0; i < MAX_TARGETS; i++) {
		/*
		 * disconnect, parity, arq, reneg on reset, and, oddly enough
		 * tags...the midlayer's notion of tagged support has to match
		 * our device settings, and since we base whether we enable a
		 * tag on a  per-cmnd basis upon what the midlayer sez, we
		 * actually enable the capability here.
		 */
		qpti->dev_param[i].device_flags = 0xcd;
		qpti->dev_param[i].execution_throttle = 16;
		if (qpti->ultra) {
			qpti->dev_param[i].synchronous_period = 12;
			qpti->dev_param[i].synchronous_offset = 8;
		} else {
			qpti->dev_param[i].synchronous_period = 25;
			qpti->dev_param[i].synchronous_offset = 12;
		}
		qpti->dev_param[i].device_enable = 1;
	}
}

static int qlogicpti_reset_hardware(struct Scsi_Host *host)
{
	struct qlogicpti *qpti = (struct qlogicpti *) host->hostdata;
	u_short param[6];
	unsigned short risc_code_addr;
	int loop_count, i;
	unsigned long flags;

	risc_code_addr = 0x1000;	/* all load addresses are at 0x1000 */

	spin_lock_irqsave(host->host_lock, flags);

	sbus_writew(HCCTRL_PAUSE, qpti->qregs + HCCTRL);

	/* Only reset the scsi bus if it is not free. */
	if (sbus_readw(qpti->qregs + CPU_PCTRL) & CPU_PCTRL_BSY) {
		sbus_writew(CPU_ORIDE_RMOD, qpti->qregs + CPU_ORIDE);
		sbus_writew(CPU_CMD_BRESET, qpti->qregs + CPU_CMD);
		udelay(400);
	}

	sbus_writew(SBUS_CTRL_RESET, qpti->qregs + SBUS_CTRL);
	sbus_writew((DMA_CTRL_CCLEAR | DMA_CTRL_CIRQ), qpti->qregs + CMD_DMA_CTRL);
	sbus_writew((DMA_CTRL_CCLEAR | DMA_CTRL_CIRQ), qpti->qregs + DATA_DMA_CTRL);

	loop_count = DEFAULT_LOOP_COUNT;
	while (--loop_count && ((sbus_readw(qpti->qregs + MBOX0) & 0xff) == 0x04))
		udelay(20);
	if (!loop_count)
		printk(KERN_EMERG "qlogicpti%d: reset_hardware loop timeout\n",
		       qpti->qpti_id);

	sbus_writew(HCCTRL_PAUSE, qpti->qregs + HCCTRL);
	set_sbus_cfg1(qpti);
	qlogicpti_enable_irqs(qpti);

	if (sbus_readw(qpti->qregs + RISC_PSR) & RISC_PSR_ULTRA) {
		qpti->ultra = 1;
		sbus_writew((RISC_MTREG_P0ULTRA | RISC_MTREG_P1ULTRA),
			    qpti->qregs + RISC_MTREG);
	} else {
		qpti->ultra = 0;
		sbus_writew((RISC_MTREG_P0DFLT | RISC_MTREG_P1DFLT),
			    qpti->qregs + RISC_MTREG);
	}

	/* reset adapter and per-device default values. */
	/* do it after finding out whether we're ultra mode capable */
	qlogicpti_set_hostdev_defaults(qpti);

	/* Release the RISC processor. */
	sbus_writew(HCCTRL_REL, qpti->qregs + HCCTRL);

	/* Get RISC to start executing the firmware code. */
	param[0] = MBOX_EXEC_FIRMWARE;
	param[1] = risc_code_addr;
	if (qlogicpti_mbox_command(qpti, param, 1)) {
		printk(KERN_EMERG "qlogicpti%d: Cannot execute ISP firmware.\n",
		       qpti->qpti_id);
		spin_unlock_irqrestore(host->host_lock, flags);
		return 1;
	}

	/* Set initiator scsi ID. */
	param[0] = MBOX_SET_INIT_SCSI_ID;
	param[1] = qpti->host_param.initiator_scsi_id;
	if (qlogicpti_mbox_command(qpti, param, 1) ||
	   (param[0] != MBOX_COMMAND_COMPLETE)) {
		printk(KERN_EMERG "qlogicpti%d: Cannot set initiator SCSI ID.\n",
		       qpti->qpti_id);
		spin_unlock_irqrestore(host->host_lock, flags);
		return 1;
	}

	/* Initialize state of the queues, both hw and sw. */
	qpti->req_in_ptr = qpti->res_out_ptr = 0;

	param[0] = MBOX_INIT_RES_QUEUE;
	param[1] = RES_QUEUE_LEN + 1;
	param[2] = (u_short) (qpti->res_dvma >> 16);
	param[3] = (u_short) (qpti->res_dvma & 0xffff);
	param[4] = param[5] = 0;
	if (qlogicpti_mbox_command(qpti, param, 1)) {
		printk(KERN_EMERG "qlogicpti%d: Cannot init response queue.\n",
		       qpti->qpti_id);
		spin_unlock_irqrestore(host->host_lock, flags);
		return 1;
	}

	param[0] = MBOX_INIT_REQ_QUEUE;
	param[1] = QLOGICPTI_REQ_QUEUE_LEN + 1;
	param[2] = (u_short) (qpti->req_dvma >> 16);
	param[3] = (u_short) (qpti->req_dvma & 0xffff);
	param[4] = param[5] = 0;
	if (qlogicpti_mbox_command(qpti, param, 1)) {
		printk(KERN_EMERG "qlogicpti%d: Cannot init request queue.\n",
		       qpti->qpti_id);
		spin_unlock_irqrestore(host->host_lock, flags);
		return 1;
	}

	param[0] = MBOX_SET_RETRY_COUNT;
	param[1] = qpti->host_param.retry_count;
	param[2] = qpti->host_param.retry_delay;
	qlogicpti_mbox_command(qpti, param, 0);

	param[0] = MBOX_SET_TAG_AGE_LIMIT;
	param[1] = qpti->host_param.tag_aging;
	qlogicpti_mbox_command(qpti, param, 0);

	for (i = 0; i < MAX_TARGETS; i++) {
		param[0] = MBOX_GET_DEV_QUEUE_PARAMS;
		param[1] = (i << 8);
		qlogicpti_mbox_command(qpti, param, 0);
	}

	param[0] = MBOX_GET_FIRMWARE_STATUS;
	qlogicpti_mbox_command(qpti, param, 0);

	param[0] = MBOX_SET_SELECT_TIMEOUT;
	param[1] = qpti->host_param.selection_timeout;
	qlogicpti_mbox_command(qpti, param, 0);

	for (i = 0; i < MAX_TARGETS; i++) {
		param[0] = MBOX_SET_TARGET_PARAMS;
		param[1] = (i << 8);
		param[2] = (qpti->dev_param[i].device_flags << 8);
		/*
		 * Since we're now loading 1.31 f/w, force narrow/async.
		 */
		param[2] |= 0xc0;
		param[3] = 0;	/* no offset, we do not have sync mode yet */
		qlogicpti_mbox_command(qpti, param, 0);
	}

	/*
	 * Always (sigh) do an initial bus reset (kicks f/w).
	 */
	param[0] = MBOX_BUS_RESET;
	param[1] = qpti->host_param.bus_reset_delay;
	qlogicpti_mbox_command(qpti, param, 0);
	qpti->send_marker = 1;

	spin_unlock_irqrestore(host->host_lock, flags);
	return 0;
}

#define PTI_RESET_LIMIT 400

static int __devinit qlogicpti_load_firmware(struct qlogicpti *qpti)
{
	struct Scsi_Host *host = qpti->qhost;
	unsigned short csum = 0;
	unsigned short param[6];
	unsigned short *risc_code, risc_code_addr, risc_code_length;
	unsigned long flags;
	int i, timeout;

	risc_code = &sbus_risc_code01[0];
	risc_code_addr = 0x1000;	/* all f/w modules load at 0x1000 */
	risc_code_length = sbus_risc_code_length01;

	spin_lock_irqsave(host->host_lock, flags);

	/* Verify the checksum twice, one before loading it, and once
	 * afterwards via the mailbox commands.
	 */
	for (i = 0; i < risc_code_length; i++)
		csum += risc_code[i];
	if (csum) {
		spin_unlock_irqrestore(host->host_lock, flags);
		printk(KERN_EMERG "qlogicpti%d: Aieee, firmware checksum failed!",
		       qpti->qpti_id);
		return 1;
	}		
	sbus_writew(SBUS_CTRL_RESET, qpti->qregs + SBUS_CTRL);
	sbus_writew((DMA_CTRL_CCLEAR | DMA_CTRL_CIRQ), qpti->qregs + CMD_DMA_CTRL);
	sbus_writew((DMA_CTRL_CCLEAR | DMA_CTRL_CIRQ), qpti->qregs + DATA_DMA_CTRL);
	timeout = PTI_RESET_LIMIT;
	while (--timeout && (sbus_readw(qpti->qregs + SBUS_CTRL) & SBUS_CTRL_RESET))
		udelay(20);
	if (!timeout) {
		spin_unlock_irqrestore(host->host_lock, flags);
		printk(KERN_EMERG "qlogicpti%d: Cannot reset the ISP.", qpti->qpti_id);
		return 1;
	}

	sbus_writew(HCCTRL_RESET, qpti->qregs + HCCTRL);
	mdelay(1);

	sbus_writew((SBUS_CTRL_GENAB | SBUS_CTRL_ERIRQ), qpti->qregs + SBUS_CTRL);
	set_sbus_cfg1(qpti);
	sbus_writew(0, qpti->qregs + SBUS_SEMAPHORE);

	if (sbus_readw(qpti->qregs + RISC_PSR) & RISC_PSR_ULTRA) {
		qpti->ultra = 1;
		sbus_writew((RISC_MTREG_P0ULTRA | RISC_MTREG_P1ULTRA),
			    qpti->qregs + RISC_MTREG);
	} else {
		qpti->ultra = 0;
		sbus_writew((RISC_MTREG_P0DFLT | RISC_MTREG_P1DFLT),
			    qpti->qregs + RISC_MTREG);
	}

	sbus_writew(HCCTRL_REL, qpti->qregs + HCCTRL);

	/* Pin lines are only stable while RISC is paused. */
	sbus_writew(HCCTRL_PAUSE, qpti->qregs + HCCTRL);
	if (sbus_readw(qpti->qregs + CPU_PDIFF) & CPU_PDIFF_MODE)
		qpti->differential = 1;
	else
		qpti->differential = 0;
	sbus_writew(HCCTRL_REL, qpti->qregs + HCCTRL);

	/* This shouldn't be necessary- we've reset things so we should be
	   running from the ROM now.. */

	param[0] = MBOX_STOP_FIRMWARE;
	param[1] = param[2] = param[3] = param[4] = param[5] = 0;
	if (qlogicpti_mbox_command(qpti, param, 1)) {
		printk(KERN_EMERG "qlogicpti%d: Cannot stop firmware for reload.\n",
		       qpti->qpti_id);
		spin_unlock_irqrestore(host->host_lock, flags);
		return 1;
	}		

	/* Load it up.. */
	for (i = 0; i < risc_code_length; i++) {
		param[0] = MBOX_WRITE_RAM_WORD;
		param[1] = risc_code_addr + i;
		param[2] = risc_code[i];
		if (qlogicpti_mbox_command(qpti, param, 1) ||
		    param[0] != MBOX_COMMAND_COMPLETE) {
			printk("qlogicpti%d: Firmware dload failed, I'm bolixed!\n",
			       qpti->qpti_id);
			spin_unlock_irqrestore(host->host_lock, flags);
			return 1;
		}
	}

	/* Reset the ISP again. */
	sbus_writew(HCCTRL_RESET, qpti->qregs + HCCTRL);
	mdelay(1);

	qlogicpti_enable_irqs(qpti);
	sbus_writew(0, qpti->qregs + SBUS_SEMAPHORE);
	sbus_writew(HCCTRL_REL, qpti->qregs + HCCTRL);

	/* Ask ISP to verify the checksum of the new code. */
	param[0] = MBOX_VERIFY_CHECKSUM;
	param[1] = risc_code_addr;
	if (qlogicpti_mbox_command(qpti, param, 1) ||
	    (param[0] != MBOX_COMMAND_COMPLETE)) {
		printk(KERN_EMERG "qlogicpti%d: New firmware csum failure!\n",
		       qpti->qpti_id);
		spin_unlock_irqrestore(host->host_lock, flags);
		return 1;
	}

	/* Start using newly downloaded firmware. */
	param[0] = MBOX_EXEC_FIRMWARE;
	param[1] = risc_code_addr;
	qlogicpti_mbox_command(qpti, param, 1);

	param[0] = MBOX_ABOUT_FIRMWARE;
	if (qlogicpti_mbox_command(qpti, param, 1) ||
	    (param[0] != MBOX_COMMAND_COMPLETE)) {
		printk(KERN_EMERG "qlogicpti%d: AboutFirmware cmd fails.\n",
		       qpti->qpti_id);
		spin_unlock_irqrestore(host->host_lock, flags);
		return 1;
	}

	/* Snag the major and minor revisions from the result. */
	qpti->fware_majrev = param[1];
	qpti->fware_minrev = param[2];
	qpti->fware_micrev = param[3];

	/* Set the clock rate */
	param[0] = MBOX_SET_CLOCK_RATE;
	param[1] = qpti->clock;
	if (qlogicpti_mbox_command(qpti, param, 1) ||
	    (param[0] != MBOX_COMMAND_COMPLETE)) {
		printk(KERN_EMERG "qlogicpti%d: could not set clock rate.\n",
		       qpti->qpti_id);
		spin_unlock_irqrestore(host->host_lock, flags);
		return 1;
	}

	if (qpti->is_pti != 0) {
		/* Load scsi initiator ID and interrupt level into sbus static ram. */
		param[0] = MBOX_WRITE_RAM_WORD;
		param[1] = 0xff80;
		param[2] = (unsigned short) qpti->scsi_id;
		qlogicpti_mbox_command(qpti, param, 1);

		param[0] = MBOX_WRITE_RAM_WORD;
		param[1] = 0xff00;
		param[2] = (unsigned short) 3;
		qlogicpti_mbox_command(qpti, param, 1);
	}

	spin_unlock_irqrestore(host->host_lock, flags);
	return 0;
}

static int qlogicpti_verify_tmon(struct qlogicpti *qpti)
{
	int curstat = sbus_readb(qpti->sreg);

	curstat &= 0xf0;
	if (!(curstat & SREG_FUSE) && (qpti->swsreg & SREG_FUSE))
		printk("qlogicpti%d: Fuse returned to normal state.\n", qpti->qpti_id);
	if (!(curstat & SREG_TPOWER) && (qpti->swsreg & SREG_TPOWER))
		printk("qlogicpti%d: termpwr back to normal state.\n", qpti->qpti_id);
	if (curstat != qpti->swsreg) {
		int error = 0;
		if (curstat & SREG_FUSE) {
			error++;
			printk("qlogicpti%d: Fuse is open!\n", qpti->qpti_id);
		}
		if (curstat & SREG_TPOWER) {
			error++;
			printk("qlogicpti%d: termpwr failure\n", qpti->qpti_id);
		}
		if (qpti->differential &&
		    (curstat & SREG_DSENSE) != SREG_DSENSE) {
			error++;
			printk("qlogicpti%d: You have a single ended device on a "
			       "differential bus!  Please fix!\n", qpti->qpti_id);
		}
		qpti->swsreg = curstat;
		return error;
	}
	return 0;
}

static irqreturn_t qpti_intr(int irq, void *dev_id);

static void __devinit qpti_chain_add(struct qlogicpti *qpti)
{
	spin_lock_irq(&qptichain_lock);
	if (qptichain != NULL) {
		struct qlogicpti *qlink = qptichain;

		while(qlink->next)
			qlink = qlink->next;
		qlink->next = qpti;
	} else {
		qptichain = qpti;
	}
	qpti->next = NULL;
	spin_unlock_irq(&qptichain_lock);
}

static void __devexit qpti_chain_del(struct qlogicpti *qpti)
{
	spin_lock_irq(&qptichain_lock);
	if (qptichain == qpti) {
		qptichain = qpti->next;
	} else {
		struct qlogicpti *qlink = qptichain;
		while(qlink->next != qpti)
			qlink = qlink->next;
		qlink->next = qpti->next;
	}
	qpti->next = NULL;
	spin_unlock_irq(&qptichain_lock);
}

static int __devinit qpti_map_regs(struct qlogicpti *qpti)
{
	struct sbus_dev *sdev = qpti->sdev;

	qpti->qregs = sbus_ioremap(&sdev->resource[0], 0,
				   sdev->reg_addrs[0].reg_size,
				   "PTI Qlogic/ISP");
	if (!qpti->qregs) {
		printk("PTI: Qlogic/ISP registers are unmappable\n");
		return -1;
	}
	if (qpti->is_pti) {
		qpti->sreg = sbus_ioremap(&sdev->resource[0], (16 * 4096),
					  sizeof(unsigned char),
					  "PTI Qlogic/ISP statreg");
		if (!qpti->sreg) {
			printk("PTI: Qlogic/ISP status register is unmappable\n");
			return -1;
		}
	}
	return 0;
}

static int __devinit qpti_register_irq(struct qlogicpti *qpti)
{
	struct sbus_dev *sdev = qpti->sdev;

	qpti->qhost->irq = qpti->irq = sdev->irqs[0];

	/* We used to try various overly-clever things to
	 * reduce the interrupt processing overhead on
	 * sun4c/sun4m when multiple PTI's shared the
	 * same IRQ.  It was too complex and messy to
	 * sanely maintain.
	 */
	if (request_irq(qpti->irq, qpti_intr,
			IRQF_SHARED, "Qlogic/PTI", qpti))
		goto fail;

	printk("qlogicpti%d: IRQ %d ", qpti->qpti_id, qpti->irq);

	return 0;

fail:
	printk("qlogicpti%d: Cannot acquire irq line\n", qpti->qpti_id);
	return -1;
}

static void __devinit qpti_get_scsi_id(struct qlogicpti *qpti)
{
	qpti->scsi_id = prom_getintdefault(qpti->prom_node,
					   "initiator-id",
					   -1);
	if (qpti->scsi_id == -1)
		qpti->scsi_id = prom_getintdefault(qpti->prom_node,
						   "scsi-initiator-id",
						   -1);
	if (qpti->scsi_id == -1)
		qpti->scsi_id =
			prom_getintdefault(qpti->sdev->bus->prom_node,
					   "scsi-initiator-id", 7);
	qpti->qhost->this_id = qpti->scsi_id;
	qpti->qhost->max_sectors = 64;

	printk("SCSI ID %d ", qpti->scsi_id);
}

static void qpti_get_bursts(struct qlogicpti *qpti)
{
	struct sbus_dev *sdev = qpti->sdev;
	u8 bursts, bmask;

	bursts = prom_getintdefault(qpti->prom_node, "burst-sizes", 0xff);
	bmask = prom_getintdefault(sdev->bus->prom_node,
				   "burst-sizes", 0xff);
	if (bmask != 0xff)
		bursts &= bmask;
	if (bursts == 0xff ||
	    (bursts & DMA_BURST16) == 0 ||
	    (bursts & DMA_BURST32) == 0)
		bursts = (DMA_BURST32 - 1);

	qpti->bursts = bursts;
}

static void qpti_get_clock(struct qlogicpti *qpti)
{
	unsigned int cfreq;

	/* Check for what the clock input to this card is.
	 * Default to 40Mhz.
	 */
	cfreq = prom_getintdefault(qpti->prom_node,"clock-frequency",40000000);
	qpti->clock = (cfreq + 500000)/1000000;
	if (qpti->clock == 0) /* bullshit */
		qpti->clock = 40;
}

/* The request and response queues must each be aligned
 * on a page boundary.
 */
static int __devinit qpti_map_queues(struct qlogicpti *qpti)
{
	struct sbus_dev *sdev = qpti->sdev;

#define QSIZE(entries)	(((entries) + 1) * QUEUE_ENTRY_LEN)
	qpti->res_cpu = sbus_alloc_consistent(sdev,
					      QSIZE(RES_QUEUE_LEN),
					      &qpti->res_dvma);
	if (qpti->res_cpu == NULL ||
	    qpti->res_dvma == 0) {
		printk("QPTI: Cannot map response queue.\n");
		return -1;
	}

	qpti->req_cpu = sbus_alloc_consistent(sdev,
					      QSIZE(QLOGICPTI_REQ_QUEUE_LEN),
					      &qpti->req_dvma);
	if (qpti->req_cpu == NULL ||
	    qpti->req_dvma == 0) {
		sbus_free_consistent(sdev, QSIZE(RES_QUEUE_LEN),
				     qpti->res_cpu, qpti->res_dvma);
		printk("QPTI: Cannot map request queue.\n");
		return -1;
	}
	memset(qpti->res_cpu, 0, QSIZE(RES_QUEUE_LEN));
	memset(qpti->req_cpu, 0, QSIZE(QLOGICPTI_REQ_QUEUE_LEN));
	return 0;
}

const char *qlogicpti_info(struct Scsi_Host *host)
{
	static char buf[80];
	struct qlogicpti *qpti = (struct qlogicpti *) host->hostdata;

	sprintf(buf, "PTI Qlogic,ISP SBUS SCSI irq %d regs at %p",
		qpti->qhost->irq, qpti->qregs);
	return buf;
}

/* I am a certified frobtronicist. */
static inline void marker_frob(struct Command_Entry *cmd)
{
	struct Marker_Entry *marker = (struct Marker_Entry *) cmd;

	memset(marker, 0, sizeof(struct Marker_Entry));
	marker->hdr.entry_cnt = 1;
	marker->hdr.entry_type = ENTRY_MARKER;
	marker->modifier = SYNC_ALL;
	marker->rsvd = 0;
}

static inline void cmd_frob(struct Command_Entry *cmd, struct scsi_cmnd *Cmnd,
			    struct qlogicpti *qpti)
{
	memset(cmd, 0, sizeof(struct Command_Entry));
	cmd->hdr.entry_cnt = 1;
	cmd->hdr.entry_type = ENTRY_COMMAND;
	cmd->target_id = Cmnd->device->id;
	cmd->target_lun = Cmnd->device->lun;
	cmd->cdb_length = Cmnd->cmd_len;
	cmd->control_flags = 0;
	if (Cmnd->device->tagged_supported) {
		if (qpti->cmd_count[Cmnd->device->id] == 0)
			qpti->tag_ages[Cmnd->device->id] = jiffies;
		if (time_after(jiffies, qpti->tag_ages[Cmnd->device->id] + (5*HZ))) {
			cmd->control_flags = CFLAG_ORDERED_TAG;
			qpti->tag_ages[Cmnd->device->id] = jiffies;
		} else
			cmd->control_flags = CFLAG_SIMPLE_TAG;
	}
	if ((Cmnd->cmnd[0] == WRITE_6) ||
	    (Cmnd->cmnd[0] == WRITE_10) ||
	    (Cmnd->cmnd[0] == WRITE_12))
		cmd->control_flags |= CFLAG_WRITE;
	else
		cmd->control_flags |= CFLAG_READ;
	cmd->time_out = 30;
	memcpy(cmd->cdb, Cmnd->cmnd, Cmnd->cmd_len);
}

/* Do it to it baby. */
static inline int load_cmd(struct scsi_cmnd *Cmnd, struct Command_Entry *cmd,
			   struct qlogicpti *qpti, u_int in_ptr, u_int out_ptr)
{
	struct dataseg *ds;
	struct scatterlist *sg, *s;
	int i, n;

	if (scsi_bufflen(Cmnd)) {
		int sg_count;

		sg = scsi_sglist(Cmnd);
		sg_count = sbus_map_sg(qpti->sdev, sg, scsi_sg_count(Cmnd),
		                                      Cmnd->sc_data_direction);

		ds = cmd->dataseg;
		cmd->segment_cnt = sg_count;

		/* Fill in first four sg entries: */
		n = sg_count;
		if (n > 4)
			n = 4;
		for_each_sg(sg, s, n, i) {
			ds[i].d_base = sg_dma_address(s);
			ds[i].d_count = sg_dma_len(s);
		}
		sg_count -= 4;
		sg = s;
		while (sg_count > 0) {
			struct Continuation_Entry *cont;

			++cmd->hdr.entry_cnt;
			cont = (struct Continuation_Entry *) &qpti->req_cpu[in_ptr];
			in_ptr = NEXT_REQ_PTR(in_ptr);
			if (in_ptr == out_ptr)
				return -1;

			cont->hdr.entry_type = ENTRY_CONTINUATION;
			cont->hdr.entry_cnt = 0;
			cont->hdr.sys_def_1 = 0;
			cont->hdr.flags = 0;
			cont->reserved = 0;
			ds = cont->dataseg;
			n = sg_count;
			if (n > 7)
				n = 7;
			for_each_sg(sg, s, n, i) {
				ds[i].d_base = sg_dma_address(s);
				ds[i].d_count = sg_dma_len(s);
			}
			sg_count -= n;
			sg = s;
		}
	} else {
		cmd->dataseg[0].d_base = 0;
		cmd->dataseg[0].d_count = 0;
		cmd->segment_cnt = 1; /* Shouldn't this be 0? */
	}

	/* Committed, record Scsi_Cmd so we can find it later. */
	cmd->handle = in_ptr;
	qpti->cmd_slots[in_ptr] = Cmnd;

	qpti->cmd_count[Cmnd->device->id]++;
	sbus_writew(in_ptr, qpti->qregs + MBOX4);
	qpti->req_in_ptr = in_ptr;

	return in_ptr;
}

static inline void update_can_queue(struct Scsi_Host *host, u_int in_ptr, u_int out_ptr)
{
	/* Temporary workaround until bug is found and fixed (one bug has been found
	   already, but fixing it makes things even worse) -jj */
	int num_free = QLOGICPTI_REQ_QUEUE_LEN - REQ_QUEUE_DEPTH(in_ptr, out_ptr) - 64;
	host->can_queue = host->host_busy + num_free;
	host->sg_tablesize = QLOGICPTI_MAX_SG(num_free);
}

static int qlogicpti_slave_configure(struct scsi_device *sdev)
{
	struct qlogicpti *qpti = shost_priv(sdev->host);
	int tgt = sdev->id;
	u_short param[6];

	/* tags handled in midlayer */
	/* enable sync mode? */
	if (sdev->sdtr) {
		qpti->dev_param[tgt].device_flags |= 0x10;
	} else {
		qpti->dev_param[tgt].synchronous_offset = 0;
		qpti->dev_param[tgt].synchronous_period = 0;
	}
	/* are we wide capable? */
	if (sdev->wdtr)
		qpti->dev_param[tgt].device_flags |= 0x20;

	param[0] = MBOX_SET_TARGET_PARAMS;
	param[1] = (tgt << 8);
	param[2] = (qpti->dev_param[tgt].device_flags << 8);
	if (qpti->dev_param[tgt].device_flags & 0x10) {
		param[3] = (qpti->dev_param[tgt].synchronous_offset << 8) |
			qpti->dev_param[tgt].synchronous_period;
	} else {
		param[3] = 0;
	}
	qlogicpti_mbox_command(qpti, param, 0);
	return 0;
}

/*
 * The middle SCSI layer ensures that queuecommand never gets invoked
 * concurrently with itself or the interrupt handler (though the
 * interrupt handler may call this routine as part of
 * request-completion handling).
 *
 * "This code must fly." -davem
 */
static int qlogicpti_queuecommand(struct scsi_cmnd *Cmnd, void (*done)(struct scsi_cmnd *))
{
	struct Scsi_Host *host = Cmnd->device->host;
	struct qlogicpti *qpti = (struct qlogicpti *) host->hostdata;
	struct Command_Entry *cmd;
	u_int out_ptr;
	int in_ptr;

	Cmnd->scsi_done = done;

	in_ptr = qpti->req_in_ptr;
	cmd = (struct Command_Entry *) &qpti->req_cpu[in_ptr];
	out_ptr = sbus_readw(qpti->qregs + MBOX4);
	in_ptr = NEXT_REQ_PTR(in_ptr);
	if (in_ptr == out_ptr)
		goto toss_command;

	if (qpti->send_marker) {
		marker_frob(cmd);
		qpti->send_marker = 0;
		if (NEXT_REQ_PTR(in_ptr) == out_ptr) {
			sbus_writew(in_ptr, qpti->qregs + MBOX4);
			qpti->req_in_ptr = in_ptr;
			goto toss_command;
		}
		cmd = (struct Command_Entry *) &qpti->req_cpu[in_ptr];
		in_ptr = NEXT_REQ_PTR(in_ptr);
	}
	cmd_frob(cmd, Cmnd, qpti);
	if ((in_ptr = load_cmd(Cmnd, cmd, qpti, in_ptr, out_ptr)) == -1)
		goto toss_command;

	update_can_queue(host, in_ptr, out_ptr);

	return 0;

toss_command:
	printk(KERN_EMERG "qlogicpti%d: request queue overflow\n",
	       qpti->qpti_id);

	/* Unfortunately, unless you use the new EH code, which
	 * we don't, the midlayer will ignore the return value,
	 * which is insane.  We pick up the pieces like this.
	 */
	Cmnd->result = DID_BUS_BUSY;
	done(Cmnd);
	return 1;
}

static int qlogicpti_return_status(struct Status_Entry *sts, int id)
{
	int host_status = DID_ERROR;

	switch (sts->completion_status) {
	      case CS_COMPLETE:
		host_status = DID_OK;
		break;
	      case CS_INCOMPLETE:
		if (!(sts->state_flags & SF_GOT_BUS))
			host_status = DID_NO_CONNECT;
		else if (!(sts->state_flags & SF_GOT_TARGET))
			host_status = DID_BAD_TARGET;
		else if (!(sts->state_flags & SF_SENT_CDB))
			host_status = DID_ERROR;
		else if (!(sts->state_flags & SF_TRANSFERRED_DATA))
			host_status = DID_ERROR;
		else if (!(sts->state_flags & SF_GOT_STATUS))
			host_status = DID_ERROR;
		else if (!(sts->state_flags & SF_GOT_SENSE))
			host_status = DID_ERROR;
		break;
	      case CS_DMA_ERROR:
	      case CS_TRANSPORT_ERROR:
		host_status = DID_ERROR;
		break;
	      case CS_RESET_OCCURRED:
	      case CS_BUS_RESET:
		host_status = DID_RESET;
		break;
	      case CS_ABORTED:
		host_status = DID_ABORT;
		break;
	      case CS_TIMEOUT:
		host_status = DID_TIME_OUT;
		break;
	      case CS_DATA_OVERRUN:
	      case CS_COMMAND_OVERRUN:
	      case CS_STATUS_OVERRUN:
	      case CS_BAD_MESSAGE:
	      case CS_NO_MESSAGE_OUT:
	      case CS_EXT_ID_FAILED:
	      case CS_IDE_MSG_FAILED:
	      case CS_ABORT_MSG_FAILED:
	      case CS_NOP_MSG_FAILED:
	      case CS_PARITY_ERROR_MSG_FAILED:
	      case CS_DEVICE_RESET_MSG_FAILED:
	      case CS_ID_MSG_FAILED:
	      case CS_UNEXP_BUS_FREE:
		host_status = DID_ERROR;
		break;
	      case CS_DATA_UNDERRUN:
		host_status = DID_OK;
		break;
	      default:
		printk(KERN_EMERG "qlogicpti%d: unknown completion status 0x%04x\n",
		       id, sts->completion_status);
		host_status = DID_ERROR;
		break;
	}

	return (sts->scsi_status & STATUS_MASK) | (host_status << 16);
}

static struct scsi_cmnd *qlogicpti_intr_handler(struct qlogicpti *qpti)
{
	struct scsi_cmnd *Cmnd, *done_queue = NULL;
	struct Status_Entry *sts;
	u_int in_ptr, out_ptr;

	if (!(sbus_readw(qpti->qregs + SBUS_STAT) & SBUS_STAT_RINT))
		return NULL;
		
	in_ptr = sbus_readw(qpti->qregs + MBOX5);
	sbus_writew(HCCTRL_CRIRQ, qpti->qregs + HCCTRL);
	if (sbus_readw(qpti->qregs + SBUS_SEMAPHORE) & SBUS_SEMAPHORE_LCK) {
		switch (sbus_readw(qpti->qregs + MBOX0)) {
		case ASYNC_SCSI_BUS_RESET:
		case EXECUTION_TIMEOUT_RESET:
			qpti->send_marker = 1;
			break;
		case INVALID_COMMAND:
		case HOST_INTERFACE_ERROR:
		case COMMAND_ERROR:
		case COMMAND_PARAM_ERROR:
			break;
		};
		sbus_writew(0, qpti->qregs + SBUS_SEMAPHORE);
	}

	/* This looks like a network driver! */
	out_ptr = qpti->res_out_ptr;
	while (out_ptr != in_ptr) {
		u_int cmd_slot;

		sts = (struct Status_Entry *) &qpti->res_cpu[out_ptr];
		out_ptr = NEXT_RES_PTR(out_ptr);

		/* We store an index in the handle, not the pointer in
		 * some form.  This avoids problems due to the fact
		 * that the handle provided is only 32-bits. -DaveM
		 */
		cmd_slot = sts->handle;
		Cmnd = qpti->cmd_slots[cmd_slot];
		qpti->cmd_slots[cmd_slot] = NULL;

		if (sts->completion_status == CS_RESET_OCCURRED ||
		    sts->completion_status == CS_ABORTED ||
		    (sts->status_flags & STF_BUS_RESET))
			qpti->send_marker = 1;

		if (sts->state_flags & SF_GOT_SENSE)
			memcpy(Cmnd->sense_buffer, sts->req_sense_data,
			       SCSI_SENSE_BUFFERSIZE);

		if (sts->hdr.entry_type == ENTRY_STATUS)
			Cmnd->result =
			    qlogicpti_return_status(sts, qpti->qpti_id);
		else
			Cmnd->result = DID_ERROR << 16;

		if (scsi_bufflen(Cmnd))
			sbus_unmap_sg(qpti->sdev,
				      scsi_sglist(Cmnd), scsi_sg_count(Cmnd),
				      Cmnd->sc_data_direction);

		qpti->cmd_count[Cmnd->device->id]--;
		sbus_writew(out_ptr, qpti->qregs + MBOX5);
		Cmnd->host_scribble = (unsigned char *) done_queue;
		done_queue = Cmnd;
	}
	qpti->res_out_ptr = out_ptr;

	return done_queue;
}

static irqreturn_t qpti_intr(int irq, void *dev_id)
{
	struct qlogicpti *qpti = dev_id;
	unsigned long flags;
	struct scsi_cmnd *dq;

	spin_lock_irqsave(qpti->qhost->host_lock, flags);
	dq = qlogicpti_intr_handler(qpti);

	if (dq != NULL) {
		do {
			struct scsi_cmnd *next;

			next = (struct scsi_cmnd *) dq->host_scribble;
			dq->scsi_done(dq);
			dq = next;
		} while (dq != NULL);
	}
	spin_unlock_irqrestore(qpti->qhost->host_lock, flags);

	return IRQ_HANDLED;
}

static int qlogicpti_abort(struct scsi_cmnd *Cmnd)
{
	u_short param[6];
	struct Scsi_Host *host = Cmnd->device->host;
	struct qlogicpti *qpti = (struct qlogicpti *) host->hostdata;
	int return_status = SUCCESS;
	u32 cmd_cookie;
	int i;

	printk(KERN_WARNING "qlogicpti%d: Aborting cmd for tgt[%d] lun[%d]\n",
	       qpti->qpti_id, (int)Cmnd->device->id, (int)Cmnd->device->lun);

	qlogicpti_disable_irqs(qpti);

	/* Find the 32-bit cookie we gave to the firmware for
	 * this command.
	 */
	for (i = 0; i < QLOGICPTI_REQ_QUEUE_LEN + 1; i++)
		if (qpti->cmd_slots[i] == Cmnd)
			break;
	cmd_cookie = i;

	param[0] = MBOX_ABORT;
	param[1] = (((u_short) Cmnd->device->id) << 8) | Cmnd->device->lun;
	param[2] = cmd_cookie >> 16;
	param[3] = cmd_cookie & 0xffff;
	if (qlogicpti_mbox_command(qpti, param, 0) ||
	    (param[0] != MBOX_COMMAND_COMPLETE)) {
		printk(KERN_EMERG "qlogicpti%d: scsi abort failure: %x\n",
		       qpti->qpti_id, param[0]);
		return_status = FAILED;
	}

	qlogicpti_enable_irqs(qpti);

	return return_status;
}

static int qlogicpti_reset(struct scsi_cmnd *Cmnd)
{
	u_short param[6];
	struct Scsi_Host *host = Cmnd->device->host;
	struct qlogicpti *qpti = (struct qlogicpti *) host->hostdata;
	int return_status = SUCCESS;

	printk(KERN_WARNING "qlogicpti%d: Resetting SCSI bus!\n",
	       qpti->qpti_id);

	qlogicpti_disable_irqs(qpti);

	param[0] = MBOX_BUS_RESET;
	param[1] = qpti->host_param.bus_reset_delay;
	if (qlogicpti_mbox_command(qpti, param, 0) ||
	   (param[0] != MBOX_COMMAND_COMPLETE)) {
		printk(KERN_EMERG "qlogicisp%d: scsi bus reset failure: %x\n",
		       qpti->qpti_id, param[0]);
		return_status = FAILED;
	}

	qlogicpti_enable_irqs(qpti);

	return return_status;
}

static struct scsi_host_template qpti_template = {
	.module			= THIS_MODULE,
	.name			= "qlogicpti",
	.info			= qlogicpti_info,
	.queuecommand		= qlogicpti_queuecommand,
	.slave_configure	= qlogicpti_slave_configure,
	.eh_abort_handler	= qlogicpti_abort,
	.eh_bus_reset_handler	= qlogicpti_reset,
	.can_queue		= QLOGICPTI_REQ_QUEUE_LEN,
	.this_id		= 7,
	.sg_tablesize		= QLOGICPTI_MAX_SG(QLOGICPTI_REQ_QUEUE_LEN),
	.cmd_per_lun		= 1,
	.use_clustering		= ENABLE_CLUSTERING,
};

static int __devinit qpti_sbus_probe(struct of_device *dev, const struct of_device_id *match)
{
	static int nqptis;
	struct sbus_dev *sdev = to_sbus_device(&dev->dev);
	struct device_node *dp = dev->node;
	struct scsi_host_template *tpnt = match->data;
	struct Scsi_Host *host;
	struct qlogicpti *qpti;
	const char *fcode;

	/* Sometimes Antares cards come up not completely
	 * setup, and we get a report of a zero IRQ.
	 */
	if (sdev->irqs[0] == 0)
		return -ENODEV;

	host = scsi_host_alloc(tpnt, sizeof(struct qlogicpti));
	if (!host)
		return -ENOMEM;

	qpti = (struct qlogicpti *) host->hostdata;

	host->max_id = MAX_TARGETS;
	qpti->qhost = host;
	qpti->sdev = sdev;
	qpti->qpti_id = nqptis;
	qpti->prom_node = sdev->prom_node;
	strcpy(qpti->prom_name, sdev->ofdev.node->name);
	qpti->is_pti = strcmp(qpti->prom_name, "QLGC,isp");

	if (qpti_map_regs(qpti) < 0)
		goto fail_unlink;

	if (qpti_register_irq(qpti) < 0)
		goto fail_unmap_regs;

	qpti_get_scsi_id(qpti);
	qpti_get_bursts(qpti);
	qpti_get_clock(qpti);

	/* Clear out scsi_cmnd array. */
	memset(qpti->cmd_slots, 0, sizeof(qpti->cmd_slots));

	if (qpti_map_queues(qpti) < 0)
		goto fail_free_irq;

	/* Load the firmware. */
	if (qlogicpti_load_firmware(qpti))
		goto fail_unmap_queues;
	if (qpti->is_pti) {
		/* Check the PTI status reg. */
		if (qlogicpti_verify_tmon(qpti))
			goto fail_unmap_queues;
	}

	/* Reset the ISP and init res/req queues. */
	if (qlogicpti_reset_hardware(host))
		goto fail_unmap_queues;

	printk("(Firmware v%d.%d.%d)", qpti->fware_majrev,
	       qpti->fware_minrev, qpti->fware_micrev);

	fcode = of_get_property(dp, "isp-fcode", NULL);
	if (fcode && fcode[0])
		printk("(FCode %s)", fcode);
	if (of_find_property(dp, "differential", NULL) != NULL)
		qpti->differential = 1;
			
	printk("\nqlogicpti%d: [%s Wide, using %s interface]\n",
		qpti->qpti_id,
		(qpti->ultra ? "Ultra" : "Fast"),
		(qpti->differential ? "differential" : "single ended"));

	if (scsi_add_host(host, &dev->dev)) {
		printk("qlogicpti%d: Failed scsi_add_host\n", qpti->qpti_id);
		goto fail_unmap_queues;
	}

	dev_set_drvdata(&sdev->ofdev.dev, qpti);

	qpti_chain_add(qpti);

	scsi_scan_host(host);
	nqptis++;

	return 0;

fail_unmap_queues:
#define QSIZE(entries)	(((entries) + 1) * QUEUE_ENTRY_LEN)
	sbus_free_consistent(qpti->sdev,
			     QSIZE(RES_QUEUE_LEN),
			     qpti->res_cpu, qpti->res_dvma);
	sbus_free_consistent(qpti->sdev,
			     QSIZE(QLOGICPTI_REQ_QUEUE_LEN),
			     qpti->req_cpu, qpti->req_dvma);
#undef QSIZE

fail_unmap_regs:
	sbus_iounmap(qpti->qregs,
		     qpti->sdev->reg_addrs[0].reg_size);
	if (qpti->is_pti)
		sbus_iounmap(qpti->sreg, sizeof(unsigned char));

fail_free_irq:
	free_irq(qpti->irq, qpti);

fail_unlink:
	scsi_host_put(host);

	return -ENODEV;
}

static int __devexit qpti_sbus_remove(struct of_device *dev)
{
	struct qlogicpti *qpti = dev_get_drvdata(&dev->dev);

	qpti_chain_del(qpti);

	scsi_remove_host(qpti->qhost);

	/* Shut up the card. */
	sbus_writew(0, qpti->qregs + SBUS_CTRL);

	/* Free IRQ handler and unmap Qlogic,ISP and PTI status regs. */
	free_irq(qpti->irq, qpti);

#define QSIZE(entries)	(((entries) + 1) * QUEUE_ENTRY_LEN)
	sbus_free_consistent(qpti->sdev,
			     QSIZE(RES_QUEUE_LEN),
			     qpti->res_cpu, qpti->res_dvma);
	sbus_free_consistent(qpti->sdev,
			     QSIZE(QLOGICPTI_REQ_QUEUE_LEN),
			     qpti->req_cpu, qpti->req_dvma);
#undef QSIZE

	sbus_iounmap(qpti->qregs, qpti->sdev->reg_addrs[0].reg_size);
	if (qpti->is_pti)
		sbus_iounmap(qpti->sreg, sizeof(unsigned char));

	scsi_host_put(qpti->qhost);

	return 0;
}

static struct of_device_id qpti_match[] = {
	{
		.name = "ptisp",
		.data = &qpti_template,
	},
	{
		.name = "PTI,ptisp",
		.data = &qpti_template,
	},
	{
		.name = "QLGC,isp",
		.data = &qpti_template,
	},
	{
		.name = "SUNW,isp",
		.data = &qpti_template,
	},
	{},
};
MODULE_DEVICE_TABLE(of, qpti_match);

static struct of_platform_driver qpti_sbus_driver = {
	.name		= "qpti",
	.match_table	= qpti_match,
	.probe		= qpti_sbus_probe,
	.remove		= __devexit_p(qpti_sbus_remove),
};

static int __init qpti_init(void)
{
	return of_register_driver(&qpti_sbus_driver, &sbus_bus_type);
}

static void __exit qpti_exit(void)
{
	of_unregister_driver(&qpti_sbus_driver);
}

MODULE_DESCRIPTION("QlogicISP SBUS driver");
MODULE_AUTHOR("David S. Miller (davem@davemloft.net)");
MODULE_LICENSE("GPL");
MODULE_VERSION("2.0");

module_init(qpti_init);
module_exit(qpti_exit);
