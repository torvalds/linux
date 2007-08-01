/*
 * Simulated SCSI driver.
 *
 * Copyright (C) 1999, 2001-2003 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 *	Stephane Eranian <eranian@hpl.hp.com>
 *
 * 02/01/15 David Mosberger	Updated for v2.5.1
 * 99/12/18 David Mosberger	Added support for READ10/WRITE10 needed by linux v2.3.33
 */
#include <linux/blkdev.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/timer.h>
#include <asm/irq.h>

#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>

#define DEBUG_SIMSCSI	0

#define SIMSCSI_REQ_QUEUE_LEN	64
#define DEFAULT_SIMSCSI_ROOT	"/var/ski-disks/sd"

/* Simulator system calls: */

#define SSC_OPEN			50
#define SSC_CLOSE			51
#define SSC_READ			52
#define SSC_WRITE			53
#define SSC_GET_COMPLETION		54
#define SSC_WAIT_COMPLETION		55

#define SSC_WRITE_ACCESS		2
#define SSC_READ_ACCESS			1

#if DEBUG_SIMSCSI
  int simscsi_debug;
# define DBG	simscsi_debug
#else
# define DBG	0
#endif

static struct Scsi_Host *host;

static void simscsi_interrupt (unsigned long val);
static DECLARE_TASKLET(simscsi_tasklet, simscsi_interrupt, 0);

struct disk_req {
	unsigned long addr;
	unsigned len;
};

struct disk_stat {
	int fd;
	unsigned count;
};

extern long ia64_ssc (long arg0, long arg1, long arg2, long arg3, int nr);

static int desc[16] = {
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
};

static struct queue_entry {
	struct scsi_cmnd *sc;
} queue[SIMSCSI_REQ_QUEUE_LEN];

static int rd, wr;
static atomic_t num_reqs = ATOMIC_INIT(0);

/* base name for default disks */
static char *simscsi_root = DEFAULT_SIMSCSI_ROOT;

#define MAX_ROOT_LEN	128

/*
 * used to setup a new base for disk images
 * to use /foo/bar/disk[a-z] as disk images
 * you have to specify simscsi=/foo/bar/disk on the command line
 */
static int __init
simscsi_setup (char *s)
{
	/* XXX Fix me we may need to strcpy() ? */
	if (strlen(s) > MAX_ROOT_LEN) {
		printk(KERN_ERR "simscsi_setup: prefix too long---using default %s\n",
		       simscsi_root);
	}
	simscsi_root = s;
	return 1;
}

__setup("simscsi=", simscsi_setup);

static void
simscsi_interrupt (unsigned long val)
{
	struct scsi_cmnd *sc;

	while ((sc = queue[rd].sc) != NULL) {
		atomic_dec(&num_reqs);
		queue[rd].sc = NULL;
		if (DBG)
			printk("simscsi_interrupt: done with %ld\n", sc->serial_number);
		(*sc->scsi_done)(sc);
		rd = (rd + 1) % SIMSCSI_REQ_QUEUE_LEN;
	}
}

static int
simscsi_biosparam (struct scsi_device *sdev, struct block_device *n,
		sector_t capacity, int ip[])
{
	ip[0] = 64;		/* heads */
	ip[1] = 32;		/* sectors */
	ip[2] = capacity >> 11;	/* cylinders */
	return 0;
}

static void
simscsi_sg_readwrite (struct scsi_cmnd *sc, int mode, unsigned long offset)
{
	int i;
	struct scatterlist *sl;
	struct disk_stat stat;
	struct disk_req req;

	stat.fd = desc[sc->device->id];

	scsi_for_each_sg(sc, sl, scsi_sg_count(sc), i) {
		req.addr = __pa(page_address(sl->page) + sl->offset);
		req.len  = sl->length;
		if (DBG)
			printk("simscsi_sg_%s @ %lx (off %lx) use_sg=%d len=%d\n",
			       mode == SSC_READ ? "read":"write", req.addr, offset,
			       scsi_sg_count(sc) - i, sl->length);
		ia64_ssc(stat.fd, 1, __pa(&req), offset, mode);
		ia64_ssc(__pa(&stat), 0, 0, 0, SSC_WAIT_COMPLETION);

		/* should not happen in our case */
		if (stat.count != req.len) {
			sc->result = DID_ERROR << 16;
			return;
		}
		offset +=  sl->length;
	}
	sc->result = GOOD;
}

