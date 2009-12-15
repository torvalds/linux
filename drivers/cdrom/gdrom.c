/* GD ROM driver for the SEGA Dreamcast
 * copyright Adrian McMenamin, 2007
 * With thanks to Marcus Comstedt and Nathan Keynes
 * for work in reversing PIO and DMA
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>
#include <linux/cdrom.h>
#include <linux/genhd.h>
#include <linux/bio.h>
#include <linux/blkdev.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/wait.h>
#include <linux/workqueue.h>
#include <linux/platform_device.h>
#include <scsi/scsi.h>
#include <asm/io.h>
#include <asm/dma.h>
#include <asm/delay.h>
#include <mach/dma.h>
#include <mach/sysasic.h>

#define GDROM_DEV_NAME "gdrom"
#define GD_SESSION_OFFSET 150

/* GD Rom commands */
#define GDROM_COM_SOFTRESET 0x08
#define GDROM_COM_EXECDIAG 0x90
#define GDROM_COM_PACKET 0xA0
#define GDROM_COM_IDDEV 0xA1

/* GD Rom registers */
#define GDROM_BASE_REG			0xA05F7000
#define GDROM_ALTSTATUS_REG		(GDROM_BASE_REG + 0x18)
#define GDROM_DATA_REG			(GDROM_BASE_REG + 0x80)
#define GDROM_ERROR_REG		(GDROM_BASE_REG + 0x84)
#define GDROM_INTSEC_REG		(GDROM_BASE_REG + 0x88)
#define GDROM_SECNUM_REG		(GDROM_BASE_REG + 0x8C)
#define GDROM_BCL_REG			(GDROM_BASE_REG + 0x90)
#define GDROM_BCH_REG			(GDROM_BASE_REG + 0x94)
#define GDROM_DSEL_REG			(GDROM_BASE_REG + 0x98)
#define GDROM_STATUSCOMMAND_REG	(GDROM_BASE_REG + 0x9C)
#define GDROM_RESET_REG		(GDROM_BASE_REG + 0x4E4)

#define GDROM_DMA_STARTADDR_REG	(GDROM_BASE_REG + 0x404)
#define GDROM_DMA_LENGTH_REG		(GDROM_BASE_REG + 0x408)
#define GDROM_DMA_DIRECTION_REG	(GDROM_BASE_REG + 0x40C)
#define GDROM_DMA_ENABLE_REG		(GDROM_BASE_REG + 0x414)
#define GDROM_DMA_STATUS_REG		(GDROM_BASE_REG + 0x418)
#define GDROM_DMA_WAIT_REG		(GDROM_BASE_REG + 0x4A0)
#define GDROM_DMA_ACCESS_CTRL_REG	(GDROM_BASE_REG + 0x4B8)

#define GDROM_HARD_SECTOR	2048
#define BLOCK_LAYER_SECTOR	512
#define GD_TO_BLK		4

#define GDROM_DEFAULT_TIMEOUT	(HZ * 7)

static const struct {
	int sense_key;
	const char * const text;
} sense_texts[] = {
	{NO_SENSE, "OK"},
	{RECOVERED_ERROR, "Recovered from error"},
	{NOT_READY, "Device not ready"},
	{MEDIUM_ERROR, "Disk not ready"},
	{HARDWARE_ERROR, "Hardware error"},
	{ILLEGAL_REQUEST, "Command has failed"},
	{UNIT_ATTENTION, "Device needs attention - disk may have been changed"},
	{DATA_PROTECT, "Data protection error"},
	{ABORTED_COMMAND, "Command aborted"},
};

static struct platform_device *pd;
static int gdrom_major;
static DECLARE_WAIT_QUEUE_HEAD(command_queue);
static DECLARE_WAIT_QUEUE_HEAD(request_queue);

static DEFINE_SPINLOCK(gdrom_lock);
static void gdrom_readdisk_dma(struct work_struct *work);
static DECLARE_WORK(work, gdrom_readdisk_dma);
static LIST_HEAD(gdrom_deferred);

