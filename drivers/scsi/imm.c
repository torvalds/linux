/* imm.c   --  low level driver for the IOMEGA MatchMaker
 * parallel port SCSI host adapter.
 * 
 * (The IMM is the embedded controller in the ZIP Plus drive.)
 * 
 * My unoffical company acronym list is 21 pages long:
 *      FLA:    Four letter acronym with built in facility for
 *              future expansion to five letters.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/blkdev.h>
#include <linux/parport.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <asm/io.h>

#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>

/* The following #define is to avoid a clash with hosts.c */
#define IMM_PROBE_SPP   0x0001
#define IMM_PROBE_PS2   0x0002
#define IMM_PROBE_ECR   0x0010
#define IMM_PROBE_EPP17 0x0100
#define IMM_PROBE_EPP19 0x0200


typedef struct {
	struct pardevice *dev;	/* Parport device entry         */
	int base;		/* Actual port address          */
	int base_hi;		/* Hi Base address for ECP-ISA chipset */
	int mode;		/* Transfer mode                */
	struct scsi_cmnd *cur_cmd;	/* Current queued command       */
	struct delayed_work imm_tq;	/* Polling interrupt stuff       */
	unsigned long jstart;	/* Jiffies at start             */
	unsigned failed:1;	/* Failure flag                 */
	unsigned dp:1;		/* Data phase present           */
	unsigned rd:1;		/* Read data in data phase      */
	unsigned wanted:1;	/* Parport sharing busy flag    */
	wait_queue_head_t *waiting;
	struct Scsi_Host *host;
	struct list_head list;
} imm_struct;

static void imm_reset_pulse(unsigned int base);
static int device_check(imm_struct *dev);

#include "imm.h"

static inline imm_struct *imm_dev(struct Scsi_Host *host)
{
	return *(imm_struct **)&host->hostdata;
}

static DEFINE_SPINLOCK(arbitration_lock);

static void got_it(imm_struct *dev)
{
	dev->base = dev->dev->port->base;
	if (dev->cur_cmd)
		dev->cur_cmd->SCp.phase = 1;
	else
		wake_up(dev->waiting);
}

static void imm_wakeup(void *ref)
{
	imm_struct *dev = (imm_struct *) ref;
	unsigned long flags;

	spin_lock_irqsave(&arbitration_lock, flags);
	if (dev->wanted) {
		parport_claim(dev->dev);
		got_it(dev);
		dev->wanted = 0;
	}
	spin_unlock_irqrestore(&arbitration_lock, flags);
}

static int imm_pb_claim(imm_struct *dev)
{
	unsigned long flags;
	int res = 1;
	spin_lock_irqsave(&arbitration_lock, flags);
	if (parport_claim(dev->dev) == 0) {
		got_it(dev);
		res = 0;
	}
	dev->wanted = res;
	spin_unlock_irqrestore(&arbitration_lock, flags);
	return res;
}

static void imm_pb_dismiss(imm_struct *dev)
{
	unsigned long flags;
	int wanted;
	spin_lock_irqsave(&arbitration_lock, flags);
	wanted = dev->wanted;
	dev->wanted = 0;
	spin_unlock_irqrestore(&arbitration_lock, flags);
	if (!wanted)
		parport_release(dev->dev);
}

static inline void imm_pb_release(imm_struct *dev)
{
	parport_release(dev->dev);
}

/* This is to give the imm driver a way to modify the timings (and other
 * parameters) by writing to the /proc/scsi/imm/0 file.
 * Very simple method really... (Too simple, no error checking :( )
 * Reason: Kernel hackers HATE having to unload and reload modules for
 * testing...
 * Also gives a method to use a script to obtain optimum timings (TODO)
 */
static inline int imm_proc_write(imm_struct *dev, char *buffer, int length)
{
	unsigned long x;

	if ((length > 5) && (strncmp(buffer, "mode=", 5) == 0)) {
		x = simple_strtoul(buffer + 5, NULL, 0);
		dev->mode = x;
		return length;
	}
	printk("imm /proc: invalid variable\n");
	return (-EINVAL);
}

static int imm_proc_info(struct Scsi_Host *host, char *buffer, char **start,
			off_t offset, int length, int inout)
{
	imm_struct *dev = imm_dev(host);
	int len = 0;

	if (inout)
		return imm_proc_write(dev, buffer, length);

	len += sprintf(buffer + len, "Version : %s\n", IMM_VERSION);
	len +=
	    sprintf(buffer + len, "Parport : %s\n",
		    dev->dev->port->name);
	len +=
	    sprintf(buffer + len, "Mode    : %s\n",
		    IMM_MODE_STRING[dev->mode]);

	/* Request for beyond end of buffer */
	if (offset > len)
		return 0;

	*start = buffer + offset;
	len -= offset;
	if (len > length)
		len = length;
	return len;
}

#if IMM_DEBUG > 0
#define imm_fail(x,y) printk("imm: imm_fail(%i) from %s at line %d\n",\
	   y, __FUNCTION__, __LINE__); imm_fail_func(x,y);
