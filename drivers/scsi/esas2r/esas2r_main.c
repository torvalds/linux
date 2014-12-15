/*
 *  linux/drivers/scsi/esas2r/esas2r_main.c
 *      For use with ATTO ExpressSAS R6xx SAS/SATA RAID controllers
 *
 *  Copyright (c) 2001-2013 ATTO Technology, Inc.
 *  (mailto:linuxdrivers@attotech.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * NO WARRANTY
 * THE PROGRAM IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED INCLUDING, WITHOUT
 * LIMITATION, ANY WARRANTIES OR CONDITIONS OF TITLE, NON-INFRINGEMENT,
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE. Each Recipient is
 * solely responsible for determining the appropriateness of using and
 * distributing the Program and assumes all risks associated with its
 * exercise of rights under this Agreement, including but not limited to
 * the risks and costs of program errors, damage to or loss of data,
 * programs or equipment, and unavailability or interruption of operations.
 *
 * DISCLAIMER OF LIABILITY
 * NEITHER RECIPIENT NOR ANY CONTRIBUTORS SHALL HAVE ANY LIABILITY FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING WITHOUT LIMITATION LOST PROFITS), HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 * TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OR DISTRIBUTION OF THE PROGRAM OR THE EXERCISE OF ANY RIGHTS GRANTED
 * HEREUNDER, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGES
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301,
 * USA.
 */

#include "esas2r.h"

MODULE_DESCRIPTION(ESAS2R_DRVR_NAME ": " ESAS2R_LONGNAME " driver");
MODULE_AUTHOR("ATTO Technology, Inc.");
MODULE_LICENSE("GPL");
MODULE_VERSION(ESAS2R_VERSION_STR);

/* global definitions */

static int found_adapters;
struct esas2r_adapter *esas2r_adapters[MAX_ADAPTERS];

#define ESAS2R_VDA_EVENT_PORT1       54414
#define ESAS2R_VDA_EVENT_PORT2       54415
#define ESAS2R_VDA_EVENT_SOCK_COUNT  2

static struct esas2r_adapter *esas2r_adapter_from_kobj(struct kobject *kobj)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct Scsi_Host *host = class_to_shost(dev);

	return (struct esas2r_adapter *)host->hostdata;
}

static ssize_t read_fw(struct file *file, struct kobject *kobj,
		       struct bin_attribute *attr,
		       char *buf, loff_t off, size_t count)
{
	struct esas2r_adapter *a = esas2r_adapter_from_kobj(kobj);

	return esas2r_read_fw(a, buf, off, count);
}

static ssize_t write_fw(struct file *file, struct kobject *kobj,
			struct bin_attribute *attr,
			char *buf, loff_t off, size_t count)
{
	struct esas2r_adapter *a = esas2r_adapter_from_kobj(kobj);

	return esas2r_write_fw(a, buf, off, count);
}

static ssize_t read_fs(struct file *file, struct kobject *kobj,
		       struct bin_attribute *attr,
		       char *buf, loff_t off, size_t count)
{
	struct esas2r_adapter *a = esas2r_adapter_from_kobj(kobj);

	return esas2r_read_fs(a, buf, off, count);
}

static ssize_t write_fs(struct file *file, struct kobject *kobj,
			struct bin_attribute *attr,
			char *buf, loff_t off, size_t count)
{
	struct esas2r_adapter *a = esas2r_adapter_from_kobj(kobj);
	int length = min(sizeof(struct esas2r_ioctl_fs), count);
	int result = 0;

	result = esas2r_write_fs(a, buf, off, count);

	if (result < 0)
		result = 0;

	return length;
}

static ssize_t read_vda(struct file *file, struct kobject *kobj,
			struct bin_attribute *attr,
			char *buf, loff_t off, size_t count)
{
	struct esas2r_adapter *a = esas2r_adapter_from_kobj(kobj);

	return esas2r_read_vda(a, buf, off, count);
}

static ssize_t write_vda(struct file *file, struct kobject *kobj,
			 struct bin_attribute *attr,
			 char *buf, loff_t off, size_t count)
{
	struct esas2r_adapter *a = esas2r_adapter_from_kobj(kobj);

	return esas2r_write_vda(a, buf, off, count);
}

static ssize_t read_live_nvram(struct file *file, struct kobject *kobj,
			       struct bin_attribute *attr,
			       char *buf, loff_t off, size_t count)
{
	struct esas2r_adapter *a = esas2r_adapter_from_kobj(kobj);
	int length = min_t(size_t, sizeof(struct esas2r_sas_nvram), PAGE_SIZE);

	memcpy(buf, a->nvram, length);
	return length;
}

static ssize_t write_live_nvram(struct file *file, struct kobject *kobj,
				struct bin_attribute *attr,
				char *buf, loff_t off, size_t count)
{
	struct esas2r_adapter *a = esas2r_adapter_from_kobj(kobj);
	struct esas2r_request *rq;
	int result = -EFAULT;

	rq = esas2r_alloc_request(a);
	if (rq == NULL)
		return -ENOMEM;

	if (esas2r_write_params(a, rq, (struct esas2r_sas_nvram *)buf))
		result = count;

	esas2r_free_request(a, rq);

	return result;
}

static ssize_t read_default_nvram(struct file *file, struct kobject *kobj,
				  struct bin_attribute *attr,
				  char *buf, loff_t off, size_t count)
{
	struct esas2r_adapter *a = esas2r_adapter_from_kobj(kobj);

	esas2r_nvram_get_defaults(a, (struct esas2r_sas_nvram *)buf);

	return sizeof(struct esas2r_sas_nvram);
}

static ssize_t read_hw(struct file *file, struct kobject *kobj,
		       struct bin_attribute *attr,
		       char *buf, loff_t off, size_t count)
{
	struct esas2r_adapter *a = esas2r_adapter_from_kobj(kobj);
	int length = min_t(size_t, sizeof(struct atto_ioctl), PAGE_SIZE);

	if (!a->local_atto_ioctl)
		return -ENOMEM;

	if (handle_hba_ioctl(a, a->local_atto_ioctl) != IOCTL_SUCCESS)
		return -ENOMEM;

	memcpy(buf, a->local_atto_ioctl, length);

	return length;
}

static ssize_t write_hw(struct file *file, struct kobject *kobj,
			struct bin_attribute *attr,
			char *buf, loff_t off, size_t count)
{
	struct esas2r_adapter *a = esas2r_adapter_from_kobj(kobj);
	int length = min(sizeof(struct atto_ioctl), count);

	if (!a->local_atto_ioctl) {
		a->local_atto_ioctl = kzalloc(sizeof(struct atto_ioctl),
					      GFP_KERNEL);
		if (a->local_atto_ioctl == NULL) {
			esas2r_log(ESAS2R_LOG_WARN,
				   "write_hw kzalloc failed for %d bytes",
				   sizeof(struct atto_ioctl));
			return -ENOMEM;
		}
	}

	memset(a->local_atto_ioctl, 0, sizeof(struct atto_ioctl));
	memcpy(a->local_atto_ioctl, buf, length);

	return length;
}

#define ESAS2R_RW_BIN_ATTR(_name) \
	struct bin_attribute bin_attr_ ## _name = { \
		.attr	= \
		{ .name = __stringify(_name), .mode  = S_IRUSR | S_IWUSR }, \
		.size	= 0, \
		.read	= read_ ## _name, \
		.write	= write_ ## _name }

ESAS2R_RW_BIN_ATTR(fw);
ESAS2R_RW_BIN_ATTR(fs);
ESAS2R_RW_BIN_ATTR(vda);
ESAS2R_RW_BIN_ATTR(hw);
ESAS2R_RW_BIN_ATTR(live_nvram);

struct bin_attribute bin_attr_default_nvram = {
	.attr	= { .name = "default_nvram", .mode = S_IRUGO },
	.size	= 0,
	.read	= read_default_nvram,
	.write	= NULL
};