struct gdromtoc {
	unsigned int entry[99];
	unsigned int first, last;
	unsigned int leadout;
};

static struct gdrom_unit {
	struct gendisk *disk;
	struct cdrom_device_info *cd_info;
	int status;
	int pending;
	int transfer;
	char disk_type;
	struct gdromtoc *toc;
	struct request_queue *gdrom_rq;
} gd;

struct gdrom_id {
	char mid;
	char modid;
	char verid;
	char padA[13];
	char mname[16];
	char modname[16];
	char firmver[16];
	char padB[16];
};

static int gdrom_getsense(short *bufstring);
static int gdrom_packetcommand(struct cdrom_device_info *cd_info,
	struct packet_command *command);
static int gdrom_hardreset(struct cdrom_device_info *cd_info);

static bool gdrom_is_busy(void)
{
	return (ctrl_inb(GDROM_ALTSTATUS_REG) & 0x80) != 0;
}

static bool gdrom_data_request(void)
{
	return (ctrl_inb(GDROM_ALTSTATUS_REG) & 0x88) == 8;
}

static bool gdrom_wait_clrbusy(void)
{
	unsigned long timeout = jiffies + GDROM_DEFAULT_TIMEOUT;
	while ((ctrl_inb(GDROM_ALTSTATUS_REG) & 0x80) &&
		(time_before(jiffies, timeout)))
		cpu_relax();
	return time_before(jiffies, timeout + 1);
}

static bool gdrom_wait_busy_sleeps(void)
{
	unsigned long timeout;
	/* Wait to get busy first */
	timeout = jiffies + GDROM_DEFAULT_TIMEOUT;
	while (!gdrom_is_busy() && time_before(jiffies, timeout))
		cpu_relax();
	/* Now wait for busy to clear */
	return gdrom_wait_clrbusy();
}

static void gdrom_identifydevice(void *buf)
{
	int c;
	short *data = buf;
	/* If the device won't clear it has probably
	* been hit by a serious failure - but we'll
	* try to return a sense key even so */
	if (!gdrom_wait_clrbusy()) {
		gdrom_getsense(NULL);
		return;
	}
	ctrl_outb(GDROM_COM_IDDEV, GDROM_STATUSCOMMAND_REG);
	if (!gdrom_wait_busy_sleeps()) {
		gdrom_getsense(NULL);
		return;
	}
	/* now read in the data */
	for (c = 0; c < 40; c++)
		data[c] = ctrl_inw(GDROM_DATA_REG);
}

static void gdrom_spicommand(void *spi_string, int buflen)
{
	short *cmd = spi_string;
	unsigned long timeout;

	/* ensure IRQ_WAIT is set */
	ctrl_outb(0x08, GDROM_ALTSTATUS_REG);
	/* specify how many bytes we expect back */
	ctrl_outb(buflen & 0xFF, GDROM_BCL_REG);
	ctrl_outb((buflen >> 8) & 0xFF, GDROM_BCH_REG);
	/* other parameters */
	ctrl_outb(0, GDROM_INTSEC_REG);
	ctrl_outb(0, GDROM_SECNUM_REG);
	ctrl_outb(0, GDROM_ERROR_REG);
	/* Wait until we can go */
	if (!gdrom_wait_clrbusy()) {
		gdrom_getsense(NULL);
		return;
	}
	timeout = jiffies + GDROM_DEFAULT_TIMEOUT;
	ctrl_outb(GDROM_COM_PACKET, GDROM_STATUSCOMMAND_REG);
	while (!gdrom_data_request() && time_before(jiffies, timeout))
		cpu_relax();
	if (!time_before(jiffies, timeout + 1)) {
		gdrom_getsense(NULL);
		return;
	}
	outsw(GDROM_DATA_REG, cmd, 6);
}


