/*
 *  drivers/block/mg_disk.c
 *
 *  Support for the mGine m[g]flash IO mode.
 *  Based on legacy hd.c
 *
 * (c) 2008 mGine Co.,LTD
 * (c) 2008 unsik Kim <donari75@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/blkdev.h>
#include <linux/hdreg.h>
#include <linux/ata.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/mg_disk.h>
#include <linux/slab.h>

#define MG_RES_SEC (CONFIG_MG_DISK_RES << 1)

/* name for block device */
#define MG_DISK_NAME "mgd"

#define MG_DISK_MAJ 0
#define MG_DISK_MAX_PART 16
#define MG_SECTOR_SIZE 512
#define MG_MAX_SECTS 256

/* Register offsets */
#define MG_BUFF_OFFSET			0x8000
#define MG_REG_OFFSET			0xC000
#define MG_REG_FEATURE			(MG_REG_OFFSET + 2)	/* write case */
#define MG_REG_ERROR			(MG_REG_OFFSET + 2)	/* read case */
#define MG_REG_SECT_CNT			(MG_REG_OFFSET + 4)
#define MG_REG_SECT_NUM			(MG_REG_OFFSET + 6)
#define MG_REG_CYL_LOW			(MG_REG_OFFSET + 8)
#define MG_REG_CYL_HIGH			(MG_REG_OFFSET + 0xA)
#define MG_REG_DRV_HEAD			(MG_REG_OFFSET + 0xC)
#define MG_REG_COMMAND			(MG_REG_OFFSET + 0xE)	/* write case */
#define MG_REG_STATUS			(MG_REG_OFFSET + 0xE)	/* read  case */
#define MG_REG_DRV_CTRL			(MG_REG_OFFSET + 0x10)
#define MG_REG_BURST_CTRL		(MG_REG_OFFSET + 0x12)

/* handy status */
#define MG_STAT_READY	(ATA_DRDY | ATA_DSC)
#define MG_READY_OK(s)	(((s) & (MG_STAT_READY | (ATA_BUSY | ATA_DF | \
				 ATA_ERR))) == MG_STAT_READY)

/* error code for others */
#define MG_ERR_NONE		0
#define MG_ERR_TIMEOUT		0x100
#define MG_ERR_INIT_STAT	0x101
#define MG_ERR_TRANSLATION	0x102
#define MG_ERR_CTRL_RST		0x103
#define MG_ERR_INV_STAT		0x104
#define MG_ERR_RSTOUT		0x105

#define MG_MAX_ERRORS	6	/* Max read/write errors */

/* command */
#define MG_CMD_RD 0x20
#define MG_CMD_WR 0x30
#define MG_CMD_SLEEP 0x99
#define MG_CMD_WAKEUP 0xC3
#define MG_CMD_ID 0xEC
#define MG_CMD_WR_CONF 0x3C
#define MG_CMD_RD_CONF 0x40

/* operation mode */
#define MG_OP_CASCADE (1 << 0)
#define MG_OP_CASCADE_SYNC_RD (1 << 1)
#define MG_OP_CASCADE_SYNC_WR (1 << 2)
#define MG_OP_INTERLEAVE (1 << 3)

/* synchronous */
#define MG_BURST_LAT_4 (3 << 4)
#define MG_BURST_LAT_5 (4 << 4)
#define MG_BURST_LAT_6 (5 << 4)
#define MG_BURST_LAT_7 (6 << 4)
#define MG_BURST_LAT_8 (7 << 4)
#define MG_BURST_LEN_4 (1 << 1)
#define MG_BURST_LEN_8 (2 << 1)
#define MG_BURST_LEN_16 (3 << 1)
#define MG_BURST_LEN_32 (4 << 1)
#define MG_BURST_LEN_CONT (0 << 1)

/* timeout value (unit: ms) */
#define MG_TMAX_CONF_TO_CMD	1
#define MG_TMAX_WAIT_RD_DRQ	10
#define MG_TMAX_WAIT_WR_DRQ	500
#define MG_TMAX_RST_TO_BUSY	10
#define MG_TMAX_HDRST_TO_RDY	500
#define MG_TMAX_SWRST_TO_RDY	500
#define MG_TMAX_RSTOUT		3000

#define MG_DEV_MASK (MG_BOOT_DEV | MG_STORAGE_DEV | MG_STORAGE_DEV_SKIP_RST)

/* main structure for mflash driver */
struct mg_host {
	struct device *dev;

	struct request_queue *breq;
	struct request *req;
	spinlock_t lock;
	struct gendisk *gd;