static inline void
imm_fail_func(imm_struct *dev, int error_code)
#else
static inline void
imm_fail(imm_struct *dev, int error_code)
#endif
{
	/* If we fail a device then we trash status / message bytes */
	if (dev->cur_cmd) {
		dev->cur_cmd->result = error_code << 16;
		dev->failed = 1;
	}
}

/*
 * Wait for the high bit to be set.
 * 
 * In principle, this could be tied to an interrupt, but the adapter
 * doesn't appear to be designed to support interrupts.  We spin on
 * the 0x80 ready bit. 
 */
static unsigned char imm_wait(imm_struct *dev)
{
	int k;
	unsigned short ppb = dev->base;
	unsigned char r;

	w_ctr(ppb, 0x0c);

	k = IMM_SPIN_TMO;
	do {
		r = r_str(ppb);
		k--;
		udelay(1);
	}
	while (!(r & 0x80) && (k));

	/*
	 * STR register (LPT base+1) to SCSI mapping:
	 *
	 * STR      imm     imm
	 * ===================================
	 * 0x80     S_REQ   S_REQ
	 * 0x40     !S_BSY  (????)
	 * 0x20     !S_CD   !S_CD
	 * 0x10     !S_IO   !S_IO
	 * 0x08     (????)  !S_BSY
	 *
	 * imm      imm     meaning
	 * ==================================
	 * 0xf0     0xb8    Bit mask
	 * 0xc0     0x88    ZIP wants more data
	 * 0xd0     0x98    ZIP wants to send more data
	 * 0xe0     0xa8    ZIP is expecting SCSI command data
	 * 0xf0     0xb8    end of transfer, ZIP is sending status
	 */
	w_ctr(ppb, 0x04);
	if (k)
		return (r & 0xb8);

	/* Counter expired - Time out occurred */
	imm_fail(dev, DID_TIME_OUT);
	printk("imm timeout in imm_wait\n");
	return 0;		/* command timed out */
}

static int imm_negotiate(imm_struct * tmp)
{
	/*
	 * The following is supposedly the IEEE 1284-1994 negotiate
	 * sequence. I have yet to obtain a copy of the above standard
	 * so this is a bit of a guess...
	 *
	 * A fair chunk of this is based on the Linux parport implementation
	 * of IEEE 1284.
	 *
	 * Return 0 if data available
	 *        1 if no data available
	 */

	unsigned short base = tmp->base;
	unsigned char a, mode;

	switch (tmp->mode) {
	case IMM_NIBBLE:
		mode = 0x00;
		break;
	case IMM_PS2:
		mode = 0x01;
		break;
	default:
		return 0;
	}

	w_ctr(base, 0x04);
	udelay(5);
	w_dtr(base, mode);
	udelay(100);
	w_ctr(base, 0x06);
	udelay(5);
	a = (r_str(base) & 0x20) ? 0 : 1;
	udelay(5);
	w_ctr(base, 0x07);
	udelay(5);
	w_ctr(base, 0x06);

	if (a) {
		printk
		    ("IMM: IEEE1284 negotiate indicates no data available.\n");
		imm_fail(tmp, DID_ERROR);
	}
	return a;
}

/* 
 * Clear EPP timeout bit. 
 */
static inline void epp_reset(unsigned short ppb)
{
	int i;

	i = r_str(ppb);
	w_str(ppb, i);
	w_str(ppb, i & 0xfe);
}

/* 
 * Wait for empty ECP fifo (if we are in ECP fifo mode only)
 */
static inline void ecp_sync(imm_struct *dev)
{
	int i, ppb_hi = dev->base_hi;

	if (ppb_hi == 0)
		return;

	if ((r_ecr(ppb_hi) & 0xe0) == 0x60) {	/* mode 011 == ECP fifo mode */
		for (i = 0; i < 100; i++) {
			if (r_ecr(ppb_hi) & 0x01)
				return;
			udelay(5);
		}
		printk("imm: ECP sync failed as data still present in FIFO.\n");
	}
}

static int imm_byte_out(unsigned short base, const char *buffer, int len)
{
	int i;

	w_ctr(base, 0x4);	/* apparently a sane mode */
	for (i = len >> 1; i; i--) {
		w_dtr(base, *buffer++);
		w_ctr(base, 0x5);	/* Drop STROBE low */
		w_dtr(base, *buffer++);
		w_ctr(base, 0x0);	/* STROBE high + INIT low */
	}
	w_ctr(base, 0x4);	/* apparently a sane mode */
	return 1;		/* All went well - we hope! */
}

static int imm_nibble_in(unsigned short base, char *buffer, int len)
{
	unsigned char l;
	int i;

	/*
	 * The following is based on documented timing signals
	 */
	w_ctr(base, 0x4);
	for (i = len; i; i--) {
		w_ctr(base, 0x6);
		l = (r_str(base) & 0xf0) >> 4;
		w_ctr(base, 0x5);
		*buffer++ = (r_str(base) & 0xf0) | l;
		w_ctr(base, 0x4);
	}
	return 1;		/* All went well - we hope! */
}