/* gdrom_command_executediagnostic:
 * Used to probe for presence of working GDROM
 * Restarts GDROM device and then applies standard ATA 3
 * Execute Diagnostic Command: a return of '1' indicates device 0
 * present and device 1 absent
 */
static char gdrom_execute_diagnostic(void)
{
	gdrom_hardreset(gd.cd_info);
	if (!gdrom_wait_clrbusy())
		return 0;
	ctrl_outb(GDROM_COM_EXECDIAG, GDROM_STATUSCOMMAND_REG);
	if (!gdrom_wait_busy_sleeps())
		return 0;
	return ctrl_inb(GDROM_ERROR_REG);
}

/*
 * Prepare disk command
 * byte 0 = 0x70
 * byte 1 = 0x1f
 */
static int gdrom_preparedisk_cmd(void)
{
	struct packet_command *spin_command;
	spin_command = kzalloc(sizeof(struct packet_command), GFP_KERNEL);
	if (!spin_command)
		return -ENOMEM;
	spin_command->cmd[0] = 0x70;
	spin_command->cmd[2] = 0x1f;
	spin_command->buflen = 0;
	gd.pending = 1;
	gdrom_packetcommand(gd.cd_info, spin_command);
	/* 60 second timeout */
	wait_event_interruptible_timeout(command_queue, gd.pending == 0,
		GDROM_DEFAULT_TIMEOUT);
	gd.pending = 0;
	kfree(spin_command);
	if (gd.status & 0x01) {
		/* log an error */
		gdrom_getsense(NULL);
		return -EIO;
	}
	return 0;
}

/*
 * Read TOC command
 * byte 0 = 0x14
 * byte 1 = session
 * byte 3 = sizeof TOC >> 8  ie upper byte
 * byte 4 = sizeof TOC & 0xff ie lower byte
 */
static int gdrom_readtoc_cmd(struct gdromtoc *toc, int session)
{
	int tocsize;
	struct packet_command *toc_command;
	int err = 0;

	toc_command = kzalloc(sizeof(struct packet_command), GFP_KERNEL);
	if (!toc_command)
		return -ENOMEM;
	tocsize = sizeof(struct gdromtoc);
	toc_command->cmd[0] = 0x14;
	toc_command->cmd[1] = session;
	toc_command->cmd[3] = tocsize >> 8;
	toc_command->cmd[4] = tocsize & 0xff;
	toc_command->buflen = tocsize;
	if (gd.pending) {
		err = -EBUSY;
		goto cleanup_readtoc_final;
	}
	gd.pending = 1;
	gdrom_packetcommand(gd.cd_info, toc_command);
	wait_event_interruptible_timeout(command_queue, gd.pending == 0,
		GDROM_DEFAULT_TIMEOUT);
	if (gd.pending) {
		err = -EINVAL;
		goto cleanup_readtoc;
	}
	insw(GDROM_DATA_REG, toc, tocsize/2);
	if (gd.status & 0x01)
		err = -EINVAL;

cleanup_readtoc:
	gd.pending = 0;
cleanup_readtoc_final:
	kfree(toc_command);
	return err;
}

/* TOC helpers */
static int get_entry_lba(int track)
{
	return (cpu_to_be32(track & 0xffffff00) - GD_SESSION_OFFSET);
}

static int get_entry_q_ctrl(int track)
{
	return (track & 0x000000f0) >> 4;
}

static int get_entry_track(int track)
{
	return (track & 0x0000ff00) >> 8;
}

static int gdrom_get_last_session(struct cdrom_device_info *cd_info,
	struct cdrom_multisession *ms_info)
{
	int fentry, lentry, track, data, tocuse, err;
	if (!gd.toc)
		return -ENOMEM;
	tocuse = 1;
	/* Check if GD-ROM */
	err = gdrom_readtoc_cmd(gd.toc, 1);
	/* Not a GD-ROM so check if standard CD-ROM */
	if (err) {
		tocuse = 0;
		err = gdrom_readtoc_cmd(gd.toc, 0);
		if (err) {
			printk(KERN_INFO "GDROM: Could not get CD "
				"table of contents\n");
			return -ENXIO;
		}
	}