static struct scsi_host_template driver_template = {
	.module				= THIS_MODULE,
	.show_info			= esas2r_show_info,
	.name				= ESAS2R_LONGNAME,
	.release			= esas2r_release,
	.info				= esas2r_info,
	.ioctl				= esas2r_ioctl,
	.queuecommand			= esas2r_queuecommand,
	.eh_abort_handler		= esas2r_eh_abort,
	.eh_device_reset_handler	= esas2r_device_reset,
	.eh_bus_reset_handler		= esas2r_bus_reset,
	.eh_host_reset_handler		= esas2r_host_reset,
	.eh_target_reset_handler	= esas2r_target_reset,
	.can_queue			= 128,
	.this_id			= -1,
	.sg_tablesize			= SCSI_MAX_SG_SEGMENTS,
	.cmd_per_lun			=
		ESAS2R_DEFAULT_CMD_PER_LUN,
	.present			= 0,
	.unchecked_isa_dma		= 0,
	.use_clustering			= ENABLE_CLUSTERING,
	.emulated			= 0,
	.proc_name			= ESAS2R_DRVR_NAME,
	.change_queue_depth		= scsi_change_queue_depth,
	.change_queue_type		= scsi_change_queue_type,
	.max_sectors			= 0xFFFF,
	.use_blk_tags			= 1,
};

int sgl_page_size = 512;
module_param(sgl_page_size, int, 0);
MODULE_PARM_DESC(sgl_page_size,
		 "Scatter/gather list (SGL) page size in number of S/G "
		 "entries.  If your application is doing a lot of very large "
		 "transfers, you may want to increase the SGL page size.  "
		 "Default 512.");

int num_sg_lists = 1024;
module_param(num_sg_lists, int, 0);
MODULE_PARM_DESC(num_sg_lists,
		 "Number of scatter/gather lists.  Default 1024.");

int sg_tablesize = SCSI_MAX_SG_SEGMENTS;
module_param(sg_tablesize, int, 0);
MODULE_PARM_DESC(sg_tablesize,
		 "Maximum number of entries in a scatter/gather table.");

int num_requests = 256;
module_param(num_requests, int, 0);
MODULE_PARM_DESC(num_requests,
		 "Number of requests.  Default 256.");

int num_ae_requests = 4;
module_param(num_ae_requests, int, 0);
MODULE_PARM_DESC(num_ae_requests,
		 "Number of VDA asynchromous event requests.  Default 4.");

int cmd_per_lun = ESAS2R_DEFAULT_CMD_PER_LUN;
module_param(cmd_per_lun, int, 0);
MODULE_PARM_DESC(cmd_per_lun,
		 "Maximum number of commands per LUN.  Default "
		 DEFINED_NUM_TO_STR(ESAS2R_DEFAULT_CMD_PER_LUN) ".");

int can_queue = 128;
module_param(can_queue, int, 0);
MODULE_PARM_DESC(can_queue,
		 "Maximum number of commands per adapter.  Default 128.");

int esas2r_max_sectors = 0xFFFF;
module_param(esas2r_max_sectors, int, 0);
MODULE_PARM_DESC(esas2r_max_sectors,
		 "Maximum number of disk sectors in a single data transfer.  "
		 "Default 65535 (largest possible setting).");

int interrupt_mode = 1;
module_param(interrupt_mode, int, 0);
MODULE_PARM_DESC(interrupt_mode,
		 "Defines the interrupt mode to use.  0 for legacy"
		 ", 1 for MSI.  Default is MSI (1).");

static struct pci_device_id
	esas2r_pci_table[] = {
	{ ATTO_VENDOR_ID, 0x0049,	  ATTO_VENDOR_ID, 0x0049,
	  0,
	  0, 0 },
	{ ATTO_VENDOR_ID, 0x0049,	  ATTO_VENDOR_ID, 0x004A,
	  0,
	  0, 0 },
	{ ATTO_VENDOR_ID, 0x0049,	  ATTO_VENDOR_ID, 0x004B,
	  0,
	  0, 0 },
	{ ATTO_VENDOR_ID, 0x0049,	  ATTO_VENDOR_ID, 0x004C,
	  0,
	  0, 0 },
	{ ATTO_VENDOR_ID, 0x0049,	  ATTO_VENDOR_ID, 0x004D,
	  0,
	  0, 0 },
	{ ATTO_VENDOR_ID, 0x0049,	  ATTO_VENDOR_ID, 0x004E,
	  0,
	  0, 0 },
	{ 0,		  0,		  0,		  0,
	  0,
	  0, 0 }
};

MODULE_DEVICE_TABLE(pci, esas2r_pci_table);

static int
esas2r_probe(struct pci_dev *pcid, const struct pci_device_id *id);

static void
esas2r_remove(struct pci_dev *pcid);

static struct pci_driver
	esas2r_pci_driver = {
	.name		= ESAS2R_DRVR_NAME,
	.id_table	= esas2r_pci_table,
	.probe		= esas2r_probe,
	.remove		= esas2r_remove,
	.suspend	= esas2r_suspend,
	.resume		= esas2r_resume,
};

static int esas2r_probe(struct pci_dev *pcid,
			const struct pci_device_id *id)
{
	struct Scsi_Host *host = NULL;
	struct esas2r_adapter *a;
	int err;

	size_t host_alloc_size = sizeof(struct esas2r_adapter)
				 + ((num_requests) +
				    1) * sizeof(struct esas2r_request);

	esas2r_log_dev(ESAS2R_LOG_DEBG, &(pcid->dev),
		       "esas2r_probe() 0x%02x 0x%02x 0x%02x 0x%02x",
		       pcid->vendor,
		       pcid->device,
		       pcid->subsystem_vendor,
		       pcid->subsystem_device);

	esas2r_log_dev(ESAS2R_LOG_INFO, &(pcid->dev),
		       "before pci_enable_device() "
		       "enable_cnt: %d",
		       pcid->enable_cnt.counter);

	err = pci_enable_device(pcid);
	if (err != 0) {
		esas2r_log_dev(ESAS2R_LOG_CRIT, &(pcid->dev),
			       "pci_enable_device() FAIL (%d)",
			       err);
		return -ENODEV;
	}

	esas2r_log_dev(ESAS2R_LOG_INFO, &(pcid->dev),
		       "pci_enable_device() OK");
	esas2r_log_dev(ESAS2R_LOG_INFO, &(pcid->dev),
		       "after pci_enable_device() enable_cnt: %d",
		       pcid->enable_cnt.counter);

	host = scsi_host_alloc(&driver_template, host_alloc_size);
	if (host == NULL) {
		esas2r_log(ESAS2R_LOG_CRIT, "scsi_host_alloc() FAIL");
		return -ENODEV;
	}

	memset(host->hostdata, 0, host_alloc_size);

	a = (struct esas2r_adapter *)host->hostdata;

	esas2r_log(ESAS2R_LOG_INFO, "scsi_host_alloc() OK host: %p", host);

	/* override max LUN and max target id */

	host->max_id = ESAS2R_MAX_ID + 1;
	host->max_lun = 255;

	/* we can handle 16-byte CDbs */

	host->max_cmd_len = 16;

	host->can_queue = can_queue;
	host->cmd_per_lun = cmd_per_lun;
	host->this_id = host->max_id + 1;
	host->max_channel = 0;
	host->unique_id = found_adapters;
	host->sg_tablesize = sg_tablesize;
	host->max_sectors = esas2r_max_sectors;

	/* set to bus master for BIOses that don't do it for us */

	esas2r_log(ESAS2R_LOG_INFO, "pci_set_master() called");

	pci_set_master(pcid);

	if (!esas2r_init_adapter(host, pcid, found_adapters)) {
		esas2r_log(ESAS2R_LOG_CRIT,
			   "unable to initialize device at PCI bus %x:%x",
			   pcid->bus->number,
			   pcid->devfn);

		esas2r_log_dev(ESAS2R_LOG_INFO, &(host->shost_gendev),
			       "scsi_host_put() called");

		scsi_host_put(host);

		return 0;

	}

	esas2r_log(ESAS2R_LOG_INFO, "pci_set_drvdata(%p, %p) called", pcid,
		   host->hostdata);

	pci_set_drvdata(pcid, host);

	esas2r_log(ESAS2R_LOG_INFO, "scsi_add_host() called");

	err = scsi_add_host(host, &pcid->dev);

	if (err) {
		esas2r_log(ESAS2R_LOG_CRIT, "scsi_add_host returned %d", err);
		esas2r_log_dev(ESAS2R_LOG_CRIT, &(host->shost_gendev),
			       "scsi_add_host() FAIL");

		esas2r_log_dev(ESAS2R_LOG_INFO, &(host->shost_gendev),
			       "scsi_host_put() called");

		scsi_host_put(host);

		esas2r_log_dev(ESAS2R_LOG_INFO, &(host->shost_gendev),
			       "pci_set_drvdata(%p, NULL) called",
			       pcid);

		pci_set_drvdata(pcid, NULL);

		return -ENODEV;
	}


	esas2r_fw_event_on(a);

	esas2r_log_dev(ESAS2R_LOG_INFO, &(host->shost_gendev),
		       "scsi_scan_host() called");

	scsi_scan_host(host);

	/* Add sysfs binary files */
	if (sysfs_create_bin_file(&host->shost_dev.kobj, &bin_attr_fw))
		esas2r_log_dev(ESAS2R_LOG_WARN, &(host->shost_gendev),
			       "Failed to create sysfs binary file: fw");
	else
		a->sysfs_fw_created = 1;

	if (sysfs_create_bin_file(&host->shost_dev.kobj, &bin_attr_fs))
		esas2r_log_dev(ESAS2R_LOG_WARN, &(host->shost_gendev),
			       "Failed to create sysfs binary file: fs");
	else
		a->sysfs_fs_created = 1;

	if (sysfs_create_bin_file(&host->shost_dev.kobj, &bin_attr_vda))
		esas2r_log_dev(ESAS2R_LOG_WARN, &(host->shost_gendev),
			       "Failed to create sysfs binary file: vda");
	else
		a->sysfs_vda_created = 1;

	if (sysfs_create_bin_file(&host->shost_dev.kobj, &bin_attr_hw))
		esas2r_log_dev(ESAS2R_LOG_WARN, &(host->shost_gendev),
			       "Failed to create sysfs binary file: hw");
	else
		a->sysfs_hw_created = 1;

	if (sysfs_create_bin_file(&host->shost_dev.kobj, &bin_attr_live_nvram))
		esas2r_log_dev(ESAS2R_LOG_WARN, &(host->shost_gendev),
			       "Failed to create sysfs binary file: live_nvram");
	else
		a->sysfs_live_nvram_created = 1;

	if (sysfs_create_bin_file(&host->shost_dev.kobj,
				  &bin_attr_default_nvram))
		esas2r_log_dev(ESAS2R_LOG_WARN, &(host->shost_gendev),
			       "Failed to create sysfs binary file: default_nvram");
	else
		a->sysfs_default_nvram_created = 1;

	found_adapters++;

	return 0;
}