static int imm_byte_in(unsigned short base, char *buffer, int len)
{
	int i;

	/*
	 * The following is based on documented timing signals
	 */
	w_ctr(base, 0x4);
	for (i = len; i; i--) {
		w_ctr(base, 0x26);
		*buffer++ = r_dtr(base);
		w_ctr(base, 0x25);
	}
	return 1;		/* All went well - we hope! */
}

static int imm_out(imm_struct *dev, char *buffer, int len)
{
	unsigned short ppb = dev->base;
	int r = imm_wait(dev);

	/*
	 * Make sure that:
	 * a) the SCSI bus is BUSY (device still listening)
	 * b) the device is listening
	 */
	if ((r & 0x18) != 0x08) {
		imm_fail(dev, DID_ERROR);
		printk("IMM: returned SCSI status %2x\n", r);
		return 0;
	}
	switch (dev->mode) {
	case IMM_EPP_32:
	case IMM_EPP_16:
	case IMM_EPP_8:
		epp_reset(ppb);
		w_ctr(ppb, 0x4);
#ifdef CONFIG_SCSI_IZIP_EPP16
		if (!(((long) buffer | len) & 0x01))
			outsw(ppb + 4, buffer, len >> 1);
#else
		if (!(((long) buffer | len) & 0x03))
			outsl(ppb + 4, buffer, len >> 2);
#endif
		else
			outsb(ppb + 4, buffer, len);
		w_ctr(ppb, 0xc);
		r = !(r_str(ppb) & 0x01);
		w_ctr(ppb, 0xc);
		ecp_sync(dev);
		break;

	case IMM_NIBBLE:
	case IMM_PS2:
		/* 8 bit output, with a loop */
		r = imm_byte_out(ppb, buffer, len);
		break;

	default:
		printk("IMM: bug in imm_out()\n");
		r = 0;
	}
	return r;
}

static int imm_in(imm_struct *dev, char *buffer, int len)
{
	unsigned short ppb = dev->base;
	int r = imm_wait(dev);

	/*
	 * Make sure that:
	 * a) the SCSI bus is BUSY (device still listening)
	 * b) the device is sending data
	 */
	if ((r & 0x18) != 0x18) {
		imm_fail(dev, DID_ERROR);
		return 0;
	}
	switch (dev->mode) {
	case IMM_NIBBLE:
		/* 4 bit input, with a loop */
		r = imm_nibble_in(ppb, buffer, len);
		w_ctr(ppb, 0xc);
		break;

	case IMM_PS2:
		/* 8 bit input, with a loop */
		r = imm_byte_in(ppb, buffer, len);
		w_ctr(ppb, 0xc);
		break;

	case IMM_EPP_32:
	case IMM_EPP_16:
	case IMM_EPP_8:
		epp_reset(ppb);
		w_ctr(ppb, 0x24);
#ifdef CONFIG_SCSI_IZIP_EPP16
		if (!(((long) buffer | len) & 0x01))
			insw(ppb + 4, buffer, len >> 1);
#else
		if (!(((long) buffer | len) & 0x03))
			insl(ppb + 4, buffer, len >> 2);
#endif
		else
			insb(ppb + 4, buffer, len);
		w_ctr(ppb, 0x2c);
		r = !(r_str(ppb) & 0x01);
		w_ctr(ppb, 0x2c);
		ecp_sync(dev);
		break;

	default:
		printk("IMM: bug in imm_ins()\n");
		r = 0;
		break;
	}
	return r;
}