	fentry = get_entry_track(gd.toc->first);
	lentry = get_entry_track(gd.toc->last);
	/* Find the first data track */
	track = get_entry_track(gd.toc->last);
	do {
		data = gd.toc->entry[track - 1];
		if (get_entry_q_ctrl(data))
			break;	/* ie a real data track */
		track--;
	} while (track >= fentry);

	if ((track > 100) || (track < get_entry_track(gd.toc->first))) {
		printk(KERN_INFO "GDROM: No data on the last "
			"session of the CD\n");
		gdrom_getsense(NULL);
		return -ENXIO;
	}

	ms_info->addr_format = CDROM_LBA;
	ms_info->addr.lba = get_entry_lba(data);
	ms_info->xa_flag = 1;
	return 0;
}

static int gdrom_open(struct cdrom_device_info *cd_info, int purpose)
{
	/* spin up the disk */
	return gdrom_preparedisk_cmd();
}

/* this function is required even if empty */
static void gdrom_release(struct cdrom_device_info *cd_info)
{
}

static int gdrom_drivestatus(struct cdrom_device_info *cd_info, int ignore)
{
	/* read the sense key */
	char sense = ctrl_inb(GDROM_ERROR_REG);
	sense &= 0xF0;
	if (sense == 0)
		return CDS_DISC_OK;
	if (sense == 0x20)
		return CDS_DRIVE_NOT_READY;
	/* default */
	return CDS_NO_INFO;
}

static int gdrom_mediachanged(struct cdrom_device_info *cd_info, int ignore)
{
	/* check the sense key */
	return (ctrl_inb(GDROM_ERROR_REG) & 0xF0) == 0x60;
}

/* reset the G1 bus */
static int gdrom_hardreset(struct cdrom_device_info *cd_info)
{
	int count;
	ctrl_outl(0x1fffff, GDROM_RESET_REG);
	for (count = 0xa0000000; count < 0xa0200000; count += 4)
		ctrl_inl(count);
	return 0;
}

/* keep the function looking like the universal
 * CD Rom specification  - returning int */
static int gdrom_packetcommand(struct cdrom_device_info *cd_info,
	struct packet_command *command)
{
	gdrom_spicommand(&command->cmd, command->buflen);
	return 0;
}

/* Get Sense SPI command
 * From Marcus Comstedt
 * cmd = 0x13
 * cmd + 4 = length of returned buffer
 * Returns 5 16 bit words
 */
static int gdrom_getsense(short *bufstring)
{
	struct packet_command *sense_command;
	short sense[5];
	int sense_key;
	int err = -EIO;

	sense_command = kzalloc(sizeof(struct packet_command), GFP_KERNEL);
	if (!sense_command)
		return -ENOMEM;
	sense_command->cmd[0] = 0x13;
	sense_command->cmd[4] = 10;
	sense_command->buflen = 10;
	/* even if something is pending try to get
	* the sense key if possible */
	if (gd.pending && !gdrom_wait_clrbusy()) {
		err = -EBUSY;
		goto cleanup_sense_final;
	}
	gd.pending = 1;
	gdrom_packetcommand(gd.cd_info, sense_command);
	wait_event_interruptible_timeout(command_queue, gd.pending == 0,
		GDROM_DEFAULT_TIMEOUT);
	if (gd.pending)
		goto cleanup_sense;
	insw(GDROM_DATA_REG, &sense, sense_command->buflen/2);
	if (sense[1] & 40) {
		printk(KERN_INFO "GDROM: Drive not ready - command aborted\n");
		goto cleanup_sense;
	}
	sense_key = sense[1] & 0x0F;
	if (sense_key < ARRAY_SIZE(sense_texts))
		printk(KERN_INFO "GDROM: %s\n", sense_texts[sense_key].text);
	else
		printk(KERN_ERR "GDROM: Unknown sense key: %d\n", sense_key);
	if (bufstring) /* return addional sense data */
		memcpy(bufstring, &sense[4], 2);
	if (sense_key < 2)
		err = 0;

cleanup_sense:
	gd.pending = 0;
cleanup_sense_final:
	kfree(sense_command);
	return err;
}

