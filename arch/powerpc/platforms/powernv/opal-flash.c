// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * PowerNV OPAL Firmware Update Interface
 *
 * Copyright 2013 IBM Corp.
 */

#define DEBUG

#include <linux/kernel.h>
#include <linux/reboot.h>
#include <linux/init.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/pagemap.h>
#include <linux/delay.h>

#include <asm/opal.h>

/* FLASH status codes */
#define FLASH_NO_OP		-1099	/* No operation initiated by user */
#define FLASH_NO_AUTH		-9002	/* Not a service authority partition */

/* Validate image status values */
#define VALIDATE_IMG_READY	-1001	/* Image ready for validation */
#define VALIDATE_IMG_INCOMPLETE	-1002	/* User copied < VALIDATE_BUF_SIZE */

/* Manage image status values */
#define MANAGE_ACTIVE_ERR	-9001	/* Cannot overwrite active img */

/* Flash image status values */
#define FLASH_IMG_READY		0	/* Img ready for flash on reboot */
#define FLASH_INVALID_IMG	-1003	/* Flash image shorter than expected */
#define FLASH_IMG_NULL_DATA	-1004	/* Bad data in sg list entry */
#define FLASH_IMG_BAD_LEN	-1005	/* Bad length in sg list entry */

/* Manage operation tokens */
#define FLASH_REJECT_TMP_SIDE	0	/* Reject temporary fw image */
#define FLASH_COMMIT_TMP_SIDE	1	/* Commit temporary fw image */

/* Update tokens */
#define FLASH_UPDATE_CANCEL	0	/* Cancel update request */
#define FLASH_UPDATE_INIT	1	/* Initiate update */

/* Validate image update result tokens */
#define VALIDATE_TMP_UPDATE	0     /* T side will be updated */
#define VALIDATE_FLASH_AUTH	1     /* Partition does not have authority */
#define VALIDATE_INVALID_IMG	2     /* Candidate image is not valid */
#define VALIDATE_CUR_UNKNOWN	3     /* Current fixpack level is unknown */
/*
 * Current T side will be committed to P side before being replace with new
 * image, and the new image is downlevel from current image
 */
#define VALIDATE_TMP_COMMIT_DL	4
/*
 * Current T side will be committed to P side before being replaced with new
 * image
 */
#define VALIDATE_TMP_COMMIT	5
/*
 * T side will be updated with a downlevel image
 */
#define VALIDATE_TMP_UPDATE_DL	6
/*
 * The candidate image's release date is later than the system's firmware
 * service entitlement date - service warranty period has expired
 */
#define VALIDATE_OUT_OF_WRNTY	7

/* Validate buffer size */
#define VALIDATE_BUF_SIZE	4096

/* XXX: Assume candidate image size is <= 1GB */
#define MAX_IMAGE_SIZE	0x40000000

/* Image status */
enum {
	IMAGE_INVALID,
	IMAGE_LOADING,
	IMAGE_READY,
};

/* Candidate image data */
struct image_data_t {
	int		status;
	void		*data;
	uint32_t	size;
};

/* Candidate image header */
struct image_header_t {
	uint16_t	magic;
	uint16_t	version;
	uint32_t	size;
};

struct validate_flash_t {
	int		status;		/* Return status */
	void		*buf;		/* Candidate image buffer */
	uint32_t	buf_size;	/* Image size */
	uint32_t	result;		/* Update results token */
};

struct manage_flash_t {
	int status;		/* Return status */
};

struct update_flash_t {
	int status;		/* Return status */
};

static struct image_header_t	image_header;
static struct image_data_t	image_data;
static struct validate_flash_t	validate_flash_data;
static struct manage_flash_t	manage_flash_data;

/* Initialize update_flash_data status to No Operation */
static struct update_flash_t	update_flash_data = {
	.status = FLASH_NO_OP,
};

static DEFINE_MUTEX(image_data_mutex);

/*
 * Validate candidate image
 */
