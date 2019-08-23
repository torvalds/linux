/* ppa.c   --  low level driver for the IOMEGA PPA3 
 * parallel port SCSI host adapter.
 * 
 * (The PPA3 is the embedded controller in the ZIP drive.)
 * 
 * (c) 1995,1996 Grant R. Guenther, grant@torque.net,
 * under the terms of the GNU General Public License.
 * 
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/blkdev.h>
#include <linux/parport.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/jiffies.h>
#include <asm/io.h>

#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>


static void ppa_reset_pulse(unsigned int base);

typedef struct {
	struct pardevice *dev;	/* Parport device entry         */
	int base;		/* Actual port address          */
	int mode;		/* Transfer mode                */
	struct scsi_cmnd *cur_cmd;	/* Current queued command       */
	struct delayed_work ppa_tq;	/* Polling interrupt stuff       */
	unsigned long jstart;	/* Jiffies at start             */
	unsigned long recon_tmo;	/* How many usecs to wait for reconnection (6th bit) */
	unsigned int failed:1;	/* Failure flag                 */
	unsigned wanted:1;	/* Parport sharing busy flag    */
	unsigned int dev_no;	/* Device number		*/
	wait_queue_head_t *waiting;
	struct Scsi_Host *host;
	struct list_head list;
} ppa_struct;

#include  "ppa.h"

static inline ppa_struct *ppa_dev(struct Scsi_Host *host)
{
	return *(ppa_struct **)&host->hostdata;
}

static DEFINE_SPINLOCK(arbitration_lock);

static void got_it(ppa_struct *dev)
{
	dev->base = dev->dev->port->base;
	if (dev->cur_cmd)
		dev->cur_cmd->SCp.phase = 1;
	else
		wake_up(dev->waiting);
}

static void ppa_wakeup(void *ref)
{
	ppa_struct *dev = (ppa_struct *) ref;
	unsigned long flags;

	spin_lock_irqsave(&arbitration_lock, flags);
	if (dev->wanted) {
		parport_claim(dev->dev);
		got_it(dev);
		dev->wanted = 0;
	}
	spin_unlock_irqrestore(&arbitration_lock, flags);
	return;
}

static int ppa_pb_claim(ppa_struct *dev)
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

static void ppa_pb_dismiss(ppa_struct *dev)
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

static inline void ppa_pb_release(ppa_struct *dev)
{
	parport_release(dev->dev);
}

/*
 * Start of Chipset kludges
 */

/* This is to give the ppa driver a way to modify the timings (and other
 * parameters) by writing to the /proc/scsi/ppa/0 file.
 * Very simple method really... (To simple, no error checking :( )
 * Reason: Kernel hackers HATE having to unload and reload modules for
 * testing...
 * Also gives a method to use a script to obtain optimum timings (TODO)
 */

static inline int ppa_write_info(struct Scsi_Host *host, char *buffer, int length)
{
	ppa_struct *dev = ppa_dev(host);
	unsigned long x;

	if ((length > 5) && (strncmp(buffer, "mode=", 5) == 0)) {
		x = simple_strtoul(buffer + 5, NULL, 0);
		dev->mode = x;
		return length;
	}
	if ((length > 10) && (strncmp(buffer, "recon_tmo=", 10) == 0)) {
		x = simple_strtoul(buffer + 10, NULL, 0);
		dev->recon_tmo = x;
		printk(KERN_INFO "ppa: recon_tmo set to %ld\n", x);
		return length;
	}
	printk(KERN_WARNING "ppa /proc: invalid variable\n");
	return -EINVAL;
}

static int ppa_show_info(struct seq_file *m, struct Scsi_Host *host)
{
	ppa_struct *dev = ppa_dev(host);

	seq_printf(m, "Version : %s\n", PPA_VERSION);
	seq_printf(m, "Parport : %s\n", dev->dev->port->name);
	seq_printf(m, "Mode    : %s\n", PPA_MODE_STRING[dev->mode]);
#if PPA_DEBUG > 0
	seq_printf(m, "recon_tmo : %lu\n", dev->recon_tmo);
#endif
	return 0;
}

