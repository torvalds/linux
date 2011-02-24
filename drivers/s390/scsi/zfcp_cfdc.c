/*
 * zfcp device driver
 *
 * Userspace interface for accessing the
 * Access Control Lists / Control File Data Channel;
 * handling of response code and states for ports and LUNs.
 *
 * Copyright IBM Corporation 2008, 2010
 */

#define KMSG_COMPONENT "zfcp"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/slab.h>
#include <linux/types.h>
#include <linux/miscdevice.h>
#include <asm/compat.h>
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
	struct ccw_device *cdev;
	struct zfcp_adapter *adapter;

	snprintf(busid, sizeof(busid), "0.0.%04x", devno);
	cdev = get_ccwdev_by_busid(&zfcp_ccw_driver, busid);
	if (!cdev)
		return NULL;

	adapter = zfcp_ccw_adapter_by_cdev(cdev);

	put_device(&cdev->dev);
	return adapter;
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
				unsigned long arg)
{
	struct zfcp_cfdc_data *data;
	struct zfcp_cfdc_data __user *data_user;
	struct zfcp_adapter *adapter;
	struct zfcp_fsf_req *req;
	struct zfcp_fsf_cfdc *fsf_cfdc;
	int retval;

	if (command != ZFCP_CFDC_IOC)
		return -ENOTTY;

	if (is_compat_task())
		data_user = compat_ptr(arg);
	else
		data_user = (void __user *)arg;

	if (!data_user)
		return -EINVAL;

	fsf_cfdc = kmalloc(sizeof(struct zfcp_fsf_cfdc), GFP_KERNEL);
	if (!fsf_cfdc)
		return -ENOMEM;

	data = memdup_user(data_user, sizeof(*data_user));
	if (IS_ERR(data)) {
		retval = PTR_ERR(data);
		goto no_mem_sense;
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
	zfcp_ccw_adapter_put(adapter);
 free_buffer:
	kfree(data);
 no_mem_sense:
	kfree(fsf_cfdc);
	return retval;
}

static const struct file_operations zfcp_cfdc_fops = {
	.open = nonseekable_open,
	.unlocked_ioctl = zfcp_cfdc_dev_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = zfcp_cfdc_dev_ioctl,
#endif
	.llseek = no_llseek,
};

struct miscdevice zfcp_cfdc_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "zfcp_cfdc",
	.fops = &zfcp_cfdc_fops,
};

/**
 * zfcp_cfdc_adapter_access_changed - Process change in adapter ACT
 * @adapter: Adapter where the Access Control Table (ACT) changed
 *
 * After a change in the adapter ACT, check if access to any
 * previously denied resources is now possible.
 */
void zfcp_cfdc_adapter_access_changed(struct zfcp_adapter *adapter)
{
	unsigned long flags;
	struct zfcp_port *port;
	struct scsi_device *sdev;
	struct zfcp_scsi_dev *zfcp_sdev;
	int status;

	if (adapter->connection_features & FSF_FEATURE_NPIV_MODE)
		return;

	read_lock_irqsave(&adapter->port_list_lock, flags);
	list_for_each_entry(port, &adapter->port_list, list) {
		status = atomic_read(&port->status);
		if ((status & ZFCP_STATUS_COMMON_ACCESS_DENIED) ||
		    (status & ZFCP_STATUS_COMMON_ACCESS_BOXED))
			zfcp_erp_port_reopen(port,
					     ZFCP_STATUS_COMMON_ERP_FAILED,
					     "cfaac_1");
	}
	read_unlock_irqrestore(&adapter->port_list_lock, flags);

	shost_for_each_device(sdev, port->adapter->scsi_host) {
		zfcp_sdev = sdev_to_zfcp(sdev);
		status = atomic_read(&zfcp_sdev->status);
		if ((status & ZFCP_STATUS_COMMON_ACCESS_DENIED) ||
		    (status & ZFCP_STATUS_COMMON_ACCESS_BOXED))
			zfcp_erp_lun_reopen(sdev,
					    ZFCP_STATUS_COMMON_ERP_FAILED,
					    "cfaac_2");
	}
}

static void zfcp_act_eval_err(struct zfcp_adapter *adapter, u32 table)
{
	u16 subtable = table >> 16;
	u16 rule = table & 0xffff;
	const char *act_type[] = { "unknown", "OS", "WWPN", "DID", "LUN" };

	if (subtable && subtable < ARRAY_SIZE(act_type))
		dev_warn(&adapter->ccw_device->dev,
			 "Access denied according to ACT rule type %s, "
			 "rule %d\n", act_type[subtable], rule);
}

/**
 * zfcp_cfdc_port_denied - Process "access denied" for port
 * @port: The port where the access has been denied
 * @qual: The FSF status qualifier for the access denied FSF status
 */
void zfcp_cfdc_port_denied(struct zfcp_port *port,
			   union fsf_status_qual *qual)
{
	dev_warn(&port->adapter->ccw_device->dev,
		 "Access denied to port 0x%016Lx\n",
		 (unsigned long long)port->wwpn);

	zfcp_act_eval_err(port->adapter, qual->halfword[0]);
	zfcp_act_eval_err(port->adapter, qual->halfword[1]);
	zfcp_erp_set_port_status(port,
				 ZFCP_STATUS_COMMON_ERP_FAILED |
				 ZFCP_STATUS_COMMON_ACCESS_DENIED);
}

/**
 * zfcp_cfdc_lun_denied - Process "access denied" for LUN
 * @sdev: The SCSI device / LUN where the access has been denied
 * @qual: The FSF status qualifier for the access denied FSF status
 */