/*
 * function handling both READ_6/WRITE_6 (non-scatter/gather mode)
 * commands.
 * Added 02/26/99 S.Eranian
 */
static void
simscsi_readwrite6 (struct scsi_cmnd *sc, int mode)
{
	unsigned long offset;

	offset = (((sc->cmnd[1] & 0x1f) << 16) | (sc->cmnd[2] << 8) | sc->cmnd[3])*512;
	simscsi_sg_readwrite(sc, mode, offset);
}

static size_t
simscsi_get_disk_size (int fd)
{
	struct disk_stat stat;
	size_t bit, sectors = 0;
	struct disk_req req;
	char buf[512];

	/*
	 * This is a bit kludgey: the simulator doesn't provide a
	 * direct way of determining the disk size, so we do a binary
	 * search, assuming a maximum disk size of 128GB.
	 */
	for (bit = (128UL << 30)/512; bit != 0; bit >>= 1) {
		req.addr = __pa(&buf);
		req.len = sizeof(buf);
		ia64_ssc(fd, 1, __pa(&req), ((sectors | bit) - 1)*512, SSC_READ);
		stat.fd = fd;
		ia64_ssc(__pa(&stat), 0, 0, 0, SSC_WAIT_COMPLETION);
		if (stat.count == sizeof(buf))
			sectors |= bit;
	}
	return sectors - 1;	/* return last valid sector number */
}

static void
simscsi_readwrite10 (struct scsi_cmnd *sc, int mode)
{
	unsigned long offset;

	offset = (((unsigned long)sc->cmnd[2] << 24) 
		| ((unsigned long)sc->cmnd[3] << 16)
		| ((unsigned long)sc->cmnd[4] <<  8) 
		| ((unsigned long)sc->cmnd[5] <<  0))*512UL;
	simscsi_sg_readwrite(sc, mode, offset);
}

static void simscsi_fillresult(struct scsi_cmnd *sc, char *buf, unsigned len)
{

	int i;
	unsigned thislen;
	struct scatterlist *slp;

	scsi_for_each_sg(sc, slp, scsi_sg_count(sc), i) {
		if (!len)
			break;
		thislen = min(len, slp->length);
		memcpy(page_address(slp->page) + slp->offset, buf, thislen);
		len -= thislen;
	}
}