static int device_check(ppa_struct *dev);

#if PPA_DEBUG > 0
#define ppa_fail(x,y) printk("ppa: ppa_fail(%i) from %s at line %d\n",\
	   y, __func__, __LINE__); ppa_fail_func(x,y);
static inline void ppa_fail_func(ppa_struct *dev, int error_code)
#else
static inline void ppa_fail(ppa_struct *dev, int error_code)
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
static unsigned char ppa_wait(ppa_struct *dev)
{
	int k;
	unsigned short ppb = dev->base;
	unsigned char r;

	k = PPA_SPIN_TMO;
	/* Wait for bit 6 and 7 - PJC */
	for (r = r_str(ppb); ((r & 0xc0) != 0xc0) && (k); k--) {
		udelay(1);
		r = r_str(ppb);
	}

	/*
	 * return some status information.
	 * Semantics: 0xc0 = ZIP wants more data
	 *            0xd0 = ZIP wants to send more data
	 *            0xe0 = ZIP is expecting SCSI command data
	 *            0xf0 = end of transfer, ZIP is sending status
	 */
	if (k)
		return (r & 0xf0);

	/* Counter expired - Time out occurred */
	ppa_fail(dev, DID_TIME_OUT);
	printk(KERN_WARNING "ppa timeout in ppa_wait\n");
	return 0;		/* command timed out */
}

/*
 * Clear EPP Timeout Bit 
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
static inline void ecp_sync(ppa_struct *dev)
{
	int i, ppb_hi = dev->dev->port->base_hi;

	if (ppb_hi == 0)
		return;

	if ((r_ecr(ppb_hi) & 0xe0) == 0x60) {	/* mode 011 == ECP fifo mode */
		for (i = 0; i < 100; i++) {
			if (r_ecr(ppb_hi) & 0x01)
				return;
			udelay(5);
		}
		printk(KERN_WARNING "ppa: ECP sync failed as data still present in FIFO.\n");
	}
}

static int ppa_byte_out(unsigned short base, const char *buffer, int len)
{
	int i;

	for (i = len; i; i--) {
		w_dtr(base, *buffer++);
		w_ctr(base, 0xe);
		w_ctr(base, 0xc);
	}
	return 1;		/* All went well - we hope! */
}

static int ppa_byte_in(unsigned short base, char *buffer, int len)
{
	int i;

	for (i = len; i; i--) {
		*buffer++ = r_dtr(base);
		w_ctr(base, 0x27);
		w_ctr(base, 0x25);
	}
	return 1;		/* All went well - we hope! */
}

static int ppa_nibble_in(unsigned short base, char *buffer, int len)
{
	for (; len; len--) {
		unsigned char h;

		w_ctr(base, 0x4);
		h = r_str(base) & 0xf0;
		w_ctr(base, 0x6);
		*buffer++ = h | ((r_str(base) & 0xf0) >> 4);
	}
	return 1;		/* All went well - we hope! */
}

static int ppa_out(ppa_struct *dev, char *buffer, int len)
{
	int r;
	unsigned short ppb = dev->base;

	r = ppa_wait(dev);

	if ((r & 0x50) != 0x40) {
		ppa_fail(dev, DID_ERROR);
		return 0;
	}
	switch (dev->mode) {
	case PPA_NIBBLE:
	case PPA_PS2:
		/* 8 bit output, with a loop */
		r = ppa_byte_out(ppb, buffer, len);
		break;

	case PPA_EPP_32:
	case PPA_EPP_16:
	case PPA_EPP_8:
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

	default:
		printk(KERN_ERR "PPA: bug in ppa_out()\n");
		r = 0;
	}
	return r;
}