static int imm_cpp(unsigned short ppb, unsigned char b)
{
	/*
	 * Comments on udelay values refer to the
	 * Command Packet Protocol (CPP) timing diagram.
	 */

	unsigned char s1, s2, s3;
	w_ctr(ppb, 0x0c);
	udelay(2);		/* 1 usec - infinite */
	w_dtr(ppb, 0xaa);
	udelay(10);		/* 7 usec - infinite */
	w_dtr(ppb, 0x55);
	udelay(10);		/* 7 usec - infinite */
	w_dtr(ppb, 0x00);
	udelay(10);		/* 7 usec - infinite */
	w_dtr(ppb, 0xff);
	udelay(10);		/* 7 usec - infinite */
	s1 = r_str(ppb) & 0xb8;
	w_dtr(ppb, 0x87);
	udelay(10);		/* 7 usec - infinite */
	s2 = r_str(ppb) & 0xb8;
	w_dtr(ppb, 0x78);
	udelay(10);		/* 7 usec - infinite */
	s3 = r_str(ppb) & 0x38;
	/*
	 * Values for b are:
	 * 0000 00aa    Assign address aa to current device
	 * 0010 00aa    Select device aa in EPP Winbond mode
	 * 0010 10aa    Select device aa in EPP mode
	 * 0011 xxxx    Deselect all devices
	 * 0110 00aa    Test device aa
	 * 1101 00aa    Select device aa in ECP mode
	 * 1110 00aa    Select device aa in Compatible mode
	 */
	w_dtr(ppb, b);
	udelay(2);		/* 1 usec - infinite */
	w_ctr(ppb, 0x0c);
	udelay(10);		/* 7 usec - infinite */
	w_ctr(ppb, 0x0d);
	udelay(2);		/* 1 usec - infinite */
	w_ctr(ppb, 0x0c);
	udelay(10);		/* 7 usec - infinite */
	w_dtr(ppb, 0xff);
	udelay(10);		/* 7 usec - infinite */

	/*
	 * The following table is electrical pin values.
	 * (BSY is inverted at the CTR register)
	 *
	 *       BSY  ACK  POut SEL  Fault
	 * S1    0    X    1    1    1
	 * S2    1    X    0    1    1
	 * S3    L    X    1    1    S
	 *
	 * L => Last device in chain
	 * S => Selected
	 *
	 * Observered values for S1,S2,S3 are:
	 * Disconnect => f8/58/78
	 * Connect    => f8/58/70
	 */
	if ((s1 == 0xb8) && (s2 == 0x18) && (s3 == 0x30))
		return 1;	/* Connected */
	if ((s1 == 0xb8) && (s2 == 0x18) && (s3 == 0x38))
		return 0;	/* Disconnected */

	return -1;		/* No device present */
}

static inline int imm_connect(imm_struct *dev, int flag)
{
	unsigned short ppb = dev->base;

	imm_cpp(ppb, 0xe0);	/* Select device 0 in compatible mode */
	imm_cpp(ppb, 0x30);	/* Disconnect all devices */

	if ((dev->mode == IMM_EPP_8) ||
	    (dev->mode == IMM_EPP_16) ||
	    (dev->mode == IMM_EPP_32))
		return imm_cpp(ppb, 0x28);	/* Select device 0 in EPP mode */
	return imm_cpp(ppb, 0xe0);	/* Select device 0 in compatible mode */
}

static void imm_disconnect(imm_struct *dev)
{
	imm_cpp(dev->base, 0x30);	/* Disconnect all devices */
}

static int imm_select(imm_struct *dev, int target)
{
	int k;
	unsigned short ppb = dev->base;

	/*
	 * Firstly we want to make sure there is nothing
	 * holding onto the SCSI bus.
	 */
	w_ctr(ppb, 0xc);

	k = IMM_SELECT_TMO;
	do {
		k--;
	} while ((r_str(ppb) & 0x08) && (k));

	if (!k)
		return 0;

	/*
	 * Now assert the SCSI ID (HOST and TARGET) on the data bus
	 */
	w_ctr(ppb, 0x4);
	w_dtr(ppb, 0x80 | (1 << target));
	udelay(1);

	/*
	 * Deassert SELIN first followed by STROBE
	 */
	w_ctr(ppb, 0xc);
	w_ctr(ppb, 0xd);

	/*
	 * ACK should drop low while SELIN is deasserted.
	 * FAULT should drop low when the SCSI device latches the bus.
	 */
	k = IMM_SELECT_TMO;
	do {
		k--;
	}
	while (!(r_str(ppb) & 0x08) && (k));

	/*
	 * Place the interface back into a sane state (status mode)
	 */
	w_ctr(ppb, 0xc);
	return (k) ? 1 : 0;
}

static int imm_init(imm_struct *dev)
{
	if (imm_connect(dev, 0) != 1)
		return -EIO;
	imm_reset_pulse(dev->base);
	mdelay(1);	/* Delay to allow devices to settle */
	imm_disconnect(dev);
	mdelay(1);	/* Another delay to allow devices to settle */
	return device_check(dev);
}

static inline int imm_send_command(struct scsi_cmnd *cmd)
{
	imm_struct *dev = imm_dev(cmd->device->host);
	int k;

	/* NOTE: IMM uses byte pairs */
	for (k = 0; k < cmd->cmd_len; k += 2)
		if (!imm_out(dev, &cmd->cmnd[k], 2))
			return 0;
	return 1;
}

/*
 * The bulk flag enables some optimisations in the data transfer loops,
 * it should be true for any command that transfers data in integral
 * numbers of sectors.
 * 
 * The driver appears to remain stable if we speed up the parallel port
 * i/o in this function, but not elsewhere.
 */
