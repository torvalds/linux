/*
 *  Sony MemoryStick Pro storage support
 *
 *  Copyright (C) 2007 Alex Dubov <oakad@yahoo.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Special thanks to Carlos Corbacho for providing various MemoryStick cards
 * that made this driver possible.
 *
 */

#include <linux/blkdev.h>
#include <linux/idr.h>
#include <linux/hdreg.h>
#include <linux/kthread.h>
#include <linux/memstick.h>

#define DRIVER_NAME "mspro_block"
#define DRIVER_VERSION "0.2"

static int major;
module_param(major, int, 0644);

#define MSPRO_BLOCK_MAX_SEGS  32
#define MSPRO_BLOCK_MAX_PAGES ((2 << 16) - 1)

#define MSPRO_BLOCK_SIGNATURE        0xa5c3
#define MSPRO_BLOCK_MAX_ATTRIBUTES   41

enum {
	MSPRO_BLOCK_ID_SYSINFO         = 0x10,
	MSPRO_BLOCK_ID_MODELNAME       = 0x15,
	MSPRO_BLOCK_ID_MBR             = 0x20,
	MSPRO_BLOCK_ID_PBR16           = 0x21,
	MSPRO_BLOCK_ID_PBR32           = 0x22,
	MSPRO_BLOCK_ID_SPECFILEVALUES1 = 0x25,
	MSPRO_BLOCK_ID_SPECFILEVALUES2 = 0x26,
	MSPRO_BLOCK_ID_DEVINFO         = 0x30
};

struct mspro_sys_attr {
	size_t                  size;
	void                    *data;
	unsigned char           id;
	char                    name[32];
	struct device_attribute dev_attr;
};

struct mspro_attr_entry {
	unsigned int  address;
	unsigned int  size;
	unsigned char id;
	unsigned char reserved[3];
} __attribute__((packed));

struct mspro_attribute {
	unsigned short          signature;
	unsigned short          version;
	unsigned char           count;
	unsigned char           reserved[11];
	struct mspro_attr_entry entries[];
} __attribute__((packed));

struct mspro_sys_info {
	unsigned char  class;
	unsigned char  reserved0;
	unsigned short block_size;
	unsigned short block_count;
	unsigned short user_block_count;
	unsigned short page_size;
	unsigned char  reserved1[2];
	unsigned char  assembly_date[8];
	unsigned int   serial_number;
	unsigned char  assembly_maker_code;
	unsigned char  assembly_model_code[3];
	unsigned short memory_maker_code;
	unsigned short memory_model_code;
	unsigned char  reserved2[4];
	unsigned char  vcc;
	unsigned char  vpp;
	unsigned short controller_number;
	unsigned short controller_function;
	unsigned short start_sector;
	unsigned short unit_size;
	unsigned char  ms_sub_class;
	unsigned char  reserved3[4];
	unsigned char  interface_type;
	unsigned short controller_code;
	unsigned char  format_type;
	unsigned char  reserved4;
	unsigned char  device_type;
	unsigned char  reserved5[7];
	unsigned char  mspro_id[16];
	unsigned char  reserved6[16];
} __attribute__((packed));

struct mspro_mbr {
	unsigned char boot_partition;
	unsigned char start_head;
	unsigned char start_sector;
	unsigned char start_cylinder;
	unsigned char partition_type;
	unsigned char end_head;
	unsigned char end_sector;
	unsigned char end_cylinder;
	unsigned int  start_sectors;
	unsigned int  sectors_per_partition;
} __attribute__((packed));

struct mspro_devinfo {
	unsigned short cylinders;
	unsigned short heads;
	unsigned short bytes_per_track;
	unsigned short bytes_per_sector;
	unsigned short sectors_per_track;
	unsigned char  reserved[6];
} __attribute__((packed));

struct mspro_block_data {
	struct memstick_dev   *card;
	unsigned int          usage_count;
	struct gendisk        *disk;
	struct request_queue  *queue;
	spinlock_t            q_lock;
	wait_queue_head_t     q_wait;
	struct task_struct    *q_thread;

	unsigned short        page_size;
	unsigned short        cylinders;
	unsigned short        heads;
	unsigned short        sectors_per_track;

	unsigned char         system;
	unsigned char         read_only:1,
			      active:1,
			      has_request:1,
			      data_dir:1;
	unsigned char         transfer_cmd;

	int                   (*mrq_handler)(struct memstick_dev *card,
					     struct memstick_request **mrq);

	struct attribute_group attr_group;

	struct scatterlist    req_sg[MSPRO_BLOCK_MAX_SEGS];
	unsigned int          seg_count;
	unsigned int          current_seg;
	unsigned short        current_page;
};

static DEFINE_IDR(mspro_block_disk_idr);
static DEFINE_MUTEX(mspro_block_disk_lock);

/*** Block device ***/

static int mspro_block_bd_open(struct inode *inode, struct file *filp)
{
	struct gendisk *disk = inode->i_bdev->bd_disk;
	struct mspro_block_data *msb = disk->private_data;
	int rc = -ENXIO;

	mutex_lock(&mspro_block_disk_lock);

	if (msb && msb->card) {
		msb->usage_count++;
		if ((filp->f_mode & FMODE_WRITE) && msb->read_only)
			rc = -EROFS;
		else
			rc = 0;
	}

	mutex_unlock(&mspro_block_disk_lock);

	return rc;
}


static int mspro_block_disk_release(struct gendisk *disk)
{
	struct mspro_block_data *msb = disk->private_data;
	int disk_id = disk->first_minor >> MEMSTICK_PART_SHIFT;

	mutex_lock(&mspro_block_disk_lock);

	if (msb->usage_count) {
		msb->usage_count--;
		if (!msb->usage_count) {
			kfree(msb);
			disk->private_data = NULL;
			idr_remove(&mspro_block_disk_idr, disk_id);
			put_disk(disk);
		}
	}

	mutex_unlock(&mspro_block_disk_lock);

	return 0;
}