static void esas2r_remove(struct pci_dev *pdev)
{
	struct Scsi_Host *host;
	int index;

	if (pdev == NULL) {
		esas2r_log(ESAS2R_LOG_WARN, "esas2r_remove pdev==NULL");
		return;
	}

	host = pci_get_drvdata(pdev);

	if (host == NULL) {
		/*
		 * this can happen if pci_set_drvdata was already called
		 * to clear the host pointer.  if this is the case, we
		 * are okay; this channel has already been cleaned up.
		 */

		return;
	}

	esas2r_log_dev(ESAS2R_LOG_INFO, &(pdev->dev),
		       "esas2r_remove(%p) called; "
		       "host:%p", pdev,
		       host);

	index = esas2r_cleanup(host);

	if (index < 0)
		esas2r_log_dev(ESAS2R_LOG_WARN, &(pdev->dev),
			       "unknown host in %s",
			       __func__);

	found_adapters--;

	/* if this was the last adapter, clean up the rest of the driver */

	if (found_adapters == 0)
		esas2r_cleanup(NULL);
}

static int __init esas2r_init(void)
{
	int i;

	esas2r_log(ESAS2R_LOG_INFO, "%s called", __func__);

	/* verify valid parameters */

	if (can_queue < 1) {
		esas2r_log(ESAS2R_LOG_WARN,
			   "warning: can_queue must be at least 1, value "
			   "forced.");
		can_queue = 1;
	} else if (can_queue > 2048) {
		esas2r_log(ESAS2R_LOG_WARN,
			   "warning: can_queue must be no larger than 2048, "
			   "value forced.");
		can_queue = 2048;
	}

	if (cmd_per_lun < 1) {
		esas2r_log(ESAS2R_LOG_WARN,
			   "warning: cmd_per_lun must be at least 1, value "
			   "forced.");
		cmd_per_lun = 1;
	} else if (cmd_per_lun > 2048) {
		esas2r_log(ESAS2R_LOG_WARN,
			   "warning: cmd_per_lun must be no larger than "
			   "2048, value forced.");
		cmd_per_lun = 2048;
	}

	if (sg_tablesize < 32) {
		esas2r_log(ESAS2R_LOG_WARN,
			   "warning: sg_tablesize must be at least 32, "
			   "value forced.");
		sg_tablesize = 32;
	}

	if (esas2r_max_sectors < 1) {
		esas2r_log(ESAS2R_LOG_WARN,
			   "warning: esas2r_max_sectors must be at least "
			   "1, value forced.");
		esas2r_max_sectors = 1;
	} else if (esas2r_max_sectors > 0xffff) {
		esas2r_log(ESAS2R_LOG_WARN,
			   "warning: esas2r_max_sectors must be no larger "
			   "than 0xffff, value forced.");
		esas2r_max_sectors = 0xffff;
	}

	sgl_page_size &= ~(ESAS2R_SGL_ALIGN - 1);

	if (sgl_page_size < SGL_PG_SZ_MIN)
		sgl_page_size = SGL_PG_SZ_MIN;
	else if (sgl_page_size > SGL_PG_SZ_MAX)
		sgl_page_size = SGL_PG_SZ_MAX;

	if (num_sg_lists < NUM_SGL_MIN)
		num_sg_lists = NUM_SGL_MIN;
	else if (num_sg_lists > NUM_SGL_MAX)
		num_sg_lists = NUM_SGL_MAX;

	if (num_requests < NUM_REQ_MIN)
		num_requests = NUM_REQ_MIN;
	else if (num_requests > NUM_REQ_MAX)
		num_requests = NUM_REQ_MAX;

	if (num_ae_requests < NUM_AE_MIN)
		num_ae_requests = NUM_AE_MIN;
	else if (num_ae_requests > NUM_AE_MAX)
		num_ae_requests = NUM_AE_MAX;

	/* set up other globals */

	for (i = 0; i < MAX_ADAPTERS; i++)
		esas2r_adapters[i] = NULL;

	/* initialize */

	driver_template.module = THIS_MODULE;

	if (pci_register_driver(&esas2r_pci_driver) != 0)
		esas2r_log(ESAS2R_LOG_CRIT, "pci_register_driver FAILED");
	else
		esas2r_log(ESAS2R_LOG_INFO, "pci_register_driver() OK");

	if (!found_adapters) {
		pci_unregister_driver(&esas2r_pci_driver);
		esas2r_cleanup(NULL);

		esas2r_log(ESAS2R_LOG_CRIT,
			   "driver will not be loaded because no ATTO "
			   "%s devices were found",
			   ESAS2R_DRVR_NAME);
		return -1;
	} else {
		esas2r_log(ESAS2R_LOG_INFO, "found %d adapters",
			   found_adapters);
	}

	return 0;
}

/* Handle ioctl calls to "/proc/scsi/esas2r/ATTOnode" */
static const struct file_operations esas2r_proc_fops = {
	.compat_ioctl	= esas2r_proc_ioctl,
	.unlocked_ioctl = esas2r_proc_ioctl,
};

static struct Scsi_Host *esas2r_proc_host;
static int esas2r_proc_major;

long esas2r_proc_ioctl(struct file *fp, unsigned int cmd, unsigned long arg)
{
	return esas2r_ioctl_handler(esas2r_proc_host->hostdata,
				    (int)cmd, (void __user *)arg);
}