static int
simscsi_queuecommand (struct scsi_cmnd *sc, void (*done)(struct scsi_cmnd *))
{
	unsigned int target_id = sc->device->id;
	char fname[MAX_ROOT_LEN+16];
	size_t disk_size;
	char *buf;
	char localbuf[36];
#if DEBUG_SIMSCSI
	register long sp asm ("sp");

	if (DBG)
		printk("simscsi_queuecommand: target=%d,cmnd=%u,sc=%lu,sp=%lx,done=%p\n",
		       target_id, sc->cmnd[0], sc->serial_number, sp, done);
#endif

	sc->result = DID_BAD_TARGET << 16;
	sc->scsi_done = done;
	if (target_id <= 15 && sc->device->lun == 0) {
		switch (sc->cmnd[0]) {
		      case INQUIRY:
			if (scsi_bufflen(sc) < 35) {
				break;
			}
			sprintf (fname, "%s%c", simscsi_root, 'a' + target_id);
			desc[target_id] = ia64_ssc(__pa(fname), SSC_READ_ACCESS|SSC_WRITE_ACCESS,
						   0, 0, SSC_OPEN);
			if (desc[target_id] < 0) {
				/* disk doesn't exist... */
				break;
			}
			buf = localbuf;
			buf[0] = 0;	/* magnetic disk */
			buf[1] = 0;	/* not a removable medium */
			buf[2] = 2;	/* SCSI-2 compliant device */
			buf[3] = 2;	/* SCSI-2 response data format */
			buf[4] = 31;	/* additional length (bytes) */
			buf[5] = 0;	/* reserved */
			buf[6] = 0;	/* reserved */
			buf[7] = 0;	/* various flags */
			memcpy(buf + 8, "HP      SIMULATED DISK  0.00",  28);
			simscsi_fillresult(sc, buf, 36);
			sc->result = GOOD;
			break;

		      case TEST_UNIT_READY:
			sc->result = GOOD;
			break;

		      case READ_6:
			if (desc[target_id] < 0 )
				break;
			simscsi_readwrite6(sc, SSC_READ);
			break;

		      case READ_10:
			if (desc[target_id] < 0 )
				break;
			simscsi_readwrite10(sc, SSC_READ);
			break;

		      case WRITE_6:
			if (desc[target_id] < 0)
				break;
			simscsi_readwrite6(sc, SSC_WRITE);
			break;

		      case WRITE_10:
			if (desc[target_id] < 0)
				break;
			simscsi_readwrite10(sc, SSC_WRITE);
			break;

		      case READ_CAPACITY:
			if (desc[target_id] < 0 || scsi_bufflen(sc) < 8) {
				break;
			}
			buf = localbuf;
			disk_size = simscsi_get_disk_size(desc[target_id]);

			buf[0] = (disk_size >> 24) & 0xff;
			buf[1] = (disk_size >> 16) & 0xff;
			buf[2] = (disk_size >>  8) & 0xff;
			buf[3] = (disk_size >>  0) & 0xff;
			/* set block size of 512 bytes: */
			buf[4] = 0;
			buf[5] = 0;
			buf[6] = 2;
			buf[7] = 0;
			simscsi_fillresult(sc, buf, 8);
			sc->result = GOOD;
			break;

		      case MODE_SENSE:
		      case MODE_SENSE_10:
			/* sd.c uses this to determine whether disk does write-caching. */
			simscsi_fillresult(sc, (char *)empty_zero_page, scsi_bufflen(sc));
			sc->result = GOOD;
			break;

		      case START_STOP:
			printk(KERN_ERR "START_STOP\n");
			break;

		      default:
			panic("simscsi: unknown SCSI command %u\n", sc->cmnd[0]);
		}
	}
	if (sc->result == DID_BAD_TARGET) {
		sc->result |= DRIVER_SENSE << 24;
		sc->sense_buffer[0] = 0x70;
		sc->sense_buffer[2] = 0x00;
	}
	if (atomic_read(&num_reqs) >= SIMSCSI_REQ_QUEUE_LEN) {
		panic("Attempt to queue command while command is pending!!");
	}
	atomic_inc(&num_reqs);
	queue[wr].sc = sc;
	wr = (wr + 1) % SIMSCSI_REQ_QUEUE_LEN;

	tasklet_schedule(&simscsi_tasklet);
	return 0;
}

static int
simscsi_host_reset (struct scsi_cmnd *sc)
{
	printk(KERN_ERR "simscsi_host_reset: not implemented\n");
	return 0;
}

static struct scsi_host_template driver_template = {
	.name			= "simulated SCSI host adapter",
	.proc_name		= "simscsi",
	.queuecommand		= simscsi_queuecommand,
	.eh_host_reset_handler	= simscsi_host_reset,
	.bios_param		= simscsi_biosparam,
	.can_queue		= SIMSCSI_REQ_QUEUE_LEN,
	.this_id		= -1,
	.sg_tablesize		= SG_ALL,
	.max_sectors		= 1024,
	.cmd_per_lun		= SIMSCSI_REQ_QUEUE_LEN,
	.use_clustering		= DISABLE_CLUSTERING,
};

static int __init
simscsi_init(void)
{
	int error;

	host = scsi_host_alloc(&driver_template, 0);
	if (!host)
		return -ENOMEM;

	error = scsi_add_host(host, NULL);
	if (!error)
		scsi_scan_host(host);
	return error;
}

static void __exit
simscsi_exit(void)
{
	scsi_remove_host(host);
	scsi_host_put(host);
}

module_init(simscsi_init);
module_exit(simscsi_exit);