static int mspro_block_bd_release(struct inode *inode, struct file *filp)
{
	struct gendisk *disk = inode->i_bdev->bd_disk;
	return mspro_block_disk_release(disk);
}

static int mspro_block_bd_getgeo(struct block_device *bdev,
				 struct hd_geometry *geo)
{
	struct mspro_block_data *msb = bdev->bd_disk->private_data;

	geo->heads = msb->heads;
	geo->sectors = msb->sectors_per_track;
	geo->cylinders = msb->cylinders;

	return 0;
}

static struct block_device_operations ms_block_bdops = {
	.open    = mspro_block_bd_open,
	.release = mspro_block_bd_release,
	.getgeo  = mspro_block_bd_getgeo,
	.owner   = THIS_MODULE
};

/*** Information ***/

static struct mspro_sys_attr *mspro_from_sysfs_attr(struct attribute *attr)
{
	struct device_attribute *dev_attr
		= container_of(attr, struct device_attribute, attr);
	return container_of(dev_attr, struct mspro_sys_attr, dev_attr);
}

static const char *mspro_block_attr_name(unsigned char tag)
{
	switch (tag) {
	case MSPRO_BLOCK_ID_SYSINFO:
		return "attr_sysinfo";
	case MSPRO_BLOCK_ID_MODELNAME:
		return "attr_modelname";
	case MSPRO_BLOCK_ID_MBR:
		return "attr_mbr";
	case MSPRO_BLOCK_ID_PBR16:
		return "attr_pbr16";
	case MSPRO_BLOCK_ID_PBR32:
		return "attr_pbr32";
	case MSPRO_BLOCK_ID_SPECFILEVALUES1:
		return "attr_specfilevalues1";
	case MSPRO_BLOCK_ID_SPECFILEVALUES2:
		return "attr_specfilevalues2";
	case MSPRO_BLOCK_ID_DEVINFO:
		return "attr_devinfo";
	default:
		return NULL;
	};
}

typedef ssize_t (*sysfs_show_t)(struct device *dev,
				struct device_attribute *attr,
				char *buffer);

static ssize_t mspro_block_attr_show_default(struct device *dev,
					     struct device_attribute *attr,
					     char *buffer)
{
	struct mspro_sys_attr *s_attr = container_of(attr,
						     struct mspro_sys_attr,
						     dev_attr);

	ssize_t cnt, rc = 0;

	for (cnt = 0; cnt < s_attr->size; cnt++) {
		if (cnt && !(cnt % 16)) {
			if (PAGE_SIZE - rc)
				buffer[rc++] = '\n';
		}

		rc += scnprintf(buffer + rc, PAGE_SIZE - rc, "%02x ",
				((unsigned char *)s_attr->data)[cnt]);
	}
	return rc;
}

static ssize_t mspro_block_attr_show_sysinfo(struct device *dev,
					     struct device_attribute *attr,
					     char *buffer)
{
	struct mspro_sys_attr *x_attr = container_of(attr,
						     struct mspro_sys_attr,
						     dev_attr);
	struct mspro_sys_info *x_sys = x_attr->data;
	ssize_t rc = 0;

	rc += scnprintf(buffer + rc, PAGE_SIZE - rc, "class: %x\n",
			x_sys->class);
	rc += scnprintf(buffer + rc, PAGE_SIZE - rc, "block size: %x\n",
			be16_to_cpu(x_sys->block_size));
	rc += scnprintf(buffer + rc, PAGE_SIZE - rc, "block count: %x\n",
			be16_to_cpu(x_sys->block_count));
	rc += scnprintf(buffer + rc, PAGE_SIZE - rc, "user block count: %x\n",
			be16_to_cpu(x_sys->user_block_count));
	rc += scnprintf(buffer + rc, PAGE_SIZE - rc, "page size: %x\n",
			be16_to_cpu(x_sys->page_size));
	rc += scnprintf(buffer + rc, PAGE_SIZE - rc, "assembly date: "
			"%d %04u-%02u-%02u %02u:%02u:%02u\n",
			x_sys->assembly_date[0],
			be16_to_cpu(*(unsigned short *)
				    &x_sys->assembly_date[1]),
			x_sys->assembly_date[3], x_sys->assembly_date[4],
			x_sys->assembly_date[5], x_sys->assembly_date[6],
			x_sys->assembly_date[7]);
	rc += scnprintf(buffer + rc, PAGE_SIZE - rc, "serial number: %x\n",
			be32_to_cpu(x_sys->serial_number));
	rc += scnprintf(buffer + rc, PAGE_SIZE - rc,
			"assembly maker code: %x\n",
			x_sys->assembly_maker_code);
	rc += scnprintf(buffer + rc, PAGE_SIZE - rc, "assembly model code: "
			"%02x%02x%02x\n", x_sys->assembly_model_code[0],
			x_sys->assembly_model_code[1],
			x_sys->assembly_model_code[2]);
	rc += scnprintf(buffer + rc, PAGE_SIZE - rc, "memory maker code: %x\n",
			be16_to_cpu(x_sys->memory_maker_code));
	rc += scnprintf(buffer + rc, PAGE_SIZE - rc, "memory model code: %x\n",
			be16_to_cpu(x_sys->memory_model_code));
	rc += scnprintf(buffer + rc, PAGE_SIZE - rc, "vcc: %x\n",
			x_sys->vcc);
	rc += scnprintf(buffer + rc, PAGE_SIZE - rc, "vpp: %x\n",
			x_sys->vpp);
	rc += scnprintf(buffer + rc, PAGE_SIZE - rc, "controller number: %x\n",
			be16_to_cpu(x_sys->controller_number));
	rc += scnprintf(buffer + rc, PAGE_SIZE - rc,
			"controller function: %x\n",
			be16_to_cpu(x_sys->controller_function));
	rc += scnprintf(buffer + rc, PAGE_SIZE - rc, "start sector: %x\n",
			be16_to_cpu(x_sys->start_sector));
	rc += scnprintf(buffer + rc, PAGE_SIZE - rc, "unit size: %x\n",
			be16_to_cpu(x_sys->unit_size));
	rc += scnprintf(buffer + rc, PAGE_SIZE - rc, "sub class: %x\n",
			x_sys->ms_sub_class);
	rc += scnprintf(buffer + rc, PAGE_SIZE - rc, "interface type: %x\n",
			x_sys->interface_type);
	rc += scnprintf(buffer + rc, PAGE_SIZE - rc, "controller code: %x\n",
			be16_to_cpu(x_sys->controller_code));
	rc += scnprintf(buffer + rc, PAGE_SIZE - rc, "format type: %x\n",
			x_sys->format_type);
	rc += scnprintf(buffer + rc, PAGE_SIZE - rc, "device type: %x\n",
			x_sys->device_type);
	rc += scnprintf(buffer + rc, PAGE_SIZE - rc, "mspro id: %s\n",
			x_sys->mspro_id);
	return rc;
}

