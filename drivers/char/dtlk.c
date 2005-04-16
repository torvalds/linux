/*                                              -*- linux-c -*-
 * dtlk.c - DoubleTalk PC driver for Linux
 *
 * Original author: Chris Pallotta <chris@allmedia.com>
 * Current maintainer: Jim Van Zandt <jrv@vanzandt.mv.com>
 * 
 * 2000-03-18 Jim Van Zandt: Fix polling.
 *  Eliminate dtlk_timer_active flag and separate dtlk_stop_timer
 *  function.  Don't restart timer in dtlk_timer_tick.  Restart timer
 *  in dtlk_poll after every poll.  dtlk_poll returns mask (duh).
 *  Eliminate unused function dtlk_write_byte.  Misc. code cleanups.
 */

/* This driver is for the DoubleTalk PC, a speech synthesizer
   manufactured by RC Systems (http://www.rcsys.com/).  It was written
   based on documentation in their User's Manual file and Developer's
   Tools disk.

   The DoubleTalk PC contains four voice synthesizers: text-to-speech
   (TTS), linear predictive coding (LPC), PCM/ADPCM, and CVSD.  It
   also has a tone generator.  Output data for LPC are written to the
   LPC port, and output data for the other modes are written to the
   TTS port.

   Two kinds of data can be read from the DoubleTalk: status
   information (in response to the "\001?" interrogation command) is
   read from the TTS port, and index markers (which mark the progress
   of the speech) are read from the LPC port.  Not all models of the
   DoubleTalk PC implement index markers.  Both the TTS and LPC ports
   can also display status flags.

   The DoubleTalk PC generates no interrupts.

   These characteristics are mapped into the Unix stream I/O model as
   follows:

   "write" sends bytes to the TTS port.  It is the responsibility of
   the user program to switch modes among TTS, PCM/ADPCM, and CVSD.
   This driver was written for use with the text-to-speech
   synthesizer.  If LPC output is needed some day, other minor device
   numbers can be used to select among output modes.

   "read" gets index markers from the LPC port.  If the device does
   not implement index markers, the read will fail with error EINVAL.

   Status information is available using the DTLK_INTERROGATE ioctl.

 */

#include <linux/module.h>

#define KERNEL
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/mm.h>		/* for verify_area */
#include <linux/errno.h>	/* for -EBUSY */
#include <linux/ioport.h>	/* for request_region */
#include <linux/delay.h>	/* for loops_per_jiffy */
#include <asm/io.h>		/* for inb_p, outb_p, inb, outb, etc. */
#include <asm/uaccess.h>	/* for get_user, etc. */
#include <linux/wait.h>		/* for wait_queue */
#include <linux/init.h>		/* for __init, module_{init,exit} */
#include <linux/poll.h>		/* for POLLIN, etc. */
#include <linux/dtlk.h>		/* local header file for DoubleTalk values */
#include <linux/devfs_fs_kernel.h>
#include <linux/smp_lock.h>

#ifdef TRACING
#define TRACE_TEXT(str) printk(str);
#define TRACE_RET printk(")")
#else				/* !TRACING */
#define TRACE_TEXT(str) ((void) 0)
#define TRACE_RET ((void) 0)
#endif				/* TRACING */


static int dtlk_major;
static int dtlk_port_lpc;
static int dtlk_port_tts;
static int dtlk_busy;
static int dtlk_has_indexing;
static unsigned int dtlk_portlist[] =
{0x25e, 0x29e, 0x2de, 0x31e, 0x35e, 0x39e, 0};
static wait_queue_head_t dtlk_process_list;
static struct timer_list dtlk_timer;

/* prototypes for file_operations struct */
static ssize_t dtlk_read(struct file *, char __user *,
			 size_t nbytes, loff_t * ppos);
static ssize_t dtlk_write(struct file *, const char __user *,
			  size_t nbytes, loff_t * ppos);
static unsigned int dtlk_poll(struct file *, poll_table *);
static int dtlk_open(struct inode *, struct file *);
static int dtlk_release(struct inode *, struct file *);
static int dtlk_ioctl(struct inode *inode, struct file *file,
		      unsigned int cmd, unsigned long arg);

static struct file_operations dtlk_fops =
{
	.owner		= THIS_MODULE,
	.read		= dtlk_read,
	.write		= dtlk_write,
	.poll		= dtlk_poll,
	.ioctl		= dtlk_ioctl,
	.open		= dtlk_open,
	.release	= dtlk_release,
};

