/*
 * zfcp device driver
 *
 * Userspace interface for accessing the
 * Access Control Lists / Control File Data Channel
 *
 * Copyright IBM Corporation 2008
 */

#define KMSG_COMPONENT "zfcp"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/types.h>
#include <linux/miscdevice.h>
#include <asm/ccwdev.h>
#include "zfcp_def.h"
#include "zfcp_ext.h"
#include "zfcp_fsf.h"

#define ZFCP_CFDC_CMND_DOWNLOAD_NORMAL		0x00010001
#define ZFCP_CFDC_CMND_DOWNLOAD_FORCE		0x00010101
#define ZFCP_CFDC_CMND_FULL_ACCESS		0x00000201
#define ZFCP_CFDC_CMND_RESTRICTED_ACCESS	0x00000401
#define ZFCP_CFDC_CMND_UPLOAD			0x00010002

#define ZFCP_CFDC_DOWNLOAD			0x00000001
#define ZFCP_CFDC_UPLOAD			0x00000002
#define ZFCP_CFDC_WITH_CONTROL_FILE		0x00010000

#define ZFCP_CFDC_IOC_MAGIC                     0xDD
#define ZFCP_CFDC_IOC \
	_IOWR(ZFCP_CFDC_IOC_MAGIC, 0, struct zfcp_cfdc_data)

/**
 * struct zfcp_cfdc_data - data for ioctl cfdc interface
 * @signature: request signature
 * @devno: FCP adapter device number
 * @command: command code
 * @fsf_status: returns status of FSF command to userspace
 * @fsf_status_qual: returned to userspace
 * @payloads: access conflicts list
 * @control_file: access control table
 */
struct zfcp_cfdc_data {
	u32 signature;
	u32 devno;
	u32 command;
	u32 fsf_status;
	u8  fsf_status_qual[FSF_STATUS_QUALIFIER_SIZE];
	u8  payloads[256];
	u8  control_file[0];
};

static int zfcp_cfdc_copy_from_user(struct scatterlist *sg,
				    void __user *user_buffer)
{
	unsigned int length;
	unsigned int size = ZFCP_CFDC_MAX_SIZE;

	while (size) {
		length = min((unsigned int)size, sg->length);
		if (copy_from_user(sg_virt(sg++), user_buffer, length))
			return -EFAULT;
		user_buffer += length;
		size -= length;
	}
	return 0;
}

static int zfcp_cfdc_copy_to_user(void __user  *user_buffer,
				  struct scatterlist *sg)
{
	unsigned int length;
	unsigned int size = ZFCP_CFDC_MAX_SIZE;

	while (size) {
		length = min((unsigned int) size, sg->length);
		if (copy_to_user(user_buffer, sg_virt(sg++), length))
			return -EFAULT;
		user_buffer += length;
		size -= length;
	}
	return 0;
}

static struct zfcp_adapter *zfcp_cfdc_get_adapter(u32 devno)
{
	char busid[9];
	snprintf(busid, sizeof(busid), "0.0.%04x", devno);
	return zfcp_get_adapter_by_busid(busid);
}