static void __exit esas2r_exit(void)
{
	esas2r_log(ESAS2R_LOG_INFO, "%s called", __func__);

	if (esas2r_proc_major > 0) {
		esas2r_log(ESAS2R_LOG_INFO, "unregister proc");

		remove_proc_entry(ATTONODE_NAME,
				  esas2r_proc_host->hostt->proc_dir);
		unregister_chrdev(esas2r_proc_major, ESAS2R_DRVR_NAME);

		esas2r_proc_major = 0;
	}

	esas2r_log(ESAS2R_LOG_INFO, "pci_unregister_driver() called");

	pci_unregister_driver(&esas2r_pci_driver);
}

int esas2r_show_info(struct seq_file *m, struct Scsi_Host *sh)
{
	struct esas2r_adapter *a = (struct esas2r_adapter *)sh->hostdata;

	struct esas2r_target *t;
	int dev_count = 0;

	esas2r_log(ESAS2R_LOG_DEBG, "esas2r_show_info (%p,%d)", m, sh->host_no);

	seq_printf(m, ESAS2R_LONGNAME "\n"
		   "Driver version: "ESAS2R_VERSION_STR "\n"
		   "Flash version: %s\n"
		   "Firmware version: %s\n"
		   "Copyright "ESAS2R_COPYRIGHT_YEARS "\n"
		   "http://www.attotech.com\n"
		   "\n",
		   a->flash_rev,
		   a->fw_rev[0] ? a->fw_rev : "(none)");


	seq_printf(m, "Adapter information:\n"
		   "--------------------\n"
		   "Model: %s\n"
		   "SAS address: %02X%02X%02X%02X:%02X%02X%02X%02X\n",
		   esas2r_get_model_name(a),
		   a->nvram->sas_addr[0],
		   a->nvram->sas_addr[1],
		   a->nvram->sas_addr[2],
		   a->nvram->sas_addr[3],
		   a->nvram->sas_addr[4],
		   a->nvram->sas_addr[5],
		   a->nvram->sas_addr[6],
		   a->nvram->sas_addr[7]);

	seq_puts(m, "\n"
		   "Discovered devices:\n"
		   "\n"
		   "   #  Target ID\n"
		   "---------------\n");

	for (t = a->targetdb; t < a->targetdb_end; t++)
		if (t->buffered_target_state == TS_PRESENT) {
			seq_printf(m, " %3d   %3d\n",
				   ++dev_count,
				   (u16)(uintptr_t)(t - a->targetdb));
		}

	if (dev_count == 0)
		seq_puts(m, "none\n");

	seq_puts(m, "\n");
	return 0;

}

int esas2r_release(struct Scsi_Host *sh)
{
	esas2r_log_dev(ESAS2R_LOG_INFO, &(sh->shost_gendev),
		       "esas2r_release() called");

	esas2r_cleanup(sh);
	if (sh->irq)
		free_irq(sh->irq, NULL);
	scsi_unregister(sh);
	return 0;
}

const char *esas2r_info(struct Scsi_Host *sh)
{
	struct esas2r_adapter *a = (struct esas2r_adapter *)sh->hostdata;
	static char esas2r_info_str[512];

	esas2r_log_dev(ESAS2R_LOG_INFO, &(sh->shost_gendev),
		       "esas2r_info() called");

	/*
	 * if we haven't done so already, register as a char driver
	 * and stick a node under "/proc/scsi/esas2r/ATTOnode"
	 */

	if (esas2r_proc_major <= 0) {
		esas2r_proc_host = sh;

		esas2r_proc_major = register_chrdev(0, ESAS2R_DRVR_NAME,
						    &esas2r_proc_fops);

		esas2r_log_dev(ESAS2R_LOG_DEBG, &(sh->shost_gendev),
			       "register_chrdev (major %d)",
			       esas2r_proc_major);

		if (esas2r_proc_major > 0) {
			struct proc_dir_entry *pde;

			pde = proc_create(ATTONODE_NAME, 0,
					  sh->hostt->proc_dir,
					  &esas2r_proc_fops);

			if (!pde) {
				esas2r_log_dev(ESAS2R_LOG_WARN,
					       &(sh->shost_gendev),
					       "failed to create_proc_entry");
				esas2r_proc_major = -1;
			}
		}
	}

	sprintf(esas2r_info_str,
		ESAS2R_LONGNAME " (bus 0x%02X, device 0x%02X, IRQ 0x%02X)"
		" driver version: "ESAS2R_VERSION_STR "  firmware version: "
		"%s\n",
		a->pcid->bus->number, a->pcid->devfn, a->pcid->irq,
		a->fw_rev[0] ? a->fw_rev : "(none)");

	return esas2r_info_str;
}

/* Callback for building a request scatter/gather list */
static u32 get_physaddr_from_sgc(struct esas2r_sg_context *sgc, u64 *addr)
{
	u32 len;

	if (likely(sgc->cur_offset == sgc->exp_offset)) {
		/*
		 * the normal case: caller used all bytes from previous call, so
		 * expected offset is the same as the current offset.
		 */

		if (sgc->sgel_count < sgc->num_sgel) {
			/* retrieve next segment, except for first time */
			if (sgc->exp_offset > (u8 *)0) {
				/* advance current segment */
				sgc->cur_sgel = sg_next(sgc->cur_sgel);
				++(sgc->sgel_count);
			}


			len = sg_dma_len(sgc->cur_sgel);
			(*addr) = sg_dma_address(sgc->cur_sgel);

			/* save the total # bytes returned to caller so far */
			sgc->exp_offset += len;

		} else {
			len = 0;
		}
	} else if (sgc->cur_offset < sgc->exp_offset) {
		/*
		 * caller did not use all bytes from previous call. need to
		 * compute the address based on current segment.
		 */

		len = sg_dma_len(sgc->cur_sgel);
		(*addr) = sg_dma_address(sgc->cur_sgel);

		sgc->exp_offset -= len;

		/* calculate PA based on prev segment address and offsets */
		*addr = *addr +
			(sgc->cur_offset - sgc->exp_offset);

		sgc->exp_offset += len;

		/* re-calculate length based on offset */
		len = lower_32_bits(
			sgc->exp_offset - sgc->cur_offset);
	} else {   /* if ( sgc->cur_offset > sgc->exp_offset ) */
		   /*
		    * we don't expect the caller to skip ahead.
		    * cur_offset will never exceed the len we return
		    */
		len = 0;
	}

	return len;
}

int esas2r_queuecommand(struct Scsi_Host *host, struct scsi_cmnd *cmd)
{
	struct esas2r_adapter *a =
		(struct esas2r_adapter *)cmd->device->host->hostdata;
	struct esas2r_request *rq;
	struct esas2r_sg_context sgc;
	unsigned bufflen;

	/* Assume success, if it fails we will fix the result later. */
	cmd->result = DID_OK << 16;

	if (unlikely(test_bit(AF_DEGRADED_MODE, &a->flags))) {
		cmd->result = DID_NO_CONNECT << 16;
		cmd->scsi_done(cmd);
		return 0;
	}

	rq = esas2r_alloc_request(a);
	if (unlikely(rq == NULL)) {
		esas2r_debug("esas2r_alloc_request failed");
		return SCSI_MLQUEUE_HOST_BUSY;
	}

	rq->cmd = cmd;
	bufflen = scsi_bufflen(cmd);

	if (likely(bufflen != 0)) {
		if (cmd->sc_data_direction == DMA_TO_DEVICE)
			rq->vrq->scsi.flags |= cpu_to_le32(FCP_CMND_WRD);
		else if (cmd->sc_data_direction == DMA_FROM_DEVICE)
			rq->vrq->scsi.flags |= cpu_to_le32(FCP_CMND_RDD);
	}

	memcpy(rq->vrq->scsi.cdb, cmd->cmnd, cmd->cmd_len);
	rq->vrq->scsi.length = cpu_to_le32(bufflen);
	rq->target_id = cmd->device->id;
	rq->vrq->scsi.flags |= cpu_to_le32(cmd->device->lun);
	rq->sense_buf = cmd->sense_buffer;
	rq->sense_len = SCSI_SENSE_BUFFERSIZE;

	esas2r_sgc_init(&sgc, a, rq, NULL);

	sgc.length = bufflen;
	sgc.cur_offset = NULL;

	sgc.cur_sgel = scsi_sglist(cmd);
	sgc.exp_offset = NULL;
	sgc.num_sgel = scsi_dma_map(cmd);
	sgc.sgel_count = 0;

	if (unlikely(sgc.num_sgel < 0)) {
		esas2r_free_request(a, rq);
		return SCSI_MLQUEUE_HOST_BUSY;
	}

	sgc.get_phys_addr = (PGETPHYSADDR)get_physaddr_from_sgc;

	if (unlikely(!esas2r_build_sg_list(a, rq, &sgc))) {
		scsi_dma_unmap(cmd);
		esas2r_free_request(a, rq);
		return SCSI_MLQUEUE_HOST_BUSY;
	}

	esas2r_debug("start request %p to %d:%d\n", rq, (int)cmd->device->id,
		     (int)cmd->device->lun);

	esas2r_start_request(a, rq);

	return 0;
}