static int imm_completion(struct scsi_cmnd *cmd)
{
	/* Return codes:
	 * -1     Error
	 *  0     Told to schedule
	 *  1     Finished data transfer
	 */
	imm_struct *dev = imm_dev(cmd->device->host);
	unsigned short ppb = dev->base;
	unsigned long start_jiffies = jiffies;

	unsigned char r, v;
	int fast, bulk, status;

	v = cmd->cmnd[0];
	bulk = ((v == READ_6) ||
		(v == READ_10) || (v == WRITE_6) || (v == WRITE_10));

	/*
	 * We only get here if the drive is ready to comunicate,
	 * hence no need for a full imm_wait.
	 */
	w_ctr(ppb, 0x0c);
	r = (r_str(ppb) & 0xb8);

	/*
	 * while (device is not ready to send status byte)
	 *     loop;
	 */
	while (r != (unsigned char) 0xb8) {
		/*
		 * If we have been running for more than a full timer tick
		 * then take a rest.
		 */
		if (time_after(jiffies, start_jiffies + 1))
			return 0;

		/*
		 * FAIL if:
		 * a) Drive status is screwy (!ready && !present)
		 * b) Drive is requesting/sending more data than expected
		 */
		if (((r & 0x88) != 0x88) || (cmd->SCp.this_residual <= 0)) {
			imm_fail(dev, DID_ERROR);
			return -1;	/* ERROR_RETURN */
		}
		/* determine if we should use burst I/O */
		if (dev->rd == 0) {
			fast = (bulk
				&& (cmd->SCp.this_residual >=
				    IMM_BURST_SIZE)) ? IMM_BURST_SIZE : 2;
			status = imm_out(dev, cmd->SCp.ptr, fast);
		} else {
			fast = (bulk
				&& (cmd->SCp.this_residual >=
				    IMM_BURST_SIZE)) ? IMM_BURST_SIZE : 1;
			status = imm_in(dev, cmd->SCp.ptr, fast);
		}

		cmd->SCp.ptr += fast;
		cmd->SCp.this_residual -= fast;

		if (!status) {
			imm_fail(dev, DID_BUS_BUSY);
			return -1;	/* ERROR_RETURN */
		}
		if (cmd->SCp.buffer && !cmd->SCp.this_residual) {
			/* if scatter/gather, advance to the next segment */
			if (cmd->SCp.buffers_residual--) {
				cmd->SCp.buffer++;
				cmd->SCp.this_residual =
				    cmd->SCp.buffer->length;
				cmd->SCp.ptr =
				    page_address(cmd->SCp.buffer->page) +
				    cmd->SCp.buffer->offset;

				/*
				 * Make sure that we transfer even number of bytes
				 * otherwise it makes imm_byte_out() messy.
				 */
				if (cmd->SCp.this_residual & 0x01)
					cmd->SCp.this_residual++;
			}
		}
		/* Now check to see if the drive is ready to comunicate */
		w_ctr(ppb, 0x0c);
		r = (r_str(ppb) & 0xb8);

		/* If not, drop back down to the scheduler and wait a timer tick */
		if (!(r & 0x80))
			return 0;
	}
	return 1;		/* FINISH_RETURN */
}

/*
 * Since the IMM itself doesn't generate interrupts, we use
 * the scheduler's task queue to generate a stream of call-backs and
 * complete the request when the drive is ready.
 */
static void imm_interrupt(struct work_struct *work)
{
	imm_struct *dev = container_of(work, imm_struct, imm_tq.work);
	struct scsi_cmnd *cmd = dev->cur_cmd;
	struct Scsi_Host *host = cmd->device->host;
	unsigned long flags;

	if (imm_engine(dev, cmd)) {
		schedule_delayed_work(&dev->imm_tq, 1);
		return;
	}
	/* Command must of completed hence it is safe to let go... */
#if IMM_DEBUG > 0
	switch ((cmd->result >> 16) & 0xff) {
	case DID_OK:
		break;
	case DID_NO_CONNECT:
		printk("imm: no device at SCSI ID %i\n", cmd->device->id);
		break;
	case DID_BUS_BUSY:
		printk("imm: BUS BUSY - EPP timeout detected\n");
		break;
	case DID_TIME_OUT:
		printk("imm: unknown timeout\n");
		break;
	case DID_ABORT:
		printk("imm: told to abort\n");
		break;
	case DID_PARITY:
		printk("imm: parity error (???)\n");
		break;
	case DID_ERROR:
		printk("imm: internal driver error\n");
		break;
	case DID_RESET:
		printk("imm: told to reset device\n");
		break;
	case DID_BAD_INTR:
		printk("imm: bad interrupt (???)\n");
		break;
	default:
		printk("imm: bad return code (%02x)\n",
		       (cmd->result >> 16) & 0xff);
	}
#endif

	if (cmd->SCp.phase > 1)
		imm_disconnect(dev);

	imm_pb_dismiss(dev);

	spin_lock_irqsave(host->host_lock, flags);
	dev->cur_cmd = NULL;
	cmd->scsi_done(cmd);
	spin_unlock_irqrestore(host->host_lock, flags);
	return;
}