static inline void opal_flash_validate(void)
{
	long ret;
	void *buf = validate_flash_data.buf;
	__be32 size = cpu_to_be32(validate_flash_data.buf_size);
	__be32 result;

	ret = opal_validate_flash(__pa(buf), &size, &result);

	validate_flash_data.status = ret;
	validate_flash_data.buf_size = be32_to_cpu(size);
	validate_flash_data.result = be32_to_cpu(result);
}

/*
 * Validate output format:
 *     validate result token
 *     current image version details
 *     new image version details
 */
static ssize_t validate_show(struct kobject *kobj,
			     struct kobj_attribute *attr, char *buf)
{
	struct validate_flash_t *args_buf = &validate_flash_data;
	int len;

	/* Candidate image is not validated */
	if (args_buf->status < VALIDATE_TMP_UPDATE) {
		len = sprintf(buf, "%d\n", args_buf->status);
		goto out;
	}

	/* Result token */
	len = sprintf(buf, "%d\n", args_buf->result);

	/* Current and candidate image version details */
	if ((args_buf->result != VALIDATE_TMP_UPDATE) &&
	    (args_buf->result < VALIDATE_CUR_UNKNOWN))
		goto out;

	if (args_buf->buf_size > (VALIDATE_BUF_SIZE - len)) {
		memcpy(buf + len, args_buf->buf, VALIDATE_BUF_SIZE - len);
		len = VALIDATE_BUF_SIZE;
	} else {
		memcpy(buf + len, args_buf->buf, args_buf->buf_size);
		len += args_buf->buf_size;
	}
out:
	/* Set status to default */
	args_buf->status = FLASH_NO_OP;
	return len;
}

/*
 * Validate candidate firmware image
 *
 * Note:
 *   We are only interested in first 4K bytes of the
 *   candidate image.
 */
static ssize_t validate_store(struct kobject *kobj,
			      struct kobj_attribute *attr,
			      const char *buf, size_t count)
{
	struct validate_flash_t *args_buf = &validate_flash_data;

	if (buf[0] != '1')
		return -EINVAL;

	mutex_lock(&image_data_mutex);

	if (image_data.status != IMAGE_READY ||
	    image_data.size < VALIDATE_BUF_SIZE) {
		args_buf->result = VALIDATE_INVALID_IMG;
		args_buf->status = VALIDATE_IMG_INCOMPLETE;
		goto out;
	}

	/* Copy first 4k bytes of candidate image */
	memcpy(args_buf->buf, image_data.data, VALIDATE_BUF_SIZE);

	args_buf->status = VALIDATE_IMG_READY;
	args_buf->buf_size = VALIDATE_BUF_SIZE;

	/* Validate candidate image */
	opal_flash_validate();

out:
	mutex_unlock(&image_data_mutex);
	return count;
}

/*
 * Manage flash routine
 */
static inline void opal_flash_manage(uint8_t op)
{
	struct manage_flash_t *const args_buf = &manage_flash_data;

	args_buf->status = opal_manage_flash(op);
}

/*
 * Show manage flash status
 */
static ssize_t manage_show(struct kobject *kobj,
			   struct kobj_attribute *attr, char *buf)
{
	struct manage_flash_t *const args_buf = &manage_flash_data;
	int rc;

	rc = sprintf(buf, "%d\n", args_buf->status);
	/* Set status to default*/
	args_buf->status = FLASH_NO_OP;
	return rc;
}

/*
 * Manage operations:
 *   0 - Reject
 *   1 - Commit
 */
static ssize_t manage_store(struct kobject *kobj,
			    struct kobj_attribute *attr,
			    const char *buf, size_t count)
{
	uint8_t op;
	switch (buf[0]) {
	case '0':
		op = FLASH_REJECT_TMP_SIDE;
		break;
	case '1':
		op = FLASH_COMMIT_TMP_SIDE;
		break;
	default:
		return -EINVAL;
	}

	/* commit/reject temporary image */
	opal_flash_manage(op);
	return count;
}

/*
 * OPAL update flash
 */