static int gdrom_audio_ioctl(struct cdrom_device_info *cdi, unsigned int cmd,
			     void *arg)
{
	return -EINVAL;
}

static struct cdrom_device_ops gdrom_ops = {
	.open			= gdrom_open,
	.release		= gdrom_release,
	.drive_status		= gdrom_drivestatus,
	.media_changed		= gdrom_mediachanged,
	.get_last_session	= gdrom_get_last_session,
	.reset			= gdrom_hardreset,
	.audio_ioctl		= gdrom_audio_ioctl,
	.capability		= CDC_MULTI_SESSION | CDC_MEDIA_CHANGED |
				  CDC_RESET | CDC_DRIVE_STATUS | CDC_CD_R,
	.n_minors		= 1,
};

static int gdrom_bdops_open(struct block_device *bdev, fmode_t mode)
{
	return cdrom_open(gd.cd_info, bdev, mode);
}

static int gdrom_bdops_release(struct gendisk *disk, fmode_t mode)
{
	cdrom_release(gd.cd_info, mode);
	return 0;
}

static int gdrom_bdops_mediachanged(struct gendisk *disk)
{
	return cdrom_media_changed(gd.cd_info);
}

static int gdrom_bdops_ioctl(struct block_device *bdev, fmode_t mode,
	unsigned cmd, unsigned long arg)
{
	return cdrom_ioctl(gd.cd_info, bdev, mode, cmd, arg);
}

static const struct block_device_operations gdrom_bdops = {
	.owner			= THIS_MODULE,
	.open			= gdrom_bdops_open,
	.release		= gdrom_bdops_release,
	.media_changed		= gdrom_bdops_mediachanged,
	.locked_ioctl		= gdrom_bdops_ioctl,
};

static irqreturn_t gdrom_command_interrupt(int irq, void *dev_id)
{
	gd.status = ctrl_inb(GDROM_STATUSCOMMAND_REG);
	if (gd.pending != 1)
		return IRQ_HANDLED;
	gd.pending = 0;
	wake_up_interruptible(&command_queue);
	return IRQ_HANDLED;
}

static irqreturn_t gdrom_dma_interrupt(int irq, void *dev_id)
{
	gd.status = ctrl_inb(GDROM_STATUSCOMMAND_REG);
	if (gd.transfer != 1)
		return IRQ_HANDLED;
	gd.transfer = 0;
	wake_up_interruptible(&request_queue);
	return IRQ_HANDLED;
}

static int __devinit gdrom_set_interrupt_handlers(void)
{
	int err;

	err = request_irq(HW_EVENT_GDROM_CMD, gdrom_command_interrupt,
		IRQF_DISABLED, "gdrom_command", &gd);
	if (err)
		return err;
	err = request_irq(HW_EVENT_GDROM_DMA, gdrom_dma_interrupt,
		IRQF_DISABLED, "gdrom_dma", &gd);
	if (err)
		free_irq(HW_EVENT_GDROM_CMD, &gd);
	return err;
}

/* Implement DMA read using SPI command
 * 0 -> 0x30
 * 1 -> mode
 * 2 -> block >> 16
 * 3 -> block >> 8
 * 4 -> block
 * 8 -> sectors >> 16
 * 9 -> sectors >> 8
 * 10 -> sectors
 */