static int imm_engine(imm_struct *dev, struct scsi_cmnd *cmd)
{
	unsigned short ppb = dev->base;
	unsigned char l = 0, h = 0;
	int retv, x;

	/* First check for any errors that may have occurred
	 * Here we check for internal errors
	 */
	if (dev->failed)
		return 0;

	switch (cmd->SCp.phase) {
	case 0:		/* Phase 0 - Waiting for parport */
		if (time_after(jiffies, dev->jstart + HZ)) {
			/*
			 * We waited more than a second
			 * for parport to call us
			 */
			imm_fail(dev, DID_BUS_BUSY);
			return 0;
		}
		return 1;	/* wait until imm_wakeup claims parport */
		/* Phase 1 - Connected */
	case 1:
		imm_connect(dev, CONNECT_EPP_MAYBE);
		cmd->SCp.phase++;

		/* Phase 2 - We are now talking to the scsi bus */
	case 2:
		if (!imm_select(dev, scmd_id(cmd))) {
			imm_fail(dev, DID_NO_CONNECT);
			return 0;
		}
		cmd->SCp.phase++;

		/* Phase 3 - Ready to accept a command */
	case 3:
		w_ctr(ppb, 0x0c);
		if (!(r_str(ppb) & 0x80))
			return 1;

		if (!imm_send_command(cmd))
			return 0;
		cmd->SCp.phase++;

		/* Phase 4 - Setup scatter/gather buffers */
	case 4:
		if (cmd->use_sg) {
			/* if many buffers are available, start filling the first */
			cmd->SCp.buffer =
			    (struct scatterlist *) cmd->request_buffer;
			cmd->SCp.this_residual = cmd->SCp.buffer->length;
			cmd->SCp.ptr =
			    page_address(cmd->SCp.buffer->page) +
			    cmd->SCp.buffer->offset;
		} else {
			/* else fill the only available buffer */
			cmd->SCp.buffer = NULL;
			cmd->SCp.this_residual = cmd->request_bufflen;
			cmd->SCp.ptr = cmd->request_buffer;
		}
		cmd->SCp.buffers_residual = cmd->use_sg - 1;
		cmd->SCp.phase++;
		if (cmd->SCp.this_residual & 0x01)
			cmd->SCp.this_residual++;
		/* Phase 5 - Pre-Data transfer stage */
	case 5:
		/* Spin lock for BUSY */
		w_ctr(ppb, 0x0c);
		if (!(r_str(ppb) & 0x80))
			return 1;

		/* Require negotiation for read requests */
		x = (r_str(ppb) & 0xb8);
		dev->rd = (x & 0x10) ? 1 : 0;
		dev->dp = (x & 0x20) ? 0 : 1;

		if ((dev->dp) && (dev->rd))
			if (imm_negotiate(dev))
				return 0;
		cmd->SCp.phase++;

		/* Phase 6 - Data transfer stage */
	case 6:
		/* Spin lock for BUSY */
		w_ctr(ppb, 0x0c);
		if (!(r_str(ppb) & 0x80))
			return 1;

		if (dev->dp) {
			retv = imm_completion(cmd);
			if (retv == -1)
				return 0;
			if (retv == 0)
				return 1;
		}
		cmd->SCp.phase++;

		/* Phase 7 - Post data transfer stage */
	case 7:
		if ((dev->dp) && (dev->rd)) {
			if ((dev->mode == IMM_NIBBLE) || (dev->mode == IMM_PS2)) {
				w_ctr(ppb, 0x4);
				w_ctr(ppb, 0xc);
				w_ctr(ppb, 0xe);
				w_ctr(ppb, 0x4);
			}
		}
		cmd->SCp.phase++;

		/* Phase 8 - Read status/message */
	case 8:
		/* Check for data overrun */
		if (imm_wait(dev) != (unsigned char) 0xb8) {
			imm_fail(dev, DID_ERROR);
			return 0;
		}
		if (imm_negotiate(dev))
			return 0;
		if (imm_in(dev, &l, 1)) {	/* read status byte */
			/* Check for optional message byte */
			if (imm_wait(dev) == (unsigned char) 0xb8)
				imm_in(dev, &h, 1);
			cmd->result = (DID_OK << 16) + (l & STATUS_MASK);
		}
		if ((dev->mode == IMM_NIBBLE) || (dev->mode == IMM_PS2)) {
			w_ctr(ppb, 0x4);
			w_ctr(ppb, 0xc);
			w_ctr(ppb, 0xe);
			w_ctr(ppb, 0x4);
		}
		return 0;	/* Finished */
		break;

	default:
		printk("imm: Invalid scsi phase\n");
	}
	return 0;
}

static int imm_queuecommand(struct scsi_cmnd *cmd,
		void (*done)(struct scsi_cmnd *))
{
	imm_struct *dev = imm_dev(cmd->device->host);

	if (dev->cur_cmd) {
		printk("IMM: bug in imm_queuecommand\n");
		return 0;
	}
	dev->failed = 0;
	dev->jstart = jiffies;
	dev->cur_cmd = cmd;
	cmd->scsi_done = done;
	cmd->result = DID_ERROR << 16;	/* default return code */
	cmd->SCp.phase = 0;	/* bus free */

	schedule_delayed_work(&dev->imm_tq, 0);

	imm_pb_claim(dev);

	return 0;
}