static int ppa_in(ppa_struct *dev, char *buffer, int len)
{
	int r;
	unsigned short ppb = dev->base;

	r = ppa_wait(dev);

	if ((r & 0x50) != 0x50) {
		ppa_fail(dev, DID_ERROR);
		return 0;
	}
	switch (dev->mode) {
	case PPA_NIBBLE:
		/* 4 bit input, with a loop */
		r = ppa_nibble_in(ppb, buffer, len);
		w_ctr(ppb, 0xc);
		break;

	case PPA_PS2:
		/* 8 bit input, with a loop */
		w_ctr(ppb, 0x25);
		r = ppa_byte_in(ppb, buffer, len);
		w_ctr(ppb, 0x4);
		w_ctr(ppb, 0xc);
		break;

	case PPA_EPP_32:
	case PPA_EPP_16:
	case PPA_EPP_8:
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
		printk(KERN_ERR "PPA: bug in ppa_ins()\n");
		r = 0;
		break;
	}
	return r;
}

/* end of ppa_io.h */
static inline void ppa_d_pulse(unsigned short ppb, unsigned char b)
{
	w_dtr(ppb, b);
	w_ctr(ppb, 0xc);
	w_ctr(ppb, 0xe);
	w_ctr(ppb, 0xc);
	w_ctr(ppb, 0x4);
	w_ctr(ppb, 0xc);
}

static void ppa_disconnect(ppa_struct *dev)
{
	unsigned short ppb = dev->base;

	ppa_d_pulse(ppb, 0);
	ppa_d_pulse(ppb, 0x3c);
	ppa_d_pulse(ppb, 0x20);
	ppa_d_pulse(ppb, 0xf);
}

static inline void ppa_c_pulse(unsigned short ppb, unsigned char b)
{
	w_dtr(ppb, b);
	w_ctr(ppb, 0x4);
	w_ctr(ppb, 0x6);
	w_ctr(ppb, 0x4);
	w_ctr(ppb, 0xc);
}

static inline void ppa_connect(ppa_struct *dev, int flag)
{
	unsigned short ppb = dev->base;

	ppa_c_pulse(ppb, 0);
	ppa_c_pulse(ppb, 0x3c);
	ppa_c_pulse(ppb, 0x20);
	if ((flag == CONNECT_EPP_MAYBE) && IN_EPP_MODE(dev->mode))
		ppa_c_pulse(ppb, 0xcf);
	else
		ppa_c_pulse(ppb, 0x8f);
}

static int ppa_select(ppa_struct *dev, int target)
{
	int k;
	unsigned short ppb = dev->base;

	/*
	 * Bit 6 (0x40) is the device selected bit.
	 * First we must wait till the current device goes off line...
	 */
	k = PPA_SELECT_TMO;
	do {
		k--;
		udelay(1);
	} while ((r_str(ppb) & 0x40) && (k));
	if (!k)
		return 0;

	w_dtr(ppb, (1 << target));
	w_ctr(ppb, 0xe);
	w_ctr(ppb, 0xc);
	w_dtr(ppb, 0x80);	/* This is NOT the initator */
	w_ctr(ppb, 0x8);

	k = PPA_SELECT_TMO;
	do {
		k--;
		udelay(1);
	}
	while (!(r_str(ppb) & 0x40) && (k));
	if (!k)
		return 0;

	return 1;
}

/* 
 * This is based on a trace of what the Iomega DOS 'guest' driver does.
 * I've tried several different kinds of parallel ports with guest and
 * coded this to react in the same ways that it does.
 * 
 * The return value from this function is just a hint about where the
 * handshaking failed.
 * 
 */
static int ppa_init(ppa_struct *dev)
{
	int retv;
	unsigned short ppb = dev->base;

	ppa_disconnect(dev);
	ppa_connect(dev, CONNECT_NORMAL);

	retv = 2;		/* Failed */

	w_ctr(ppb, 0xe);
	if ((r_str(ppb) & 0x08) == 0x08)
		retv--;

	w_ctr(ppb, 0xc);
	if ((r_str(ppb) & 0x08) == 0x00)
		retv--;

	if (!retv)
		ppa_reset_pulse(ppb);
	udelay(1000);		/* Allow devices to settle down */
	ppa_disconnect(dev);
	udelay(1000);		/* Another delay to allow devices to settle */

	if (retv)
		return -EIO;

	return device_check(dev);
}