static ssize_t mspro_block_attr_show_modelname(struct device *dev,
					       struct device_attribute *attr,
					       char *buffer)
{
	struct mspro_sys_attr *s_attr = container_of(attr,
						     struct mspro_sys_attr,
						     dev_attr);

	return scnprintf(buffer, PAGE_SIZE, "%s", (char *)s_attr->data);
}

static ssize_t mspro_block_attr_show_mbr(struct device *dev,
					 struct device_attribute *attr,
					 char *buffer)
{
	struct mspro_sys_attr *x_attr = container_of(attr,
						     struct mspro_sys_attr,
						     dev_attr);
	struct mspro_mbr *x_mbr = x_attr->data;
	ssize_t rc = 0;

	rc += scnprintf(buffer + rc, PAGE_SIZE - rc, "boot partition: %x\n",
			x_mbr->boot_partition);
	rc += scnprintf(buffer + rc, PAGE_SIZE - rc, "start head: %x\n",
			x_mbr->start_head);
	rc += scnprintf(buffer + rc, PAGE_SIZE - rc, "start sector: %x\n",
			x_mbr->start_sector);
	rc += scnprintf(buffer + rc, PAGE_SIZE - rc, "start cylinder: %x\n",
			x_mbr->start_cylinder);
	rc += scnprintf(buffer + rc, PAGE_SIZE - rc, "partition type: %x\n",
			x_mbr->partition_type);
	rc += scnprintf(buffer + rc, PAGE_SIZE - rc, "end head: %x\n",
			x_mbr->end_head);
	rc += scnprintf(buffer + rc, PAGE_SIZE - rc, "end sector: %x\n",
			x_mbr->end_sector);
	rc += scnprintf(buffer + rc, PAGE_SIZE - rc, "end cylinder: %x\n",
			x_mbr->end_cylinder);
	rc += scnprintf(buffer + rc, PAGE_SIZE - rc, "start sectors: %x\n",
			x_mbr->start_sectors);
	rc += scnprintf(buffer + rc, PAGE_SIZE - rc,
			"sectors per partition: %x\n",
			x_mbr->sectors_per_partition);
	return rc;
}

static ssize_t mspro_block_attr_show_devinfo(struct device *dev,
					     struct device_attribute *attr,
					     char *buffer)
{
	struct mspro_sys_attr *x_attr = container_of(attr,
						     struct mspro_sys_attr,
						     dev_attr);
	struct mspro_devinfo *x_devinfo = x_attr->data;
	ssize_t rc = 0;

	rc += scnprintf(buffer + rc, PAGE_SIZE - rc, "cylinders: %x\n",
			be16_to_cpu(x_devinfo->cylinders));
	rc += scnprintf(buffer + rc, PAGE_SIZE - rc, "heads: %x\n",
			be16_to_cpu(x_devinfo->heads));
	rc += scnprintf(buffer + rc, PAGE_SIZE - rc, "bytes per track: %x\n",
			be16_to_cpu(x_devinfo->bytes_per_track));
	rc += scnprintf(buffer + rc, PAGE_SIZE - rc, "bytes per sector: %x\n",
			be16_to_cpu(x_devinfo->bytes_per_sector));
	rc += scnprintf(buffer + rc, PAGE_SIZE - rc, "sectors per track: %x\n",
			be16_to_cpu(x_devinfo->sectors_per_track));
	return rc;
}

static sysfs_show_t mspro_block_attr_show(unsigned char tag)
{
	switch (tag) {
	case MSPRO_BLOCK_ID_SYSINFO:
		return mspro_block_attr_show_sysinfo;
	case MSPRO_BLOCK_ID_MODELNAME:
		return mspro_block_attr_show_modelname;
	case MSPRO_BLOCK_ID_MBR:
		return mspro_block_attr_show_mbr;
	case MSPRO_BLOCK_ID_DEVINFO:
		return mspro_block_attr_show_devinfo;
	default:
		return mspro_block_attr_show_default;
	}
}

/*** Protocol handlers ***/

/*
 * Functions prefixed with "h_" are protocol callbacks. They can be called from
 * interrupt context. Return value of 0 means that request processing is still
 * ongoing, while special error value of -EAGAIN means that current request is
 * finished (and request processor should come back some time later).
 */

static int h_mspro_block_req_init(struct memstick_dev *card,
				  struct memstick_request **mrq)
{
	struct mspro_block_data *msb = memstick_get_drvdata(card);

	*mrq = &card->current_mrq;
	card->next_request = msb->mrq_handler;
	return 0;
}

static int h_mspro_block_default(struct memstick_dev *card,
				 struct memstick_request **mrq)
{
	complete(&card->mrq_complete);
	if (!(*mrq)->error)
		return -EAGAIN;
	else
		return (*mrq)->error;
}

static int h_mspro_block_get_ro(struct memstick_dev *card,
				struct memstick_request **mrq)
{
	struct mspro_block_data *msb = memstick_get_drvdata(card);

	if ((*mrq)->error) {
		complete(&card->mrq_complete);
		return (*mrq)->error;
	}