/* local prototypes */
static int dtlk_dev_probe(void);
static struct dtlk_settings *dtlk_interrogate(void);
static int dtlk_readable(void);
static char dtlk_read_lpc(void);
static char dtlk_read_tts(void);
static int dtlk_writeable(void);
static char dtlk_write_bytes(const char *buf, int n);
static char dtlk_write_tts(char);
/*
   static void dtlk_handle_error(char, char, unsigned int);
 */
static void dtlk_timer_tick(unsigned long data);

static ssize_t dtlk_read(struct file *file, char __user *buf,
			 size_t count, loff_t * ppos)
{
	unsigned int minor = iminor(file->f_dentry->d_inode);
	char ch;
	int i = 0, retries;

	TRACE_TEXT("(dtlk_read");
	/*  printk("DoubleTalk PC - dtlk_read()\n"); */

	if (minor != DTLK_MINOR || !dtlk_has_indexing)
		return -EINVAL;

	for (retries = 0; retries < loops_per_jiffy; retries++) {
		while (i < count && dtlk_readable()) {
			ch = dtlk_read_lpc();
			/*        printk("dtlk_read() reads 0x%02x\n", ch); */
			if (put_user(ch, buf++))
				return -EFAULT;
			i++;
		}
		if (i)
			return i;
		if (file->f_flags & O_NONBLOCK)
			break;
		msleep_interruptible(100);
	}
	if (retries == loops_per_jiffy)
		printk(KERN_ERR "dtlk_read times out\n");
	TRACE_RET;
	return -EAGAIN;
}

static ssize_t dtlk_write(struct file *file, const char __user *buf,
			  size_t count, loff_t * ppos)
{
	int i = 0, retries = 0, ch;

	TRACE_TEXT("(dtlk_write");
#ifdef TRACING
	printk(" \"");
	{
		int i, ch;
		for (i = 0; i < count; i++) {
			if (get_user(ch, buf + i))
				return -EFAULT;
			if (' ' <= ch && ch <= '~')
				printk("%c", ch);
			else
				printk("\\%03o", ch);
		}
		printk("\"");
	}
#endif

	if (iminor(file->f_dentry->d_inode) != DTLK_MINOR)
		return -EINVAL;

	while (1) {
		while (i < count && !get_user(ch, buf) &&
		       (ch == DTLK_CLEAR || dtlk_writeable())) {
			dtlk_write_tts(ch);
			buf++;
			i++;
			if (i % 5 == 0)
				/* We yield our time until scheduled
				   again.  This reduces the transfer
				   rate to 500 bytes/sec, but that's
				   still enough to keep up with the
				   speech synthesizer. */
				msleep_interruptible(1);
			else {
				/* the RDY bit goes zero 2-3 usec
				   after writing, and goes 1 again
				   180-190 usec later.  Here, we wait
				   up to 250 usec for the RDY bit to
				   go nonzero. */
				for (retries = 0;
				     retries < loops_per_jiffy / (4000/HZ);
				     retries++)
					if (inb_p(dtlk_port_tts) &
					    TTS_WRITABLE)
						break;
			}
			retries = 0;
		}
		if (i == count)
			return i;
		if (file->f_flags & O_NONBLOCK)
			break;

		msleep_interruptible(1);

		if (++retries > 10 * HZ) { /* wait no more than 10 sec
					      from last write */
			printk("dtlk: write timeout.  "
			       "inb_p(dtlk_port_tts) = 0x%02x\n",
			       inb_p(dtlk_port_tts));
			TRACE_RET;
			return -EBUSY;
		}
	}
	TRACE_RET;
	return -EAGAIN;
}

static unsigned int dtlk_poll(struct file *file, poll_table * wait)
{
	int mask = 0;
	unsigned long expires;

	TRACE_TEXT(" dtlk_poll");
	/*
	   static long int j;
	   printk(".");
	   printk("<%ld>", jiffies-j);
	   j=jiffies;
	 */
	poll_wait(file, &dtlk_process_list, wait);

	if (dtlk_has_indexing && dtlk_readable()) {
	        del_timer(&dtlk_timer);
		mask = POLLIN | POLLRDNORM;
	}
	if (dtlk_writeable()) {
	        del_timer(&dtlk_timer);
		mask |= POLLOUT | POLLWRNORM;
	}
	/* there are no exception conditions */

	/* There won't be any interrupts, so we set a timer instead. */
	expires = jiffies + 3*HZ / 100;
	mod_timer(&dtlk_timer, expires);

	return mask;
}

