// SPDX-License-Identifier: GPL-2.0-only
/*
 *  Sony MemoryStick Pro storage support
 *
 *  Copyright (C) 2007 Alex Dubov <oakad@yahoo.com>
 *
 * Special thanks to Carlos Corbacho for providing various MemoryStick cards
 * that made this driver possible.
 */

#include <linux/blk-mq.h>
#include <linux/idr.h>
#include <linux/hdreg.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/memstick.h>
#include <linux/module.h>

#define DRIVER_NAME "mspro_block"

static int major;
module_param(major, int, 0644);

#define MSPRO_BLOCK_MAX_SEGS  32
#define MSPRO_BLOCK_MAX_PAGES ((2 << 16) - 1)

#define MSPRO_BLOCK_SIGNATURE        0xa5c3
#define MSPRO_BLOCK_MAX_ATTRIBUTES   41

#define MSPRO_BLOCK_PART_SHIFT 3

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
	__be32 address;
	__be32 size;
	unsigned char id;
	unsigned char reserved[3];
} __attribute__((packed));

struct mspro_attribute {
	__be16 signature;
	unsigned short          version;
	unsigned char           count;
	unsigned char           reserved[11];
	struct mspro_attr_entry entries[];
} __attribute__((packed));

struct mspro_sys_info {
	unsigned char  class;
	unsigned char  reserved0;
	__be16 block_size;
	__be16 block_count;
	__be16 user_block_count;
	__be16 page_size;
	unsigned char  reserved1[2];
	unsigned char  assembly_date[8];
	__be32 serial_number;
	unsigned char  assembly_maker_code;
	unsigned char  assembly_model_code[3];
	__be16 memory_maker_code;
	__be16 memory_model_code;
	unsigned char  reserved2[4];
	unsigned char  vcc;
	unsigned char  vpp;
	__be16 controller_number;
	__be16 controller_function;
	__be16 start_sector;
	__be16 unit_size;
	unsigned char  ms_sub_class;
	unsigned char  reserved3[4];
	unsigned char  interface_type;
	__be16 controller_code;
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

struct mspro_specfile {
	char           name[8];
	char           ext[3];
	unsigned char  attr;
	unsigned char  reserved[10];
	unsigned short time;
	unsigned short date;
	unsigned short cluster;
	unsigned int   size;
} __attribute__((packed));

struct mspro_devinfo {
	__be16 cylinders;
	__be16 heads;
	__be16 bytes_per_track;
	__be16 bytes_per_sector;
	__be16 sectors_per_track;
	unsigned char  reserved[6];
} __attribute__((packed));

struct mspro_block_data {
	struct memstick_dev   *card;
	unsigned int          usage_count;
	unsigned int          caps;
	struct gendisk        *disk;
	struct request_queue  *queue;
	struct request        *block_req;
	struct blk_mq_tag_set tag_set;
	spinlock_t            q_lock;

	unsigned short        page_size;
	unsigned short        cylinders;
	unsigned short        heads;
	unsigned short        sectors_per_track;

	unsigned char         system;
	unsigned char         read_only:1,
			      eject:1,
			      data_dir:1,
			      active:1;
	unsigned char         transfer_cmd;

	int                   (*mrq_handler)(struct memstick_dev *card,
					     struct memstick_request **mrq);


	/* Default request setup function for data access method preferred by
	 * this host instance.
	 */
	void                  (*setup_transfer)(struct memstick_dev *card,
						u64 offset, size_t length);

	struct attribute_group attr_group;