	if ((*mrq)->data[offsetof(struct ms_status_register, status0)]
	    & MEMSTICK_STATUS0_WP)
		msb->read_only = 1;
	else
		msb->read_only = 0;

	complete(&card->mrq_complete);
	return -EAGAIN;
}

static int h_mspro_block_wait_for_ced(struct memstick_dev *card,
				      struct memstick_request **mrq)
{
	if ((*mrq)->error) {
		complete(&card->mrq_complete);
		return (*mrq)->error;
	}

	dev_dbg(&card->dev, "wait for ced: value %x\n", (*mrq)->data[0]);

	if ((*mrq)->data[0] & (MEMSTICK_INT_CMDNAK | MEMSTICK_INT_ERR)) {
		card->current_mrq.error = -EFAULT;
		complete(&card->mrq_complete);
		return card->current_mrq.error;
	}

	if (!((*mrq)->data[0] & MEMSTICK_INT_CED))
		return 0;
	else {
		card->current_mrq.error = 0;
		complete(&card->mrq_complete);
		return -EAGAIN;
	}
}

static int h_mspro_block_transfer_data(struct memstick_dev *card,
				       struct memstick_request **mrq)
{
	struct memstick_host *host = card->host;
	struct mspro_block_data *msb = memstick_get_drvdata(card);
	unsigned char t_val = 0;
	struct scatterlist t_sg = { 0 };
	size_t t_offset;

	if ((*mrq)->error) {
		complete(&card->mrq_complete);
		return (*mrq)->error;
	}

	switch ((*mrq)->tpc) {
	case MS_TPC_WRITE_REG:
		memstick_init_req(*mrq, MS_TPC_SET_CMD, &msb->transfer_cmd, 1);
		(*mrq)->get_int_reg = 1;
		return 0;
	case MS_TPC_SET_CMD:
		t_val = (*mrq)->int_reg;
		memstick_init_req(*mrq, MS_TPC_GET_INT, NULL, 1);
		if (host->caps & MEMSTICK_CAP_AUTO_GET_INT)
			goto has_int_reg;
		return 0;
	case MS_TPC_GET_INT:
		t_val = (*mrq)->data[0];
has_int_reg:
		if (t_val & (MEMSTICK_INT_CMDNAK | MEMSTICK_INT_ERR)) {
			t_val = MSPRO_CMD_STOP;
			memstick_init_req(*mrq, MS_TPC_SET_CMD, &t_val, 1);
			card->next_request = h_mspro_block_default;
			return 0;
		}

		if (msb->current_page
		    == (msb->req_sg[msb->current_seg].length
			/ msb->page_size)) {
			msb->current_page = 0;
			msb->current_seg++;

			if (msb->current_seg == msb->seg_count) {
				if (t_val & MEMSTICK_INT_CED) {
					complete(&card->mrq_complete);
					return -EAGAIN;
				} else {
					card->next_request
						= h_mspro_block_wait_for_ced;
					memstick_init_req(*mrq, MS_TPC_GET_INT,
							  NULL, 1);
					return 0;
				}
			}
		}

		if (!(t_val & MEMSTICK_INT_BREQ)) {
			memstick_init_req(*mrq, MS_TPC_GET_INT, NULL, 1);
			return 0;
		}

		t_offset = msb->req_sg[msb->current_seg].offset;
		t_offset += msb->current_page * msb->page_size;

		sg_set_page(&t_sg,
			    nth_page(sg_page(&(msb->req_sg[msb->current_seg])),
				     t_offset >> PAGE_SHIFT),
			    msb->page_size, offset_in_page(t_offset));

		memstick_init_req_sg(*mrq, msb->data_dir == READ
					   ? MS_TPC_READ_LONG_DATA
					   : MS_TPC_WRITE_LONG_DATA,
				     &t_sg);
		(*mrq)->get_int_reg = 1;
		return 0;
	case MS_TPC_READ_LONG_DATA:
	case MS_TPC_WRITE_LONG_DATA:
		msb->current_page++;
		if (host->caps & MEMSTICK_CAP_AUTO_GET_INT) {
			t_val = (*mrq)->int_reg;
			goto has_int_reg;
		} else {
			memstick_init_req(*mrq, MS_TPC_GET_INT, NULL, 1);
			return 0;
		}

	default:
		BUG();
	}
}

/*** Data transfer ***/

static void mspro_block_process_request(struct memstick_dev *card,
					struct request *req)
{
	struct mspro_block_data *msb = memstick_get_drvdata(card);
	struct mspro_param_register param;
	int rc, chunk, cnt;
	unsigned short page_count;
	sector_t t_sec;
	unsigned long flags;

	do {
		page_count = 0;
		msb->current_seg = 0;
		msb->seg_count = blk_rq_map_sg(req->q, req, msb->req_sg);

		if (msb->seg_count) {
			msb->current_page = 0;
			for (rc = 0; rc < msb->seg_count; rc++)
				page_count += msb->req_sg[rc].length
					      / msb->page_size;

			t_sec = req->sector;
			sector_div(t_sec, msb->page_size >> 9);
			param.system = msb->system;
			param.data_count = cpu_to_be16(page_count);
			param.data_address = cpu_to_be32((uint32_t)t_sec);
			param.tpc_param = 0;

			msb->data_dir = rq_data_dir(req);
			msb->transfer_cmd = msb->data_dir == READ
					    ? MSPRO_CMD_READ_DATA
					    : MSPRO_CMD_WRITE_DATA;

			dev_dbg(&card->dev, "data transfer: cmd %x, "
				"lba %x, count %x\n", msb->transfer_cmd,
				be32_to_cpu(param.data_address),
				page_count);

			card->next_request = h_mspro_block_req_init;
			msb->mrq_handler = h_mspro_block_transfer_data;
			memstick_init_req(&card->current_mrq, MS_TPC_WRITE_REG,
					  &param, sizeof(param));
			memstick_new_req(card->host);
			wait_for_completion(&card->mrq_complete);
			rc = card->current_mrq.error;

			if (rc || (card->current_mrq.tpc == MSPRO_CMD_STOP)) {
				for (cnt = 0; cnt < msb->current_seg; cnt++)
					page_count += msb->req_sg[cnt].length
						      / msb->page_size;

				if (msb->current_page)
					page_count += msb->current_page - 1;

				if (page_count && (msb->data_dir == READ))
					rc = msb->page_size * page_count;
				else
					rc = -EIO;
			} else
				rc = msb->page_size * page_count;
		} else
			rc = -EFAULT;

		spin_lock_irqsave(&msb->q_lock, flags);
		if (rc >= 0)
			chunk = __blk_end_request(req, 0, rc);
		else
			chunk = __blk_end_request(req, rc, 0);

		dev_dbg(&card->dev, "end chunk %d, %d\n", rc, chunk);
		spin_unlock_irqrestore(&msb->q_lock, flags);
	} while (chunk);
}