/*
 * Apparently the disk->capacity attribute is off by 1 sector 
 * for all disk drives.  We add the one here, but it should really
 * be done in sd.c.  Even if it gets fixed there, this will still
 * work.
 */
static int imm_biosparam(struct scsi_device *sdev, struct block_device *dev,
			 sector_t capacity, int ip[])
{
	ip[0] = 0x40;
	ip[1] = 0x20;
	ip[2] = ((unsigned long) capacity + 1) / (ip[0] * ip[1]);
	if (ip[2] > 1024) {
		ip[0] = 0xff;
		ip[1] = 0x3f;
		ip[2] = ((unsigned long) capacity + 1) / (ip[0] * ip[1]);
	}
	return 0;
}

static int imm_abort(struct scsi_cmnd *cmd)
{
	imm_struct *dev = imm_dev(cmd->device->host);
	/*
	 * There is no method for aborting commands since Iomega
	 * have tied the SCSI_MESSAGE line high in the interface
	 */

	switch (cmd->SCp.phase) {
	case 0:		/* Do not have access to parport */
	case 1:		/* Have not connected to interface */
		dev->cur_cmd = NULL;	/* Forget the problem */
		return SUCCESS;
		break;
	default:		/* SCSI command sent, can not abort */
		return FAILED;
		break;
	}
}

static void imm_reset_pulse(unsigned int base)
{
	w_ctr(base, 0x04);
	w_dtr(base, 0x40);
	udelay(1);
	w_ctr(base, 0x0c);
	w_ctr(base, 0x0d);
	udelay(50);
	w_ctr(base, 0x0c);
	w_ctr(base, 0x04);
}

static int imm_reset(struct scsi_cmnd *cmd)
{
	imm_struct *dev = imm_dev(cmd->device->host);

	if (cmd->SCp.phase)
		imm_disconnect(dev);
	dev->cur_cmd = NULL;	/* Forget the problem */

	imm_connect(dev, CONNECT_NORMAL);
	imm_reset_pulse(dev->base);
	mdelay(1);		/* device settle delay */
	imm_disconnect(dev);
	mdelay(1);		/* device settle delay */
	return SUCCESS;
}

static int device_check(imm_struct *dev)
{
	/* This routine looks for a device and then attempts to use EPP
	   to send a command. If all goes as planned then EPP is available. */

	static char cmd[6] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
	int loop, old_mode, status, k, ppb = dev->base;
	unsigned char l;

	old_mode = dev->mode;
	for (loop = 0; loop < 8; loop++) {
		/* Attempt to use EPP for Test Unit Ready */
		if ((ppb & 0x0007) == 0x0000)
			dev->mode = IMM_EPP_32;

	      second_pass:
		imm_connect(dev, CONNECT_EPP_MAYBE);
		/* Select SCSI device */
		if (!imm_select(dev, loop)) {
			imm_disconnect(dev);
			continue;
		}
		printk("imm: Found device at ID %i, Attempting to use %s\n",
		       loop, IMM_MODE_STRING[dev->mode]);

		/* Send SCSI command */
		status = 1;
		w_ctr(ppb, 0x0c);
		for (l = 0; (l < 3) && (status); l++)
			status = imm_out(dev, &cmd[l << 1], 2);

		if (!status) {
			imm_disconnect(dev);
			imm_connect(dev, CONNECT_EPP_MAYBE);
			imm_reset_pulse(dev->base);
			udelay(1000);
			imm_disconnect(dev);
			udelay(1000);
			if (dev->mode == IMM_EPP_32) {
				dev->mode = old_mode;
				goto second_pass;
			}
			printk("imm: Unable to establish communication\n");
			return -EIO;
		}
		w_ctr(ppb, 0x0c);

		k = 1000000;	/* 1 Second */
		do {
			l = r_str(ppb);
			k--;
			udelay(1);
		} while (!(l & 0x80) && (k));

		l &= 0xb8;

		if (l != 0xb8) {
			imm_disconnect(dev);
			imm_connect(dev, CONNECT_EPP_MAYBE);
			imm_reset_pulse(dev->base);
			udelay(1000);
			imm_disconnect(dev);
			udelay(1000);
			if (dev->mode == IMM_EPP_32) {
				dev->mode = old_mode;
				goto second_pass;
			}
			printk
			    ("imm: Unable to establish communication\n");
			return -EIO;
		}
		imm_disconnect(dev);
		printk
		    ("imm: Communication established at 0x%x with ID %i using %s\n",
		     ppb, loop, IMM_MODE_STRING[dev->mode]);
		imm_connect(dev, CONNECT_EPP_MAYBE);
		imm_reset_pulse(dev->base);
		udelay(1000);
		imm_disconnect(dev);
		udelay(1000);
		return 0;
	}
	printk("imm: No devices found\n");
	return -ENODEV;
}

/*
 * imm cannot deal with highmem, so this causes all IO pages for this host
 * to reside in low memory (hence mapped)
 */