static void dtlk_timer_tick(unsigned long data)
{
	TRACE_TEXT(" dtlk_timer_tick");
	wake_up_interruptible(&dtlk_process_list);
}

static int dtlk_ioctl(struct inode *inode,
		      struct file *file,
		      unsigned int cmd,
		      unsigned long arg)
{
	char __user *argp = (char __user *)arg;
	struct dtlk_settings *sp;
	char portval;
	TRACE_TEXT(" dtlk_ioctl");

	switch (cmd) {

	case DTLK_INTERROGATE:
		sp = dtlk_interrogate();
		if (copy_to_user(argp, sp, sizeof(struct dtlk_settings)))
			return -EINVAL;
		return 0;

	case DTLK_STATUS:
		portval = inb_p(dtlk_port_tts);
		return put_user(portval, argp);

	default:
		return -EINVAL;
	}
}

static int dtlk_open(struct inode *inode, struct file *file)
{
	TRACE_TEXT("(dtlk_open");

	nonseekable_open(inode, file);
	switch (iminor(inode)) {
	case DTLK_MINOR:
		if (dtlk_busy)
			return -EBUSY;
		return nonseekable_open(inode, file);

	default:
		return -ENXIO;
	}
}

static int dtlk_release(struct inode *inode, struct file *file)
{
	TRACE_TEXT("(dtlk_release");

	switch (iminor(inode)) {
	case DTLK_MINOR:
		break;

	default:
		break;
	}
	TRACE_RET;
	
	del_timer(&dtlk_timer);

	return 0;
}

static int __init dtlk_init(void)
{
	dtlk_port_lpc = 0;
	dtlk_port_tts = 0;
	dtlk_busy = 0;
	dtlk_major = register_chrdev(0, "dtlk", &dtlk_fops);
	if (dtlk_major == 0) {
		printk(KERN_ERR "DoubleTalk PC - cannot register device\n");
		return 0;
	}
	if (dtlk_dev_probe() == 0)
		printk(", MAJOR %d\n", dtlk_major);

	devfs_mk_cdev(MKDEV(dtlk_major, DTLK_MINOR),
		       S_IFCHR | S_IRUSR | S_IWUSR, "dtlk");

	init_timer(&dtlk_timer);
	dtlk_timer.function = dtlk_timer_tick;
	init_waitqueue_head(&dtlk_process_list);

	return 0;
}

static void __exit dtlk_cleanup (void)
{
	dtlk_write_bytes("goodbye", 8);
	msleep_interruptible(500);		/* nap 0.50 sec but
						   could be awakened
						   earlier by
						   signals... */

	dtlk_write_tts(DTLK_CLEAR);
	unregister_chrdev(dtlk_major, "dtlk");
	devfs_remove("dtlk");
	release_region(dtlk_port_lpc, DTLK_IO_EXTENT);
}

module_init(dtlk_init);
module_exit(dtlk_cleanup);

/* ------------------------------------------------------------------------ */

static int dtlk_readable(void)
{
#ifdef TRACING
	printk(" dtlk_readable=%u@%u", inb_p(dtlk_port_lpc) != 0x7f, jiffies);
#endif
	return inb_p(dtlk_port_lpc) != 0x7f;
}

static int dtlk_writeable(void)
{
	/* TRACE_TEXT(" dtlk_writeable"); */
#ifdef TRACINGMORE
	printk(" dtlk_writeable=%u", (inb_p(dtlk_port_tts) & TTS_WRITABLE)!=0);
#endif
	return inb_p(dtlk_port_tts) & TTS_WRITABLE;
}