static int mspro_block_has_request(struct mspro_block_data *msb)
{
	int rc = 0;
	unsigned long flags;

	spin_lock_irqsave(&msb->q_lock, flags);
	if (kthread_should_stop() || msb->has_request)
		rc = 1;
	spin_unlock_irqrestore(&msb->q_lock, flags);
	return rc;
}

static int mspro_block_queue_thread(void *data)
{
	struct memstick_dev *card = data;
	struct memstick_host *host = card->host;
	struct mspro_block_data *msb = memstick_get_drvdata(card);
	struct request *req;
	unsigned long flags;

	while (1) {
		wait_event(msb->q_wait, mspro_block_has_request(msb));
		dev_dbg(&card->dev, "thread iter\n");

		spin_lock_irqsave(&msb->q_lock, flags);
		req = elv_next_request(msb->queue);
		dev_dbg(&card->dev, "next req %p\n", req);
		if (!req) {
			msb->has_request = 0;
			if (kthread_should_stop()) {
				spin_unlock_irqrestore(&msb->q_lock, flags);
				break;
			}
		} else
			msb->has_request = 1;
		spin_unlock_irqrestore(&msb->q_lock, flags);

		if (req) {
			mutex_lock(&host->lock);
			mspro_block_process_request(card, req);
			mutex_unlock(&host->lock);
		}
	}
	dev_dbg(&card->dev, "thread finished\n");
	return 0;
}

static void mspro_block_request(struct request_queue *q)
{
	struct memstick_dev *card = q->queuedata;
	struct mspro_block_data *msb = memstick_get_drvdata(card);
	struct request *req = NULL;

	if (msb->q_thread) {
		msb->has_request = 1;
		wake_up_all(&msb->q_wait);
	} else {
		while ((req = elv_next_request(q)) != NULL)
			end_queued_request(req, -ENODEV);
	}
}

/*** Initialization ***/

static int mspro_block_wait_for_ced(struct memstick_dev *card)
{
	struct mspro_block_data *msb = memstick_get_drvdata(card);

	card->next_request = h_mspro_block_req_init;
	msb->mrq_handler = h_mspro_block_wait_for_ced;
	memstick_init_req(&card->current_mrq, MS_TPC_GET_INT, NULL, 1);
	memstick_new_req(card->host);
	wait_for_completion(&card->mrq_complete);
	return card->current_mrq.error;
}

static int mspro_block_switch_to_parallel(struct memstick_dev *card)
{
	struct memstick_host *host = card->host;
	struct mspro_block_data *msb = memstick_get_drvdata(card);
	struct mspro_param_register param = {
		.system = 0,
		.data_count = 0,
		.data_address = 0,
		.tpc_param = 0
	};

	card->next_request = h_mspro_block_req_init;
	msb->mrq_handler = h_mspro_block_default;
	memstick_init_req(&card->current_mrq, MS_TPC_WRITE_REG, &param,
			  sizeof(param));
	memstick_new_req(host);
	wait_for_completion(&card->mrq_complete);
	if (card->current_mrq.error)
		return card->current_mrq.error;

	msb->system = MEMSTICK_SYS_PAR4;
	host->set_param(host, MEMSTICK_INTERFACE, MEMSTICK_PAR4);

	card->next_request = h_mspro_block_req_init;
	msb->mrq_handler = h_mspro_block_default;
	memstick_init_req(&card->current_mrq, MS_TPC_GET_INT, NULL, 1);
	memstick_new_req(card->host);
	wait_for_completion(&card->mrq_complete);

	if (card->current_mrq.error) {
		msb->system = 0x80;
		host->set_param(host, MEMSTICK_INTERFACE, MEMSTICK_SERIAL);
		return -EFAULT;
	}

	return 0;
}

/* Memory allocated for attributes by this function should be freed by
 * mspro_block_data_clear, no matter if the initialization process succeded
 * or failed.
 */