static inline int ppa_send_command(struct scsi_cmnd *cmd)
{
	ppa_struct *dev = ppa_dev(cmd->device->host);
	int k;

	w_ctr(dev->base, 0x0c);

	for (k = 0; k < cmd->cmd_len; k++)
		if (!ppa_out(dev, &cmd->cmnd[k], 1))
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
static int ppa_completion(struct scsi_cmnd *cmd)
{
	/* Return codes:
	 * -1     Error
	 *  0     Told to schedule
	 *  1     Finished data transfer
	 */
	ppa_struct *dev = ppa_dev(cmd->device->host);
	unsigned short ppb = dev->base;
	unsigned long start_jiffies = jiffies;

	unsigned char r, v;
	int fast, bulk, status;

	v = cmd->cmnd[0];
	bulk = ((v == READ_6) ||
		(v == READ_10) || (v == WRITE_6) || (v == WRITE_10));

	/*
	 * We only get here if the drive is ready to comunicate,
	 * hence no need for a full ppa_wait.
	 */
	r = (r_str(ppb) & 0xf0);

	while (r != (unsigned char) 0xf0) {
		/*
		 * If we have been running for more than a full timer tick
		 * then take a rest.
		 */
		if (time_after(jiffies, start_jiffies + 1))
			return 0;

		if ((cmd->SCp.this_residual <= 0)) {
			ppa_fail(dev, DID_ERROR);
			return -1;	/* ERROR_RETURN */
		}

		/* On some hardware we have SCSI disconnected (6th bit low)
		 * for about 100usecs. It is too expensive to wait a 
		 * tick on every loop so we busy wait for no more than
		 * 500usecs to give the drive a chance first. We do not 
		 * change things for "normal" hardware since generally 
		 * the 6th bit is always high.
		 * This makes the CPU load higher on some hardware 
		 * but otherwise we can not get more than 50K/secs 
		 * on this problem hardware.
		 */
		if ((r & 0xc0) != 0xc0) {
			/* Wait for reconnection should be no more than 
			 * jiffy/2 = 5ms = 5000 loops
			 */
			unsigned long k = dev->recon_tmo;
			for (; k && ((r = (r_str(ppb) & 0xf0)) & 0xc0) != 0xc0;
			     k--)
				udelay(1);

			if (!k)
				return 0;
		}

		/* determine if we should use burst I/O */
		fast = (bulk && (cmd->SCp.this_residual >= PPA_BURST_SIZE))
		    ? PPA_BURST_SIZE : 1;

		if (r == (unsigned char) 0xc0)
			status = ppa_out(dev, cmd->SCp.ptr, fast);
		else
			status = ppa_in(dev, cmd->SCp.ptr, fast);

		cmd->SCp.ptr += fast;
		cmd->SCp.this_residual -= fast;

		if (!status) {
			ppa_fail(dev, DID_BUS_BUSY);
			return -1;	/* ERROR_RETURN */
		}
		if (cmd->SCp.buffer && !cmd->SCp.this_residual) {
			/* if scatter/gather, advance to the next segment */
			if (cmd->SCp.buffers_residual--) {
				cmd->SCp.buffer = sg_next(cmd->SCp.buffer);
				cmd->SCp.this_residual =
				    cmd->SCp.buffer->length;
				cmd->SCp.ptr = sg_virt(cmd->SCp.buffer);
			}
		}
		/* Now check to see if the drive is ready to comunicate */
		r = (r_str(ppb) & 0xf0);
		/* If not, drop back down to the scheduler and wait a timer tick */
		if (!(r & 0x80))
			return 0;
	}
	return 1;		/* FINISH_RETURN */
}

/*
 * Since the PPA itself doesn't generate interrupts, we use
 * the scheduler's task queue to generate a stream of call-backs and
 * complete the request when the drive is ready.
 */
static void ppa_interrupt(struct work_struct *work)
{
	ppa_struct *dev = container_of(work, ppa_struct, ppa_tq.work);
	struct scsi_cmnd *cmd = dev->cur_cmd;

	if (!cmd) {
		printk(KERN_ERR "PPA: bug in ppa_interrupt\n");
		return;
	}
	if (ppa_engine(dev, cmd)) {
		schedule_delayed_work(&dev->ppa_tq, 1);
		return;
	}
	/* Command must of completed hence it is safe to let go... */
#if PPA_DEBUG > 0
	switch ((cmd->result >> 16) & 0xff) {
	case DID_OK:
		break;
	case DID_NO_CONNECT:
		printk(KERN_DEBUG "ppa: no device at SCSI ID %i\n", cmd->device->target);
		break;
	case DID_BUS_BUSY:
		printk(KERN_DEBUG "ppa: BUS BUSY - EPP timeout detected\n");
		break;
	case DID_TIME_OUT:
		printk(KERN_DEBUG "ppa: unknown timeout\n");
		break;
	case DID_ABORT:
		printk(KERN_DEBUG "ppa: told to abort\n");
		break;
	case DID_PARITY:
		printk(KERN_DEBUG "ppa: parity error (???)\n");
		break;
	case DID_ERROR:
		printk(KERN_DEBUG "ppa: internal driver error\n");
		break;
	case DID_RESET:
		printk(KERN_DEBUG "ppa: told to reset device\n");
		break;
	case DID_BAD_INTR:
		printk(KERN_WARNING "ppa: bad interrupt (???)\n");
		break;
	default:
		printk(KERN_WARNING "ppa: bad return code (%02x)\n",
		       (cmd->result >> 16) & 0xff);
	}
#endif

	if (cmd->SCp.phase > 1)
		ppa_disconnect(dev);

	ppa_pb_dismiss(dev);

	dev->cur_cmd = NULL;

	cmd->scsi_done(cmd);
}

static int ppa_engine(ppa_struct *dev, struct scsi_cmnd *cmd)
{
	unsigned short ppb = dev->base;
	unsigned char l = 0, h = 0;
	int retv;

	/* First check for any errors that may of occurred
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
			ppa_fail(dev, DID_BUS_BUSY);
			return 0;
		}
		return 1;	/* wait until ppa_wakeup claims parport */
	case 1:		/* Phase 1 - Connected */
		{		/* Perform a sanity check for cable unplugged */
			int retv = 2;	/* Failed */

			ppa_connect(dev, CONNECT_EPP_MAYBE);

			w_ctr(ppb, 0xe);
			if ((r_str(ppb) & 0x08) == 0x08)
				retv--;

			w_ctr(ppb, 0xc);
			if ((r_str(ppb) & 0x08) == 0x00)
				retv--;

			if (retv) {
				if (time_after(jiffies, dev->jstart + (1 * HZ))) {
					printk(KERN_ERR "ppa: Parallel port cable is unplugged.\n");
					ppa_fail(dev, DID_BUS_BUSY);
					return 0;
				} else {
					ppa_disconnect(dev);
					return 1;	/* Try again in a jiffy */
				}
			}
			cmd->SCp.phase++;
		}
		/* fall through */

	case 2:		/* Phase 2 - We are now talking to the scsi bus */
		if (!ppa_select(dev, scmd_id(cmd))) {
			ppa_fail(dev, DID_NO_CONNECT);
			return 0;
		}
		cmd->SCp.phase++;
		/* fall through */

	case 3:		/* Phase 3 - Ready to accept a command */
		w_ctr(ppb, 0x0c);
		if (!(r_str(ppb) & 0x80))
			return 1;

		if (!ppa_send_command(cmd))
			return 0;
		cmd->SCp.phase++;
		/* fall through */

	case 4:		/* Phase 4 - Setup scatter/gather buffers */
		if (scsi_bufflen(cmd)) {
			cmd->SCp.buffer = scsi_sglist(cmd);
			cmd->SCp.this_residual = cmd->SCp.buffer->length;
			cmd->SCp.ptr = sg_virt(cmd->SCp.buffer);
		} else {
			cmd->SCp.buffer = NULL;
			cmd->SCp.this_residual = 0;
			cmd->SCp.ptr = NULL;
		}
		cmd->SCp.buffers_residual = scsi_sg_count(cmd) - 1;
		cmd->SCp.phase++;
		/* fall through */

	case 5:		/* Phase 5 - Data transfer stage */
		w_ctr(ppb, 0x0c);
		if (!(r_str(ppb) & 0x80))
			return 1;

		retv = ppa_completion(cmd);
		if (retv == -1)
			return 0;
		if (retv == 0)
			return 1;
		cmd->SCp.phase++;
		/* fall through */

	case 6:		/* Phase 6 - Read status/message */
		cmd->result = DID_OK << 16;
		/* Check for data overrun */
		if (ppa_wait(dev) != (unsigned char) 0xf0) {
			ppa_fail(dev, DID_ERROR);
			return 0;
		}
		if (ppa_in(dev, &l, 1)) {	/* read status byte */
			/* Check for optional message byte */
			if (ppa_wait(dev) == (unsigned char) 0xf0)
				ppa_in(dev, &h, 1);
			cmd->result =
			    (DID_OK << 16) + (h << 8) + (l & STATUS_MASK);
		}
		return 0;	/* Finished */
		break;

	default:
		printk(KERN_ERR "ppa: Invalid scsi phase\n");
	}
	return 0;
}