static void complete_task_management_request(struct esas2r_adapter *a,
					     struct esas2r_request *rq)
{
	(*rq->task_management_status_ptr) = rq->req_stat;
	esas2r_free_request(a, rq);
}

/**
 * Searches the specified queue for the specified queue for the command
 * to abort.
 *
 * @param [in] a
 * @param [in] abort_request
 * @param [in] cmd
 * t
 * @return 0 on failure, 1 if command was not found, 2 if command was found
 */
static int esas2r_check_active_queue(struct esas2r_adapter *a,
				     struct esas2r_request **abort_request,
				     struct scsi_cmnd *cmd,
				     struct list_head *queue)
{
	bool found = false;
	struct esas2r_request *ar = *abort_request;
	struct esas2r_request *rq;
	struct list_head *element, *next;

	list_for_each_safe(element, next, queue) {

		rq = list_entry(element, struct esas2r_request, req_list);

		if (rq->cmd == cmd) {

			/* Found the request.  See what to do with it. */
			if (queue == &a->active_list) {
				/*
				 * We are searching the active queue, which
				 * means that we need to send an abort request
				 * to the firmware.
				 */
				ar = esas2r_alloc_request(a);
				if (ar == NULL) {
					esas2r_log_dev(ESAS2R_LOG_WARN,
						       &(a->host->shost_gendev),
						       "unable to allocate an abort request for cmd %p",
						       cmd);
					return 0; /* Failure */
				}

				/*
				 * Task management request must be formatted
				 * with a lock held.
				 */
				ar->sense_len = 0;
				ar->vrq->scsi.length = 0;
				ar->target_id = rq->target_id;
				ar->vrq->scsi.flags |= cpu_to_le32(
					(u8)le32_to_cpu(rq->vrq->scsi.flags));

				memset(ar->vrq->scsi.cdb, 0,
				       sizeof(ar->vrq->scsi.cdb));

				ar->vrq->scsi.flags |= cpu_to_le32(
					FCP_CMND_TRM);
				ar->vrq->scsi.u.abort_handle =
					rq->vrq->scsi.handle;
			} else {
				/*
				 * The request is pending but not active on
				 * the firmware.  Just free it now and we'll
				 * report the successful abort below.
				 */
				list_del_init(&rq->req_list);
				esas2r_free_request(a, rq);
			}

			found = true;
			break;
		}

	}

	if (!found)
		return 1;       /* Not found */

	return 2;               /* found */


}

int esas2r_eh_abort(struct scsi_cmnd *cmd)
{
	struct esas2r_adapter *a =
		(struct esas2r_adapter *)cmd->device->host->hostdata;
	struct esas2r_request *abort_request = NULL;
	unsigned long flags;
	struct list_head *queue;
	int result;

	esas2r_log(ESAS2R_LOG_INFO, "eh_abort (%p)", cmd);

	if (test_bit(AF_DEGRADED_MODE, &a->flags)) {
		cmd->result = DID_ABORT << 16;

		scsi_set_resid(cmd, 0);

		cmd->scsi_done(cmd);

		return SUCCESS;
	}

	spin_lock_irqsave(&a->queue_lock, flags);

	/*
	 * Run through the defer and active queues looking for the request
	 * to abort.
	 */

	queue = &a->defer_list;

check_active_queue:

	result = esas2r_check_active_queue(a, &abort_request, cmd, queue);

	if (!result) {
		spin_unlock_irqrestore(&a->queue_lock, flags);
		return FAILED;
	} else if (result == 2 && (queue == &a->defer_list)) {
		queue = &a->active_list;
		goto check_active_queue;
	}

	spin_unlock_irqrestore(&a->queue_lock, flags);

	if (abort_request) {
		u8 task_management_status = RS_PENDING;

		/*
		 * the request is already active, so we need to tell
		 * the firmware to abort it and wait for the response.
		 */

		abort_request->comp_cb = complete_task_management_request;
		abort_request->task_management_status_ptr =
			&task_management_status;

		esas2r_start_request(a, abort_request);

		if (atomic_read(&a->disable_cnt) == 0)
			esas2r_do_deferred_processes(a);

		while (task_management_status == RS_PENDING)
			msleep(10);

		/*
		 * Once we get here, the original request will have been
		 * completed by the firmware and the abort request will have
		 * been cleaned up.  we're done!
		 */

		return SUCCESS;
	}

	/*
	 * If we get here, either we found the inactive request and
	 * freed it, or we didn't find it at all.  Either way, success!
	 */

	cmd->result = DID_ABORT << 16;

	scsi_set_resid(cmd, 0);

	cmd->scsi_done(cmd);

	return SUCCESS;
}

static int esas2r_host_bus_reset(struct scsi_cmnd *cmd, bool host_reset)
{
	struct esas2r_adapter *a =
		(struct esas2r_adapter *)cmd->device->host->hostdata;

	if (test_bit(AF_DEGRADED_MODE, &a->flags))
		return FAILED;

	if (host_reset)
		esas2r_reset_adapter(a);
	else
		esas2r_reset_bus(a);

	/* above call sets the AF_OS_RESET flag.  wait for it to clear. */

	while (test_bit(AF_OS_RESET, &a->flags)) {
		msleep(10);

		if (test_bit(AF_DEGRADED_MODE, &a->flags))
			return FAILED;
	}

	if (test_bit(AF_DEGRADED_MODE, &a->flags))
		return FAILED;

	return SUCCESS;
}

int esas2r_host_reset(struct scsi_cmnd *cmd)
{
	esas2r_log(ESAS2R_LOG_INFO, "host_reset (%p)", cmd);

	return esas2r_host_bus_reset(cmd, true);
}

int esas2r_bus_reset(struct scsi_cmnd *cmd)
{
	esas2r_log(ESAS2R_LOG_INFO, "bus_reset (%p)", cmd);

	return esas2r_host_bus_reset(cmd, false);
}