static int mspro_block_read_attributes(struct memstick_dev *card)
{
	struct mspro_block_data *msb = memstick_get_drvdata(card);
	struct mspro_param_register param = {
		.system = msb->system,
		.data_count = cpu_to_be16(1),
		.data_address = 0,
		.tpc_param = 0
	};
	struct mspro_attribute *attr = NULL;
	struct mspro_sys_attr *s_attr = NULL;
	unsigned char *buffer = NULL;
	int cnt, rc, attr_count;
	unsigned int addr;
	unsigned short page_count;

	attr = kmalloc(msb->page_size, GFP_KERNEL);
	if (!attr)
		return -ENOMEM;

	sg_init_one(&msb->req_sg[0], attr, msb->page_size);
	msb->seg_count = 1;
	msb->current_seg = 0;
	msb->current_page = 0;
	msb->data_dir = READ;
	msb->transfer_cmd = MSPRO_CMD_READ_ATRB;

	card->next_request = h_mspro_block_req_init;
	msb->mrq_handler = h_mspro_block_transfer_data;
	memstick_init_req(&card->current_mrq, MS_TPC_WRITE_REG, &param,
			  sizeof(param));
	memstick_new_req(card->host);
	wait_for_completion(&card->mrq_complete);
	if (card->current_mrq.error) {
		rc = card->current_mrq.error;
		goto out_free_attr;
	}

	if (be16_to_cpu(attr->signature) != MSPRO_BLOCK_SIGNATURE) {
		printk(KERN_ERR "%s: unrecognized device signature %x\n",
		       card->dev.bus_id, be16_to_cpu(attr->signature));
		rc = -ENODEV;
		goto out_free_attr;
	}

	if (attr->count > MSPRO_BLOCK_MAX_ATTRIBUTES) {
		printk(KERN_WARNING "%s: way too many attribute entries\n",
		       card->dev.bus_id);
		attr_count = MSPRO_BLOCK_MAX_ATTRIBUTES;
	} else
		attr_count = attr->count;

	msb->attr_group.attrs = kzalloc((attr_count + 1)
					* sizeof(struct attribute),
					GFP_KERNEL);
	if (!msb->attr_group.attrs) {
		rc = -ENOMEM;
		goto out_free_attr;
	}
	msb->attr_group.name = "media_attributes";

	buffer = kmalloc(msb->page_size, GFP_KERNEL);
	if (!buffer) {
		rc = -ENOMEM;
		goto out_free_attr;
	}
	memcpy(buffer, (char *)attr, msb->page_size);
	page_count = 1;

	for (cnt = 0; cnt < attr_count; ++cnt) {
		s_attr = kzalloc(sizeof(struct mspro_sys_attr), GFP_KERNEL);
		if (!s_attr) {
			rc = -ENOMEM;
			goto out_free_buffer;
		}

		msb->attr_group.attrs[cnt] = &s_attr->dev_attr.attr;
		addr = be32_to_cpu(attr->entries[cnt].address);
		rc = be32_to_cpu(attr->entries[cnt].size);
		dev_dbg(&card->dev, "adding attribute %d: id %x, address %x, "
			"size %x\n", cnt, attr->entries[cnt].id, addr, rc);
		s_attr->id = attr->entries[cnt].id;
		if (mspro_block_attr_name(s_attr->id))
			snprintf(s_attr->name, sizeof(s_attr->name), "%s",
				 mspro_block_attr_name(attr->entries[cnt].id));
		else
			snprintf(s_attr->name, sizeof(s_attr->name),
				 "attr_x%02x", attr->entries[cnt].id);

		s_attr->dev_attr.attr.name = s_attr->name;
		s_attr->dev_attr.attr.mode = S_IRUGO;
		s_attr->dev_attr.attr.owner = THIS_MODULE;
		s_attr->dev_attr.show = mspro_block_attr_show(s_attr->id);

		if (!rc)
			continue;

		s_attr->size = rc;
		s_attr->data = kmalloc(rc, GFP_KERNEL);
		if (!s_attr->data) {
			rc = -ENOMEM;
			goto out_free_buffer;
		}

		if (((addr / msb->page_size)
		     == be32_to_cpu(param.data_address))
		    && (((addr + rc - 1) / msb->page_size)
			== be32_to_cpu(param.data_address))) {
			memcpy(s_attr->data, buffer + addr % msb->page_size,
			       rc);
			continue;
		}

		if (page_count <= (rc / msb->page_size)) {
			kfree(buffer);
			page_count = (rc / msb->page_size) + 1;
			buffer = kmalloc(page_count * msb->page_size,
					 GFP_KERNEL);
			if (!buffer) {
				rc = -ENOMEM;
				goto out_free_attr;
			}
		}

		param.system = msb->system;
		param.data_count = cpu_to_be16((rc / msb->page_size) + 1);
		param.data_address = cpu_to_be32(addr / msb->page_size);
		param.tpc_param = 0;

		sg_init_one(&msb->req_sg[0], buffer,
			    be16_to_cpu(param.data_count) * msb->page_size);
		msb->seg_count = 1;
		msb->current_seg = 0;
		msb->current_page = 0;
		msb->data_dir = READ;
		msb->transfer_cmd = MSPRO_CMD_READ_ATRB;

		dev_dbg(&card->dev, "reading attribute pages %x, %x\n",
			be32_to_cpu(param.data_address),
			be16_to_cpu(param.data_count));

		card->next_request = h_mspro_block_req_init;
		msb->mrq_handler = h_mspro_block_transfer_data;
		memstick_init_req(&card->current_mrq, MS_TPC_WRITE_REG,
				  (char *)&param, sizeof(param));
		memstick_new_req(card->host);
		wait_for_completion(&card->mrq_complete);
		if (card->current_mrq.error) {
			rc = card->current_mrq.error;
			goto out_free_buffer;
		}

		memcpy(s_attr->data, buffer + addr % msb->page_size, rc);
	}

	rc = 0;
out_free_buffer:
	kfree(buffer);
out_free_attr:
	kfree(attr);
	return rc;
}

static int mspro_block_init_card(struct memstick_dev *card)
{
	struct mspro_block_data *msb = memstick_get_drvdata(card);
	struct memstick_host *host = card->host;
	int rc = 0;

	msb->system = MEMSTICK_SYS_SERIAL;
	card->reg_addr.r_offset = offsetof(struct mspro_register, status);
	card->reg_addr.r_length = sizeof(struct ms_status_register);
	card->reg_addr.w_offset = offsetof(struct mspro_register, param);
	card->reg_addr.w_length = sizeof(struct mspro_param_register);

	if (memstick_set_rw_addr(card))
		return -EIO;

	if (host->caps & MEMSTICK_CAP_PAR4) {
		if (mspro_block_switch_to_parallel(card))
			printk(KERN_WARNING "%s: could not switch to "
			       "parallel interface\n", card->dev.bus_id);
	}

	rc = mspro_block_wait_for_ced(card);
	if (rc)
		return rc;
	dev_dbg(&card->dev, "card activated\n");

	card->next_request = h_mspro_block_req_init;
	msb->mrq_handler = h_mspro_block_get_ro;
	memstick_init_req(&card->current_mrq, MS_TPC_READ_REG, NULL,
			  sizeof(struct ms_status_register));
	memstick_new_req(card->host);
	wait_for_completion(&card->mrq_complete);
	if (card->current_mrq.error)
		return card->current_mrq.error;

	dev_dbg(&card->dev, "card r/w status %d\n", msb->read_only ? 0 : 1);

	msb->page_size = 512;
	rc = mspro_block_read_attributes(card);
	if (rc)
		return rc;

	dev_dbg(&card->dev, "attributes loaded\n");
	return 0;

}

