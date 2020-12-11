// SPDX-License-Identifier: GPL-2.0-or-later
/* GD ROM driver for the SEGA Dreamcast
 * copyright Adrian McMenamin, 2007
 * With thanks to Marcus Comstedt and Nathan Keynes
 * for work in reversing PIO and DMA
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

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
#include <linux/blk-mq.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/wait.h>
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

static DEFINE_MUTEX(gdrom_mutex);
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
	struct blk_mq_tag_set tag_set;
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
	return (__raw_readb(GDROM_ALTSTATUS_REG) & 0x80) != 0;
}

static bool gdrom_data_request(void)
{
	return (__raw_readb(GDROM_ALTSTATUS_REG) & 0x88) == 8;
}

static bool gdrom_wait_clrbusy(void)
{
	unsigned long timeout = jiffies + GDROM_DEFAULT_TIMEOUT;
	while ((__raw_readb(GDROM_ALTSTATUS_REG) & 0x80) &&
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
	__raw_writeb(GDROM_COM_IDDEV, GDROM_STATUSCOMMAND_REG);
	if (!gdrom_wait_busy_sleeps()) {
		gdrom_getsense(NULL);
		return;
	}
	/* now read in the data */
	for (c = 0; c < 40; c++)
		data[c] = __raw_readw(GDROM_DATA_REG);
}