static void gdrom_readdisk_dma(struct work_struct *work)
{
	int err, block, block_cnt;
	struct packet_command *read_command;
	struct list_head *elem, *next;
	struct request *req;
	unsigned long timeout;

	if (list_empty(&gdrom_deferred))
		return;
	read_command = kzalloc(sizeof(struct packet_command), GFP_KERNEL);
	if (!read_command)
		return; /* get more memory later? */
	read_command->cmd[0] = 0x30;
	read_command->cmd[1] = 0x20;
	spin_lock(&gdrom_lock);
	list_for_each_safe(elem, next, &gdrom_deferred) {
		req = list_entry(elem, struct request, queuelist);
		spin_unlock(&gdrom_lock);
		block = blk_rq_pos(req)/GD_TO_BLK + GD_SESSION_OFFSET;
		block_cnt = blk_rq_sectors(req)/GD_TO_BLK;
		ctrl_outl(virt_to_phys(req->buffer), GDROM_DMA_STARTADDR_REG);
		ctrl_outl(block_cnt * GDROM_HARD_SECTOR, GDROM_DMA_LENGTH_REG);
		ctrl_outl(1, GDROM_DMA_DIRECTION_REG);
		ctrl_outl(1, GDROM_DMA_ENABLE_REG);
		read_command->cmd[2] = (block >> 16) & 0xFF;
		read_command->cmd[3] = (block >> 8) & 0xFF;
		read_command->cmd[4] = block & 0xFF;
		read_command->cmd[8] = (block_cnt >> 16) & 0xFF;
		read_command->cmd[9] = (block_cnt >> 8) & 0xFF;
		read_command->cmd[10] = block_cnt & 0xFF;
		/* set for DMA */
		ctrl_outb(1, GDROM_ERROR_REG);
		/* other registers */
		ctrl_outb(0, GDROM_SECNUM_REG);
		ctrl_outb(0, GDROM_BCL_REG);
		ctrl_outb(0, GDROM_BCH_REG);
		ctrl_outb(0, GDROM_DSEL_REG);
		ctrl_outb(0, GDROM_INTSEC_REG);
		/* Wait for registers to reset after any previous activity */
		timeout = jiffies + HZ / 2;
		while (gdrom_is_busy() && time_before(jiffies, timeout))
			cpu_relax();
		ctrl_outb(GDROM_COM_PACKET, GDROM_STATUSCOMMAND_REG);
		timeout = jiffies + HZ / 2;
		/* Wait for packet command to finish */
		while (gdrom_is_busy() && time_before(jiffies, timeout))
			cpu_relax();
		gd.pending = 1;
		gd.transfer = 1;
		outsw(GDROM_DATA_REG, &read_command->cmd, 6);
		timeout = jiffies + HZ / 2;
		/* Wait for any pending DMA to finish */
		while (ctrl_inb(GDROM_DMA_STATUS_REG) &&
			time_before(jiffies, timeout))
			cpu_relax();
		/* start transfer */
		ctrl_outb(1, GDROM_DMA_STATUS_REG);
		wait_event_interruptible_timeout(request_queue,
			gd.transfer == 0, GDROM_DEFAULT_TIMEOUT);
		err = gd.transfer ? -EIO : 0;
		gd.transfer = 0;
		gd.pending = 0;
		/* now seek to take the request spinlock
		* before handling ending the request */
		spin_lock(&gdrom_lock);
		list_del_init(&req->queuelist);
		__blk_end_request_all(req, err);
	}
	spin_unlock(&gdrom_lock);
	kfree(read_command);
}

static void gdrom_request(struct request_queue *rq)
{
	struct request *req;

	while ((req = blk_fetch_request(rq)) != NULL) {
		if (!blk_fs_request(req)) {
			printk(KERN_DEBUG "GDROM: Non-fs request ignored\n");
			__blk_end_request_all(req, -EIO);
			continue;
		}
		if (rq_data_dir(req) != READ) {
			printk(KERN_NOTICE "GDROM: Read only device -");
			printk(" write request ignored\n");
			__blk_end_request_all(req, -EIO);
			continue;
		}

		/*
		 * Add to list of deferred work and then schedule
		 * workqueue.
		 */
		list_add_tail(&req->queuelist, &gdrom_deferred);
		schedule_work(&work);
	}
}