static int imm_adjust_queue(struct scsi_device *device)
{
	blk_queue_bounce_limit(device->request_queue, BLK_BOUNCE_HIGH);
	return 0;
}

static struct scsi_host_template imm_template = {
	.module			= THIS_MODULE,
	.proc_name		= "imm",
	.proc_info		= imm_proc_info,
	.name			= "Iomega VPI2 (imm) interface",
	.queuecommand		= imm_queuecommand,
	.eh_abort_handler	= imm_abort,
	.eh_bus_reset_handler	= imm_reset,
	.eh_host_reset_handler	= imm_reset,
	.bios_param		= imm_biosparam,
	.this_id		= 7,
	.sg_tablesize		= SG_ALL,
	.cmd_per_lun		= 1,
	.use_clustering		= ENABLE_CLUSTERING,
	.can_queue		= 1,
	.slave_alloc		= imm_adjust_queue,
};

/***************************************************************************
 *                   Parallel port probing routines                        *
 ***************************************************************************/

static LIST_HEAD(imm_hosts);

static int __imm_attach(struct parport *pb)
{
	struct Scsi_Host *host;
	imm_struct *dev;
	DECLARE_WAIT_QUEUE_HEAD_ONSTACK(waiting);
	DEFINE_WAIT(wait);
	int ports;
	int modes, ppb;
	int err = -ENOMEM;

	init_waitqueue_head(&waiting);

	dev = kzalloc(sizeof(imm_struct), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;


	dev->base = -1;
	dev->mode = IMM_AUTODETECT;
	INIT_LIST_HEAD(&dev->list);

	dev->dev = parport_register_device(pb, "imm", NULL, imm_wakeup,
						NULL, 0, dev);

	if (!dev->dev)
		goto out;


	/* Claim the bus so it remembers what we do to the control
	 * registers. [ CTR and ECP ]
	 */
	err = -EBUSY;
	dev->waiting = &waiting;
	prepare_to_wait(&waiting, &wait, TASK_UNINTERRUPTIBLE);
	if (imm_pb_claim(dev))
		schedule_timeout(3 * HZ);
	if (dev->wanted) {
		printk(KERN_ERR "imm%d: failed to claim parport because "
			"a pardevice is owning the port for too long "
			"time!\n", pb->number);
		imm_pb_dismiss(dev);
		dev->waiting = NULL;
		finish_wait(&waiting, &wait);
		goto out1;
	}
	dev->waiting = NULL;
	finish_wait(&waiting, &wait);
	ppb = dev->base = dev->dev->port->base;
	dev->base_hi = dev->dev->port->base_hi;
	w_ctr(ppb, 0x0c);
	modes = dev->dev->port->modes;

	/* Mode detection works up the chain of speed
	 * This avoids a nasty if-then-else-if-... tree
	 */
	dev->mode = IMM_NIBBLE;

	if (modes & PARPORT_MODE_TRISTATE)
		dev->mode = IMM_PS2;

	/* Done configuration */

	err = imm_init(dev);

	imm_pb_release(dev);

	if (err)
		goto out1;

	/* now the glue ... */
	if (dev->mode == IMM_NIBBLE || dev->mode == IMM_PS2)
		ports = 3;
	else
		ports = 8;

	INIT_DELAYED_WORK(&dev->imm_tq, imm_interrupt);

	err = -ENOMEM;
	host = scsi_host_alloc(&imm_template, sizeof(imm_struct *));
	if (!host)
		goto out1;
	host->io_port = pb->base;
	host->n_io_port = ports;
	host->dma_channel = -1;
	host->unique_id = pb->number;
	*(imm_struct **)&host->hostdata = dev;
	dev->host = host;
	list_add_tail(&dev->list, &imm_hosts);
	err = scsi_add_host(host, NULL);
	if (err)
		goto out2;
	scsi_scan_host(host);
	return 0;

out2:
	list_del_init(&dev->list);
	scsi_host_put(host);
out1:
	parport_unregister_device(dev->dev);
out:
	kfree(dev);
	return err;
}

static void imm_attach(struct parport *pb)
{
	__imm_attach(pb);
}

static void imm_detach(struct parport *pb)
{
	imm_struct *dev;
	list_for_each_entry(dev, &imm_hosts, list) {
		if (dev->dev->port == pb) {
			list_del_init(&dev->list);
			scsi_remove_host(dev->host);
			scsi_host_put(dev->host);
			parport_unregister_device(dev->dev);
			kfree(dev);
			break;
		}
	}
}

static struct parport_driver imm_driver = {
	.name	= "imm",
	.attach	= imm_attach,
	.detach	= imm_detach,
};

static int __init imm_driver_init(void)
{
	printk("imm: Version %s\n", IMM_VERSION);
	return parport_register_driver(&imm_driver);
}

static void __exit imm_driver_exit(void)
{
	parport_unregister_driver(&imm_driver);
}

module_init(imm_driver_init);
module_exit(imm_driver_exit);

MODULE_LICENSE("GPL");