	struct scatterlist    req_sg[MSPRO_BLOCK_MAX_SEGS];
	unsigned int          seg_count;
	unsigned int          current_seg;
	unsigned int          current_page;
};

static DEFINE_IDR(mspro_block_disk_idr);
static DEFINE_MUTEX(mspro_block_disk_lock);

static int mspro_block_complete_req(struct memstick_dev *card, int error);

/*** Block device ***/

static int mspro_block_bd_open(struct block_device *bdev, fmode_t mode)
{
	struct gendisk *disk = bdev->bd_disk;
	struct mspro_block_data *msb = disk->private_data;
	int rc = -ENXIO;

	mutex_lock(&mspro_block_disk_lock);

	if (msb && msb->card) {
		msb->usage_count++;
		if ((mode & FMODE_WRITE) && msb->read_only)
			rc = -EROFS;
		else
			rc = 0;
	}

	mutex_unlock(&mspro_block_disk_lock);

	return rc;
}


static void mspro_block_disk_release(struct gendisk *disk)
{
	struct mspro_block_data *msb = disk->private_data;
	int disk_id = MINOR(disk_devt(disk)) >> MSPRO_BLOCK_PART_SHIFT;

	mutex_lock(&mspro_block_disk_lock);

	if (msb) {
		if (msb->usage_count)
			msb->usage_count--;

		if (!msb->usage_count) {
			kfree(msb);
			disk->private_data = NULL;
			idr_remove(&mspro_block_disk_idr, disk_id);
			put_disk(disk);
		}
	}

	mutex_unlock(&mspro_block_disk_lock);
}

static void mspro_block_bd_release(struct gendisk *disk, fmode_t mode)
{
	mspro_block_disk_release(disk);
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

static const struct block_device_operations ms_block_bdops = {
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
	}
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
	int date_tz = 0, date_tz_f = 0;

	if (x_sys->assembly_date[0] > 0x80U) {
		date_tz = (~x_sys->assembly_date[0]) + 1;
		date_tz_f = date_tz & 3;
		date_tz >>= 2;
		date_tz = -date_tz;
		date_tz_f *= 15;
	} else if (x_sys->assembly_date[0] < 0x80U) {
		date_tz = x_sys->assembly_date[0];
		date_tz_f = date_tz & 3;
		date_tz >>= 2;
		date_tz_f *= 15;
	}

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
			"GMT%+d:%d %04u-%02u-%02u %02u:%02u:%02u\n",
			date_tz, date_tz_f,
			be16_to_cpup((__be16 *)&x_sys->assembly_date[1]),
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

static ssize_t mspro_block_attr_show_specfile(struct device *dev,
					      struct device_attribute *attr,
					      char *buffer)
{
	struct mspro_sys_attr *x_attr = container_of(attr,
						     struct mspro_sys_attr,
						     dev_attr);
	struct mspro_specfile *x_spfile = x_attr->data;
	char name[9], ext[4];
	ssize_t rc = 0;

	memcpy(name, x_spfile->name, 8);
	name[8] = 0;
	memcpy(ext, x_spfile->ext, 3);
	ext[3] = 0;

	rc += scnprintf(buffer + rc, PAGE_SIZE - rc, "name: %s\n", name);
	rc += scnprintf(buffer + rc, PAGE_SIZE - rc, "ext: %s\n", ext);
	rc += scnprintf(buffer + rc, PAGE_SIZE - rc, "attribute: %x\n",
			x_spfile->attr);
	rc += scnprintf(buffer + rc, PAGE_SIZE - rc, "time: %d:%d:%d\n",
			x_spfile->time >> 11,
			(x_spfile->time >> 5) & 0x3f,
			(x_spfile->time & 0x1f) * 2);
	rc += scnprintf(buffer + rc, PAGE_SIZE - rc, "date: %d-%d-%d\n",
			(x_spfile->date >> 9) + 1980,
			(x_spfile->date >> 5) & 0xf,
			x_spfile->date & 0x1f);
	rc += scnprintf(buffer + rc, PAGE_SIZE - rc, "start cluster: %x\n",
			x_spfile->cluster);
	rc += scnprintf(buffer + rc, PAGE_SIZE - rc, "size: %x\n",
			x_spfile->size);
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
	case MSPRO_BLOCK_ID_SPECFILEVALUES1:
	case MSPRO_BLOCK_ID_SPECFILEVALUES2:
		return mspro_block_attr_show_specfile;
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
	return mspro_block_complete_req(card, (*mrq)->error);
}

static int h_mspro_block_default_bad(struct memstick_dev *card,
				     struct memstick_request **mrq)
{
	return -ENXIO;
}

static int h_mspro_block_get_ro(struct memstick_dev *card,
				struct memstick_request **mrq)
{
	struct mspro_block_data *msb = memstick_get_drvdata(card);

	if (!(*mrq)->error) {
		if ((*mrq)->data[offsetof(struct ms_status_register, status0)]
		    & MEMSTICK_STATUS0_WP)
			msb->read_only = 1;
		else
			msb->read_only = 0;
	}

	return mspro_block_complete_req(card, (*mrq)->error);
}

static int h_mspro_block_wait_for_ced(struct memstick_dev *card,
				      struct memstick_request **mrq)
{
	dev_dbg(&card->dev, "wait for ced: value %x\n", (*mrq)->data[0]);

	if (!(*mrq)->error) {
		if ((*mrq)->data[0] & (MEMSTICK_INT_CMDNAK | MEMSTICK_INT_ERR))
			(*mrq)->error = -EFAULT;
		else if (!((*mrq)->data[0] & MEMSTICK_INT_CED))
			return 0;
	}

	return mspro_block_complete_req(card, (*mrq)->error);
}

static int h_mspro_block_transfer_data(struct memstick_dev *card,
				       struct memstick_request **mrq)
{
	struct mspro_block_data *msb = memstick_get_drvdata(card);
	unsigned char t_val = 0;
	struct scatterlist t_sg = { 0 };
	size_t t_offset;

	if ((*mrq)->error)
		return mspro_block_complete_req(card, (*mrq)->error);

	switch ((*mrq)->tpc) {
	case MS_TPC_WRITE_REG:
		memstick_init_req(*mrq, MS_TPC_SET_CMD, &msb->transfer_cmd, 1);
		(*mrq)->need_card_int = 1;
		return 0;
	case MS_TPC_SET_CMD:
		t_val = (*mrq)->int_reg;
		memstick_init_req(*mrq, MS_TPC_GET_INT, NULL, 1);
		if (msb->caps & MEMSTICK_CAP_AUTO_GET_INT)
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
					return mspro_block_complete_req(card,
									0);
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
		(*mrq)->need_card_int = 1;
		return 0;
	case MS_TPC_READ_LONG_DATA:
	case MS_TPC_WRITE_LONG_DATA:
		msb->current_page++;
		if (msb->caps & MEMSTICK_CAP_AUTO_GET_INT) {
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

/*** Transfer setup functions for different access methods. ***/

/** Setup data transfer request for SET_CMD TPC with arguments in card
 *  registers.
 *
 *  @card    Current media instance
 *  @offset  Target data offset in bytes
 *  @length  Required transfer length in bytes.
 */
static void h_mspro_block_setup_cmd(struct memstick_dev *card, u64 offset,
				    size_t length)
{
	struct mspro_block_data *msb = memstick_get_drvdata(card);
	struct mspro_param_register param = {
		.system = msb->system,
		.data_count = cpu_to_be16((uint16_t)(length / msb->page_size)),
		/* ISO C90 warning precludes direct initialization for now. */
		.data_address = 0,
		.tpc_param = 0
	};

	do_div(offset, msb->page_size);
	param.data_address = cpu_to_be32((uint32_t)offset);

	card->next_request = h_mspro_block_req_init;
	msb->mrq_handler = h_mspro_block_transfer_data;
	memstick_init_req(&card->current_mrq, MS_TPC_WRITE_REG,
			  &param, sizeof(param));
}

/*** Data transfer ***/

static int mspro_block_issue_req(struct memstick_dev *card)
{
	struct mspro_block_data *msb = memstick_get_drvdata(card);
	u64 t_off;
	unsigned int count;

	while (true) {
		msb->current_page = 0;
		msb->current_seg = 0;
		msb->seg_count = blk_rq_map_sg(msb->block_req->q,
					       msb->block_req,
					       msb->req_sg);

		if (!msb->seg_count) {
			unsigned int bytes = blk_rq_cur_bytes(msb->block_req);
			bool chunk;

			chunk = blk_update_request(msb->block_req,
							BLK_STS_RESOURCE,
							bytes);
			if (chunk)
				continue;
			__blk_mq_end_request(msb->block_req,
						BLK_STS_RESOURCE);
			msb->block_req = NULL;
			return -EAGAIN;
		}

		t_off = blk_rq_pos(msb->block_req);
		t_off <<= 9;
		count = blk_rq_bytes(msb->block_req);

		msb->setup_transfer(card, t_off, count);

		msb->data_dir = rq_data_dir(msb->block_req);
		msb->transfer_cmd = msb->data_dir == READ
				    ? MSPRO_CMD_READ_DATA
				    : MSPRO_CMD_WRITE_DATA;

		memstick_new_req(card->host);
		return 0;
	}
}

static int mspro_block_complete_req(struct memstick_dev *card, int error)
{
	struct mspro_block_data *msb = memstick_get_drvdata(card);
	int cnt;
	bool chunk;
	unsigned int t_len = 0;
	unsigned long flags;

	spin_lock_irqsave(&msb->q_lock, flags);
	dev_dbg(&card->dev, "complete %d, %d\n", msb->block_req ? 1 : 0,
		error);

	if (msb->block_req) {
		/* Nothing to do - not really an error */
		if (error == -EAGAIN)
			error = 0;

		if (error || (card->current_mrq.tpc == MSPRO_CMD_STOP)) {
			if (msb->data_dir == READ) {
				for (cnt = 0; cnt < msb->current_seg; cnt++) {
					t_len += msb->req_sg[cnt].length
						 / msb->page_size;

					if (msb->current_page)
						t_len += msb->current_page - 1;

					t_len *= msb->page_size;
				}
			}
		} else
			t_len = blk_rq_bytes(msb->block_req);

		dev_dbg(&card->dev, "transferred %x (%d)\n", t_len, error);

		if (error && !t_len)
			t_len = blk_rq_cur_bytes(msb->block_req);

		chunk = blk_update_request(msb->block_req,
				errno_to_blk_status(error), t_len);
		if (chunk) {
			error = mspro_block_issue_req(card);
			if (!error)
				goto out;
		} else {
			__blk_mq_end_request(msb->block_req,
						errno_to_blk_status(error));
			msb->block_req = NULL;
		}
	} else {
		if (!error)
			error = -EAGAIN;
	}

	card->next_request = h_mspro_block_default_bad;
	complete_all(&card->mrq_complete);
out:
	spin_unlock_irqrestore(&msb->q_lock, flags);
	return error;
}

static void mspro_block_stop(struct memstick_dev *card)
{
	struct mspro_block_data *msb = memstick_get_drvdata(card);
	int rc = 0;
	unsigned long flags;

	while (1) {
		spin_lock_irqsave(&msb->q_lock, flags);
		if (!msb->block_req) {
			blk_mq_stop_hw_queues(msb->queue);
			rc = 1;
		}
		spin_unlock_irqrestore(&msb->q_lock, flags);

		if (rc)
			break;

		wait_for_completion(&card->mrq_complete);
	}
}

static void mspro_block_start(struct memstick_dev *card)
{
	struct mspro_block_data *msb = memstick_get_drvdata(card);

	blk_mq_start_hw_queues(msb->queue);
}

static blk_status_t mspro_queue_rq(struct blk_mq_hw_ctx *hctx,
				   const struct blk_mq_queue_data *bd)
{
	struct memstick_dev *card = hctx->queue->queuedata;
	struct mspro_block_data *msb = memstick_get_drvdata(card);

	spin_lock_irq(&msb->q_lock);

	if (msb->block_req) {
		spin_unlock_irq(&msb->q_lock);
		return BLK_STS_DEV_RESOURCE;
	}

	if (msb->eject) {
		spin_unlock_irq(&msb->q_lock);
		blk_mq_start_request(bd->rq);
		return BLK_STS_IOERR;
	}

	msb->block_req = bd->rq;
	blk_mq_start_request(bd->rq);

	if (mspro_block_issue_req(card))
		msb->block_req = NULL;

	spin_unlock_irq(&msb->q_lock);
	return BLK_STS_OK;
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

static int mspro_block_set_interface(struct memstick_dev *card,
				     unsigned char sys_reg)
{
	struct memstick_host *host = card->host;
	struct mspro_block_data *msb = memstick_get_drvdata(card);
	struct mspro_param_register param = {
		.system = sys_reg,
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
	return card->current_mrq.error;
}

static int mspro_block_switch_interface(struct memstick_dev *card)
{
	struct memstick_host *host = card->host;
	struct mspro_block_data *msb = memstick_get_drvdata(card);
	int rc = 0;

try_again:
	if (msb->caps & MEMSTICK_CAP_PAR4)
		rc = mspro_block_set_interface(card, MEMSTICK_SYS_PAR4);
	else
		return 0;

	if (rc) {
		printk(KERN_WARNING
		       "%s: could not switch to 4-bit mode, error %d\n",
		       dev_name(&card->dev), rc);
		return 0;
	}

	msb->system = MEMSTICK_SYS_PAR4;
	host->set_param(host, MEMSTICK_INTERFACE, MEMSTICK_PAR4);
	printk(KERN_INFO "%s: switching to 4-bit parallel mode\n",
	       dev_name(&card->dev));

	if (msb->caps & MEMSTICK_CAP_PAR8) {
		rc = mspro_block_set_interface(card, MEMSTICK_SYS_PAR8);

		if (!rc) {
			msb->system = MEMSTICK_SYS_PAR8;
			host->set_param(host, MEMSTICK_INTERFACE,
					MEMSTICK_PAR8);
			printk(KERN_INFO
			       "%s: switching to 8-bit parallel mode\n",
			       dev_name(&card->dev));
		} else
			printk(KERN_WARNING
			       "%s: could not switch to 8-bit mode, error %d\n",
			       dev_name(&card->dev), rc);
	}

	card->next_request = h_mspro_block_req_init;
	msb->mrq_handler = h_mspro_block_default;
	memstick_init_req(&card->current_mrq, MS_TPC_GET_INT, NULL, 1);
	memstick_new_req(card->host);
	wait_for_completion(&card->mrq_complete);
	rc = card->current_mrq.error;

	if (rc) {
		printk(KERN_WARNING
		       "%s: interface error, trying to fall back to serial\n",
		       dev_name(&card->dev));
		msb->system = MEMSTICK_SYS_SERIAL;
		host->set_param(host, MEMSTICK_POWER, MEMSTICK_POWER_OFF);
		msleep(10);
		host->set_param(host, MEMSTICK_POWER, MEMSTICK_POWER_ON);
		host->set_param(host, MEMSTICK_INTERFACE, MEMSTICK_SERIAL);

		rc = memstick_set_rw_addr(card);
		if (!rc)
			rc = mspro_block_set_interface(card, msb->system);

		if (!rc) {
			msleep(150);
			rc = mspro_block_wait_for_ced(card);
			if (rc)
				return rc;

			if (msb->caps & MEMSTICK_CAP_PAR8) {
				msb->caps &= ~MEMSTICK_CAP_PAR8;
				goto try_again;
			}
		}
	}
	return rc;
}

/* Memory allocated for attributes by this function should be freed by
 * mspro_block_data_clear, no matter if the initialization process succeeded
 * or failed.
 */
static int mspro_block_read_attributes(struct memstick_dev *card)
{
	struct mspro_block_data *msb = memstick_get_drvdata(card);
	struct mspro_attribute *attr = NULL;
	struct mspro_sys_attr *s_attr = NULL;
	unsigned char *buffer = NULL;
	int cnt, rc, attr_count;
	/* While normally physical device offsets, represented here by
	 * attr_offset and attr_len will be of large numeric types, we can be
	 * sure, that attributes are close enough to the beginning of the
	 * device, to save ourselves some trouble.
	 */
	unsigned int addr, attr_offset = 0, attr_len = msb->page_size;

	attr = kmalloc(msb->page_size, GFP_KERNEL);
	if (!attr)
		return -ENOMEM;

	sg_init_one(&msb->req_sg[0], attr, msb->page_size);
	msb->seg_count = 1;
	msb->current_seg = 0;
	msb->current_page = 0;
	msb->data_dir = READ;
	msb->transfer_cmd = MSPRO_CMD_READ_ATRB;

	msb->setup_transfer(card, attr_offset, attr_len);

	memstick_new_req(card->host);
	wait_for_completion(&card->mrq_complete);
	if (card->current_mrq.error) {
		rc = card->current_mrq.error;
		goto out_free_attr;
	}

	if (be16_to_cpu(attr->signature) != MSPRO_BLOCK_SIGNATURE) {
		printk(KERN_ERR "%s: unrecognized device signature %x\n",
		       dev_name(&card->dev), be16_to_cpu(attr->signature));
		rc = -ENODEV;
		goto out_free_attr;
	}

	if (attr->count > MSPRO_BLOCK_MAX_ATTRIBUTES) {
		printk(KERN_WARNING "%s: way too many attribute entries\n",
		       dev_name(&card->dev));
		attr_count = MSPRO_BLOCK_MAX_ATTRIBUTES;
	} else
		attr_count = attr->count;

	msb->attr_group.attrs = kcalloc(attr_count + 1,
					sizeof(*msb->attr_group.attrs),
					GFP_KERNEL);
	if (!msb->attr_group.attrs) {
		rc = -ENOMEM;
		goto out_free_attr;
	}
	msb->attr_group.name = "media_attributes";

	buffer = kmemdup(attr, attr_len, GFP_KERNEL);
	if (!buffer) {
		rc = -ENOMEM;
		goto out_free_attr;
	}

	for (cnt = 0; cnt < attr_count; ++cnt) {
		s_attr = kzalloc(sizeof(struct mspro_sys_attr), GFP_KERNEL);
		if (!s_attr) {
			rc = -ENOMEM;
			goto out_free_buffer;
		}

		msb->attr_group.attrs[cnt] = &s_attr->dev_attr.attr;
		addr = be32_to_cpu(attr->entries[cnt].address);
		s_attr->size = be32_to_cpu(attr->entries[cnt].size);
		dev_dbg(&card->dev, "adding attribute %d: id %x, address %x, "
			"size %zx\n", cnt, attr->entries[cnt].id, addr,
			s_attr->size);
		s_attr->id = attr->entries[cnt].id;
		if (mspro_block_attr_name(s_attr->id))
			snprintf(s_attr->name, sizeof(s_attr->name), "%s",
				 mspro_block_attr_name(attr->entries[cnt].id));
		else
			snprintf(s_attr->name, sizeof(s_attr->name),
				 "attr_x%02x", attr->entries[cnt].id);

		sysfs_attr_init(&s_attr->dev_attr.attr);
		s_attr->dev_attr.attr.name = s_attr->name;
		s_attr->dev_attr.attr.mode = S_IRUGO;
		s_attr->dev_attr.show = mspro_block_attr_show(s_attr->id);

		if (!s_attr->size)
			continue;

		s_attr->data = kmalloc(s_attr->size, GFP_KERNEL);
		if (!s_attr->data) {
			rc = -ENOMEM;
			goto out_free_buffer;
		}

		if (((addr / msb->page_size) == (attr_offset / msb->page_size))
		    && (((addr + s_attr->size - 1) / msb->page_size)
			== (attr_offset / msb->page_size))) {
			memcpy(s_attr->data, buffer + addr % msb->page_size,
			       s_attr->size);
			continue;
		}

		attr_offset = (addr / msb->page_size) * msb->page_size;

		if ((attr_offset + attr_len) < (addr + s_attr->size)) {
			kfree(buffer);
			attr_len = (((addr + s_attr->size) / msb->page_size)
				    + 1 ) * msb->page_size - attr_offset;
			buffer = kmalloc(attr_len, GFP_KERNEL);
			if (!buffer) {
				rc = -ENOMEM;
				goto out_free_attr;
			}
		}

		sg_init_one(&msb->req_sg[0], buffer, attr_len);
		msb->seg_count = 1;
		msb->current_seg = 0;
		msb->current_page = 0;
		msb->data_dir = READ;
		msb->transfer_cmd = MSPRO_CMD_READ_ATRB;

		dev_dbg(&card->dev, "reading attribute range %x, %x\n",
			attr_offset, attr_len);

		msb->setup_transfer(card, attr_offset, attr_len);
		memstick_new_req(card->host);
		wait_for_completion(&card->mrq_complete);
		if (card->current_mrq.error) {
			rc = card->current_mrq.error;
			goto out_free_buffer;
		}

		memcpy(s_attr->data, buffer + addr % msb->page_size,
		       s_attr->size);
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
	msb->setup_transfer = h_mspro_block_setup_cmd;

	card->reg_addr.r_offset = offsetof(struct mspro_register, status);
	card->reg_addr.r_length = sizeof(struct ms_status_register);
	card->reg_addr.w_offset = offsetof(struct mspro_register, param);
	card->reg_addr.w_length = sizeof(struct mspro_param_register);

	if (memstick_set_rw_addr(card))
		return -EIO;

	msb->caps = host->caps;

	msleep(150);
	rc = mspro_block_wait_for_ced(card);
	if (rc)
		return rc;

	rc = mspro_block_switch_interface(card);
	if (rc)
		return rc;

	dev_dbg(&card->dev, "card activated\n");
	if (msb->system != MEMSTICK_SYS_SERIAL)
		msb->caps |= MEMSTICK_CAP_AUTO_GET_INT;

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

static const struct blk_mq_ops mspro_mq_ops = {
	.queue_rq	= mspro_queue_rq,
};

static int mspro_block_init_disk(struct memstick_dev *card)
{
	struct mspro_block_data *msb = memstick_get_drvdata(card);
	struct mspro_devinfo *dev_info = NULL;
	struct mspro_sys_info *sys_info = NULL;
	struct mspro_sys_attr *s_attr = NULL;
	int rc, disk_id;
	unsigned long capacity;

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

	mutex_lock(&mspro_block_disk_lock);
	disk_id = idr_alloc(&mspro_block_disk_idr, card, 0, 256, GFP_KERNEL);
	mutex_unlock(&mspro_block_disk_lock);
	if (disk_id < 0)
		return disk_id;

	rc = blk_mq_alloc_sq_tag_set(&msb->tag_set, &mspro_mq_ops, 2,
				     BLK_MQ_F_SHOULD_MERGE);
	if (rc)
		goto out_release_id;

	msb->disk = blk_mq_alloc_disk(&msb->tag_set, card);
	if (IS_ERR(msb->disk)) {
		rc = PTR_ERR(msb->disk);
		goto out_free_tag_set;
	}
	msb->queue = msb->disk->queue;

	blk_queue_max_hw_sectors(msb->queue, MSPRO_BLOCK_MAX_PAGES);
	blk_queue_max_segments(msb->queue, MSPRO_BLOCK_MAX_SEGS);
	blk_queue_max_segment_size(msb->queue,
				   MSPRO_BLOCK_MAX_PAGES * msb->page_size);

	msb->disk->major = major;
	msb->disk->first_minor = disk_id << MSPRO_BLOCK_PART_SHIFT;
	msb->disk->minors = 1 << MSPRO_BLOCK_PART_SHIFT;
	msb->disk->fops = &ms_block_bdops;
	msb->usage_count = 1;
	msb->disk->private_data = msb;

	sprintf(msb->disk->disk_name, "mspblk%d", disk_id);

	blk_queue_logical_block_size(msb->queue, msb->page_size);

	capacity = be16_to_cpu(sys_info->user_block_count);
	capacity *= be16_to_cpu(sys_info->block_size);
	capacity *= msb->page_size >> 9;
	set_capacity(msb->disk, capacity);
	dev_dbg(&card->dev, "capacity set %ld\n", capacity);

	device_add_disk(&card->dev, msb->disk, NULL);
	msb->active = 1;
	return 0;

out_free_tag_set:
	blk_mq_free_tag_set(&msb->tag_set);
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
	spin_lock_init(&msb->q_lock);

	rc = mspro_block_init_card(card);

	if (rc)
		goto out_free;

	rc = sysfs_create_group(&card->dev.kobj, &msb->attr_group);
	if (rc)
		goto out_free;

	rc = mspro_block_init_disk(card);
	if (!rc) {
		card->check = mspro_block_check_card;
		card->stop = mspro_block_stop;
		card->start = mspro_block_start;
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
	unsigned long flags;

	spin_lock_irqsave(&msb->q_lock, flags);
	msb->eject = 1;
	spin_unlock_irqrestore(&msb->q_lock, flags);
	blk_mq_start_hw_queues(msb->queue);

	del_gendisk(msb->disk);
	dev_dbg(&card->dev, "mspro block remove\n");

	blk_cleanup_queue(msb->queue);
	blk_mq_free_tag_set(&msb->tag_set);
	msb->queue = NULL;

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
	unsigned long flags;

	blk_mq_stop_hw_queues(msb->queue);

	spin_lock_irqsave(&msb->q_lock, flags);
	msb->active = 0;
	spin_unlock_irqrestore(&msb->q_lock, flags);

	return 0;
}

static int mspro_block_resume(struct memstick_dev *card)
{
	struct mspro_block_data *msb = memstick_get_drvdata(card);
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
	rc = mspro_block_init_card(card);
	if (rc)
		goto out_free;

	for (cnt = 0; new_msb->attr_group.attrs[cnt]
		      && msb->attr_group.attrs[cnt]; ++cnt) {
		s_attr = mspro_from_sysfs_attr(new_msb->attr_group.attrs[cnt]);
		r_attr = mspro_from_sysfs_attr(msb->attr_group.attrs[cnt]);

		if (s_attr->id == MSPRO_BLOCK_ID_SYSINFO
		    && r_attr->id == s_attr->id) {
			if (memcmp(s_attr->data, r_attr->data, s_attr->size))
				break;

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

	blk_mq_start_hw_queues(msb->queue);
	return rc;
}

#else

#define mspro_block_suspend NULL
#define mspro_block_resume NULL

#endif /* CONFIG_PM */

static struct memstick_device_id mspro_block_id_tbl[] = {
	{MEMSTICK_MATCH_ALL, MEMSTICK_TYPE_PRO, MEMSTICK_CATEGORY_STORAGE_DUO,
	 MEMSTICK_CLASS_DUO},
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