static int __init dtlk_dev_probe(void)
{
	unsigned int testval = 0;
	int i = 0;
	struct dtlk_settings *sp;

	if (dtlk_port_lpc | dtlk_port_tts)
		return -EBUSY;

	for (i = 0; dtlk_portlist[i]; i++) {
#if 0
		printk("DoubleTalk PC - Port %03x = %04x\n",
		       dtlk_portlist[i], (testval = inw_p(dtlk_portlist[i])));
#endif

		if (!request_region(dtlk_portlist[i], DTLK_IO_EXTENT, 
			       "dtlk"))
			continue;
		testval = inw_p(dtlk_portlist[i]);
		if ((testval &= 0xfbff) == 0x107f) {
			dtlk_port_lpc = dtlk_portlist[i];
			dtlk_port_tts = dtlk_port_lpc + 1;

			sp = dtlk_interrogate();
			printk("DoubleTalk PC at %03x-%03x, "
			       "ROM version %s, serial number %u",
			       dtlk_portlist[i], dtlk_portlist[i] +
			       DTLK_IO_EXTENT - 1,
			       sp->rom_version, sp->serial_number);

                        /* put LPC port into known state, so
			   dtlk_readable() gives valid result */
			outb_p(0xff, dtlk_port_lpc); 

                        /* INIT string and index marker */
			dtlk_write_bytes("\036\1@\0\0012I\r", 8);
			/* posting an index takes 18 msec.  Here, we
			   wait up to 100 msec to see whether it
			   appears. */
			msleep_interruptible(100);
			dtlk_has_indexing = dtlk_readable();
#ifdef TRACING
			printk(", indexing %d\n", dtlk_has_indexing);
#endif
#ifdef INSCOPE
			{
/* This macro records ten samples read from the LPC port, for later display */
#define LOOK					\
for (i = 0; i < 10; i++)			\
  {						\
    buffer[b++] = inb_p(dtlk_port_lpc);		\
    __delay(loops_per_jiffy/(1000000/HZ));             \
  }
				char buffer[1000];
				int b = 0, i, j;

				LOOK
				outb_p(0xff, dtlk_port_lpc);
				buffer[b++] = 0;
				LOOK
				dtlk_write_bytes("\0012I\r", 4);
				buffer[b++] = 0;
				__delay(50 * loops_per_jiffy / (1000/HZ));
				outb_p(0xff, dtlk_port_lpc);
				buffer[b++] = 0;
				LOOK

				printk("\n");
				for (j = 0; j < b; j++)
					printk(" %02x", buffer[j]);
				printk("\n");
			}
#endif				/* INSCOPE */

#ifdef OUTSCOPE
			{
/* This macro records ten samples read from the TTS port, for later display */
#define LOOK					\
for (i = 0; i < 10; i++)			\
  {						\
    buffer[b++] = inb_p(dtlk_port_tts);		\
    __delay(loops_per_jiffy/(1000000/HZ));  /* 1 us */ \
  }
				char buffer[1000];
				int b = 0, i, j;

				mdelay(10);	/* 10 ms */
				LOOK
				outb_p(0x03, dtlk_port_tts);
				buffer[b++] = 0;
				LOOK
				LOOK

				printk("\n");
				for (j = 0; j < b; j++)
					printk(" %02x", buffer[j]);
				printk("\n");
			}
#endif				/* OUTSCOPE */

			dtlk_write_bytes("Double Talk found", 18);

			return 0;
		}
		release_region(dtlk_portlist[i], DTLK_IO_EXTENT);
	}

	printk(KERN_INFO "\nDoubleTalk PC - not found\n");
	return -ENODEV;
}

/*
   static void dtlk_handle_error(char op, char rc, unsigned int minor)
   {
   printk(KERN_INFO"\nDoubleTalk PC - MINOR: %d, OPCODE: %d, ERROR: %d\n", 
   minor, op, rc);
   return;
   }
 */

/* interrogate the DoubleTalk PC and return its settings */
static struct dtlk_settings *dtlk_interrogate(void)
{
	unsigned char *t;
	static char buf[sizeof(struct dtlk_settings) + 1];
	int total, i;
	static struct dtlk_settings status;
	TRACE_TEXT("(dtlk_interrogate");
	dtlk_write_bytes("\030\001?", 3);
	for (total = 0, i = 0; i < 50; i++) {
		buf[total] = dtlk_read_tts();
		if (total > 2 && buf[total] == 0x7f)
			break;
		if (total < sizeof(struct dtlk_settings))
			total++;
	}
	/*
	   if (i==50) printk("interrogate() read overrun\n");
	   for (i=0; i<sizeof(buf); i++)
	   printk(" %02x", buf[i]);
	   printk("\n");
	 */
	t = buf;
	status.serial_number = t[0] + t[1] * 256; /* serial number is
						     little endian */
	t += 2;