void zfcp_cfdc_lun_denied(struct scsi_device *sdev,
			  union fsf_status_qual *qual)
{
	struct zfcp_scsi_dev *zfcp_sdev = sdev_to_zfcp(sdev);

	dev_warn(&zfcp_sdev->port->adapter->ccw_device->dev,
		 "Access denied to LUN 0x%016Lx on port 0x%016Lx\n",
		 zfcp_scsi_dev_lun(sdev),
		 (unsigned long long)zfcp_sdev->port->wwpn);
	zfcp_act_eval_err(zfcp_sdev->port->adapter, qual->halfword[0]);
	zfcp_act_eval_err(zfcp_sdev->port->adapter, qual->halfword[1]);
	zfcp_erp_set_lun_status(sdev,
				ZFCP_STATUS_COMMON_ERP_FAILED |
				ZFCP_STATUS_COMMON_ACCESS_DENIED);

	atomic_clear_mask(ZFCP_STATUS_LUN_SHARED, &zfcp_sdev->status);
	atomic_clear_mask(ZFCP_STATUS_LUN_READONLY, &zfcp_sdev->status);
}

/**
 * zfcp_cfdc_lun_shrng_vltn - Evaluate LUN sharing violation status
 * @sdev: The LUN / SCSI device where sharing violation occurred
 * @qual: The FSF status qualifier from the LUN sharing violation
 */
void zfcp_cfdc_lun_shrng_vltn(struct scsi_device *sdev,
			      union fsf_status_qual *qual)
{
	struct zfcp_scsi_dev *zfcp_sdev = sdev_to_zfcp(sdev);

	if (qual->word[0])
		dev_warn(&zfcp_sdev->port->adapter->ccw_device->dev,
			 "LUN 0x%Lx on port 0x%Lx is already in "
			 "use by CSS%d, MIF Image ID %x\n",
			 zfcp_scsi_dev_lun(sdev),
			 (unsigned long long)zfcp_sdev->port->wwpn,
			 qual->fsf_queue_designator.cssid,
			 qual->fsf_queue_designator.hla);
	else
		zfcp_act_eval_err(zfcp_sdev->port->adapter, qual->word[2]);

	zfcp_erp_set_lun_status(sdev,
				ZFCP_STATUS_COMMON_ERP_FAILED |
				ZFCP_STATUS_COMMON_ACCESS_DENIED);
	atomic_clear_mask(ZFCP_STATUS_LUN_SHARED, &zfcp_sdev->status);
	atomic_clear_mask(ZFCP_STATUS_LUN_READONLY, &zfcp_sdev->status);
}

/**
 * zfcp_cfdc_open_lun_eval - Eval access ctrl. status for successful "open lun"
 * @sdev: The SCSI device / LUN where to evaluate the status
 * @bottom: The qtcb bottom with the status from the "open lun"
 *
 * Returns: 0 if LUN is usable, -EACCES if the access control table
 *          reports an unsupported configuration.
 */
int zfcp_cfdc_open_lun_eval(struct scsi_device *sdev,
			    struct fsf_qtcb_bottom_support *bottom)
{
	int shared, rw;
	struct zfcp_scsi_dev *zfcp_sdev = sdev_to_zfcp(sdev);
	struct zfcp_adapter *adapter = zfcp_sdev->port->adapter;

	if ((adapter->connection_features & FSF_FEATURE_NPIV_MODE) ||
	    !(adapter->adapter_features & FSF_FEATURE_LUN_SHARING) ||
	    zfcp_ccw_priv_sch(adapter))
		return 0;

	shared = !(bottom->lun_access_info & FSF_UNIT_ACCESS_EXCLUSIVE);
	rw = (bottom->lun_access_info & FSF_UNIT_ACCESS_OUTBOUND_TRANSFER);

	if (shared)
		atomic_set_mask(ZFCP_STATUS_LUN_SHARED, &zfcp_sdev->status);

	if (!rw) {
		atomic_set_mask(ZFCP_STATUS_LUN_READONLY, &zfcp_sdev->status);
		dev_info(&adapter->ccw_device->dev, "SCSI device at LUN "
			 "0x%016Lx on port 0x%016Lx opened read-only\n",
			 zfcp_scsi_dev_lun(sdev),
			 (unsigned long long)zfcp_sdev->port->wwpn);
	}

	if (!shared && !rw) {
		dev_err(&adapter->ccw_device->dev, "Exclusive read-only access "
			"not supported (LUN 0x%016Lx, port 0x%016Lx)\n",
			zfcp_scsi_dev_lun(sdev),
			(unsigned long long)zfcp_sdev->port->wwpn);
		zfcp_erp_set_lun_status(sdev, ZFCP_STATUS_COMMON_ERP_FAILED);
		zfcp_erp_lun_shutdown(sdev, 0, "fsouh_6");
		return -EACCES;
	}

	if (shared && rw) {
		dev_err(&adapter->ccw_device->dev,
			"Shared read-write access not supported "
			"(LUN 0x%016Lx, port 0x%016Lx)\n",
			zfcp_scsi_dev_lun(sdev),
			(unsigned long long)zfcp_sdev->port->wwpn);
		zfcp_erp_set_lun_status(sdev, ZFCP_STATUS_COMMON_ERP_FAILED);
		zfcp_erp_lun_shutdown(sdev, 0, "fsosh_8");
		return -EACCES;
	}

	return 0;
}