static int mspro_block_init_disk(struct memstick_dev *card)
{
	struct mspro_block_data *msb = memstick_get_drvdata(card);
	struct memstick_host *host = card->host;
	struct mspro_devinfo *dev_info = NULL;
	struct mspro_sys_info *sys_info = NULL;
	struct mspro_sys_attr *s_attr = NULL;
	int rc, disk_id;
	u64 limit = BLK_BOUNCE_HIGH;
	unsigned long capacity;

	if (host->cdev.dev->dma_mask && *(host->cdev.dev->dma_mask))
		limit = *(host->cdev.dev->dma_mask);

	for (rc = 0; msb->attr_group.attrs[rc]; ++rc) {
		s_attr = mspro_from_sysfs_attr(msb->attr_group.attrs[rc]);

		if (s_attr->id == MSPRO_BLOCK_ID_DEVINFO)
			dev_info = s_attr->data;
		else if (s_attr->id == MSPRO_BLOCK_ID_SYSINFO)
			sys_info = s_attr->data;
	}

	if (!dev_info || !sys_info)
		return -ENODEV;

	msb->cylinders = be16_to_cpu(dev_info->cylinders);
	msb->heads = be16_to_cpu(dev_info->heads);
	msb->sectors_per_track = be16_to_cpu(dev_info->sectors_per_track);

	msb->page_size = be16_to_cpu(sys_info->unit_size);

	if (!idr_pre_get(&mspro_block_disk_idr, GFP_KERNEL))
		return -ENOMEM;

	mutex_lock(&mspro_block_disk_lock);
	rc = idr_get_new(&mspro_block_disk_idr, card, &disk_id);
	mutex_unlock(&mspro_block_disk_lock);

	if (rc)
		return rc;

	if ((disk_id << MEMSTICK_PART_SHIFT) > 255) {
		rc = -ENOSPC;
		goto out_release_id;
	}

	msb->disk = alloc_disk(1 << MEMSTICK_PART_SHIFT);
	if (!msb->disk) {
		rc = -ENOMEM;
		goto out_release_id;
	}

	spin_lock_init(&msb->q_lock);
	init_waitqueue_head(&msb->q_wait);

	msb->queue = blk_init_queue(mspro_block_request, &msb->q_lock);
	if (!msb->queue) {
		rc = -ENOMEM;
		goto out_put_disk;
	}

	msb->queue->queuedata = card;

	blk_queue_bounce_limit(msb->queue, limit);
	blk_queue_max_sectors(msb->queue, MSPRO_BLOCK_MAX_PAGES);
	blk_queue_max_phys_segments(msb->queue, MSPRO_BLOCK_MAX_SEGS);
	blk_queue_max_hw_segments(msb->queue, MSPRO_BLOCK_MAX_SEGS);
	blk_queue_max_segment_size(msb->queue,
				   MSPRO_BLOCK_MAX_PAGES * msb->page_size);

	msb->disk->major = major;
	msb->disk->first_minor = disk_id << MEMSTICK_PART_SHIFT;
	msb->disk->fops = &ms_block_bdops;
	msb->usage_count = 1;
	msb->disk->private_data = msb;
	msb->disk->queue = msb->queue;
	msb->disk->driverfs_dev = &card->dev;

	sprintf(msb->disk->disk_name, "mspblk%d", disk_id);

	blk_queue_hardsect_size(msb->queue, msb->page_size);

	capacity = be16_to_cpu(sys_info->user_block_count);
	capacity *= be16_to_cpu(sys_info->block_size);
	capacity *= msb->page_size >> 9;
	set_capacity(msb->disk, capacity);
	dev_dbg(&card->dev, "capacity set %ld\n", capacity);
	msb->q_thread = kthread_run(mspro_block_queue_thread, card,
				    DRIVER_NAME"d");
	if (IS_ERR(msb->q_thread))
		goto out_put_disk;

	mutex_unlock(&host->lock);
	add_disk(msb->disk);
	mutex_lock(&host->lock);
	msb->active = 1;
	return 0;

out_put_disk:
	put_disk(msb->disk);
out_release_id:
	mutex_lock(&mspro_block_disk_lock);
	idr_remove(&mspro_block_disk_idr, disk_id);
	mutex_unlock(&mspro_block_disk_lock);
	return rc;
}

static void mspro_block_data_clear(struct mspro_block_data *msb)
{
	int cnt;
	struct mspro_sys_attr *s_attr;

	if (msb->attr_group.attrs) {
		for (cnt = 0; msb->attr_group.attrs[cnt]; ++cnt) {
			s_attr = mspro_from_sysfs_attr(msb->attr_group
							   .attrs[cnt]);
			kfree(s_attr->data);
			kfree(s_attr);
		}
		kfree(msb->attr_group.attrs);
	}

	msb->card = NULL;
}

static int mspro_block_check_card(struct memstick_dev *card)
{
	struct mspro_block_data *msb = memstick_get_drvdata(card);

	return (msb->active == 1);
}

static int mspro_block_probe(struct memstick_dev *card)
{
	struct mspro_block_data *msb;
	int rc = 0;

	msb = kzalloc(sizeof(struct mspro_block_data), GFP_KERNEL);
	if (!msb)
		return -ENOMEM;
	memstick_set_drvdata(card, msb);
	msb->card = card;

	rc = mspro_block_init_card(card);

	if (rc)
		goto out_free;

	rc = sysfs_create_group(&card->dev.kobj, &msb->attr_group);
	if (rc)
		goto out_free;

	rc = mspro_block_init_disk(card);
	if (!rc) {
		card->check = mspro_block_check_card;
		return 0;
	}

	sysfs_remove_group(&card->dev.kobj, &msb->attr_group);
out_free:
	memstick_set_drvdata(card, NULL);
	mspro_block_data_clear(msb);
	kfree(msb);
	return rc;
}