	i = 0;
	while (*t != '\r') {
		status.rom_version[i] = *t;
		if (i < sizeof(status.rom_version) - 1)
			i++;
		t++;
	}
	status.rom_version[i] = 0;
	t++;

	status.mode = *t++;
	status.punc_level = *t++;
	status.formant_freq = *t++;
	status.pitch = *t++;
	status.speed = *t++;
	status.volume = *t++;
	status.tone = *t++;
	status.expression = *t++;
	status.ext_dict_loaded = *t++;
	status.ext_dict_status = *t++;
	status.free_ram = *t++;
	status.articulation = *t++;
	status.reverb = *t++;
	status.eob = *t++;
	status.has_indexing = dtlk_has_indexing;
	TRACE_RET;
	return &status;
}

static char dtlk_read_tts(void)
{
	int portval, retries = 0;
	char ch;
	TRACE_TEXT("(dtlk_read_tts");

	/* verify DT is ready, read char, wait for ACK */
	do {
		portval = inb_p(dtlk_port_tts);
	} while ((portval & TTS_READABLE) == 0 &&
		 retries++ < DTLK_MAX_RETRIES);
	if (retries == DTLK_MAX_RETRIES)
		printk(KERN_ERR "dtlk_read_tts() timeout\n");

	ch = inb_p(dtlk_port_tts);	/* input from TTS port */
	ch &= 0x7f;
	outb_p(ch, dtlk_port_tts);

	retries = 0;
	do {
		portval = inb_p(dtlk_port_tts);
	} while ((portval & TTS_READABLE) != 0 &&
		 retries++ < DTLK_MAX_RETRIES);
	if (retries == DTLK_MAX_RETRIES)
		printk(KERN_ERR "dtlk_read_tts() timeout\n");

	TRACE_RET;
	return ch;
}

static char dtlk_read_lpc(void)
{
	int retries = 0;
	char ch;
	TRACE_TEXT("(dtlk_read_lpc");

	/* no need to test -- this is only called when the port is readable */

	ch = inb_p(dtlk_port_lpc);	/* input from LPC port */

	outb_p(0xff, dtlk_port_lpc);

	/* acknowledging a read takes 3-4
	   usec.  Here, we wait up to 20 usec
	   for the acknowledgement */
	retries = (loops_per_jiffy * 20) / (1000000/HZ);
	while (inb_p(dtlk_port_lpc) != 0x7f && --retries > 0);
	if (retries == 0)
		printk(KERN_ERR "dtlk_read_lpc() timeout\n");

	TRACE_RET;
	return ch;
}

/* write n bytes to tts port */
static char dtlk_write_bytes(const char *buf, int n)
{
	char val = 0;
	/*  printk("dtlk_write_bytes(\"%-*s\", %d)\n", n, buf, n); */
	TRACE_TEXT("(dtlk_write_bytes");
	while (n-- > 0)
		val = dtlk_write_tts(*buf++);
	TRACE_RET;
	return val;
}

static char dtlk_write_tts(char ch)
{
	int retries = 0;
#ifdef TRACINGMORE
	printk("  dtlk_write_tts(");
	if (' ' <= ch && ch <= '~')
		printk("'%c'", ch);
	else
		printk("0x%02x", ch);
#endif
	if (ch != DTLK_CLEAR)	/* no flow control for CLEAR command */
		while ((inb_p(dtlk_port_tts) & TTS_WRITABLE) == 0 &&
		       retries++ < DTLK_MAX_RETRIES)	/* DT ready? */
			;
	if (retries == DTLK_MAX_RETRIES)
		printk(KERN_ERR "dtlk_write_tts() timeout\n");

	outb_p(ch, dtlk_port_tts);	/* output to TTS port */
	/* the RDY bit goes zero 2-3 usec after writing, and goes
	   1 again 180-190 usec later.  Here, we wait up to 10
	   usec for the RDY bit to go zero. */
	for (retries = 0; retries < loops_per_jiffy / (100000/HZ); retries++)
		if ((inb_p(dtlk_port_tts) & TTS_WRITABLE) == 0)
			break;

#ifdef TRACINGMORE
	printk(")\n");
#endif
	return 0;
}

MODULE_LICENSE("GPL");