static int zfcp_cfdc_set_fsf(struct zfcp_fsf_cfdc *fsf_cfdc, int command)
{
	switch (command) {
	case ZFCP_CFDC_CMND_DOWNLOAD_NORMAL:
		fsf_cfdc->command = FSF_QTCB_DOWNLOAD_CONTROL_FILE;
		fsf_cfdc->option = FSF_CFDC_OPTION_NORMAL_MODE;
		break;
	case ZFCP_CFDC_CMND_DOWNLOAD_FORCE:
		fsf_cfdc->command = FSF_QTCB_DOWNLOAD_CONTROL_FILE;
		fsf_cfdc->option = FSF_CFDC_OPTION_FORCE;
		break;
	case ZFCP_CFDC_CMND_FULL_ACCESS:
		fsf_cfdc->command = FSF_QTCB_DOWNLOAD_CONTROL_FILE;
		fsf_cfdc->option = FSF_CFDC_OPTION_FULL_ACCESS;
		break;
	case ZFCP_CFDC_CMND_RESTRICTED_ACCESS:
		fsf_cfdc->command = FSF_QTCB_DOWNLOAD_CONTROL_FILE;
		fsf_cfdc->option = FSF_CFDC_OPTION_RESTRICTED_ACCESS;
		break;
	case ZFCP_CFDC_CMND_UPLOAD:
		fsf_cfdc->command = FSF_QTCB_UPLOAD_CONTROL_FILE;
		fsf_cfdc->option = 0;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int zfcp_cfdc_sg_setup(int command, struct scatterlist *sg,
			      u8 __user *control_file)
{
	int retval;
	retval = zfcp_sg_setup_table(sg, ZFCP_CFDC_PAGES);
	if (retval)
		return retval;

	sg[ZFCP_CFDC_PAGES - 1].length = ZFCP_CFDC_MAX_SIZE % PAGE_SIZE;

	if (command & ZFCP_CFDC_WITH_CONTROL_FILE &&
	    command & ZFCP_CFDC_DOWNLOAD) {
		retval = zfcp_cfdc_copy_from_user(sg, control_file);
		if (retval) {
			zfcp_sg_free_table(sg, ZFCP_CFDC_PAGES);
			return -EFAULT;
		}
	}

	return 0;
}

static void zfcp_cfdc_req_to_sense(struct zfcp_cfdc_data *data,
				   struct zfcp_fsf_req *req)
{
	data->fsf_status = req->qtcb->header.fsf_status;
	memcpy(&data->fsf_status_qual, &req->qtcb->header.fsf_status_qual,
	       sizeof(union fsf_status_qual));
	memcpy(&data->payloads, &req->qtcb->bottom.support.els,
	       sizeof(req->qtcb->bottom.support.els));
}

static long zfcp_cfdc_dev_ioctl(struct file *file, unsigned int command,
				unsigned long buffer)
{
	struct zfcp_cfdc_data *data;
	struct zfcp_cfdc_data __user *data_user;
	struct zfcp_adapter *adapter;
	struct zfcp_fsf_req *req;
	struct zfcp_fsf_cfdc *fsf_cfdc;
	int retval;

	if (command != ZFCP_CFDC_IOC)
		return -ENOTTY;

	data_user = (void __user *) buffer;
	if (!data_user)
		return -EINVAL;

	fsf_cfdc = kmalloc(sizeof(struct zfcp_fsf_cfdc), GFP_KERNEL);
	if (!fsf_cfdc)
		return -ENOMEM;

	data = kmalloc(sizeof(struct zfcp_cfdc_data), GFP_KERNEL);
	if (!data) {
		retval = -ENOMEM;
		goto no_mem_sense;
	}

	retval = copy_from_user(data, data_user, sizeof(*data));
	if (retval) {
		retval = -EFAULT;
		goto free_buffer;
	}

	if (data->signature != 0xCFDCACDF) {
		retval = -EINVAL;
		goto free_buffer;
	}

	retval = zfcp_cfdc_set_fsf(fsf_cfdc, data->command);

	adapter = zfcp_cfdc_get_adapter(data->devno);
	if (!adapter) {
		retval = -ENXIO;
		goto free_buffer;
	}

	retval = zfcp_cfdc_sg_setup(data->command, fsf_cfdc->sg,
				    data_user->control_file);
	if (retval)
		goto adapter_put;
	req = zfcp_fsf_control_file(adapter, fsf_cfdc);
	if (IS_ERR(req)) {
		retval = PTR_ERR(req);
		goto free_sg;
	}

	if (req->status & ZFCP_STATUS_FSFREQ_ERROR) {
		retval = -ENXIO;
		goto free_fsf;
	}

	zfcp_cfdc_req_to_sense(data, req);
	retval = copy_to_user(data_user, data, sizeof(*data_user));
	if (retval) {
		retval = -EFAULT;
		goto free_fsf;
	}

	if (data->command & ZFCP_CFDC_UPLOAD)
		retval = zfcp_cfdc_copy_to_user(&data_user->control_file,
						fsf_cfdc->sg);

 free_fsf:
	zfcp_fsf_req_free(req);
 free_sg:
	zfcp_sg_free_table(fsf_cfdc->sg, ZFCP_CFDC_PAGES);
 adapter_put:
	zfcp_adapter_put(adapter);
 free_buffer:
	kfree(data);
 no_mem_sense:
	kfree(fsf_cfdc);
	return retval;
}

static const struct file_operations zfcp_cfdc_fops = {
	.unlocked_ioctl = zfcp_cfdc_dev_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = zfcp_cfdc_dev_ioctl
#endif
};

struct miscdevice zfcp_cfdc_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "zfcp_cfdc",
	.fops = &zfcp_cfdc_fops,
};