	struct timer_list timer;
	void (*mg_do_intr) (struct mg_host *);

	u16 id[ATA_ID_WORDS];

	u16 cyls;
	u16 heads;
	u16 sectors;
	u32 n_sectors;
	u32 nres_sectors;

	void __iomem *dev_base;
	unsigned int irq;
	unsigned int rst;
	unsigned int rstout;

	u32 major;
	u32 error;
};

/*
 * Debugging macro and defines
 */
#undef DO_MG_DEBUG
#ifdef DO_MG_DEBUG
#  define MG_DBG(fmt, args...) \
	printk(KERN_DEBUG "%s:%d "fmt, __func__, __LINE__, ##args)
#else /* CONFIG_MG_DEBUG */
#  define MG_DBG(fmt, args...) do { } while (0)
#endif /* CONFIG_MG_DEBUG */

static void mg_request(struct request_queue *);

static bool mg_end_request(struct mg_host *host, int err, unsigned int nr_bytes)
{
	if (__blk_end_request(host->req, err, nr_bytes))
		return true;

	host->req = NULL;
	return false;
}

static bool mg_end_request_cur(struct mg_host *host, int err)
{
	return mg_end_request(host, err, blk_rq_cur_bytes(host->req));
}

static void mg_dump_status(const char *msg, unsigned int stat,
		struct mg_host *host)
{
	char *name = MG_DISK_NAME;

	if (host->req)
		name = host->req->rq_disk->disk_name;

	printk(KERN_ERR "%s: %s: status=0x%02x { ", name, msg, stat & 0xff);
	if (stat & ATA_BUSY)
		printk("Busy ");
	if (stat & ATA_DRDY)
		printk("DriveReady ");
	if (stat & ATA_DF)
		printk("WriteFault ");
	if (stat & ATA_DSC)
		printk("SeekComplete ");
	if (stat & ATA_DRQ)
		printk("DataRequest ");
	if (stat & ATA_CORR)
		printk("CorrectedError ");
	if (stat & ATA_ERR)
		printk("Error ");
	printk("}\n");
	if ((stat & ATA_ERR) == 0) {
		host->error = 0;
	} else {
		host->error = inb((unsigned long)host->dev_base + MG_REG_ERROR);
		printk(KERN_ERR "%s: %s: error=0x%02x { ", name, msg,
				host->error & 0xff);
		if (host->error & ATA_BBK)
			printk("BadSector ");
		if (host->error & ATA_UNC)
			printk("UncorrectableError ");
		if (host->error & ATA_IDNF)
			printk("SectorIdNotFound ");
		if (host->error & ATA_ABORTED)
			printk("DriveStatusError ");
		if (host->error & ATA_AMNF)
			printk("AddrMarkNotFound ");
		printk("}");
		if (host->error & (ATA_BBK | ATA_UNC | ATA_IDNF | ATA_AMNF)) {
			if (host->req)
				printk(", sector=%u",
				       (unsigned int)blk_rq_pos(host->req));
		}
		printk("\n");
	}
}

static unsigned int mg_wait(struct mg_host *host, u32 expect, u32 msec)
{
	u8 status;
	unsigned long expire, cur_jiffies;
	struct mg_drv_data *prv_data = host->dev->platform_data;

	host->error = MG_ERR_NONE;
	expire = jiffies + msecs_to_jiffies(msec);

	/* These 2 times dummy status read prevents reading invalid
	 * status. A very little time (3 times of mflash operating clk)
	 * is required for busy bit is set. Use dummy read instead of
	 * busy wait, because mflash's PLL is machine dependent.
	 */
	if (prv_data->use_polling) {
		status = inb((unsigned long)host->dev_base + MG_REG_STATUS);
		status = inb((unsigned long)host->dev_base + MG_REG_STATUS);
	}

	status = inb((unsigned long)host->dev_base + MG_REG_STATUS);

	do {
		cur_jiffies = jiffies;
		if (status & ATA_BUSY) {
			if (expect == ATA_BUSY)
				break;
		} else {
			/* Check the error condition! */
			if (status & ATA_ERR) {
				mg_dump_status("mg_wait", status, host);
				break;
			}

			if (expect == MG_STAT_READY)
				if (MG_READY_OK(status))
					break;

			if (expect == ATA_DRQ)
				if (status & ATA_DRQ)
					break;
		}
		if (!msec) {
			mg_dump_status("not ready", status, host);
			return MG_ERR_INV_STAT;
		}

		status = inb((unsigned long)host->dev_base + MG_REG_STATUS);
	} while (time_before(cur_jiffies, expire));

	if (time_after_eq(cur_jiffies, expire) && msec)
		host->error = MG_ERR_TIMEOUT;

	return host->error;
}

static unsigned int mg_wait_rstout(u32 rstout, u32 msec)
{
	unsigned long expire;

	expire = jiffies + msecs_to_jiffies(msec);
	while (time_before(jiffies, expire)) {
		if (gpio_get_value(rstout) == 1)
			return MG_ERR_NONE;
		msleep(10);
	}

	return MG_ERR_RSTOUT;
}

static void mg_unexpected_intr(struct mg_host *host)
{
	u32 status = inb((unsigned long)host->dev_base + MG_REG_STATUS);

	mg_dump_status("mg_unexpected_intr", status, host);
}

static irqreturn_t mg_irq(int irq, void *dev_id)
{
	struct mg_host *host = dev_id;
	void (*handler)(struct mg_host *) = host->mg_do_intr;

	spin_lock(&host->lock);

	host->mg_do_intr = NULL;
	del_timer(&host->timer);
	if (!handler)
		handler = mg_unexpected_intr;
	handler(host);

	spin_unlock(&host->lock);

	return IRQ_HANDLED;
}

/* local copy of ata_id_string() */
static void mg_id_string(const u16 *id, unsigned char *s,
			 unsigned int ofs, unsigned int len)
{
	unsigned int c;

	BUG_ON(len & 1);

	while (len > 0) {
		c = id[ofs] >> 8;
		*s = c;
		s++;

		c = id[ofs] & 0xff;
		*s = c;
		s++;

		ofs++;
		len -= 2;
	}
}

/* local copy of ata_id_c_string() */
static void mg_id_c_string(const u16 *id, unsigned char *s,
			   unsigned int ofs, unsigned int len)
{
	unsigned char *p;

	mg_id_string(id, s, ofs, len - 1);

	p = s + strnlen(s, len - 1);
	while (p > s && p[-1] == ' ')
		p--;
	*p = '\0';
}

static int mg_get_disk_id(struct mg_host *host)
{
	u32 i;
	s32 err;
	const u16 *id = host->id;
	struct mg_drv_data *prv_data = host->dev->platform_data;
	char fwrev[ATA_ID_FW_REV_LEN + 1];
	char model[ATA_ID_PROD_LEN + 1];
	char serial[ATA_ID_SERNO_LEN + 1];

	if (!prv_data->use_polling)
		outb(ATA_NIEN, (unsigned long)host->dev_base + MG_REG_DRV_CTRL);

	outb(MG_CMD_ID, (unsigned long)host->dev_base + MG_REG_COMMAND);
	err = mg_wait(host, ATA_DRQ, MG_TMAX_WAIT_RD_DRQ);
	if (err)
		return err;

	for (i = 0; i < (MG_SECTOR_SIZE >> 1); i++)
		host->id[i] = le16_to_cpu(inw((unsigned long)host->dev_base +
					MG_BUFF_OFFSET + i * 2));

	outb(MG_CMD_RD_CONF, (unsigned long)host->dev_base + MG_REG_COMMAND);
	err = mg_wait(host, MG_STAT_READY, MG_TMAX_CONF_TO_CMD);
	if (err)
		return err;

	if ((id[ATA_ID_FIELD_VALID] & 1) == 0)
		return MG_ERR_TRANSLATION;

	host->n_sectors = ata_id_u32(id, ATA_ID_LBA_CAPACITY);
	host->cyls = id[ATA_ID_CYLS];
	host->heads = id[ATA_ID_HEADS];
	host->sectors = id[ATA_ID_SECTORS];

	if (MG_RES_SEC && host->heads && host->sectors) {
		/* modify cyls, n_sectors */
		host->cyls = (host->n_sectors - MG_RES_SEC) /
			host->heads / host->sectors;
		host->nres_sectors = host->n_sectors - host->cyls *
			host->heads * host->sectors;
		host->n_sectors -= host->nres_sectors;
	}

	mg_id_c_string(id, fwrev, ATA_ID_FW_REV, sizeof(fwrev));
	mg_id_c_string(id, model, ATA_ID_PROD, sizeof(model));
	mg_id_c_string(id, serial, ATA_ID_SERNO, sizeof(serial));
	printk(KERN_INFO "mg_disk: model: %s\n", model);
	printk(KERN_INFO "mg_disk: firm: %.8s\n", fwrev);
	printk(KERN_INFO "mg_disk: serial: %s\n", serial);
	printk(KERN_INFO "mg_disk: %d + reserved %d sectors\n",
			host->n_sectors, host->nres_sectors);

	if (!prv_data->use_polling)
		outb(0, (unsigned long)host->dev_base + MG_REG_DRV_CTRL);

	return err;
}


static int mg_disk_init(struct mg_host *host)
{
	struct mg_drv_data *prv_data = host->dev->platform_data;
	s32 err;
	u8 init_status;

	/* hdd rst low */
	gpio_set_value(host->rst, 0);
	err = mg_wait(host, ATA_BUSY, MG_TMAX_RST_TO_BUSY);
	if (err)
		return err;

	/* hdd rst high */
	gpio_set_value(host->rst, 1);
	err = mg_wait(host, MG_STAT_READY, MG_TMAX_HDRST_TO_RDY);
	if (err)
		return err;

	/* soft reset on */
	outb(ATA_SRST | (prv_data->use_polling ? ATA_NIEN : 0),
			(unsigned long)host->dev_base + MG_REG_DRV_CTRL);
	err = mg_wait(host, ATA_BUSY, MG_TMAX_RST_TO_BUSY);
	if (err)
		return err;

	/* soft reset off */
	outb(prv_data->use_polling ? ATA_NIEN : 0,
			(unsigned long)host->dev_base + MG_REG_DRV_CTRL);
	err = mg_wait(host, MG_STAT_READY, MG_TMAX_SWRST_TO_RDY);
	if (err)
		return err;

	init_status = inb((unsigned long)host->dev_base + MG_REG_STATUS) & 0xf;

	if (init_status == 0xf)
		return MG_ERR_INIT_STAT;

	return err;
}

static void mg_bad_rw_intr(struct mg_host *host)
{
	if (host->req)
		if (++host->req->errors >= MG_MAX_ERRORS ||
		    host->error == MG_ERR_TIMEOUT)
			mg_end_request_cur(host, -EIO);
}

static unsigned int mg_out(struct mg_host *host,
		unsigned int sect_num,
		unsigned int sect_cnt,
		unsigned int cmd,
		void (*intr_addr)(struct mg_host *))
{
	struct mg_drv_data *prv_data = host->dev->platform_data;

	if (mg_wait(host, MG_STAT_READY, MG_TMAX_CONF_TO_CMD))
		return host->error;

	if (!prv_data->use_polling) {
		host->mg_do_intr = intr_addr;
		mod_timer(&host->timer, jiffies + 3 * HZ);
	}
	if (MG_RES_SEC)
		sect_num += MG_RES_SEC;
	outb((u8)sect_cnt, (unsigned long)host->dev_base + MG_REG_SECT_CNT);
	outb((u8)sect_num, (unsigned long)host->dev_base + MG_REG_SECT_NUM);
	outb((u8)(sect_num >> 8), (unsigned long)host->dev_base +
			MG_REG_CYL_LOW);
	outb((u8)(sect_num >> 16), (unsigned long)host->dev_base +
			MG_REG_CYL_HIGH);
	outb((u8)((sect_num >> 24) | ATA_LBA | ATA_DEVICE_OBS),
			(unsigned long)host->dev_base + MG_REG_DRV_HEAD);
	outb(cmd, (unsigned long)host->dev_base + MG_REG_COMMAND);
	return MG_ERR_NONE;
}

static void mg_read_one(struct mg_host *host, struct request *req)
{
	u16 *buff = (u16 *)req->buffer;
	u32 i;

	for (i = 0; i < MG_SECTOR_SIZE >> 1; i++)
		*buff++ = inw((unsigned long)host->dev_base + MG_BUFF_OFFSET +
			      (i << 1));
}

static void mg_read(struct request *req)
{
	struct mg_host *host = req->rq_disk->private_data;

	if (mg_out(host, blk_rq_pos(req), blk_rq_sectors(req),
		   MG_CMD_RD, NULL) != MG_ERR_NONE)
		mg_bad_rw_intr(host);

	MG_DBG("requested %d sects (from %ld), buffer=0x%p\n",
	       blk_rq_sectors(req), blk_rq_pos(req), req->buffer);

	do {
		if (mg_wait(host, ATA_DRQ,
			    MG_TMAX_WAIT_RD_DRQ) != MG_ERR_NONE) {
			mg_bad_rw_intr(host);
			return;
		}

		mg_read_one(host, req);

		outb(MG_CMD_RD_CONF, (unsigned long)host->dev_base +
				MG_REG_COMMAND);
	} while (mg_end_request(host, 0, MG_SECTOR_SIZE));
}

static void mg_write_one(struct mg_host *host, struct request *req)
{
	u16 *buff = (u16 *)req->buffer;
	u32 i;

	for (i = 0; i < MG_SECTOR_SIZE >> 1; i++)
		outw(*buff++, (unsigned long)host->dev_base + MG_BUFF_OFFSET +
		     (i << 1));
}

static void mg_write(struct request *req)
{
	struct mg_host *host = req->rq_disk->private_data;
	unsigned int rem = blk_rq_sectors(req);

	if (mg_out(host, blk_rq_pos(req), rem,
		   MG_CMD_WR, NULL) != MG_ERR_NONE) {
		mg_bad_rw_intr(host);
		return;
	}

	MG_DBG("requested %d sects (from %ld), buffer=0x%p\n",
	       rem, blk_rq_pos(req), req->buffer);

	if (mg_wait(host, ATA_DRQ,
		    MG_TMAX_WAIT_WR_DRQ) != MG_ERR_NONE) {
		mg_bad_rw_intr(host);
		return;
	}

	do {
		mg_write_one(host, req);

		outb(MG_CMD_WR_CONF, (unsigned long)host->dev_base +
				MG_REG_COMMAND);

		rem--;
		if (rem > 1 && mg_wait(host, ATA_DRQ,
					MG_TMAX_WAIT_WR_DRQ) != MG_ERR_NONE) {
			mg_bad_rw_intr(host);
			return;
		} else if (mg_wait(host, MG_STAT_READY,
					MG_TMAX_WAIT_WR_DRQ) != MG_ERR_NONE) {
			mg_bad_rw_intr(host);
			return;
		}
	} while (mg_end_request(host, 0, MG_SECTOR_SIZE));
}

static void mg_read_intr(struct mg_host *host)
{
	struct request *req = host->req;
	u32 i;

	/* check status */
	do {
		i = inb((unsigned long)host->dev_base + MG_REG_STATUS);
		if (i & ATA_BUSY)
			break;
		if (!MG_READY_OK(i))
			break;
		if (i & ATA_DRQ)
			goto ok_to_read;
	} while (0);
	mg_dump_status("mg_read_intr", i, host);
	mg_bad_rw_intr(host);
	mg_request(host->breq);
	return;

ok_to_read:
	mg_read_one(host, req);

	MG_DBG("sector %ld, remaining=%ld, buffer=0x%p\n",
	       blk_rq_pos(req), blk_rq_sectors(req) - 1, req->buffer);

	/* send read confirm */
	outb(MG_CMD_RD_CONF, (unsigned long)host->dev_base + MG_REG_COMMAND);

	if (mg_end_request(host, 0, MG_SECTOR_SIZE)) {
		/* set handler if read remains */
		host->mg_do_intr = mg_read_intr;
		mod_timer(&host->timer, jiffies + 3 * HZ);
	} else /* goto next request */
		mg_request(host->breq);
}

static void mg_write_intr(struct mg_host *host)
{
	struct request *req = host->req;
	u32 i;
	bool rem;

	/* check status */
	do {
		i = inb((unsigned long)host->dev_base + MG_REG_STATUS);
		if (i & ATA_BUSY)
			break;
		if (!MG_READY_OK(i))
			break;
		if ((blk_rq_sectors(req) <= 1) || (i & ATA_DRQ))
			goto ok_to_write;
	} while (0);
	mg_dump_status("mg_write_intr", i, host);
	mg_bad_rw_intr(host);
	mg_request(host->breq);
	return;

ok_to_write:
	if ((rem = mg_end_request(host, 0, MG_SECTOR_SIZE))) {
		/* write 1 sector and set handler if remains */
		mg_write_one(host, req);
		MG_DBG("sector %ld, remaining=%ld, buffer=0x%p\n",
		       blk_rq_pos(req), blk_rq_sectors(req), req->buffer);
		host->mg_do_intr = mg_write_intr;
		mod_timer(&host->timer, jiffies + 3 * HZ);
	}

	/* send write confirm */
	outb(MG_CMD_WR_CONF, (unsigned long)host->dev_base + MG_REG_COMMAND);

	if (!rem)
		mg_request(host->breq);
}

void mg_times_out(unsigned long data)
{
	struct mg_host *host = (struct mg_host *)data;
	char *name;

	spin_lock_irq(&host->lock);

	if (!host->req)
		goto out_unlock;

	host->mg_do_intr = NULL;

	name = host->req->rq_disk->disk_name;
	printk(KERN_DEBUG "%s: timeout\n", name);

	host->error = MG_ERR_TIMEOUT;
	mg_bad_rw_intr(host);

out_unlock:
	mg_request(host->breq);
	spin_unlock_irq(&host->lock);
}

static void mg_request_poll(struct request_queue *q)
{
	struct mg_host *host = q->queuedata;

	while (1) {
		if (!host->req) {
			host->req = blk_fetch_request(q);
			if (!host->req)
				break;
		}

		if (unlikely(host->req->cmd_type != REQ_TYPE_FS)) {
			mg_end_request_cur(host, -EIO);
			continue;
		}

		if (rq_data_dir(host->req) == READ)
			mg_read(host->req);
		else
			mg_write(host->req);
	}
}

static unsigned int mg_issue_req(struct request *req,
		struct mg_host *host,
		unsigned int sect_num,
		unsigned int sect_cnt)
{
	switch (rq_data_dir(req)) {
	case READ:
		if (mg_out(host, sect_num, sect_cnt, MG_CMD_RD, &mg_read_intr)
				!= MG_ERR_NONE) {
			mg_bad_rw_intr(host);
			return host->error;
		}
		break;
	case WRITE:
		/* TODO : handler */
		outb(ATA_NIEN, (unsigned long)host->dev_base + MG_REG_DRV_CTRL);
		if (mg_out(host, sect_num, sect_cnt, MG_CMD_WR, &mg_write_intr)
				!= MG_ERR_NONE) {
			mg_bad_rw_intr(host);
			return host->error;
		}
		del_timer(&host->timer);
		mg_wait(host, ATA_DRQ, MG_TMAX_WAIT_WR_DRQ);
		outb(0, (unsigned long)host->dev_base + MG_REG_DRV_CTRL);
		if (host->error) {
			mg_bad_rw_intr(host);
			return host->error;
		}
		mg_write_one(host, req);
		mod_timer(&host->timer, jiffies + 3 * HZ);
		outb(MG_CMD_WR_CONF, (unsigned long)host->dev_base +
				MG_REG_COMMAND);
		break;
	}
	return MG_ERR_NONE;
}

/* This function also called from IRQ context */
static void mg_request(struct request_queue *q)
{
	struct mg_host *host = q->queuedata;
	struct request *req;
	u32 sect_num, sect_cnt;

	while (1) {
		if (!host->req) {
			host->req = blk_fetch_request(q);
			if (!host->req)
				break;
		}
		req = host->req;

		/* check unwanted request call */
		if (host->mg_do_intr)
			return;

		del_timer(&host->timer);

		sect_num = blk_rq_pos(req);
		/* deal whole segments */
		sect_cnt = blk_rq_sectors(req);

		/* sanity check */
		if (sect_num >= get_capacity(req->rq_disk) ||
				((sect_num + sect_cnt) >
				 get_capacity(req->rq_disk))) {
			printk(KERN_WARNING
					"%s: bad access: sector=%d, count=%d\n",
					req->rq_disk->disk_name,
					sect_num, sect_cnt);
			mg_end_request_cur(host, -EIO);
			continue;
		}

		if (unlikely(req->cmd_type != REQ_TYPE_FS)) {
			mg_end_request_cur(host, -EIO);
			continue;
		}

		if (!mg_issue_req(req, host, sect_num, sect_cnt))
			return;
	}
}

static int mg_getgeo(struct block_device *bdev, struct hd_geometry *geo)
{
	struct mg_host *host = bdev->bd_disk->private_data;

	geo->cylinders = (unsigned short)host->cyls;
	geo->heads = (unsigned char)host->heads;
	geo->sectors = (unsigned char)host->sectors;
	return 0;
}

static const struct block_device_operations mg_disk_ops = {
	.getgeo = mg_getgeo
};

static int mg_suspend(struct platform_device *plat_dev, pm_message_t state)
{
	struct mg_drv_data *prv_data = plat_dev->dev.platform_data;
	struct mg_host *host = prv_data->host;

	if (mg_wait(host, MG_STAT_READY, MG_TMAX_CONF_TO_CMD))
		return -EIO;

	if (!prv_data->use_polling)
		outb(ATA_NIEN, (unsigned long)host->dev_base + MG_REG_DRV_CTRL);

	outb(MG_CMD_SLEEP, (unsigned long)host->dev_base + MG_REG_COMMAND);
	/* wait until mflash deep sleep */
	msleep(1);

	if (mg_wait(host, MG_STAT_READY, MG_TMAX_CONF_TO_CMD)) {
		if (!prv_data->use_polling)
			outb(0, (unsigned long)host->dev_base + MG_REG_DRV_CTRL);
		return -EIO;
	}

	return 0;
}

static int mg_resume(struct platform_device *plat_dev)
{
	struct mg_drv_data *prv_data = plat_dev->dev.platform_data;
	struct mg_host *host = prv_data->host;

	if (mg_wait(host, MG_STAT_READY, MG_TMAX_CONF_TO_CMD))
		return -EIO;

	outb(MG_CMD_WAKEUP, (unsigned long)host->dev_base + MG_REG_COMMAND);
	/* wait until mflash wakeup */
	msleep(1);

	if (mg_wait(host, MG_STAT_READY, MG_TMAX_CONF_TO_CMD))
		return -EIO;

	if (!prv_data->use_polling)
		outb(0, (unsigned long)host->dev_base + MG_REG_DRV_CTRL);

	return 0;
}

static int mg_probe(struct platform_device *plat_dev)
{
	struct mg_host *host;
	struct resource *rsc;
	struct mg_drv_data *prv_data = plat_dev->dev.platform_data;
	int err = 0;

	if (!prv_data) {
		printk(KERN_ERR	"%s:%d fail (no driver_data)\n",
				__func__, __LINE__);
		err = -EINVAL;
		goto probe_err;
	}

	/* alloc mg_host */
	host = kzalloc(sizeof(struct mg_host), GFP_KERNEL);
	if (!host) {
		printk(KERN_ERR "%s:%d fail (no memory for mg_host)\n",
				__func__, __LINE__);
		err = -ENOMEM;
		goto probe_err;
	}
	host->major = MG_DISK_MAJ;

	/* link each other */
	prv_data->host = host;
	host->dev = &plat_dev->dev;

	/* io remap */
	rsc = platform_get_resource(plat_dev, IORESOURCE_MEM, 0);
	if (!rsc) {
		printk(KERN_ERR "%s:%d platform_get_resource fail\n",
				__func__, __LINE__);
		err = -EINVAL;
		goto probe_err_2;
	}
	host->dev_base = ioremap(rsc->start, resource_size(rsc));
	if (!host->dev_base) {
		printk(KERN_ERR "%s:%d ioremap fail\n",
				__func__, __LINE__);
		err = -EIO;
		goto probe_err_2;
	}
	MG_DBG("dev_base = 0x%x\n", (u32)host->dev_base);

	/* get reset pin */
	rsc = platform_get_resource_byname(plat_dev, IORESOURCE_IO,
			MG_RST_PIN);
	if (!rsc) {
		printk(KERN_ERR "%s:%d get reset pin fail\n",
				__func__, __LINE__);
		err = -EIO;
		goto probe_err_3;
	}
	host->rst = rsc->start;

	/* init rst pin */
	err = gpio_request(host->rst, MG_RST_PIN);
	if (err)
		goto probe_err_3;
	gpio_direction_output(host->rst, 1);

	/* reset out pin */
	if (!(prv_data->dev_attr & MG_DEV_MASK))
		goto probe_err_3a;

	if (prv_data->dev_attr != MG_BOOT_DEV) {
		rsc = platform_get_resource_byname(plat_dev, IORESOURCE_IO,
				MG_RSTOUT_PIN);
		if (!rsc) {
			printk(KERN_ERR "%s:%d get reset-out pin fail\n",
					__func__, __LINE__);
			err = -EIO;
			goto probe_err_3a;
		}
		host->rstout = rsc->start;
		err = gpio_request(host->rstout, MG_RSTOUT_PIN);
		if (err)
			goto probe_err_3a;
		gpio_direction_input(host->rstout);
	}

	/* disk reset */
	if (prv_data->dev_attr == MG_STORAGE_DEV) {
		/* If POR seq. not yet finised, wait */
		err = mg_wait_rstout(host->rstout, MG_TMAX_RSTOUT);
		if (err)
			goto probe_err_3b;
		err = mg_disk_init(host);
		if (err) {
			printk(KERN_ERR "%s:%d fail (err code : %d)\n",
					__func__, __LINE__, err);
			err = -EIO;
			goto probe_err_3b;
		}
	}

	/* get irq resource */
	if (!prv_data->use_polling) {
		host->irq = platform_get_irq(plat_dev, 0);
		if (host->irq == -ENXIO) {
			err = host->irq;
			goto probe_err_3b;
		}
		err = request_irq(host->irq, mg_irq,
				IRQF_DISABLED | IRQF_TRIGGER_RISING,
				MG_DEV_NAME, host);
		if (err) {
			printk(KERN_ERR "%s:%d fail (request_irq err=%d)\n",
					__func__, __LINE__, err);
			goto probe_err_3b;
		}

	}

	/* get disk id */
	err = mg_get_disk_id(host);
	if (err) {
		printk(KERN_ERR "%s:%d fail (err code : %d)\n",
				__func__, __LINE__, err);
		err = -EIO;
		goto probe_err_4;
	}

	err = register_blkdev(host->major, MG_DISK_NAME);
	if (err < 0) {
		printk(KERN_ERR "%s:%d register_blkdev fail (err code : %d)\n",
				__func__, __LINE__, err);
		goto probe_err_4;
	}
	if (!host->major)
		host->major = err;

	spin_lock_init(&host->lock);

	if (prv_data->use_polling)
		host->breq = blk_init_queue(mg_request_poll, &host->lock);
	else
		host->breq = blk_init_queue(mg_request, &host->lock);

	if (!host->breq) {
		err = -ENOMEM;
		printk(KERN_ERR "%s:%d (blk_init_queue) fail\n",
				__func__, __LINE__);
		goto probe_err_5;
	}
	host->breq->queuedata = host;

	/* mflash is random device, thanx for the noop */
	err = elevator_change(host->breq, "noop");
	if (err) {
		printk(KERN_ERR "%s:%d (elevator_init) fail\n",
				__func__, __LINE__);
		goto probe_err_6;
	}
	blk_queue_max_hw_sectors(host->breq, MG_MAX_SECTS);
	blk_queue_logical_block_size(host->breq, MG_SECTOR_SIZE);

	init_timer(&host->timer);
	host->timer.function = mg_times_out;
	host->timer.data = (unsigned long)host;

	host->gd = alloc_disk(MG_DISK_MAX_PART);
	if (!host->gd) {
		printk(KERN_ERR "%s:%d (alloc_disk) fail\n",
				__func__, __LINE__);
		err = -ENOMEM;
		goto probe_err_7;
	}
	host->gd->major = host->major;
	host->gd->first_minor = 0;
	host->gd->fops = &mg_disk_ops;
	host->gd->queue = host->breq;
	host->gd->private_data = host;
	sprintf(host->gd->disk_name, MG_DISK_NAME"a");

	set_capacity(host->gd, host->n_sectors);

	add_disk(host->gd);

	return err;

probe_err_7:
	del_timer_sync(&host->timer);
probe_err_6:
	blk_cleanup_queue(host->breq);
probe_err_5:
	unregister_blkdev(MG_DISK_MAJ, MG_DISK_NAME);
probe_err_4:
	if (!prv_data->use_polling)
		free_irq(host->irq, host);
probe_err_3b:
	gpio_free(host->rstout);
probe_err_3a:
	gpio_free(host->rst);
probe_err_3:
	iounmap(host->dev_base);
probe_err_2:
	kfree(host);
probe_err:
	return err;
}

static int mg_remove(struct platform_device *plat_dev)
{
	struct mg_drv_data *prv_data = plat_dev->dev.platform_data;
	struct mg_host *host = prv_data->host;
	int err = 0;

	/* delete timer */
	del_timer_sync(&host->timer);

	/* remove disk */
	if (host->gd) {
		del_gendisk(host->gd);
		put_disk(host->gd);
	}
	/* remove queue */
	if (host->breq)
		blk_cleanup_queue(host->breq);

	/* unregister blk device */
	unregister_blkdev(host->major, MG_DISK_NAME);

	/* free irq */
	if (!prv_data->use_polling)
		free_irq(host->irq, host);

	/* free reset-out pin */
	if (prv_data->dev_attr != MG_BOOT_DEV)
		gpio_free(host->rstout);

	/* free rst pin */
	if (host->rst)
		gpio_free(host->rst);

	/* unmap io */
	if (host->dev_base)
		iounmap(host->dev_base);

	/* free mg_host */
	kfree(host);

	return err;
}

static struct platform_driver mg_disk_driver = {
	.probe = mg_probe,
	.remove = mg_remove,
	.suspend = mg_suspend,
	.resume = mg_resume,
	.driver = {
		.name = MG_DEV_NAME,
		.owner = THIS_MODULE,
	}
};

/****************************************************************************
 *
 * Module stuff
 *
 ****************************************************************************/

static int __init mg_init(void)
{
	printk(KERN_INFO "mGine mflash driver, (c) 2008 mGine Co.\n");
	return platform_driver_register(&mg_disk_driver);
}

static void __exit mg_exit(void)
{
	printk(KERN_INFO "mflash driver : bye bye\n");
	platform_driver_unregister(&mg_disk_driver);
}

module_init(mg_init);
module_exit(mg_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("unsik Kim <donari75@gmail.com>");
MODULE_DESCRIPTION("mGine m[g]flash device driver");