static int esas2r_dev_targ_reset(struct scsi_cmnd *cmd, bool target_reset)
{
	struct esas2r_adapter *a =
		(struct esas2r_adapter *)cmd->device->host->hostdata;
	struct esas2r_request *rq;
	u8 task_management_status = RS_PENDING;
	bool completed;

	if (test_bit(AF_DEGRADED_MODE, &a->flags))
		return FAILED;

retry:
	rq = esas2r_alloc_request(a);
	if (rq == NULL) {
		if (target_reset) {
			esas2r_log(ESAS2R_LOG_CRIT,
				   "unable to allocate a request for a "
				   "target reset (%d)!",
				   cmd->device->id);
		} else {
			esas2r_log(ESAS2R_LOG_CRIT,
				   "unable to allocate a request for a "
				   "device reset (%d:%d)!",
				   cmd->device->id,
				   cmd->device->lun);
		}


		return FAILED;
	}

	rq->target_id = cmd->device->id;
	rq->vrq->scsi.flags |= cpu_to_le32(cmd->device->lun);
	rq->req_stat = RS_PENDING;

	rq->comp_cb = complete_task_management_request;
	rq->task_management_status_ptr = &task_management_status;

	if (target_reset) {
		esas2r_debug("issuing target reset (%p) to id %d", rq,
			     cmd->device->id);
		completed = esas2r_send_task_mgmt(a, rq, 0x20);
	} else {
		esas2r_debug("issuing device reset (%p) to id %d lun %d", rq,
			     cmd->device->id, cmd->device->lun);
		completed = esas2r_send_task_mgmt(a, rq, 0x10);
	}

	if (completed) {
		/* Task management cmd completed right away, need to free it. */

		esas2r_free_request(a, rq);
	} else {
		/*
		 * Wait for firmware to complete the request.  Completion
		 * callback will free it.
		 */
		while (task_management_status == RS_PENDING)
			msleep(10);
	}

	if (test_bit(AF_DEGRADED_MODE, &a->flags))
		return FAILED;

	if (task_management_status == RS_BUSY) {
		/*
		 * Busy, probably because we are flashing.  Wait a bit and
		 * try again.
		 */
		msleep(100);
		goto retry;
	}

	return SUCCESS;
}

int esas2r_device_reset(struct scsi_cmnd *cmd)
{
	esas2r_log(ESAS2R_LOG_INFO, "device_reset (%p)", cmd);

	return esas2r_dev_targ_reset(cmd, false);

}

int esas2r_target_reset(struct scsi_cmnd *cmd)
{
	esas2r_log(ESAS2R_LOG_INFO, "target_reset (%p)", cmd);

	return esas2r_dev_targ_reset(cmd, true);
}

void esas2r_log_request_failure(struct esas2r_adapter *a,
				struct esas2r_request *rq)
{
	u8 reqstatus = rq->req_stat;

	if (reqstatus == RS_SUCCESS)
		return;

	if (rq->vrq->scsi.function == VDA_FUNC_SCSI) {
		if (reqstatus == RS_SCSI_ERROR) {
			if (rq->func_rsp.scsi_rsp.sense_len >= 13) {
				esas2r_log(ESAS2R_LOG_WARN,
					   "request failure - SCSI error %x ASC:%x ASCQ:%x CDB:%x",
					   rq->sense_buf[2], rq->sense_buf[12],
					   rq->sense_buf[13],
					   rq->vrq->scsi.cdb[0]);
			} else {
				esas2r_log(ESAS2R_LOG_WARN,
					   "request failure - SCSI error CDB:%x\n",
					   rq->vrq->scsi.cdb[0]);
			}
		} else if ((rq->vrq->scsi.cdb[0] != INQUIRY
			    && rq->vrq->scsi.cdb[0] != REPORT_LUNS)
			   || (reqstatus != RS_SEL
			       && reqstatus != RS_SEL2)) {
			if ((reqstatus == RS_UNDERRUN) &&
			    (rq->vrq->scsi.cdb[0] == INQUIRY)) {
				/* Don't log inquiry underruns */
			} else {
				esas2r_log(ESAS2R_LOG_WARN,
					   "request failure - cdb:%x reqstatus:%d target:%d",
					   rq->vrq->scsi.cdb[0], reqstatus,
					   rq->target_id);
			}
		}
	}
}

void esas2r_wait_request(struct esas2r_adapter *a, struct esas2r_request *rq)
{
	u32 starttime;
	u32 timeout;

	starttime = jiffies_to_msecs(jiffies);
	timeout = rq->timeout ? rq->timeout : 5000;

	while (true) {
		esas2r_polled_interrupt(a);

		if (rq->req_stat != RS_STARTED)
			break;

		schedule_timeout_interruptible(msecs_to_jiffies(100));

		if ((jiffies_to_msecs(jiffies) - starttime) > timeout) {
			esas2r_hdebug("request TMO");
			esas2r_bugon();

			rq->req_stat = RS_TIMEOUT;

			esas2r_local_reset_adapter(a);
			return;
		}
	}
}

u32 esas2r_map_data_window(struct esas2r_adapter *a, u32 addr_lo)
{
	u32 offset = addr_lo & (MW_DATA_WINDOW_SIZE - 1);
	u32 base = addr_lo & -(signed int)MW_DATA_WINDOW_SIZE;

	if (a->window_base != base) {
		esas2r_write_register_dword(a, MVR_PCI_WIN1_REMAP,
					    base | MVRPW1R_ENABLE);
		esas2r_flush_register_dword(a, MVR_PCI_WIN1_REMAP);
		a->window_base = base;
	}

	return offset;
}

/* Read a block of data from chip memory */
bool esas2r_read_mem_block(struct esas2r_adapter *a,
			   void *to,
			   u32 from,
			   u32 size)
{
	u8 *end = (u8 *)to;

	while (size) {
		u32 len;
		u32 offset;
		u32 iatvr;

		iatvr = (from & -(signed int)MW_DATA_WINDOW_SIZE);

		esas2r_map_data_window(a, iatvr);

		offset = from & (MW_DATA_WINDOW_SIZE - 1);
		len = size;

		if (len > MW_DATA_WINDOW_SIZE - offset)
			len = MW_DATA_WINDOW_SIZE - offset;

		from += len;
		size -= len;

		while (len--) {
			*end++ = esas2r_read_data_byte(a, offset);
			offset++;
		}
	}

	return true;
}

void esas2r_nuxi_mgt_data(u8 function, void *data)
{
	struct atto_vda_grp_info *g;
	struct atto_vda_devinfo *d;
	struct atto_vdapart_info *p;
	struct atto_vda_dh_info *h;
	struct atto_vda_metrics_info *m;
	struct atto_vda_schedule_info *s;
	struct atto_vda_buzzer_info *b;
	u8 i;

	switch (function) {
	case VDAMGT_BUZZER_INFO:
	case VDAMGT_BUZZER_SET:

		b = (struct atto_vda_buzzer_info *)data;

		b->duration = le32_to_cpu(b->duration);
		break;

	case VDAMGT_SCHEDULE_INFO:
	case VDAMGT_SCHEDULE_EVENT:

		s = (struct atto_vda_schedule_info *)data;

		s->id = le32_to_cpu(s->id);

		break;

	case VDAMGT_DEV_INFO:
	case VDAMGT_DEV_CLEAN:
	case VDAMGT_DEV_PT_INFO:
	case VDAMGT_DEV_FEATURES:
	case VDAMGT_DEV_PT_FEATURES:
	case VDAMGT_DEV_OPERATION:

		d = (struct atto_vda_devinfo *)data;

		d->capacity = le64_to_cpu(d->capacity);
		d->block_size = le32_to_cpu(d->block_size);
		d->ses_dev_index = le16_to_cpu(d->ses_dev_index);
		d->target_id = le16_to_cpu(d->target_id);
		d->lun = le16_to_cpu(d->lun);
		d->features = le16_to_cpu(d->features);
		break;

	case VDAMGT_GRP_INFO:
	case VDAMGT_GRP_CREATE:
	case VDAMGT_GRP_DELETE:
	case VDAMGT_ADD_STORAGE:
	case VDAMGT_MEMBER_ADD:
	case VDAMGT_GRP_COMMIT:
	case VDAMGT_GRP_REBUILD:
	case VDAMGT_GRP_COMMIT_INIT:
	case VDAMGT_QUICK_RAID:
	case VDAMGT_GRP_FEATURES:
	case VDAMGT_GRP_COMMIT_INIT_AUTOMAP:
	case VDAMGT_QUICK_RAID_INIT_AUTOMAP:
	case VDAMGT_SPARE_LIST:
	case VDAMGT_SPARE_ADD:
	case VDAMGT_SPARE_REMOVE:
	case VDAMGT_LOCAL_SPARE_ADD:
	case VDAMGT_GRP_OPERATION:

		g = (struct atto_vda_grp_info *)data;

		g->capacity = le64_to_cpu(g->capacity);
		g->block_size = le32_to_cpu(g->block_size);
		g->interleave = le32_to_cpu(g->interleave);
		g->features = le16_to_cpu(g->features);

		for (i = 0; i < 32; i++)
			g->members[i] = le16_to_cpu(g->members[i]);

		break;

	case VDAMGT_PART_INFO:
	case VDAMGT_PART_MAP:
	case VDAMGT_PART_UNMAP:
	case VDAMGT_PART_AUTOMAP:
	case VDAMGT_PART_SPLIT:
	case VDAMGT_PART_MERGE:

		p = (struct atto_vdapart_info *)data;

		p->part_size = le64_to_cpu(p->part_size);
		p->start_lba = le32_to_cpu(p->start_lba);
		p->block_size = le32_to_cpu(p->block_size);
		p->target_id = le16_to_cpu(p->target_id);
		break;

	case VDAMGT_DEV_HEALTH_REQ:

		h = (struct atto_vda_dh_info *)data;

		h->med_defect_cnt = le32_to_cpu(h->med_defect_cnt);
		h->info_exc_cnt = le32_to_cpu(h->info_exc_cnt);
		break;

	case VDAMGT_DEV_METRICS:

		m = (struct atto_vda_metrics_info *)data;

		for (i = 0; i < 32; i++)
			m->dev_indexes[i] = le16_to_cpu(m->dev_indexes[i]);

		break;

	default:
		break;
	}
}