static void mspro_block_remove(struct memstick_dev *card)
{
	struct mspro_block_data *msb = memstick_get_drvdata(card);
	struct task_struct *q_thread = NULL;
	unsigned long flags;

	del_gendisk(msb->disk);
	dev_dbg(&card->dev, "mspro block remove\n");
	spin_lock_irqsave(&msb->q_lock, flags);
	q_thread = msb->q_thread;
	msb->q_thread = NULL;
	msb->active = 0;
	spin_unlock_irqrestore(&msb->q_lock, flags);

	if (q_thread) {
		mutex_unlock(&card->host->lock);
		kthread_stop(q_thread);
		mutex_lock(&card->host->lock);
	}

	dev_dbg(&card->dev, "queue thread stopped\n");

	blk_cleanup_queue(msb->queue);

	sysfs_remove_group(&card->dev.kobj, &msb->attr_group);

	mutex_lock(&mspro_block_disk_lock);
	mspro_block_data_clear(msb);
	mutex_unlock(&mspro_block_disk_lock);

	mspro_block_disk_release(msb->disk);
	memstick_set_drvdata(card, NULL);
}

#ifdef CONFIG_PM

static int mspro_block_suspend(struct memstick_dev *card, pm_message_t state)
{
	struct mspro_block_data *msb = memstick_get_drvdata(card);
	struct task_struct *q_thread = NULL;
	unsigned long flags;

	spin_lock_irqsave(&msb->q_lock, flags);
	q_thread = msb->q_thread;
	msb->q_thread = NULL;
	msb->active = 0;
	blk_stop_queue(msb->queue);
	spin_unlock_irqrestore(&msb->q_lock, flags);

	if (q_thread)
		kthread_stop(q_thread);

	return 0;
}

static int mspro_block_resume(struct memstick_dev *card)
{
	struct mspro_block_data *msb = memstick_get_drvdata(card);
	unsigned long flags;
	int rc = 0;

#ifdef CONFIG_MEMSTICK_UNSAFE_RESUME

	struct mspro_block_data *new_msb;
	struct memstick_host *host = card->host;
	struct mspro_sys_attr *s_attr, *r_attr;
	unsigned char cnt;

	mutex_lock(&host->lock);
	new_msb = kzalloc(sizeof(struct mspro_block_data), GFP_KERNEL);
	if (!new_msb) {
		rc = -ENOMEM;
		goto out_unlock;
	}

	new_msb->card = card;
	memstick_set_drvdata(card, new_msb);
	if (mspro_block_init_card(card))
		goto out_free;

	for (cnt = 0; new_msb->attr_group.attrs[cnt]
		      && msb->attr_group.attrs[cnt]; ++cnt) {
		s_attr = mspro_from_sysfs_attr(new_msb->attr_group.attrs[cnt]);
		r_attr = mspro_from_sysfs_attr(msb->attr_group.attrs[cnt]);

		if (s_attr->id == MSPRO_BLOCK_ID_SYSINFO
		    && r_attr->id == s_attr->id) {
			if (memcmp(s_attr->data, r_attr->data, s_attr->size))
				break;

			memstick_set_drvdata(card, msb);
			msb->q_thread = kthread_run(mspro_block_queue_thread,
						    card, DRIVER_NAME"d");
			if (IS_ERR(msb->q_thread))
				msb->q_thread = NULL;
			else
				msb->active = 1;

			break;
		}
	}

out_free:
	memstick_set_drvdata(card, msb);
	mspro_block_data_clear(new_msb);
	kfree(new_msb);
out_unlock:
	mutex_unlock(&host->lock);

#endif /* CONFIG_MEMSTICK_UNSAFE_RESUME */

	spin_lock_irqsave(&msb->q_lock, flags);
	blk_start_queue(msb->queue);
	spin_unlock_irqrestore(&msb->q_lock, flags);
	return rc;
}

#else

#define mspro_block_suspend NULL
#define mspro_block_resume NULL

#endif /* CONFIG_PM */

static struct memstick_device_id mspro_block_id_tbl[] = {
	{MEMSTICK_MATCH_ALL, MEMSTICK_TYPE_PRO, MEMSTICK_CATEGORY_STORAGE_DUO,
	 MEMSTICK_CLASS_GENERIC_DUO},
	{}
};


static struct memstick_driver mspro_block_driver = {
	.driver = {
		.name  = DRIVER_NAME,
		.owner = THIS_MODULE
	},
	.id_table = mspro_block_id_tbl,
	.probe    = mspro_block_probe,
	.remove   = mspro_block_remove,
	.suspend  = mspro_block_suspend,
	.resume   = mspro_block_resume
};

static int __init mspro_block_init(void)
{
	int rc = -ENOMEM;

	rc = register_blkdev(major, DRIVER_NAME);
	if (rc < 0) {
		printk(KERN_ERR DRIVER_NAME ": failed to register "
		       "major %d, error %d\n", major, rc);
		return rc;
	}
	if (!major)
		major = rc;

	rc = memstick_register_driver(&mspro_block_driver);
	if (rc)
		unregister_blkdev(major, DRIVER_NAME);
	return rc;
}

static void __exit mspro_block_exit(void)
{
	memstick_unregister_driver(&mspro_block_driver);
	unregister_blkdev(major, DRIVER_NAME);
	idr_destroy(&mspro_block_disk_idr);
}

module_init(mspro_block_init);
module_exit(mspro_block_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Alex Dubov");
MODULE_DESCRIPTION("Sony MemoryStickPro block device driver");
MODULE_DEVICE_TABLE(memstick, mspro_block_id_tbl);
MODULE_VERSION(DRIVER_VERSION);