static int ppa_queuecommand_lck(struct scsi_cmnd *cmd,
		void (*done) (struct scsi_cmnd *))
{
	ppa_struct *dev = ppa_dev(cmd->device->host);

	if (dev->cur_cmd) {
		printk(KERN_ERR "PPA: bug in ppa_queuecommand\n");
		return 0;
	}
	dev->failed = 0;
	dev->jstart = jiffies;
	dev->cur_cmd = cmd;
	cmd->scsi_done = done;
	cmd->result = DID_ERROR << 16;	/* default return code */
	cmd->SCp.phase = 0;	/* bus free */

	schedule_delayed_work(&dev->ppa_tq, 0);

	ppa_pb_claim(dev);

	return 0;
}

static DEF_SCSI_QCMD(ppa_queuecommand)

/*
 * Apparently the disk->capacity attribute is off by 1 sector 
 * for all disk drives.  We add the one here, but it should really
 * be done in sd.c.  Even if it gets fixed there, this will still
 * work.
 */
static int ppa_biosparam(struct scsi_device *sdev, struct block_device *dev,
	      sector_t capacity, int ip[])
{
	ip[0] = 0x40;
	ip[1] = 0x20;
	ip[2] = ((unsigned long) capacity + 1) / (ip[0] * ip[1]);
	if (ip[2] > 1024) {
		ip[0] = 0xff;
		ip[1] = 0x3f;
		ip[2] = ((unsigned long) capacity + 1) / (ip[0] * ip[1]);
		if (ip[2] > 1023)
			ip[2] = 1023;
	}
	return 0;
}