void esas2r_nuxi_cfg_data(u8 function, void *data)
{
	struct atto_vda_cfg_init *ci;

	switch (function) {
	case VDA_CFG_INIT:
	case VDA_CFG_GET_INIT:
	case VDA_CFG_GET_INIT2:

		ci = (struct atto_vda_cfg_init *)data;

		ci->date_time.year = le16_to_cpu(ci->date_time.year);
		ci->sgl_page_size = le32_to_cpu(ci->sgl_page_size);
		ci->vda_version = le32_to_cpu(ci->vda_version);
		ci->epoch_time = le32_to_cpu(ci->epoch_time);
		ci->ioctl_tunnel = le32_to_cpu(ci->ioctl_tunnel);
		ci->num_targets_backend = le32_to_cpu(ci->num_targets_backend);
		break;

	default:
		break;
	}
}

void esas2r_nuxi_ae_data(union atto_vda_ae *ae)
{
	struct atto_vda_ae_raid *r = &ae->raid;
	struct atto_vda_ae_lu *l = &ae->lu;

	switch (ae->hdr.bytype) {
	case VDAAE_HDR_TYPE_RAID:

		r->dwflags = le32_to_cpu(r->dwflags);
		break;

	case VDAAE_HDR_TYPE_LU:

		l->dwevent = le32_to_cpu(l->dwevent);
		l->wphys_target_id = le16_to_cpu(l->wphys_target_id);
		l->id.tgtlun.wtarget_id = le16_to_cpu(l->id.tgtlun.wtarget_id);

		if (l->hdr.bylength >= offsetof(struct atto_vda_ae_lu, id)
		    + sizeof(struct atto_vda_ae_lu_tgt_lun_raid)) {
			l->id.tgtlun_raid.dwinterleave
				= le32_to_cpu(l->id.tgtlun_raid.dwinterleave);
			l->id.tgtlun_raid.dwblock_size
				= le32_to_cpu(l->id.tgtlun_raid.dwblock_size);
		}

		break;

	case VDAAE_HDR_TYPE_DISK:
	default:
		break;
	}
}

void esas2r_free_request(struct esas2r_adapter *a, struct esas2r_request *rq)
{
	unsigned long flags;

	esas2r_rq_destroy_request(rq, a);
	spin_lock_irqsave(&a->request_lock, flags);
	list_add(&rq->comp_list, &a->avail_request);
	spin_unlock_irqrestore(&a->request_lock, flags);
}

struct esas2r_request *esas2r_alloc_request(struct esas2r_adapter *a)
{
	struct esas2r_request *rq;
	unsigned long flags;

	spin_lock_irqsave(&a->request_lock, flags);

	if (unlikely(list_empty(&a->avail_request))) {
		spin_unlock_irqrestore(&a->request_lock, flags);
		return NULL;
	}

	rq = list_first_entry(&a->avail_request, struct esas2r_request,
			      comp_list);
	list_del(&rq->comp_list);
	spin_unlock_irqrestore(&a->request_lock, flags);
	esas2r_rq_init_request(rq, a);

	return rq;

}

void esas2r_complete_request_cb(struct esas2r_adapter *a,
				struct esas2r_request *rq)
{
	esas2r_debug("completing request %p\n", rq);

	scsi_dma_unmap(rq->cmd);

	if (unlikely(rq->req_stat != RS_SUCCESS)) {
		esas2r_debug("[%x STATUS %x:%x (%x)]", rq->target_id,
			     rq->req_stat,
			     rq->func_rsp.scsi_rsp.scsi_stat,
			     rq->cmd);

		rq->cmd->result =
			((esas2r_req_status_to_error(rq->req_stat) << 16)
			 | (rq->func_rsp.scsi_rsp.scsi_stat & STATUS_MASK));

		if (rq->req_stat == RS_UNDERRUN)
			scsi_set_resid(rq->cmd,
				       le32_to_cpu(rq->func_rsp.scsi_rsp.
						   residual_length));
		else
			scsi_set_resid(rq->cmd, 0);
	}

	rq->cmd->scsi_done(rq->cmd);

	esas2r_free_request(a, rq);
}

/* Run tasklet to handle stuff outside of interrupt context. */
void esas2r_adapter_tasklet(unsigned long context)
{
	struct esas2r_adapter *a = (struct esas2r_adapter *)context;

	if (unlikely(test_bit(AF2_TIMER_TICK, &a->flags2))) {
		clear_bit(AF2_TIMER_TICK, &a->flags2);
		esas2r_timer_tick(a);
	}

	if (likely(test_bit(AF2_INT_PENDING, &a->flags2))) {
		clear_bit(AF2_INT_PENDING, &a->flags2);
		esas2r_adapter_interrupt(a);
	}

	if (esas2r_is_tasklet_pending(a))
		esas2r_do_tasklet_tasks(a);

	if (esas2r_is_tasklet_pending(a)
	    || (test_bit(AF2_INT_PENDING, &a->flags2))
	    || (test_bit(AF2_TIMER_TICK, &a->flags2))) {
		clear_bit(AF_TASKLET_SCHEDULED, &a->flags);
		esas2r_schedule_tasklet(a);
	} else {
		clear_bit(AF_TASKLET_SCHEDULED, &a->flags);
	}
}

static void esas2r_timer_callback(unsigned long context);

void esas2r_kickoff_timer(struct esas2r_adapter *a)
{
	init_timer(&a->timer);

	a->timer.function = esas2r_timer_callback;
	a->timer.data = (unsigned long)a;
	a->timer.expires = jiffies +
			   msecs_to_jiffies(100);

	add_timer(&a->timer);
}

static void esas2r_timer_callback(unsigned long context)
{
	struct esas2r_adapter *a = (struct esas2r_adapter *)context;

	set_bit(AF2_TIMER_TICK, &a->flags2);

	esas2r_schedule_tasklet(a);

	esas2r_kickoff_timer(a);
}

/*
 * Firmware events need to be handled outside of interrupt context
 * so we schedule a delayed_work to handle them.
 */

static void
esas2r_free_fw_event(struct esas2r_fw_event_work *fw_event)
{
	unsigned long flags;
	struct esas2r_adapter *a = fw_event->a;

	spin_lock_irqsave(&a->fw_event_lock, flags);
	list_del(&fw_event->list);
	kfree(fw_event);
	spin_unlock_irqrestore(&a->fw_event_lock, flags);
}

void
esas2r_fw_event_off(struct esas2r_adapter *a)
{
	unsigned long flags;

	spin_lock_irqsave(&a->fw_event_lock, flags);
	a->fw_events_off = 1;
	spin_unlock_irqrestore(&a->fw_event_lock, flags);
}

void
esas2r_fw_event_on(struct esas2r_adapter *a)
{
	unsigned long flags;

	spin_lock_irqsave(&a->fw_event_lock, flags);
	a->fw_events_off = 0;
	spin_unlock_irqrestore(&a->fw_event_lock, flags);
}