/* Print string identifying GD ROM device */
static int __devinit gdrom_outputversion(void)
{
	struct gdrom_id *id;
	char *model_name, *manuf_name, *firmw_ver;
	int err = -ENOMEM;

	/* query device ID */
	id = kzalloc(sizeof(struct gdrom_id), GFP_KERNEL);
	if (!id)
		return err;
	gdrom_identifydevice(id);
	model_name = kstrndup(id->modname, 16, GFP_KERNEL);
	if (!model_name)
		goto free_id;
	manuf_name = kstrndup(id->mname, 16, GFP_KERNEL);
	if (!manuf_name)
		goto free_model_name;
	firmw_ver = kstrndup(id->firmver, 16, GFP_KERNEL);
	if (!firmw_ver)
		goto free_manuf_name;
	printk(KERN_INFO "GDROM: %s from %s with firmware %s\n",
		model_name, manuf_name, firmw_ver);
	err = 0;
	kfree(firmw_ver);
free_manuf_name:
	kfree(manuf_name);
free_model_name:
	kfree(model_name);
free_id:
	kfree(id);
	return err;
}

/* set the default mode for DMA transfer */
static int __devinit gdrom_init_dma_mode(void)
{
	ctrl_outb(0x13, GDROM_ERROR_REG);
	ctrl_outb(0x22, GDROM_INTSEC_REG);
	if (!gdrom_wait_clrbusy())
		return -EBUSY;
	ctrl_outb(0xEF, GDROM_STATUSCOMMAND_REG);
	if (!gdrom_wait_busy_sleeps())
		return -EBUSY;
	/* Memory protection setting for GDROM DMA
	* Bits 31 - 16 security: 0x8843
	* Bits 15 and 7 reserved (0)
	* Bits 14 - 8 start of transfer range in 1 MB blocks OR'ed with 0x80
	* Bits 6 - 0 end of transfer range in 1 MB blocks OR'ed with 0x80
	* (0x40 | 0x80) = start range at 0x0C000000
	* (0x7F | 0x80) = end range at 0x0FFFFFFF */
	ctrl_outl(0x8843407F, GDROM_DMA_ACCESS_CTRL_REG);
	ctrl_outl(9, GDROM_DMA_WAIT_REG); /* DMA word setting */
	return 0;
}

static void __devinit probe_gdrom_setupcd(void)
{
	gd.cd_info->ops = &gdrom_ops;
	gd.cd_info->capacity = 1;
	strcpy(gd.cd_info->name, GDROM_DEV_NAME);
	gd.cd_info->mask = CDC_CLOSE_TRAY|CDC_OPEN_TRAY|CDC_LOCK|
		CDC_SELECT_DISC;
}

static void __devinit probe_gdrom_setupdisk(void)
{
	gd.disk->major = gdrom_major;
	gd.disk->first_minor = 1;
	gd.disk->minors = 1;
	strcpy(gd.disk->disk_name, GDROM_DEV_NAME);
}

static int __devinit probe_gdrom_setupqueue(void)
{
	blk_queue_logical_block_size(gd.gdrom_rq, GDROM_HARD_SECTOR);
	/* using DMA so memory will need to be contiguous */
	blk_queue_max_hw_segments(gd.gdrom_rq, 1);
	/* set a large max size to get most from DMA */
	blk_queue_max_segment_size(gd.gdrom_rq, 0x40000);
	gd.disk->queue = gd.gdrom_rq;
	return gdrom_init_dma_mode();
}

/*
 * register this as a block device and as compliant with the
 * universal CD Rom driver interface
 */