static int ppa_abort(struct scsi_cmnd *cmd)
{
	ppa_struct *dev = ppa_dev(cmd->device->host);
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

static void ppa_reset_pulse(unsigned int base)
{
	w_dtr(base, 0x40);
	w_ctr(base, 0x8);
	udelay(30);
	w_ctr(base, 0xc);
}

static int ppa_reset(struct scsi_cmnd *cmd)
{
	ppa_struct *dev = ppa_dev(cmd->device->host);

	if (cmd->SCp.phase)
		ppa_disconnect(dev);
	dev->cur_cmd = NULL;	/* Forget the problem */

	ppa_connect(dev, CONNECT_NORMAL);
	ppa_reset_pulse(dev->base);
	mdelay(1);		/* device settle delay */
	ppa_disconnect(dev);
	mdelay(1);		/* device settle delay */
	return SUCCESS;
}

static int device_check(ppa_struct *dev)
{
	/* This routine looks for a device and then attempts to use EPP
	   to send a command. If all goes as planned then EPP is available. */

	static u8 cmd[6] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
	int loop, old_mode, status, k, ppb = dev->base;
	unsigned char l;

	old_mode = dev->mode;
	for (loop = 0; loop < 8; loop++) {
		/* Attempt to use EPP for Test Unit Ready */
		if ((ppb & 0x0007) == 0x0000)
			dev->mode = PPA_EPP_32;

second_pass:
		ppa_connect(dev, CONNECT_EPP_MAYBE);
		/* Select SCSI device */
		if (!ppa_select(dev, loop)) {
			ppa_disconnect(dev);
			continue;
		}
		printk(KERN_INFO "ppa: Found device at ID %i, Attempting to use %s\n",
		       loop, PPA_MODE_STRING[dev->mode]);

		/* Send SCSI command */
		status = 1;
		w_ctr(ppb, 0x0c);
		for (l = 0; (l < 6) && (status); l++)
			status = ppa_out(dev, cmd, 1);

		if (!status) {
			ppa_disconnect(dev);
			ppa_connect(dev, CONNECT_EPP_MAYBE);
			w_dtr(ppb, 0x40);
			w_ctr(ppb, 0x08);
			udelay(30);
			w_ctr(ppb, 0x0c);
			udelay(1000);
			ppa_disconnect(dev);
			udelay(1000);
			if (dev->mode == PPA_EPP_32) {
				dev->mode = old_mode;
				goto second_pass;
			}
			return -EIO;
		}
		w_ctr(ppb, 0x0c);
		k = 1000000;	/* 1 Second */
		do {
			l = r_str(ppb);
			k--;
			udelay(1);
		} while (!(l & 0x80) && (k));

		l &= 0xf0;

		if (l != 0xf0) {
			ppa_disconnect(dev);
			ppa_connect(dev, CONNECT_EPP_MAYBE);
			ppa_reset_pulse(ppb);
			udelay(1000);
			ppa_disconnect(dev);
			udelay(1000);
			if (dev->mode == PPA_EPP_32) {
				dev->mode = old_mode;
				goto second_pass;
			}
			return -EIO;
		}
		ppa_disconnect(dev);
		printk(KERN_INFO "ppa: Communication established with ID %i using %s\n",
		       loop, PPA_MODE_STRING[dev->mode]);
		ppa_connect(dev, CONNECT_EPP_MAYBE);
		ppa_reset_pulse(ppb);
		udelay(1000);
		ppa_disconnect(dev);
		udelay(1000);
		return 0;
	}
	return -ENODEV;
}

static int ppa_adjust_queue(struct scsi_device *device)
{
	blk_queue_bounce_limit(device->request_queue, BLK_BOUNCE_HIGH);
	return 0;
}

static struct scsi_host_template ppa_template = {
	.module			= THIS_MODULE,
	.proc_name		= "ppa",
	.show_info		= ppa_show_info,
	.write_info		= ppa_write_info,
	.name			= "Iomega VPI0 (ppa) interface",
	.queuecommand		= ppa_queuecommand,
	.eh_abort_handler	= ppa_abort,
	.eh_host_reset_handler	= ppa_reset,
	.bios_param		= ppa_biosparam,
	.this_id		= -1,
	.sg_tablesize		= SG_ALL,
	.can_queue		= 1,
	.slave_alloc		= ppa_adjust_queue,
};

/***************************************************************************
 *                   Parallel port probing routines                        *
 ***************************************************************************/

static LIST_HEAD(ppa_hosts);

/*
 * Finds the first available device number that can be alloted to the
 * new ppa device and returns the address of the previous node so that
 * we can add to the tail and have a list in the ascending order.
 */

static inline ppa_struct *find_parent(void)
{
	ppa_struct *dev, *par = NULL;
	unsigned int cnt = 0;

	if (list_empty(&ppa_hosts))
		return NULL;

	list_for_each_entry(dev, &ppa_hosts, list) {
		if (dev->dev_no != cnt)
			return par;
		cnt++;
		par = dev;
	}

	return par;
}

static int __ppa_attach(struct parport *pb)
{
	struct Scsi_Host *host;
	DECLARE_WAIT_QUEUE_HEAD_ONSTACK(waiting);
	DEFINE_WAIT(wait);
	ppa_struct *dev, *temp;
	int ports;
	int modes, ppb, ppb_hi;
	int err = -ENOMEM;
	struct pardev_cb ppa_cb;

	dev = kzalloc(sizeof(ppa_struct), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;
	dev->base = -1;
	dev->mode = PPA_AUTODETECT;
	dev->recon_tmo = PPA_RECON_TMO;
	init_waitqueue_head(&waiting);
	temp = find_parent();
	if (temp)
		dev->dev_no = temp->dev_no + 1;

	memset(&ppa_cb, 0, sizeof(ppa_cb));
	ppa_cb.private = dev;
	ppa_cb.wakeup = ppa_wakeup;

	dev->dev = parport_register_dev_model(pb, "ppa", &ppa_cb, dev->dev_no);

	if (!dev->dev)
		goto out;

	/* Claim the bus so it remembers what we do to the control
	 * registers. [ CTR and ECP ]
	 */
	err = -EBUSY;
	dev->waiting = &waiting;
	prepare_to_wait(&waiting, &wait, TASK_UNINTERRUPTIBLE);
	if (ppa_pb_claim(dev))
		schedule_timeout(3 * HZ);
	if (dev->wanted) {
		printk(KERN_ERR "ppa%d: failed to claim parport because "
				"a pardevice is owning the port for too long "
				"time!\n", pb->number);
		ppa_pb_dismiss(dev);
		dev->waiting = NULL;
		finish_wait(&waiting, &wait);
		goto out1;
	}
	dev->waiting = NULL;
	finish_wait(&waiting, &wait);
	ppb = dev->base = dev->dev->port->base;
	ppb_hi = dev->dev->port->base_hi;
	w_ctr(ppb, 0x0c);
	modes = dev->dev->port->modes;

	/* Mode detection works up the chain of speed
	 * This avoids a nasty if-then-else-if-... tree
	 */
	dev->mode = PPA_NIBBLE;

	if (modes & PARPORT_MODE_TRISTATE)
		dev->mode = PPA_PS2;

	if (modes & PARPORT_MODE_ECP) {
		w_ecr(ppb_hi, 0x20);
		dev->mode = PPA_PS2;
	}
	if ((modes & PARPORT_MODE_EPP) && (modes & PARPORT_MODE_ECP))
		w_ecr(ppb_hi, 0x80);

	/* Done configuration */

	err = ppa_init(dev);
	ppa_pb_release(dev);

	if (err)
		goto out1;

	/* now the glue ... */
	if (dev->mode == PPA_NIBBLE || dev->mode == PPA_PS2)
		ports = 3;
	else
		ports = 8;

	INIT_DELAYED_WORK(&dev->ppa_tq, ppa_interrupt);

	err = -ENOMEM;
	host = scsi_host_alloc(&ppa_template, sizeof(ppa_struct *));
	if (!host)
		goto out1;
	host->io_port = pb->base;
	host->n_io_port = ports;
	host->dma_channel = -1;
	host->unique_id = pb->number;
	*(ppa_struct **)&host->hostdata = dev;
	dev->host = host;
	list_add_tail(&dev->list, &ppa_hosts);
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

static void ppa_attach(struct parport *pb)
{
	__ppa_attach(pb);
}

static void ppa_detach(struct parport *pb)
{
	ppa_struct *dev;
	list_for_each_entry(dev, &ppa_hosts, list) {
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

static struct parport_driver ppa_driver = {
	.name		= "ppa",
	.match_port	= ppa_attach,
	.detach		= ppa_detach,
	.devmodel	= true,
};

static int __init ppa_driver_init(void)
{
	printk(KERN_INFO "ppa: Version %s\n", PPA_VERSION);
	return parport_register_driver(&ppa_driver);
}

static void __exit ppa_driver_exit(void)
{
	parport_unregister_driver(&ppa_driver);
}

module_init(ppa_driver_init);
module_exit(ppa_driver_exit);
MODULE_LICENSE("GPL");