static int opal_flash_update(int op)
{
	struct opal_sg_list *list;
	unsigned long addr;
	int64_t rc = OPAL_PARAMETER;

	if (op == FLASH_UPDATE_CANCEL) {
		pr_alert("FLASH: Image update cancelled\n");
		addr = '\0';
		goto flash;
	}

	list = opal_vmalloc_to_sg_list(image_data.data, image_data.size);
	if (!list)
		goto invalid_img;

	/* First entry address */
	addr = __pa(list);

flash:
	rc = opal_update_flash(addr);

invalid_img:
	return rc;
}

/* This gets called just before system reboots */
void opal_flash_update_print_message(void)
{
	if (update_flash_data.status != FLASH_IMG_READY)
		return;

	pr_alert("FLASH: Flashing new firmware\n");
	pr_alert("FLASH: Image is %u bytes\n", image_data.size);
	pr_alert("FLASH: Performing flash and reboot/shutdown\n");
	pr_alert("FLASH: This will take several minutes. Do not power off!\n");

	/* Small delay to help getting the above message out */
	msleep(500);
}

/*
 * Show candidate image status
 */
static ssize_t update_show(struct kobject *kobj,
			   struct kobj_attribute *attr, char *buf)
{
	struct update_flash_t *const args_buf = &update_flash_data;
	return sprintf(buf, "%d\n", args_buf->status);
}

/*
 * Set update image flag
 *  1 - Flash new image
 *  0 - Cancel flash request
 */
static ssize_t update_store(struct kobject *kobj,
			    struct kobj_attribute *attr,
			    const char *buf, size_t count)
{
	struct update_flash_t *const args_buf = &update_flash_data;
	int rc = count;

	mutex_lock(&image_data_mutex);

	switch (buf[0]) {
	case '0':
		if (args_buf->status == FLASH_IMG_READY)
			opal_flash_update(FLASH_UPDATE_CANCEL);
		args_buf->status = FLASH_NO_OP;
		break;
	case '1':
		/* Image is loaded? */
		if (image_data.status == IMAGE_READY)
			args_buf->status =
				opal_flash_update(FLASH_UPDATE_INIT);
		else
			args_buf->status = FLASH_INVALID_IMG;
		break;
	default:
		rc = -EINVAL;
	}

	mutex_unlock(&image_data_mutex);
	return rc;
}

/*
 * Free image buffer
 */
static void free_image_buf(void)
{
	void *addr;
	int size;

	addr = image_data.data;
	size = PAGE_ALIGN(image_data.size);
	while (size > 0) {
		ClearPageReserved(vmalloc_to_page(addr));
		addr += PAGE_SIZE;
		size -= PAGE_SIZE;
	}
	vfree(image_data.data);
	image_data.data = NULL;
	image_data.status = IMAGE_INVALID;
}

/*
 * Allocate image buffer.
 */
static int alloc_image_buf(char *buffer, size_t count)
{
	void *addr;
	int size;

	if (count < sizeof(image_header)) {
		pr_warn("FLASH: Invalid candidate image\n");
		return -EINVAL;
	}

	memcpy(&image_header, (void *)buffer, sizeof(image_header));
	image_data.size = be32_to_cpu(image_header.size);
	pr_debug("FLASH: Candidate image size = %u\n", image_data.size);

	if (image_data.size > MAX_IMAGE_SIZE) {
		pr_warn("FLASH: Too large image\n");
		return -EINVAL;
	}
	if (image_data.size < VALIDATE_BUF_SIZE) {
		pr_warn("FLASH: Image is shorter than expected\n");
		return -EINVAL;
	}

	image_data.data = vzalloc(PAGE_ALIGN(image_data.size));
	if (!image_data.data) {
		pr_err("%s : Failed to allocate memory\n", __func__);
		return -ENOMEM;
	}

	/* Pin memory */
	addr = image_data.data;
	size = PAGE_ALIGN(image_data.size);
	while (size > 0) {
		SetPageReserved(vmalloc_to_page(addr));
		addr += PAGE_SIZE;
		size -= PAGE_SIZE;
	}

	image_data.status = IMAGE_LOADING;
	return 0;
}