static void gdrom_spicommand(void *spi_string, int buflen)
{
	short *cmd = spi_string;
	unsigned long timeout;

	/* ensure IRQ_WAIT is set */
	__raw_writeb(0x08, GDROM_ALTSTATUS_REG);
	/* specify how many bytes we expect back */
	__raw_writeb(buflen & 0xFF, GDROM_BCL_REG);
	__raw_writeb((buflen >> 8) & 0xFF, GDROM_BCH_REG);
	/* other parameters */
	__raw_writeb(0, GDROM_INTSEC_REG);
	__raw_writeb(0, GDROM_SECNUM_REG);
	__raw_writeb(0, GDROM_ERROR_REG);
	/* Wait until we can go */
	if (!gdrom_wait_clrbusy()) {
		gdrom_getsense(NULL);
		return;
	}
	timeout = jiffies + GDROM_DEFAULT_TIMEOUT;
	__raw_writeb(GDROM_COM_PACKET, GDROM_STATUSCOMMAND_REG);
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
	__raw_writeb(GDROM_COM_EXECDIAG, GDROM_STATUSCOMMAND_REG);
	if (!gdrom_wait_busy_sleeps())
		return 0;
	return __raw_readb(GDROM_ERROR_REG);
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
	int fentry, lentry, track, data, err;

	if (!gd.toc)
		return -ENOMEM;

	/* Check if GD-ROM */
	err = gdrom_readtoc_cmd(gd.toc, 1);
	/* Not a GD-ROM so check if standard CD-ROM */
	if (err) {
		err = gdrom_readtoc_cmd(gd.toc, 0);
		if (err) {
			pr_info("Could not get CD table of contents\n");
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
		pr_info("No data on the last session of the CD\n");
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
	char sense = __raw_readb(GDROM_ERROR_REG);
	sense &= 0xF0;
	if (sense == 0)
		return CDS_DISC_OK;
	if (sense == 0x20)
		return CDS_DRIVE_NOT_READY;
	/* default */
	return CDS_NO_INFO;
}

static unsigned int gdrom_check_events(struct cdrom_device_info *cd_info,
				       unsigned int clearing, int ignore)
{
	/* check the sense key */
	return (__raw_readb(GDROM_ERROR_REG) & 0xF0) == 0x60 ?
		DISK_EVENT_MEDIA_CHANGE : 0;
}

/* reset the G1 bus */
static int gdrom_hardreset(struct cdrom_device_info *cd_info)
{
	int count;
	__raw_writel(0x1fffff, GDROM_RESET_REG);
	for (count = 0xa0000000; count < 0xa0200000; count += 4)
		__raw_readl(count);
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
		pr_info("Drive not ready - command aborted\n");
		goto cleanup_sense;
	}
	sense_key = sense[1] & 0x0F;
	if (sense_key < ARRAY_SIZE(sense_texts))
		pr_info("%s\n", sense_texts[sense_key].text);
	else
		pr_err("Unknown sense key: %d\n", sense_key);
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

static const struct cdrom_device_ops gdrom_ops = {
	.open			= gdrom_open,
	.release		= gdrom_release,
	.drive_status		= gdrom_drivestatus,
	.check_events		= gdrom_check_events,
	.get_last_session	= gdrom_get_last_session,
	.reset			= gdrom_hardreset,
	.audio_ioctl		= gdrom_audio_ioctl,
	.generic_packet		= cdrom_dummy_generic_packet,
	.capability		= CDC_MULTI_SESSION | CDC_MEDIA_CHANGED |
				  CDC_RESET | CDC_DRIVE_STATUS | CDC_CD_R,
};

static int gdrom_bdops_open(struct block_device *bdev, fmode_t mode)
{
	int ret;

	bdev_check_media_change(bdev);

	mutex_lock(&gdrom_mutex);
	ret = cdrom_open(gd.cd_info, bdev, mode);
	mutex_unlock(&gdrom_mutex);
	return ret;
}

static void gdrom_bdops_release(struct gendisk *disk, fmode_t mode)
{
	mutex_lock(&gdrom_mutex);
	cdrom_release(gd.cd_info, mode);
	mutex_unlock(&gdrom_mutex);
}

static unsigned int gdrom_bdops_check_events(struct gendisk *disk,
					     unsigned int clearing)
{
	return cdrom_check_events(gd.cd_info, clearing);
}

static int gdrom_bdops_ioctl(struct block_device *bdev, fmode_t mode,
	unsigned cmd, unsigned long arg)
{
	int ret;

	mutex_lock(&gdrom_mutex);
	ret = cdrom_ioctl(gd.cd_info, bdev, mode, cmd, arg);
	mutex_unlock(&gdrom_mutex);

	return ret;
}

static const struct block_device_operations gdrom_bdops = {
	.owner			= THIS_MODULE,
	.open			= gdrom_bdops_open,
	.release		= gdrom_bdops_release,
	.check_events		= gdrom_bdops_check_events,
	.ioctl			= gdrom_bdops_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl		= blkdev_compat_ptr_ioctl,
#endif
};

static irqreturn_t gdrom_command_interrupt(int irq, void *dev_id)
{
	gd.status = __raw_readb(GDROM_STATUSCOMMAND_REG);
	if (gd.pending != 1)
		return IRQ_HANDLED;
	gd.pending = 0;
	wake_up_interruptible(&command_queue);
	return IRQ_HANDLED;
}

static irqreturn_t gdrom_dma_interrupt(int irq, void *dev_id)
{
	gd.status = __raw_readb(GDROM_STATUSCOMMAND_REG);
	if (gd.transfer != 1)
		return IRQ_HANDLED;
	gd.transfer = 0;
	wake_up_interruptible(&request_queue);
	return IRQ_HANDLED;
}

static int gdrom_set_interrupt_handlers(void)
{
	int err;

	err = request_irq(HW_EVENT_GDROM_CMD, gdrom_command_interrupt,
		0, "gdrom_command", &gd);
	if (err)
		return err;
	err = request_irq(HW_EVENT_GDROM_DMA, gdrom_dma_interrupt,
		0, "gdrom_dma", &gd);
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
static blk_status_t gdrom_readdisk_dma(struct request *req)
{
	int block, block_cnt;
	blk_status_t err;
	struct packet_command *read_command;
	unsigned long timeout;

	read_command = kzalloc(sizeof(struct packet_command), GFP_KERNEL);
	if (!read_command)
		return BLK_STS_RESOURCE;

	read_command->cmd[0] = 0x30;
	read_command->cmd[1] = 0x20;
	block = blk_rq_pos(req)/GD_TO_BLK + GD_SESSION_OFFSET;
	block_cnt = blk_rq_sectors(req)/GD_TO_BLK;
	__raw_writel(virt_to_phys(bio_data(req->bio)), GDROM_DMA_STARTADDR_REG);
	__raw_writel(block_cnt * GDROM_HARD_SECTOR, GDROM_DMA_LENGTH_REG);
	__raw_writel(1, GDROM_DMA_DIRECTION_REG);
	__raw_writel(1, GDROM_DMA_ENABLE_REG);
	read_command->cmd[2] = (block >> 16) & 0xFF;
	read_command->cmd[3] = (block >> 8) & 0xFF;
	read_command->cmd[4] = block & 0xFF;
	read_command->cmd[8] = (block_cnt >> 16) & 0xFF;
	read_command->cmd[9] = (block_cnt >> 8) & 0xFF;
	read_command->cmd[10] = block_cnt & 0xFF;
	/* set for DMA */
	__raw_writeb(1, GDROM_ERROR_REG);
	/* other registers */
	__raw_writeb(0, GDROM_SECNUM_REG);
	__raw_writeb(0, GDROM_BCL_REG);
	__raw_writeb(0, GDROM_BCH_REG);
	__raw_writeb(0, GDROM_DSEL_REG);
	__raw_writeb(0, GDROM_INTSEC_REG);
	/* Wait for registers to reset after any previous activity */
	timeout = jiffies + HZ / 2;
	while (gdrom_is_busy() && time_before(jiffies, timeout))
		cpu_relax();
	__raw_writeb(GDROM_COM_PACKET, GDROM_STATUSCOMMAND_REG);
	timeout = jiffies + HZ / 2;
	/* Wait for packet command to finish */
	while (gdrom_is_busy() && time_before(jiffies, timeout))
		cpu_relax();
	gd.pending = 1;
	gd.transfer = 1;
	outsw(GDROM_DATA_REG, &read_command->cmd, 6);
	timeout = jiffies + HZ / 2;
	/* Wait for any pending DMA to finish */
	while (__raw_readb(GDROM_DMA_STATUS_REG) &&
		time_before(jiffies, timeout))
		cpu_relax();
	/* start transfer */
	__raw_writeb(1, GDROM_DMA_STATUS_REG);
	wait_event_interruptible_timeout(request_queue,
		gd.transfer == 0, GDROM_DEFAULT_TIMEOUT);
	err = gd.transfer ? BLK_STS_IOERR : BLK_STS_OK;
	gd.transfer = 0;
	gd.pending = 0;

	blk_mq_end_request(req, err);
	kfree(read_command);
	return BLK_STS_OK;
}

static blk_status_t gdrom_queue_rq(struct blk_mq_hw_ctx *hctx,
				   const struct blk_mq_queue_data *bd)
{
	blk_mq_start_request(bd->rq);

	switch (req_op(bd->rq)) {
	case REQ_OP_READ:
		return gdrom_readdisk_dma(bd->rq);
	case REQ_OP_WRITE:
		pr_notice("Read only device - write request ignored\n");
		return BLK_STS_IOERR;
	default:
		printk(KERN_DEBUG "gdrom: Non-fs request ignored\n");
		return BLK_STS_IOERR;
	}
}

/* Print string identifying GD ROM device */
static int gdrom_outputversion(void)
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
	pr_info("%s from %s with firmware %s\n",
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
static int gdrom_init_dma_mode(void)
{
	__raw_writeb(0x13, GDROM_ERROR_REG);
	__raw_writeb(0x22, GDROM_INTSEC_REG);
	if (!gdrom_wait_clrbusy())
		return -EBUSY;
	__raw_writeb(0xEF, GDROM_STATUSCOMMAND_REG);
	if (!gdrom_wait_busy_sleeps())
		return -EBUSY;
	/* Memory protection setting for GDROM DMA
	* Bits 31 - 16 security: 0x8843
	* Bits 15 and 7 reserved (0)
	* Bits 14 - 8 start of transfer range in 1 MB blocks OR'ed with 0x80
	* Bits 6 - 0 end of transfer range in 1 MB blocks OR'ed with 0x80
	* (0x40 | 0x80) = start range at 0x0C000000
	* (0x7F | 0x80) = end range at 0x0FFFFFFF */
	__raw_writel(0x8843407F, GDROM_DMA_ACCESS_CTRL_REG);
	__raw_writel(9, GDROM_DMA_WAIT_REG); /* DMA word setting */
	return 0;
}

static void probe_gdrom_setupcd(void)
{
	gd.cd_info->ops = &gdrom_ops;
	gd.cd_info->capacity = 1;
	strcpy(gd.cd_info->name, GDROM_DEV_NAME);
	gd.cd_info->mask = CDC_CLOSE_TRAY|CDC_OPEN_TRAY|CDC_LOCK|
		CDC_SELECT_DISC;
}

static void probe_gdrom_setupdisk(void)
{
	gd.disk->major = gdrom_major;
	gd.disk->first_minor = 1;
	gd.disk->minors = 1;
	strcpy(gd.disk->disk_name, GDROM_DEV_NAME);
}

static int probe_gdrom_setupqueue(void)
{
	blk_queue_logical_block_size(gd.gdrom_rq, GDROM_HARD_SECTOR);
	/* using DMA so memory will need to be contiguous */
	blk_queue_max_segments(gd.gdrom_rq, 1);
	/* set a large max size to get most from DMA */
	blk_queue_max_segment_size(gd.gdrom_rq, 0x40000);
	gd.disk->queue = gd.gdrom_rq;
	return gdrom_init_dma_mode();
}

static const struct blk_mq_ops gdrom_mq_ops = {
	.queue_rq	= gdrom_queue_rq,
};

/*
 * register this as a block device and as compliant with the
 * universal CD Rom driver interface
 */
static int probe_gdrom(struct platform_device *devptr)
{
	int err;
	/* Start the device */
	if (gdrom_execute_diagnostic() != 1) {
		pr_warn("ATA Probe for GDROM failed\n");
		return -ENODEV;
	}
	/* Print out firmware ID */
	if (gdrom_outputversion())
		return -ENOMEM;
	/* Register GDROM */
	gdrom_major = register_blkdev(0, GDROM_DEV_NAME);
	if (gdrom_major <= 0)
		return gdrom_major;
	pr_info("Registered with major number %d\n",
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
	if (register_cdrom(gd.disk, gd.cd_info)) {
		err = -ENODEV;
		goto probe_fail_cdrom_register;
	}
	gd.disk->fops = &gdrom_bdops;
	gd.disk->events = DISK_EVENT_MEDIA_CHANGE;
	/* latch on to the interrupt */
	err = gdrom_set_interrupt_handlers();
	if (err)
		goto probe_fail_cmdirq_register;

	gd.gdrom_rq = blk_mq_init_sq_queue(&gd.tag_set, &gdrom_mq_ops, 1,
				BLK_MQ_F_SHOULD_MERGE | BLK_MQ_F_BLOCKING);
	if (IS_ERR(gd.gdrom_rq)) {
		err = PTR_ERR(gd.gdrom_rq);
		gd.gdrom_rq = NULL;
		goto probe_fail_requestq;
	}

	blk_queue_bounce_limit(gd.gdrom_rq, BLK_BOUNCE_HIGH);

	err = probe_gdrom_setupqueue();
	if (err)
		goto probe_fail_toc;

	gd.toc = kzalloc(sizeof(struct gdromtoc), GFP_KERNEL);
	if (!gd.toc) {
		err = -ENOMEM;
		goto probe_fail_toc;
	}
	add_disk(gd.disk);
	return 0;

probe_fail_toc:
	blk_cleanup_queue(gd.gdrom_rq);
	blk_mq_free_tag_set(&gd.tag_set);
probe_fail_requestq:
	free_irq(HW_EVENT_GDROM_DMA, &gd);
	free_irq(HW_EVENT_GDROM_CMD, &gd);
probe_fail_cmdirq_register:
probe_fail_cdrom_register:
	del_gendisk(gd.disk);
probe_fail_no_disk:
	kfree(gd.cd_info);
probe_fail_no_mem:
	unregister_blkdev(gdrom_major, GDROM_DEV_NAME);
	gdrom_major = 0;
	pr_warn("Probe failed - error is 0x%X\n", err);
	return err;
}

static int remove_gdrom(struct platform_device *devptr)
{
	blk_cleanup_queue(gd.gdrom_rq);
	blk_mq_free_tag_set(&gd.tag_set);
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
	.remove = remove_gdrom,
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
	kfree(gd.cd_info);
}

module_init(init_gdrom);
module_exit(exit_gdrom);
MODULE_AUTHOR("Adrian McMenamin <adrian@mcmen.demon.co.uk>");
MODULE_DESCRIPTION("SEGA Dreamcast GD-ROM Driver");
MODULE_LICENSE("GPL");