static int __devinit probe_gdrom(struct platform_device *devptr)
{
	int err;
	/* Start the device */
	if (gdrom_execute_diagnostic() != 1) {
		printk(KERN_WARNING "GDROM: ATA Probe for GDROM failed.\n");
		return -ENODEV;
	}
	/* Print out firmware ID */
	if (gdrom_outputversion())
		return -ENOMEM;
	/* Register GDROM */
	gdrom_major = register_blkdev(0, GDROM_DEV_NAME);
	if (gdrom_major <= 0)
		return gdrom_major;
	printk(KERN_INFO "GDROM: Registered with major number %d\n",
		gdrom_major);
	/* Specify basic properties of drive */
	gd.cd_info = kzalloc(sizeof(struct cdrom_device_info), GFP_KERNEL);
	if (!gd.cd_info) {
		err = -ENOMEM;
		goto probe_fail_no_mem;
	}
	probe_gdrom_setupcd();
	gd.disk = alloc_disk(1);
	if (!gd.disk) {
		err = -ENODEV;
		goto probe_fail_no_disk;
	}
	probe_gdrom_setupdisk();
	if (register_cdrom(gd.cd_info)) {
		err = -ENODEV;
		goto probe_fail_cdrom_register;
	}
	gd.disk->fops = &gdrom_bdops;
	/* latch on to the interrupt */
	err = gdrom_set_interrupt_handlers();
	if (err)
		goto probe_fail_cmdirq_register;
	gd.gdrom_rq = blk_init_queue(gdrom_request, &gdrom_lock);
	if (!gd.gdrom_rq)
		goto probe_fail_requestq;

	err = probe_gdrom_setupqueue();
	if (err)
		goto probe_fail_toc;

	gd.toc = kzalloc(sizeof(struct gdromtoc), GFP_KERNEL);
	if (!gd.toc)
		goto probe_fail_toc;
	add_disk(gd.disk);
	return 0;

probe_fail_toc:
	blk_cleanup_queue(gd.gdrom_rq);
probe_fail_requestq:
	free_irq(HW_EVENT_GDROM_DMA, &gd);
	free_irq(HW_EVENT_GDROM_CMD, &gd);
probe_fail_cmdirq_register:
probe_fail_cdrom_register:
	del_gendisk(gd.disk);
probe_fail_no_disk:
	kfree(gd.cd_info);
	unregister_blkdev(gdrom_major, GDROM_DEV_NAME);
	gdrom_major = 0;
probe_fail_no_mem:
	printk(KERN_WARNING "GDROM: Probe failed - error is 0x%X\n", err);
	return err;
}

static int __devexit remove_gdrom(struct platform_device *devptr)
{
	flush_scheduled_work();
	blk_cleanup_queue(gd.gdrom_rq);
	free_irq(HW_EVENT_GDROM_CMD, &gd);
	free_irq(HW_EVENT_GDROM_DMA, &gd);
	del_gendisk(gd.disk);
	if (gdrom_major)
		unregister_blkdev(gdrom_major, GDROM_DEV_NAME);
	unregister_cdrom(gd.cd_info);

	return 0;
}

static struct platform_driver gdrom_driver = {
	.probe = probe_gdrom,
	.remove = __devexit_p(remove_gdrom),
	.driver = {
			.name = GDROM_DEV_NAME,
	},
};

static int __init init_gdrom(void)
{
	int rc;
	gd.toc = NULL;
	rc = platform_driver_register(&gdrom_driver);
	if (rc)
		return rc;
	pd = platform_device_register_simple(GDROM_DEV_NAME, -1, NULL, 0);
	if (IS_ERR(pd)) {
		platform_driver_unregister(&gdrom_driver);
		return PTR_ERR(pd);
	}
	return 0;
}

static void __exit exit_gdrom(void)
{
	platform_device_unregister(pd);
	platform_driver_unregister(&gdrom_driver);
	kfree(gd.toc);
}

module_init(init_gdrom);
module_exit(exit_gdrom);
MODULE_AUTHOR("Adrian McMenamin <adrian@mcmen.demon.co.uk>");
MODULE_DESCRIPTION("SEGA Dreamcast GD-ROM Driver");
MODULE_LICENSE("GPL");