/*
 * Copy candidate image
 *
 * Parse candidate image header to get total image size
 * and pre-allocate required memory.
 */
static ssize_t image_data_write(struct file *filp, struct kobject *kobj,
				struct bin_attribute *bin_attr,
				char *buffer, loff_t pos, size_t count)
{
	int rc;

	mutex_lock(&image_data_mutex);

	/* New image ? */
	if (pos == 0) {
		/* Free memory, if already allocated */
		if (image_data.data)
			free_image_buf();

		/* Cancel outstanding image update request */
		if (update_flash_data.status == FLASH_IMG_READY)
			opal_flash_update(FLASH_UPDATE_CANCEL);

		/* Allocate memory */
		rc = alloc_image_buf(buffer, count);
		if (rc)
			goto out;
	}

	if (image_data.status != IMAGE_LOADING) {
		rc = -ENOMEM;
		goto out;
	}

	if ((pos + count) > image_data.size) {
		rc = -EINVAL;
		goto out;
	}

	memcpy(image_data.data + pos, (void *)buffer, count);
	rc = count;

	/* Set image status */
	if ((pos + count) == image_data.size) {
		pr_debug("FLASH: Candidate image loaded....\n");
		image_data.status = IMAGE_READY;
	}

out:
	mutex_unlock(&image_data_mutex);
	return rc;
}

/*
 * sysfs interface :
 *  OPAL uses below sysfs files for code update.
 *  We create these files under /sys/firmware/opal.
 *
 *   image		: Interface to load candidate firmware image
 *   validate_flash	: Validate firmware image
 *   manage_flash	: Commit/Reject firmware image
 *   update_flash	: Flash new firmware image
 *
 */
static const struct bin_attribute image_data_attr = {
	.attr = {.name = "image", .mode = 0200},
	.size = MAX_IMAGE_SIZE,	/* Limit image size */
	.write = image_data_write,
};

static struct kobj_attribute validate_attribute =
	__ATTR(validate_flash, 0600, validate_show, validate_store);

static struct kobj_attribute manage_attribute =
	__ATTR(manage_flash, 0600, manage_show, manage_store);

static struct kobj_attribute update_attribute =
	__ATTR(update_flash, 0600, update_show, update_store);

static struct attribute *image_op_attrs[] = {
	&validate_attribute.attr,
	&manage_attribute.attr,
	&update_attribute.attr,
	NULL	/* need to NULL terminate the list of attributes */
};

static struct attribute_group image_op_attr_group = {
	.attrs = image_op_attrs,
};

void __init opal_flash_update_init(void)
{
	int ret;

	/* Allocate validate image buffer */
	validate_flash_data.buf = kzalloc(VALIDATE_BUF_SIZE, GFP_KERNEL);
	if (!validate_flash_data.buf) {
		pr_err("%s : Failed to allocate memory\n", __func__);
		return;
	}

	/* Make sure /sys/firmware/opal directory is created */
	if (!opal_kobj) {
		pr_warn("FLASH: opal kobject is not available\n");
		goto nokobj;
	}

	/* Create the sysfs files */
	ret = sysfs_create_group(opal_kobj, &image_op_attr_group);
	if (ret) {
		pr_warn("FLASH: Failed to create sysfs files\n");
		goto nokobj;
	}

	ret = sysfs_create_bin_file(opal_kobj, &image_data_attr);
	if (ret) {
		pr_warn("FLASH: Failed to create sysfs files\n");
		goto nosysfs_file;
	}

	/* Set default status */
	validate_flash_data.status = FLASH_NO_OP;
	manage_flash_data.status = FLASH_NO_OP;
	update_flash_data.status = FLASH_NO_OP;
	image_data.status = IMAGE_INVALID;
	return;

nosysfs_file:
	sysfs_remove_group(opal_kobj, &image_op_attr_group);

nokobj:
	kfree(validate_flash_data.buf);
	return;
}