static void esas2r_add_device(struct esas2r_adapter *a, u16 target_id)
{
	int ret;
	struct scsi_device *scsi_dev;

	scsi_dev = scsi_device_lookup(a->host, 0, target_id, 0);

	if (scsi_dev) {
		esas2r_log_dev(
			ESAS2R_LOG_WARN,
			&(scsi_dev->
			  sdev_gendev),
			"scsi device already exists at id %d", target_id);

		scsi_device_put(scsi_dev);
	} else {
		esas2r_log_dev(
			ESAS2R_LOG_INFO,
			&(a->host->
			  shost_gendev),
			"scsi_add_device() called for 0:%d:0",
			target_id);

		ret = scsi_add_device(a->host, 0, target_id, 0);
		if (ret) {
			esas2r_log_dev(
				ESAS2R_LOG_CRIT,
				&(a->host->
				  shost_gendev),
				"scsi_add_device failed with %d for id %d",
				ret, target_id);
		}
	}
}

static void esas2r_remove_device(struct esas2r_adapter *a, u16 target_id)
{
	struct scsi_device *scsi_dev;

	scsi_dev = scsi_device_lookup(a->host, 0, target_id, 0);

	if (scsi_dev) {
		scsi_device_set_state(scsi_dev, SDEV_OFFLINE);

		esas2r_log_dev(
			ESAS2R_LOG_INFO,
			&(scsi_dev->
			  sdev_gendev),
			"scsi_remove_device() called for 0:%d:0",
			target_id);

		scsi_remove_device(scsi_dev);

		esas2r_log_dev(
			ESAS2R_LOG_INFO,
			&(scsi_dev->
			  sdev_gendev),
			"scsi_device_put() called");

		scsi_device_put(scsi_dev);
	} else {
		esas2r_log_dev(
			ESAS2R_LOG_WARN,
			&(a->host->shost_gendev),
			"no target found at id %d",
			target_id);
	}
}

/*
 * Sends a firmware asynchronous event to anyone who happens to be
 * listening on the defined ATTO VDA event ports.
 */
static void esas2r_send_ae_event(struct esas2r_fw_event_work *fw_event)
{
	struct esas2r_vda_ae *ae = (struct esas2r_vda_ae *)fw_event->data;
	char *type;

	switch (ae->vda_ae.hdr.bytype) {
	case VDAAE_HDR_TYPE_RAID:
		type = "RAID group state change";
		break;

	case VDAAE_HDR_TYPE_LU:
		type = "Mapped destination LU change";
		break;

	case VDAAE_HDR_TYPE_DISK:
		type = "Physical disk inventory change";
		break;

	case VDAAE_HDR_TYPE_RESET:
		type = "Firmware reset";
		break;

	case VDAAE_HDR_TYPE_LOG_INFO:
		type = "Event Log message (INFO level)";
		break;

	case VDAAE_HDR_TYPE_LOG_WARN:
		type = "Event Log message (WARN level)";
		break;

	case VDAAE_HDR_TYPE_LOG_CRIT:
		type = "Event Log message (CRIT level)";
		break;

	case VDAAE_HDR_TYPE_LOG_FAIL:
		type = "Event Log message (FAIL level)";
		break;

	case VDAAE_HDR_TYPE_NVC:
		type = "NVCache change";
		break;

	case VDAAE_HDR_TYPE_TLG_INFO:
		type = "Time stamped log message (INFO level)";
		break;

	case VDAAE_HDR_TYPE_TLG_WARN:
		type = "Time stamped log message (WARN level)";
		break;

	case VDAAE_HDR_TYPE_TLG_CRIT:
		type = "Time stamped log message (CRIT level)";
		break;

	case VDAAE_HDR_TYPE_PWRMGT:
		type = "Power management";
		break;

	case VDAAE_HDR_TYPE_MUTE:
		type = "Mute button pressed";
		break;

	case VDAAE_HDR_TYPE_DEV:
		type = "Device attribute change";
		break;

	default:
		type = "Unknown";
		break;
	}

	esas2r_log(ESAS2R_LOG_WARN,
		   "An async event of type \"%s\" was received from the firmware.  The event contents are:",
		   type);
	esas2r_log_hexdump(ESAS2R_LOG_WARN, &ae->vda_ae,
			   ae->vda_ae.hdr.bylength);

}

static void
esas2r_firmware_event_work(struct work_struct *work)
{
	struct esas2r_fw_event_work *fw_event =
		container_of(work, struct esas2r_fw_event_work, work.work);

	struct esas2r_adapter *a = fw_event->a;

	u16 target_id = *(u16 *)&fw_event->data[0];

	if (a->fw_events_off)
		goto done;

	switch (fw_event->type) {
	case fw_event_null:
		break; /* do nothing */

	case fw_event_lun_change:
		esas2r_remove_device(a, target_id);
		esas2r_add_device(a, target_id);
		break;

	case fw_event_present:
		esas2r_add_device(a, target_id);
		break;

	case fw_event_not_present:
		esas2r_remove_device(a, target_id);
		break;

	case fw_event_vda_ae:
		esas2r_send_ae_event(fw_event);
		break;
	}

done:
	esas2r_free_fw_event(fw_event);
}

void esas2r_queue_fw_event(struct esas2r_adapter *a,
			   enum fw_event_type type,
			   void *data,
			   int data_sz)
{
	struct esas2r_fw_event_work *fw_event;
	unsigned long flags;

	fw_event = kzalloc(sizeof(struct esas2r_fw_event_work), GFP_ATOMIC);
	if (!fw_event) {
		esas2r_log(ESAS2R_LOG_WARN,
			   "esas2r_queue_fw_event failed to alloc");
		return;
	}

	if (type == fw_event_vda_ae) {
		struct esas2r_vda_ae *ae =
			(struct esas2r_vda_ae *)fw_event->data;

		ae->signature = ESAS2R_VDA_EVENT_SIG;
		ae->bus_number = a->pcid->bus->number;
		ae->devfn = a->pcid->devfn;
		memcpy(&ae->vda_ae, data, sizeof(ae->vda_ae));
	} else {
		memcpy(fw_event->data, data, data_sz);
	}

	fw_event->type = type;
	fw_event->a = a;

	spin_lock_irqsave(&a->fw_event_lock, flags);
	list_add_tail(&fw_event->list, &a->fw_event_list);
	INIT_DELAYED_WORK(&fw_event->work, esas2r_firmware_event_work);
	queue_delayed_work_on(
		smp_processor_id(), a->fw_event_q, &fw_event->work,
		msecs_to_jiffies(1));
	spin_unlock_irqrestore(&a->fw_event_lock, flags);
}

void esas2r_target_state_changed(struct esas2r_adapter *a, u16 targ_id,
				 u8 state)
{
	if (state == TS_LUN_CHANGE)
		esas2r_queue_fw_event(a, fw_event_lun_change, &targ_id,
				      sizeof(targ_id));
	else if (state == TS_PRESENT)
		esas2r_queue_fw_event(a, fw_event_present, &targ_id,
				      sizeof(targ_id));
	else if (state == TS_NOT_PRESENT)
		esas2r_queue_fw_event(a, fw_event_not_present, &targ_id,
				      sizeof(targ_id));
}

/* Translate status to a Linux SCSI mid-layer error code */
int esas2r_req_status_to_error(u8 req_stat)
{
	switch (req_stat) {
	case RS_OVERRUN:
	case RS_UNDERRUN:
	case RS_SUCCESS:
	/*
	 * NOTE: SCSI mid-layer wants a good status for a SCSI error, because
	 *       it will check the scsi_stat value in the completion anyway.
	 */
	case RS_SCSI_ERROR:
		return DID_OK;

	case RS_SEL:
	case RS_SEL2:
		return DID_NO_CONNECT;

	case RS_RESET:
		return DID_RESET;

	case RS_ABORTED:
		return DID_ABORT;

	case RS_BUSY:
		return DID_BUS_BUSY;
	}

	/* everything else is just an error. */

	return DID_ERROR;
}

module_init(esas2r_init);
module_exit(esas2r_exit);
