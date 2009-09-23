/*
 * Scsi Host Layer for MPT (Message Passing Technology) based controllers
 *
 * This code is based on drivers/scsi/mpt2sas/mpt2_scsih.c
 * Copyright (C) 2007-2009  LSI Corporation
 *  (mailto:DL-MPTFusionLinux@lsi.com)
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

 * DISCLAIMER OF LIABILITY
 * NEITHER RECIPIENT NOR ANY CONTRIBUTORS SHALL HAVE ANY LIABILITY FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING WITHOUT LIMITATION LOST PROFITS), HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 * TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OR DISTRIBUTION OF THE PROGRAM OR THE EXERCISE OF ANY RIGHTS GRANTED
 * HEREUNDER, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGES

 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301,
 * USA.
 */

#include <linux/version.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/blkdev.h>
#include <linux/sched.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/interrupt.h>

#include "mpt2sas_base.h"

MODULE_AUTHOR(MPT2SAS_AUTHOR);
MODULE_DESCRIPTION(MPT2SAS_DESCRIPTION);
MODULE_LICENSE("GPL");
MODULE_VERSION(MPT2SAS_DRIVER_VERSION);

#define RAID_CHANNEL 1

/* forward proto's */
static void _scsih_expander_node_remove(struct MPT2SAS_ADAPTER *ioc,
    struct _sas_node *sas_expander);
static void _firmware_event_work(struct work_struct *work);

/* global parameters */
LIST_HEAD(mpt2sas_ioc_list);

/* local parameters */
static u8 scsi_io_cb_idx = -1;
static u8 tm_cb_idx = -1;
static u8 ctl_cb_idx = -1;
static u8 base_cb_idx = -1;
static u8 transport_cb_idx = -1;
static u8 config_cb_idx = -1;
static int mpt_ids;

static u8 tm_tr_cb_idx = -1 ;
static u8 tm_sas_control_cb_idx = -1;

/* command line options */
static u32 logging_level;
MODULE_PARM_DESC(logging_level, " bits for enabling additional logging info "
    "(default=0)");

/* scsi-mid layer global parmeter is max_report_luns, which is 511 */
#define MPT2SAS_MAX_LUN (16895)
static int max_lun = MPT2SAS_MAX_LUN;
module_param(max_lun, int, 0);
MODULE_PARM_DESC(max_lun, " max lun, default=16895 ");

/**
 * struct sense_info - common structure for obtaining sense keys
 * @skey: sense key
 * @asc: additional sense code
 * @ascq: additional sense code qualifier
 */
struct sense_info {
	u8 skey;
	u8 asc;
	u8 ascq;
};


/**
 * struct fw_event_work - firmware event struct
 * @list: link list framework
 * @work: work object (ioc->fault_reset_work_q)
 * @ioc: per adapter object
 * @VF_ID: virtual function id
 * @VP_ID: virtual port id
 * @host_reset_handling: handling events during host reset
 * @ignore: flag meaning this event has been marked to ignore
 * @event: firmware event MPI2_EVENT_XXX defined in mpt2_ioc.h
 * @event_data: reply event data payload follows
 *
 * This object stored on ioc->fw_event_list.
 */
struct fw_event_work {
	struct list_head 	list;
	struct work_struct	work;
	struct MPT2SAS_ADAPTER *ioc;
	u8			VF_ID;
	u8			VP_ID;
	u8			host_reset_handling;
	u8			ignore;
	u16			event;
	void			*event_data;
};

/**
 * struct _scsi_io_transfer - scsi io transfer
 * @handle: sas device handle (assigned by firmware)
 * @is_raid: flag set for hidden raid components
 * @dir: DMA_TO_DEVICE, DMA_FROM_DEVICE,
 * @data_length: data transfer length
 * @data_dma: dma pointer to data
 * @sense: sense data
 * @lun: lun number
 * @cdb_length: cdb length
 * @cdb: cdb contents
 * @timeout: timeout for this command
 * @VF_ID: virtual function id
 * @VP_ID: virtual port id
 * @valid_reply: flag set for reply message
 * @sense_length: sense length
 * @ioc_status: ioc status
 * @scsi_state: scsi state
 * @scsi_status: scsi staus
 * @log_info: log information
 * @transfer_length: data length transfer when there is a reply message
 *
 * Used for sending internal scsi commands to devices within this module.
 * Refer to _scsi_send_scsi_io().
 */
struct _scsi_io_transfer {
	u16	handle;
	u8	is_raid;
	enum dma_data_direction dir;
	u32	data_length;
	dma_addr_t data_dma;
	u8 	sense[SCSI_SENSE_BUFFERSIZE];
	u32	lun;
	u8	cdb_length;
	u8	cdb[32];
	u8	timeout;
	u8	VF_ID;
	u8	VP_ID;
	u8	valid_reply;
  /* the following bits are only valid when 'valid_reply = 1' */
	u32	sense_length;
	u16	ioc_status;
	u8	scsi_state;
	u8	scsi_status;
	u32	log_info;
	u32	transfer_length;
};

/*
 * The pci device ids are defined in mpi/mpi2_cnfg.h.
 */
static struct pci_device_id scsih_pci_table[] = {
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI2_MFGPAGE_DEVID_SAS2004,
		PCI_ANY_ID, PCI_ANY_ID },
	/* Falcon ~ 2008*/
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI2_MFGPAGE_DEVID_SAS2008,
		PCI_ANY_ID, PCI_ANY_ID },
	/* Liberator ~ 2108 */
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI2_MFGPAGE_DEVID_SAS2108_1,
		PCI_ANY_ID, PCI_ANY_ID },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI2_MFGPAGE_DEVID_SAS2108_2,
		PCI_ANY_ID, PCI_ANY_ID },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI2_MFGPAGE_DEVID_SAS2108_3,
		PCI_ANY_ID, PCI_ANY_ID },
	/* Meteor ~ 2116 */
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI2_MFGPAGE_DEVID_SAS2116_1,
		PCI_ANY_ID, PCI_ANY_ID },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI2_MFGPAGE_DEVID_SAS2116_2,
		PCI_ANY_ID, PCI_ANY_ID },
	/* Thunderbolt ~ 2208 */
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI2_MFGPAGE_DEVID_SAS2208_1,
		PCI_ANY_ID, PCI_ANY_ID },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI2_MFGPAGE_DEVID_SAS2208_2,
		PCI_ANY_ID, PCI_ANY_ID },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI2_MFGPAGE_DEVID_SAS2208_3,
		PCI_ANY_ID, PCI_ANY_ID },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI2_MFGPAGE_DEVID_SAS2208_4,
		PCI_ANY_ID, PCI_ANY_ID },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI2_MFGPAGE_DEVID_SAS2208_5,
		PCI_ANY_ID, PCI_ANY_ID },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI2_MFGPAGE_DEVID_SAS2208_6,
		PCI_ANY_ID, PCI_ANY_ID },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI2_MFGPAGE_DEVID_SAS2208_7,
		PCI_ANY_ID, PCI_ANY_ID },
	{ MPI2_MFGPAGE_VENDORID_LSI, MPI2_MFGPAGE_DEVID_SAS2208_8,
		PCI_ANY_ID, PCI_ANY_ID },
	{0}	/* Terminating entry */
};
MODULE_DEVICE_TABLE(pci, scsih_pci_table);

/**
 * _scsih_set_debug_level - global setting of ioc->logging_level.
 *
 * Note: The logging levels are defined in mpt2sas_debug.h.
 */
static int
_scsih_set_debug_level(const char *val, struct kernel_param *kp)
{
	int ret = param_set_int(val, kp);
	struct MPT2SAS_ADAPTER *ioc;

	if (ret)
		return ret;

	printk(KERN_INFO "setting logging_level(0x%08x)\n", logging_level);
	list_for_each_entry(ioc, &mpt2sas_ioc_list, list)
		ioc->logging_level = logging_level;
	return 0;
}
module_param_call(logging_level, _scsih_set_debug_level, param_get_int,
    &logging_level, 0644);

/**
 * _scsih_srch_boot_sas_address - search based on sas_address
 * @sas_address: sas address
 * @boot_device: boot device object from bios page 2
 *
 * Returns 1 when there's a match, 0 means no match.
 */
static inline int
_scsih_srch_boot_sas_address(u64 sas_address,
    Mpi2BootDeviceSasWwid_t *boot_device)
{
	return (sas_address == le64_to_cpu(boot_device->SASAddress)) ?  1 : 0;
}

/**
 * _scsih_srch_boot_device_name - search based on device name
 * @device_name: device name specified in INDENTIFY fram
 * @boot_device: boot device object from bios page 2
 *
 * Returns 1 when there's a match, 0 means no match.
 */
static inline int
_scsih_srch_boot_device_name(u64 device_name,
    Mpi2BootDeviceDeviceName_t *boot_device)
{
	return (device_name == le64_to_cpu(boot_device->DeviceName)) ? 1 : 0;
}

/**
 * _scsih_srch_boot_encl_slot - search based on enclosure_logical_id/slot
 * @enclosure_logical_id: enclosure logical id
 * @slot_number: slot number
 * @boot_device: boot device object from bios page 2
 *
 * Returns 1 when there's a match, 0 means no match.
 */
static inline int
_scsih_srch_boot_encl_slot(u64 enclosure_logical_id, u16 slot_number,
    Mpi2BootDeviceEnclosureSlot_t *boot_device)
{
	return (enclosure_logical_id == le64_to_cpu(boot_device->
	    EnclosureLogicalID) && slot_number == le16_to_cpu(boot_device->
	    SlotNumber)) ? 1 : 0;
}

/**
 * _scsih_is_boot_device - search for matching boot device.
 * @sas_address: sas address
 * @device_name: device name specified in INDENTIFY fram
 * @enclosure_logical_id: enclosure logical id
 * @slot_number: slot number
 * @form: specifies boot device form
 * @boot_device: boot device object from bios page 2
 *
 * Returns 1 when there's a match, 0 means no match.
 */
static int
_scsih_is_boot_device(u64 sas_address, u64 device_name,
    u64 enclosure_logical_id, u16 slot, u8 form,
    Mpi2BiosPage2BootDevice_t *boot_device)
{
	int rc = 0;

	switch (form) {
	case MPI2_BIOSPAGE2_FORM_SAS_WWID:
		if (!sas_address)
			break;
		rc = _scsih_srch_boot_sas_address(
		    sas_address, &boot_device->SasWwid);
		break;
	case MPI2_BIOSPAGE2_FORM_ENCLOSURE_SLOT:
		if (!enclosure_logical_id)
			break;
		rc = _scsih_srch_boot_encl_slot(
		    enclosure_logical_id,
		    slot, &boot_device->EnclosureSlot);
		break;
	case MPI2_BIOSPAGE2_FORM_DEVICE_NAME:
		if (!device_name)
			break;
		rc = _scsih_srch_boot_device_name(
		    device_name, &boot_device->DeviceName);
		break;
	case MPI2_BIOSPAGE2_FORM_NO_DEVICE_SPECIFIED:
		break;
	}

	return rc;
}

/**
 * _scsih_get_sas_address - set the sas_address for given device handle
 * @handle: device handle
 * @sas_address: sas address
 *
 * Returns 0 success, non-zero when failure
 */
static int
_scsih_get_sas_address(struct MPT2SAS_ADAPTER *ioc, u16 handle,
    u64 *sas_address)
{
	Mpi2SasDevicePage0_t sas_device_pg0;
	Mpi2ConfigReply_t mpi_reply;
	u32 ioc_status;

	if (handle <= ioc->sas_hba.num_phys) {
		*sas_address = ioc->sas_hba.sas_address;
		return 0;
	} else
		*sas_address = 0;

	if ((mpt2sas_config_get_sas_device_pg0(ioc, &mpi_reply, &sas_device_pg0,
	    MPI2_SAS_DEVICE_PGAD_FORM_HANDLE, handle))) {
		printk(MPT2SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		return -ENXIO;
	}

	ioc_status = le16_to_cpu(mpi_reply.IOCStatus) &
	    MPI2_IOCSTATUS_MASK;
	if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
		printk(MPT2SAS_ERR_FMT "handle(0x%04x), ioc_status(0x%04x)"
		    "\nfailure at %s:%d/%s()!\n", ioc->name, handle, ioc_status,
		     __FILE__, __LINE__, __func__);
		return -EIO;
	}

	*sas_address = le64_to_cpu(sas_device_pg0.SASAddress);
	return 0;
}

/**
 * _scsih_determine_boot_device - determine boot device.
 * @ioc: per adapter object
 * @device: either sas_device or raid_device object
 * @is_raid: [flag] 1 = raid object, 0 = sas object
 *
 * Determines whether this device should be first reported device to
 * to scsi-ml or sas transport, this purpose is for persistant boot device.
 * There are primary, alternate, and current entries in bios page 2. The order
 * priority is primary, alternate, then current.  This routine saves
 * the corresponding device object and is_raid flag in the ioc object.
 * The saved data to be used later in _scsih_probe_boot_devices().
 */
static void
_scsih_determine_boot_device(struct MPT2SAS_ADAPTER *ioc,
    void *device, u8 is_raid)
{
	struct _sas_device *sas_device;
	struct _raid_device *raid_device;
	u64 sas_address;
	u64 device_name;
	u64 enclosure_logical_id;
	u16 slot;

	 /* only process this function when driver loads */
	if (!ioc->wait_for_port_enable_to_complete)
		return;

	if (!is_raid) {
		sas_device = device;
		sas_address = sas_device->sas_address;
		device_name = sas_device->device_name;
		enclosure_logical_id = sas_device->enclosure_logical_id;
		slot = sas_device->slot;
	} else {
		raid_device = device;
		sas_address = raid_device->wwid;
		device_name = 0;
		enclosure_logical_id = 0;
		slot = 0;
	}

	if (!ioc->req_boot_device.device) {
		if (_scsih_is_boot_device(sas_address, device_name,
		    enclosure_logical_id, slot,
		    (ioc->bios_pg2.ReqBootDeviceForm &
		    MPI2_BIOSPAGE2_FORM_MASK),
		    &ioc->bios_pg2.RequestedBootDevice)) {
			dinitprintk(ioc, printk(MPT2SAS_DEBUG_FMT
			   "%s: req_boot_device(0x%016llx)\n",
			    ioc->name, __func__,
			    (unsigned long long)sas_address));
			ioc->req_boot_device.device = device;
			ioc->req_boot_device.is_raid = is_raid;
		}
	}

	if (!ioc->req_alt_boot_device.device) {
		if (_scsih_is_boot_device(sas_address, device_name,
		    enclosure_logical_id, slot,
		    (ioc->bios_pg2.ReqAltBootDeviceForm &
		    MPI2_BIOSPAGE2_FORM_MASK),
		    &ioc->bios_pg2.RequestedAltBootDevice)) {
			dinitprintk(ioc, printk(MPT2SAS_DEBUG_FMT
			   "%s: req_alt_boot_device(0x%016llx)\n",
			    ioc->name, __func__,
			    (unsigned long long)sas_address));
			ioc->req_alt_boot_device.device = device;
			ioc->req_alt_boot_device.is_raid = is_raid;
		}
	}

	if (!ioc->current_boot_device.device) {
		if (_scsih_is_boot_device(sas_address, device_name,
		    enclosure_logical_id, slot,
		    (ioc->bios_pg2.CurrentBootDeviceForm &
		    MPI2_BIOSPAGE2_FORM_MASK),
		    &ioc->bios_pg2.CurrentBootDevice)) {
			dinitprintk(ioc, printk(MPT2SAS_DEBUG_FMT
			   "%s: current_boot_device(0x%016llx)\n",
			    ioc->name, __func__,
			    (unsigned long long)sas_address));
			ioc->current_boot_device.device = device;
			ioc->current_boot_device.is_raid = is_raid;
		}
	}
}

/**
 * mpt2sas_scsih_sas_device_find_by_sas_address - sas device search
 * @ioc: per adapter object
 * @sas_address: sas address
 * Context: Calling function should acquire ioc->sas_device_lock
 *
 * This searches for sas_device based on sas_address, then return sas_device
 * object.
 */
struct _sas_device *
mpt2sas_scsih_sas_device_find_by_sas_address(struct MPT2SAS_ADAPTER *ioc,
    u64 sas_address)
{
	struct _sas_device *sas_device, *r;

	r = NULL;
	/* check the sas_device_init_list */
	list_for_each_entry(sas_device, &ioc->sas_device_init_list,
	    list) {
		if (sas_device->sas_address != sas_address)
			continue;
		r = sas_device;
		goto out;
	}

	/* then check the sas_device_list */
	list_for_each_entry(sas_device, &ioc->sas_device_list, list) {
		if (sas_device->sas_address != sas_address)
			continue;
		r = sas_device;
		goto out;
	}
 out:
	return r;
}

/**
 * _scsih_sas_device_find_by_handle - sas device search
 * @ioc: per adapter object
 * @handle: sas device handle (assigned by firmware)
 * Context: Calling function should acquire ioc->sas_device_lock
 *
 * This searches for sas_device based on sas_address, then return sas_device
 * object.
 */
static struct _sas_device *
_scsih_sas_device_find_by_handle(struct MPT2SAS_ADAPTER *ioc, u16 handle)
{
	struct _sas_device *sas_device, *r;

	r = NULL;
	if (ioc->wait_for_port_enable_to_complete) {
		list_for_each_entry(sas_device, &ioc->sas_device_init_list,
		    list) {
			if (sas_device->handle != handle)
				continue;
			r = sas_device;
			goto out;
		}
	} else {
		list_for_each_entry(sas_device, &ioc->sas_device_list, list) {
			if (sas_device->handle != handle)
				continue;
			r = sas_device;
			goto out;
		}
	}

 out:
	return r;
}

/**
 * _scsih_sas_device_remove - remove sas_device from list.
 * @ioc: per adapter object
 * @sas_device: the sas_device object
 * Context: This function will acquire ioc->sas_device_lock.
 *
 * Removing object and freeing associated memory from the ioc->sas_device_list.
 */
static void
_scsih_sas_device_remove(struct MPT2SAS_ADAPTER *ioc,
    struct _sas_device *sas_device)
{
	unsigned long flags;

	spin_lock_irqsave(&ioc->sas_device_lock, flags);
	list_del(&sas_device->list);
	memset(sas_device, 0, sizeof(struct _sas_device));
	kfree(sas_device);
	spin_unlock_irqrestore(&ioc->sas_device_lock, flags);
}

/**
 * _scsih_sas_device_add - insert sas_device to the list.
 * @ioc: per adapter object
 * @sas_device: the sas_device object
 * Context: This function will acquire ioc->sas_device_lock.
 *
 * Adding new object to the ioc->sas_device_list.
 */
static void
_scsih_sas_device_add(struct MPT2SAS_ADAPTER *ioc,
    struct _sas_device *sas_device)
{
	unsigned long flags;

	dewtprintk(ioc, printk(MPT2SAS_DEBUG_FMT "%s: handle"
	    "(0x%04x), sas_addr(0x%016llx)\n", ioc->name, __func__,
	    sas_device->handle, (unsigned long long)sas_device->sas_address));

	spin_lock_irqsave(&ioc->sas_device_lock, flags);
	list_add_tail(&sas_device->list, &ioc->sas_device_list);
	spin_unlock_irqrestore(&ioc->sas_device_lock, flags);

	if (!mpt2sas_transport_port_add(ioc, sas_device->handle,
	     sas_device->sas_address_parent))
		_scsih_sas_device_remove(ioc, sas_device);
}

/**
 * _scsih_sas_device_init_add - insert sas_device to the list.
 * @ioc: per adapter object
 * @sas_device: the sas_device object
 * Context: This function will acquire ioc->sas_device_lock.
 *
 * Adding new object at driver load time to the ioc->sas_device_init_list.
 */
static void
_scsih_sas_device_init_add(struct MPT2SAS_ADAPTER *ioc,
    struct _sas_device *sas_device)
{
	unsigned long flags;

	dewtprintk(ioc, printk(MPT2SAS_DEBUG_FMT "%s: handle"
	    "(0x%04x), sas_addr(0x%016llx)\n", ioc->name, __func__,
	    sas_device->handle, (unsigned long long)sas_device->sas_address));

	spin_lock_irqsave(&ioc->sas_device_lock, flags);
	list_add_tail(&sas_device->list, &ioc->sas_device_init_list);
	spin_unlock_irqrestore(&ioc->sas_device_lock, flags);
	_scsih_determine_boot_device(ioc, sas_device, 0);
}

/**
 * _scsih_raid_device_find_by_id - raid device search
 * @ioc: per adapter object
 * @id: sas device target id
 * @channel: sas device channel
 * Context: Calling function should acquire ioc->raid_device_lock
 *
 * This searches for raid_device based on target id, then return raid_device
 * object.
 */
static struct _raid_device *
_scsih_raid_device_find_by_id(struct MPT2SAS_ADAPTER *ioc, int id, int channel)
{
	struct _raid_device *raid_device, *r;

	r = NULL;
	list_for_each_entry(raid_device, &ioc->raid_device_list, list) {
		if (raid_device->id == id && raid_device->channel == channel) {
			r = raid_device;
			goto out;
		}
	}

 out:
	return r;
}

/**
 * _scsih_raid_device_find_by_handle - raid device search
 * @ioc: per adapter object
 * @handle: sas device handle (assigned by firmware)
 * Context: Calling function should acquire ioc->raid_device_lock
 *
 * This searches for raid_device based on handle, then return raid_device
 * object.
 */
static struct _raid_device *
_scsih_raid_device_find_by_handle(struct MPT2SAS_ADAPTER *ioc, u16 handle)
{
	struct _raid_device *raid_device, *r;

	r = NULL;
	list_for_each_entry(raid_device, &ioc->raid_device_list, list) {
		if (raid_device->handle != handle)
			continue;
		r = raid_device;
		goto out;
	}

 out:
	return r;
}

/**
 * _scsih_raid_device_find_by_wwid - raid device search
 * @ioc: per adapter object
 * @handle: sas device handle (assigned by firmware)
 * Context: Calling function should acquire ioc->raid_device_lock
 *
 * This searches for raid_device based on wwid, then return raid_device
 * object.
 */
static struct _raid_device *
_scsih_raid_device_find_by_wwid(struct MPT2SAS_ADAPTER *ioc, u64 wwid)
{
	struct _raid_device *raid_device, *r;

	r = NULL;
	list_for_each_entry(raid_device, &ioc->raid_device_list, list) {
		if (raid_device->wwid != wwid)
			continue;
		r = raid_device;
		goto out;
	}

 out:
	return r;
}

/**
 * _scsih_raid_device_add - add raid_device object
 * @ioc: per adapter object
 * @raid_device: raid_device object
 *
 * This is added to the raid_device_list link list.
 */
static void
_scsih_raid_device_add(struct MPT2SAS_ADAPTER *ioc,
    struct _raid_device *raid_device)
{
	unsigned long flags;

	dewtprintk(ioc, printk(MPT2SAS_DEBUG_FMT "%s: handle"
	    "(0x%04x), wwid(0x%016llx)\n", ioc->name, __func__,
	    raid_device->handle, (unsigned long long)raid_device->wwid));

	spin_lock_irqsave(&ioc->raid_device_lock, flags);
	list_add_tail(&raid_device->list, &ioc->raid_device_list);
	spin_unlock_irqrestore(&ioc->raid_device_lock, flags);
}

/**
 * _scsih_raid_device_remove - delete raid_device object
 * @ioc: per adapter object
 * @raid_device: raid_device object
 *
 * This is removed from the raid_device_list link list.
 */
static void
_scsih_raid_device_remove(struct MPT2SAS_ADAPTER *ioc,
    struct _raid_device *raid_device)
{
	unsigned long flags;

	spin_lock_irqsave(&ioc->raid_device_lock, flags);
	list_del(&raid_device->list);
	memset(raid_device, 0, sizeof(struct _raid_device));
	kfree(raid_device);
	spin_unlock_irqrestore(&ioc->raid_device_lock, flags);
}

/**
 * mpt2sas_scsih_expander_find_by_handle - expander device search
 * @ioc: per adapter object
 * @handle: expander handle (assigned by firmware)
 * Context: Calling function should acquire ioc->sas_device_lock
 *
 * This searches for expander device based on handle, then returns the
 * sas_node object.
 */
struct _sas_node *
mpt2sas_scsih_expander_find_by_handle(struct MPT2SAS_ADAPTER *ioc, u16 handle)
{
	struct _sas_node *sas_expander, *r;

	r = NULL;
	list_for_each_entry(sas_expander, &ioc->sas_expander_list, list) {
		if (sas_expander->handle != handle)
			continue;
		r = sas_expander;
		goto out;
	}
 out:
	return r;
}

/**
 * mpt2sas_scsih_expander_find_by_sas_address - expander device search
 * @ioc: per adapter object
 * @sas_address: sas address
 * Context: Calling function should acquire ioc->sas_node_lock.
 *
 * This searches for expander device based on sas_address, then returns the
 * sas_node object.
 */
struct _sas_node *
mpt2sas_scsih_expander_find_by_sas_address(struct MPT2SAS_ADAPTER *ioc,
    u64 sas_address)
{
	struct _sas_node *sas_expander, *r;

	r = NULL;
	list_for_each_entry(sas_expander, &ioc->sas_expander_list, list) {
		if (sas_expander->sas_address != sas_address)
			continue;
		r = sas_expander;
		goto out;
	}
 out:
	return r;
}

/**
 * _scsih_expander_node_add - insert expander device to the list.
 * @ioc: per adapter object
 * @sas_expander: the sas_device object
 * Context: This function will acquire ioc->sas_node_lock.
 *
 * Adding new object to the ioc->sas_expander_list.
 *
 * Return nothing.
 */
static void
_scsih_expander_node_add(struct MPT2SAS_ADAPTER *ioc,
    struct _sas_node *sas_expander)
{
	unsigned long flags;

	spin_lock_irqsave(&ioc->sas_node_lock, flags);
	list_add_tail(&sas_expander->list, &ioc->sas_expander_list);
	spin_unlock_irqrestore(&ioc->sas_node_lock, flags);
}

/**
 * _scsih_is_end_device - determines if device is an end device
 * @device_info: bitfield providing information about the device.
 * Context: none
 *
 * Returns 1 if end device.
 */
static int
_scsih_is_end_device(u32 device_info)
{
	if (device_info & MPI2_SAS_DEVICE_INFO_END_DEVICE &&
		((device_info & MPI2_SAS_DEVICE_INFO_SSP_TARGET) |
		(device_info & MPI2_SAS_DEVICE_INFO_STP_TARGET) |
		(device_info & MPI2_SAS_DEVICE_INFO_SATA_DEVICE)))
		return 1;
	else
		return 0;
}

/**
 * mptscsih_get_scsi_lookup - returns scmd entry
 * @ioc: per adapter object
 * @smid: system request message index
 *
 * Returns the smid stored scmd pointer.
 */
static struct scsi_cmnd *
_scsih_scsi_lookup_get(struct MPT2SAS_ADAPTER *ioc, u16 smid)
{
	return ioc->scsi_lookup[smid - 1].scmd;
}

/**
 * _scsih_scsi_lookup_find_by_scmd - scmd lookup
 * @ioc: per adapter object
 * @smid: system request message index
 * @scmd: pointer to scsi command object
 * Context: This function will acquire ioc->scsi_lookup_lock.
 *
 * This will search for a scmd pointer in the scsi_lookup array,
 * returning the revelent smid.  A returned value of zero means invalid.
 */
static u16
_scsih_scsi_lookup_find_by_scmd(struct MPT2SAS_ADAPTER *ioc, struct scsi_cmnd
    *scmd)
{
	u16 smid;
	unsigned long	flags;
	int i;

	spin_lock_irqsave(&ioc->scsi_lookup_lock, flags);
	smid = 0;
	for (i = 0; i < ioc->scsiio_depth; i++) {
		if (ioc->scsi_lookup[i].scmd == scmd) {
			smid = ioc->scsi_lookup[i].smid;
			goto out;
		}
	}
 out:
	spin_unlock_irqrestore(&ioc->scsi_lookup_lock, flags);
	return smid;
}

/**
 * _scsih_scsi_lookup_find_by_target - search for matching channel:id
 * @ioc: per adapter object
 * @id: target id
 * @channel: channel
 * Context: This function will acquire ioc->scsi_lookup_lock.
 *
 * This will search for a matching channel:id in the scsi_lookup array,
 * returning 1 if found.
 */
static u8
_scsih_scsi_lookup_find_by_target(struct MPT2SAS_ADAPTER *ioc, int id,
    int channel)
{
	u8 found;
	unsigned long	flags;
	int i;

	spin_lock_irqsave(&ioc->scsi_lookup_lock, flags);
	found = 0;
	for (i = 0 ; i < ioc->scsiio_depth; i++) {
		if (ioc->scsi_lookup[i].scmd &&
		    (ioc->scsi_lookup[i].scmd->device->id == id &&
		    ioc->scsi_lookup[i].scmd->device->channel == channel)) {
			found = 1;
			goto out;
		}
	}
 out:
	spin_unlock_irqrestore(&ioc->scsi_lookup_lock, flags);
	return found;
}

/**
 * _scsih_scsi_lookup_find_by_lun - search for matching channel:id:lun
 * @ioc: per adapter object
 * @id: target id
 * @lun: lun number
 * @channel: channel
 * Context: This function will acquire ioc->scsi_lookup_lock.
 *
 * This will search for a matching channel:id:lun in the scsi_lookup array,
 * returning 1 if found.
 */
static u8
_scsih_scsi_lookup_find_by_lun(struct MPT2SAS_ADAPTER *ioc, int id,
    unsigned int lun, int channel)
{
	u8 found;
	unsigned long	flags;
	int i;

	spin_lock_irqsave(&ioc->scsi_lookup_lock, flags);
	found = 0;
	for (i = 0 ; i < ioc->scsiio_depth; i++) {
		if (ioc->scsi_lookup[i].scmd &&
		    (ioc->scsi_lookup[i].scmd->device->id == id &&
		    ioc->scsi_lookup[i].scmd->device->channel == channel &&
		    ioc->scsi_lookup[i].scmd->device->lun == lun)) {
			found = 1;
			goto out;
		}
	}
 out:
	spin_unlock_irqrestore(&ioc->scsi_lookup_lock, flags);
	return found;
}

/**
 * _scsih_get_chain_buffer_dma - obtain block of chains (dma address)
 * @ioc: per adapter object
 * @smid: system request message index
 *
 * Returns phys pointer to chain buffer.
 */
static dma_addr_t
_scsih_get_chain_buffer_dma(struct MPT2SAS_ADAPTER *ioc, u16 smid)
{
	return ioc->chain_dma + ((smid - 1) * (ioc->request_sz *
	    ioc->chains_needed_per_io));
}

/**
 * _scsih_get_chain_buffer - obtain block of chains assigned to a mf request
 * @ioc: per adapter object
 * @smid: system request message index
 *
 * Returns virt pointer to chain buffer.
 */
static void *
_scsih_get_chain_buffer(struct MPT2SAS_ADAPTER *ioc, u16 smid)
{
	return (void *)(ioc->chain + ((smid - 1) * (ioc->request_sz *
	    ioc->chains_needed_per_io)));
}

/**
 * _scsih_build_scatter_gather - main sg creation routine
 * @ioc: per adapter object
 * @scmd: scsi command
 * @smid: system request message index
 * Context: none.
 *
 * The main routine that builds scatter gather table from a given
 * scsi request sent via the .queuecommand main handler.
 *
 * Returns 0 success, anything else error
 */
static int
_scsih_build_scatter_gather(struct MPT2SAS_ADAPTER *ioc,
    struct scsi_cmnd *scmd, u16 smid)
{
	Mpi2SCSIIORequest_t *mpi_request;
	dma_addr_t chain_dma;
	struct scatterlist *sg_scmd;
	void *sg_local, *chain;
	u32 chain_offset;
	u32 chain_length;
	u32 chain_flags;
	u32 sges_left;
	u32 sges_in_segment;
	u32 sgl_flags;
	u32 sgl_flags_last_element;
	u32 sgl_flags_end_buffer;

	mpi_request = mpt2sas_base_get_msg_frame(ioc, smid);

	/* init scatter gather flags */
	sgl_flags = MPI2_SGE_FLAGS_SIMPLE_ELEMENT;
	if (scmd->sc_data_direction == DMA_TO_DEVICE)
		sgl_flags |= MPI2_SGE_FLAGS_HOST_TO_IOC;
	sgl_flags_last_element = (sgl_flags | MPI2_SGE_FLAGS_LAST_ELEMENT)
	    << MPI2_SGE_FLAGS_SHIFT;
	sgl_flags_end_buffer = (sgl_flags | MPI2_SGE_FLAGS_LAST_ELEMENT |
	    MPI2_SGE_FLAGS_END_OF_BUFFER | MPI2_SGE_FLAGS_END_OF_LIST)
	    << MPI2_SGE_FLAGS_SHIFT;
	sgl_flags = sgl_flags << MPI2_SGE_FLAGS_SHIFT;

	sg_scmd = scsi_sglist(scmd);
	sges_left = scsi_dma_map(scmd);
	if (!sges_left) {
		sdev_printk(KERN_ERR, scmd->device, "pci_map_sg"
		" failed: request for %d bytes!\n", scsi_bufflen(scmd));
		return -ENOMEM;
	}

	sg_local = &mpi_request->SGL;
	sges_in_segment = ioc->max_sges_in_main_message;
	if (sges_left <= sges_in_segment)
		goto fill_in_last_segment;

	mpi_request->ChainOffset = (offsetof(Mpi2SCSIIORequest_t, SGL) +
	    (sges_in_segment * ioc->sge_size))/4;

	/* fill in main message segment when there is a chain following */
	while (sges_in_segment) {
		if (sges_in_segment == 1)
			ioc->base_add_sg_single(sg_local,
			    sgl_flags_last_element | sg_dma_len(sg_scmd),
			    sg_dma_address(sg_scmd));
		else
			ioc->base_add_sg_single(sg_local, sgl_flags |
			    sg_dma_len(sg_scmd), sg_dma_address(sg_scmd));
		sg_scmd = sg_next(sg_scmd);
		sg_local += ioc->sge_size;
		sges_left--;
		sges_in_segment--;
	}

	/* initializing the chain flags and pointers */
	chain_flags = MPI2_SGE_FLAGS_CHAIN_ELEMENT << MPI2_SGE_FLAGS_SHIFT;
	chain = _scsih_get_chain_buffer(ioc, smid);
	chain_dma = _scsih_get_chain_buffer_dma(ioc, smid);
	do {
		sges_in_segment = (sges_left <=
		    ioc->max_sges_in_chain_message) ? sges_left :
		    ioc->max_sges_in_chain_message;
		chain_offset = (sges_left == sges_in_segment) ?
		    0 : (sges_in_segment * ioc->sge_size)/4;
		chain_length = sges_in_segment * ioc->sge_size;
		if (chain_offset) {
			chain_offset = chain_offset <<
			    MPI2_SGE_CHAIN_OFFSET_SHIFT;
			chain_length += ioc->sge_size;
		}
		ioc->base_add_sg_single(sg_local, chain_flags | chain_offset |
		    chain_length, chain_dma);
		sg_local = chain;
		if (!chain_offset)
			goto fill_in_last_segment;

		/* fill in chain segments */
		while (sges_in_segment) {
			if (sges_in_segment == 1)
				ioc->base_add_sg_single(sg_local,
				    sgl_flags_last_element |
				    sg_dma_len(sg_scmd),
				    sg_dma_address(sg_scmd));
			else
				ioc->base_add_sg_single(sg_local, sgl_flags |
				    sg_dma_len(sg_scmd),
				    sg_dma_address(sg_scmd));
			sg_scmd = sg_next(sg_scmd);
			sg_local += ioc->sge_size;
			sges_left--;
			sges_in_segment--;
		}

		chain_dma += ioc->request_sz;
		chain += ioc->request_sz;
	} while (1);


 fill_in_last_segment:

	/* fill the last segment */
	while (sges_left) {
		if (sges_left == 1)
			ioc->base_add_sg_single(sg_local, sgl_flags_end_buffer |
			    sg_dma_len(sg_scmd), sg_dma_address(sg_scmd));
		else
			ioc->base_add_sg_single(sg_local, sgl_flags |
			    sg_dma_len(sg_scmd), sg_dma_address(sg_scmd));
		sg_scmd = sg_next(sg_scmd);
		sg_local += ioc->sge_size;
		sges_left--;
	}

	return 0;
}

/**
 * _scsih_change_queue_depth - setting device queue depth
 * @sdev: scsi device struct
 * @qdepth: requested queue depth
 *
 * Returns queue depth.
 */
static int
_scsih_change_queue_depth(struct scsi_device *sdev, int qdepth)
{
	struct Scsi_Host *shost = sdev->host;
	int max_depth;
	int tag_type;
	struct MPT2SAS_ADAPTER *ioc = shost_priv(shost);
	struct MPT2SAS_DEVICE *sas_device_priv_data;
	struct MPT2SAS_TARGET *sas_target_priv_data;
	struct _sas_device *sas_device;
	unsigned long flags;

	max_depth = shost->can_queue;

	/* limit max device queue for SATA to 32 */
	sas_device_priv_data = sdev->hostdata;
	if (!sas_device_priv_data)
		goto not_sata;
	sas_target_priv_data = sas_device_priv_data->sas_target;
	if (!sas_target_priv_data)
		goto not_sata;
	if ((sas_target_priv_data->flags & MPT_TARGET_FLAGS_VOLUME))
		goto not_sata;
	spin_lock_irqsave(&ioc->sas_device_lock, flags);
	sas_device = mpt2sas_scsih_sas_device_find_by_sas_address(ioc,
	   sas_device_priv_data->sas_target->sas_address);
	spin_unlock_irqrestore(&ioc->sas_device_lock, flags);
	if (sas_device && sas_device->device_info &
	    MPI2_SAS_DEVICE_INFO_SATA_DEVICE)
		max_depth = MPT2SAS_SATA_QUEUE_DEPTH;

 not_sata:

	if (!sdev->tagged_supported)
		max_depth = 1;
	if (qdepth > max_depth)
		qdepth = max_depth;
	tag_type = (qdepth == 1) ? 0 : MSG_SIMPLE_TAG;
	scsi_adjust_queue_depth(sdev, tag_type, qdepth);

	if (sdev->inquiry_len > 7)
		sdev_printk(KERN_INFO, sdev, "qdepth(%d), tagged(%d), "
		"simple(%d), ordered(%d), scsi_level(%d), cmd_que(%d)\n",
		sdev->queue_depth, sdev->tagged_supported, sdev->simple_tags,
		sdev->ordered_tags, sdev->scsi_level,
		(sdev->inquiry[7] & 2) >> 1);

	return sdev->queue_depth;
}

/**
 * _scsih_change_queue_type - changing device queue tag type
 * @sdev: scsi device struct
 * @tag_type: requested tag type
 *
 * Returns queue tag type.
 */
static int
_scsih_change_queue_type(struct scsi_device *sdev, int tag_type)
{
	if (sdev->tagged_supported) {
		scsi_set_tag_type(sdev, tag_type);
		if (tag_type)
			scsi_activate_tcq(sdev, sdev->queue_depth);
		else
			scsi_deactivate_tcq(sdev, sdev->queue_depth);
	} else
		tag_type = 0;

	return tag_type;
}

/**
 * _scsih_target_alloc - target add routine
 * @starget: scsi target struct
 *
 * Returns 0 if ok. Any other return is assumed to be an error and
 * the device is ignored.
 */
static int
_scsih_target_alloc(struct scsi_target *starget)
{
	struct Scsi_Host *shost = dev_to_shost(&starget->dev);
	struct MPT2SAS_ADAPTER *ioc = shost_priv(shost);
	struct MPT2SAS_TARGET *sas_target_priv_data;
	struct _sas_device *sas_device;
	struct _raid_device *raid_device;
	unsigned long flags;
	struct sas_rphy *rphy;

	sas_target_priv_data = kzalloc(sizeof(struct scsi_target), GFP_KERNEL);
	if (!sas_target_priv_data)
		return -ENOMEM;

	starget->hostdata = sas_target_priv_data;
	sas_target_priv_data->starget = starget;
	sas_target_priv_data->handle = MPT2SAS_INVALID_DEVICE_HANDLE;

	/* RAID volumes */
	if (starget->channel == RAID_CHANNEL) {
		spin_lock_irqsave(&ioc->raid_device_lock, flags);
		raid_device = _scsih_raid_device_find_by_id(ioc, starget->id,
		    starget->channel);
		if (raid_device) {
			sas_target_priv_data->handle = raid_device->handle;
			sas_target_priv_data->sas_address = raid_device->wwid;
			sas_target_priv_data->flags |= MPT_TARGET_FLAGS_VOLUME;
			raid_device->starget = starget;
		}
		spin_unlock_irqrestore(&ioc->raid_device_lock, flags);
		return 0;
	}

	/* sas/sata devices */
	spin_lock_irqsave(&ioc->sas_device_lock, flags);
	rphy = dev_to_rphy(starget->dev.parent);
	sas_device = mpt2sas_scsih_sas_device_find_by_sas_address(ioc,
	   rphy->identify.sas_address);

	if (sas_device) {
		sas_target_priv_data->handle = sas_device->handle;
		sas_target_priv_data->sas_address = sas_device->sas_address;
		sas_device->starget = starget;
		sas_device->id = starget->id;
		sas_device->channel = starget->channel;
		if (sas_device->hidden_raid_component)
			sas_target_priv_data->flags |=
			    MPT_TARGET_FLAGS_RAID_COMPONENT;
	}
	spin_unlock_irqrestore(&ioc->sas_device_lock, flags);

	return 0;
}

/**
 * _scsih_target_destroy - target destroy routine
 * @starget: scsi target struct
 *
 * Returns nothing.
 */
static void
_scsih_target_destroy(struct scsi_target *starget)
{
	struct Scsi_Host *shost = dev_to_shost(&starget->dev);
	struct MPT2SAS_ADAPTER *ioc = shost_priv(shost);
	struct MPT2SAS_TARGET *sas_target_priv_data;
	struct _sas_device *sas_device;
	struct _raid_device *raid_device;
	unsigned long flags;
	struct sas_rphy *rphy;

	sas_target_priv_data = starget->hostdata;
	if (!sas_target_priv_data)
		return;

	if (starget->channel == RAID_CHANNEL) {
		spin_lock_irqsave(&ioc->raid_device_lock, flags);
		raid_device = _scsih_raid_device_find_by_id(ioc, starget->id,
		    starget->channel);
		if (raid_device) {
			raid_device->starget = NULL;
			raid_device->sdev = NULL;
		}
		spin_unlock_irqrestore(&ioc->raid_device_lock, flags);
		goto out;
	}

	spin_lock_irqsave(&ioc->sas_device_lock, flags);
	rphy = dev_to_rphy(starget->dev.parent);
	sas_device = mpt2sas_scsih_sas_device_find_by_sas_address(ioc,
	   rphy->identify.sas_address);
	if (sas_device && (sas_device->starget == starget) &&
	    (sas_device->id == starget->id) &&
	    (sas_device->channel == starget->channel))
		sas_device->starget = NULL;

	spin_unlock_irqrestore(&ioc->sas_device_lock, flags);

 out:
	kfree(sas_target_priv_data);
	starget->hostdata = NULL;
}

/**
 * _scsih_slave_alloc - device add routine
 * @sdev: scsi device struct
 *
 * Returns 0 if ok. Any other return is assumed to be an error and
 * the device is ignored.
 */
static int
_scsih_slave_alloc(struct scsi_device *sdev)
{
	struct Scsi_Host *shost;
	struct MPT2SAS_ADAPTER *ioc;
	struct MPT2SAS_TARGET *sas_target_priv_data;
	struct MPT2SAS_DEVICE *sas_device_priv_data;
	struct scsi_target *starget;
	struct _raid_device *raid_device;
	struct _sas_device *sas_device;
	unsigned long flags;

	sas_device_priv_data = kzalloc(sizeof(struct scsi_device), GFP_KERNEL);
	if (!sas_device_priv_data)
		return -ENOMEM;

	sas_device_priv_data->lun = sdev->lun;
	sas_device_priv_data->flags = MPT_DEVICE_FLAGS_INIT;

	starget = scsi_target(sdev);
	sas_target_priv_data = starget->hostdata;
	sas_target_priv_data->num_luns++;
	sas_device_priv_data->sas_target = sas_target_priv_data;
	sdev->hostdata = sas_device_priv_data;
	if ((sas_target_priv_data->flags & MPT_TARGET_FLAGS_RAID_COMPONENT))
		sdev->no_uld_attach = 1;

	shost = dev_to_shost(&starget->dev);
	ioc = shost_priv(shost);
	if (starget->channel == RAID_CHANNEL) {
		spin_lock_irqsave(&ioc->raid_device_lock, flags);
		raid_device = _scsih_raid_device_find_by_id(ioc,
		    starget->id, starget->channel);
		if (raid_device)
			raid_device->sdev = sdev; /* raid is single lun */
		spin_unlock_irqrestore(&ioc->raid_device_lock, flags);
	} else {
		/* set TLR bit for SSP devices */
		if (!(ioc->facts.IOCCapabilities &
		     MPI2_IOCFACTS_CAPABILITY_TLR))
			goto out;
		spin_lock_irqsave(&ioc->sas_device_lock, flags);
		sas_device = mpt2sas_scsih_sas_device_find_by_sas_address(ioc,
		   sas_device_priv_data->sas_target->sas_address);
		spin_unlock_irqrestore(&ioc->sas_device_lock, flags);
		if (sas_device && sas_device->device_info &
		    MPI2_SAS_DEVICE_INFO_SSP_TARGET)
			sas_device_priv_data->flags |= MPT_DEVICE_TLR_ON;
	}

 out:
	return 0;
}

/**
 * _scsih_slave_destroy - device destroy routine
 * @sdev: scsi device struct
 *
 * Returns nothing.
 */
static void
_scsih_slave_destroy(struct scsi_device *sdev)
{
	struct MPT2SAS_TARGET *sas_target_priv_data;
	struct scsi_target *starget;

	if (!sdev->hostdata)
		return;

	starget = scsi_target(sdev);
	sas_target_priv_data = starget->hostdata;
	sas_target_priv_data->num_luns--;
	kfree(sdev->hostdata);
	sdev->hostdata = NULL;
}

/**
 * _scsih_display_sata_capabilities - sata capabilities
 * @ioc: per adapter object
 * @sas_device: the sas_device object
 * @sdev: scsi device struct
 */
static void
_scsih_display_sata_capabilities(struct MPT2SAS_ADAPTER *ioc,
    struct _sas_device *sas_device, struct scsi_device *sdev)
{
	Mpi2ConfigReply_t mpi_reply;
	Mpi2SasDevicePage0_t sas_device_pg0;
	u32 ioc_status;
	u16 flags;
	u32 device_info;

	if ((mpt2sas_config_get_sas_device_pg0(ioc, &mpi_reply, &sas_device_pg0,
	    MPI2_SAS_DEVICE_PGAD_FORM_HANDLE, sas_device->handle))) {
		printk(MPT2SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		return;
	}

	ioc_status = le16_to_cpu(mpi_reply.IOCStatus) &
	    MPI2_IOCSTATUS_MASK;
	if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
		printk(MPT2SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		return;
	}

	flags = le16_to_cpu(sas_device_pg0.Flags);
	device_info = le16_to_cpu(sas_device_pg0.DeviceInfo);

	sdev_printk(KERN_INFO, sdev,
	    "atapi(%s), ncq(%s), asyn_notify(%s), smart(%s), fua(%s), "
	    "sw_preserve(%s)\n",
	    (device_info & MPI2_SAS_DEVICE_INFO_ATAPI_DEVICE) ? "y" : "n",
	    (flags & MPI2_SAS_DEVICE0_FLAGS_SATA_NCQ_SUPPORTED) ? "y" : "n",
	    (flags & MPI2_SAS_DEVICE0_FLAGS_SATA_ASYNCHRONOUS_NOTIFY) ? "y" :
	    "n",
	    (flags & MPI2_SAS_DEVICE0_FLAGS_SATA_SMART_SUPPORTED) ? "y" : "n",
	    (flags & MPI2_SAS_DEVICE0_FLAGS_SATA_FUA_SUPPORTED) ? "y" : "n",
	    (flags & MPI2_SAS_DEVICE0_FLAGS_SATA_SW_PRESERVE) ? "y" : "n");
}

/**
 * _scsih_get_volume_capabilities - volume capabilities
 * @ioc: per adapter object
 * @sas_device: the raid_device object
 */
static void
_scsih_get_volume_capabilities(struct MPT2SAS_ADAPTER *ioc,
    struct _raid_device *raid_device)
{
	Mpi2RaidVolPage0_t *vol_pg0;
	Mpi2RaidPhysDiskPage0_t pd_pg0;
	Mpi2SasDevicePage0_t sas_device_pg0;
	Mpi2ConfigReply_t mpi_reply;
	u16 sz;
	u8 num_pds;

	if ((mpt2sas_config_get_number_pds(ioc, raid_device->handle,
	    &num_pds)) || !num_pds) {
		printk(MPT2SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		return;
	}

	raid_device->num_pds = num_pds;
	sz = offsetof(Mpi2RaidVolPage0_t, PhysDisk) + (num_pds *
	    sizeof(Mpi2RaidVol0PhysDisk_t));
	vol_pg0 = kzalloc(sz, GFP_KERNEL);
	if (!vol_pg0) {
		printk(MPT2SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		return;
	}

	if ((mpt2sas_config_get_raid_volume_pg0(ioc, &mpi_reply, vol_pg0,
	     MPI2_RAID_VOLUME_PGAD_FORM_HANDLE, raid_device->handle, sz))) {
		printk(MPT2SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		kfree(vol_pg0);
		return;
	}

	raid_device->volume_type = vol_pg0->VolumeType;

	/* figure out what the underlying devices are by
	 * obtaining the device_info bits for the 1st device
	 */
	if (!(mpt2sas_config_get_phys_disk_pg0(ioc, &mpi_reply,
	    &pd_pg0, MPI2_PHYSDISK_PGAD_FORM_PHYSDISKNUM,
	    vol_pg0->PhysDisk[0].PhysDiskNum))) {
		if (!(mpt2sas_config_get_sas_device_pg0(ioc, &mpi_reply,
		    &sas_device_pg0, MPI2_SAS_DEVICE_PGAD_FORM_HANDLE,
		    le16_to_cpu(pd_pg0.DevHandle)))) {
			raid_device->device_info =
			    le32_to_cpu(sas_device_pg0.DeviceInfo);
		}
	}

	kfree(vol_pg0);
}

/**
 * _scsih_slave_configure - device configure routine.
 * @sdev: scsi device struct
 *
 * Returns 0 if ok. Any other return is assumed to be an error and
 * the device is ignored.
 */
static int
_scsih_slave_configure(struct scsi_device *sdev)
{
	struct Scsi_Host *shost = sdev->host;
	struct MPT2SAS_ADAPTER *ioc = shost_priv(shost);
	struct MPT2SAS_DEVICE *sas_device_priv_data;
	struct MPT2SAS_TARGET *sas_target_priv_data;
	struct _sas_device *sas_device;
	struct _raid_device *raid_device;
	unsigned long flags;
	int qdepth;
	u8 ssp_target = 0;
	char *ds = "";
	char *r_level = "";

	qdepth = 1;
	sas_device_priv_data = sdev->hostdata;
	sas_device_priv_data->configured_lun = 1;
	sas_device_priv_data->flags &= ~MPT_DEVICE_FLAGS_INIT;
	sas_target_priv_data = sas_device_priv_data->sas_target;

	/* raid volume handling */
	if (sas_target_priv_data->flags & MPT_TARGET_FLAGS_VOLUME) {

		spin_lock_irqsave(&ioc->raid_device_lock, flags);
		raid_device = _scsih_raid_device_find_by_handle(ioc,
		     sas_target_priv_data->handle);
		spin_unlock_irqrestore(&ioc->raid_device_lock, flags);
		if (!raid_device) {
			printk(MPT2SAS_ERR_FMT "failure at %s:%d/%s()!\n",
			    ioc->name, __FILE__, __LINE__, __func__);
			return 0;
		}

		_scsih_get_volume_capabilities(ioc, raid_device);

		/* RAID Queue Depth Support
		 * IS volume = underlying qdepth of drive type, either
		 *    MPT2SAS_SAS_QUEUE_DEPTH or MPT2SAS_SATA_QUEUE_DEPTH
		 * IM/IME/R10 = 128 (MPT2SAS_RAID_QUEUE_DEPTH)
		 */
		if (raid_device->device_info &
		    MPI2_SAS_DEVICE_INFO_SSP_TARGET) {
			qdepth = MPT2SAS_SAS_QUEUE_DEPTH;
			ds = "SSP";
		} else {
			qdepth = MPT2SAS_SATA_QUEUE_DEPTH;
			 if (raid_device->device_info &
			    MPI2_SAS_DEVICE_INFO_SATA_DEVICE)
				ds = "SATA";
			else
				ds = "STP";
		}

		switch (raid_device->volume_type) {
		case MPI2_RAID_VOL_TYPE_RAID0:
			r_level = "RAID0";
			break;
		case MPI2_RAID_VOL_TYPE_RAID1E:
			qdepth = MPT2SAS_RAID_QUEUE_DEPTH;
			if (ioc->manu_pg10.OEMIdentifier &&
			    (ioc->manu_pg10.GenericFlags0 &
			    MFG10_GF0_R10_DISPLAY) &&
			    !(raid_device->num_pds % 2))
				r_level = "RAID10";
			else
				r_level = "RAID1E";
			break;
		case MPI2_RAID_VOL_TYPE_RAID1:
			qdepth = MPT2SAS_RAID_QUEUE_DEPTH;
			r_level = "RAID1";
			break;
		case MPI2_RAID_VOL_TYPE_RAID10:
			qdepth = MPT2SAS_RAID_QUEUE_DEPTH;
			r_level = "RAID10";
			break;
		case MPI2_RAID_VOL_TYPE_UNKNOWN:
		default:
			qdepth = MPT2SAS_RAID_QUEUE_DEPTH;
			r_level = "RAIDX";
			break;
		}

		sdev_printk(KERN_INFO, sdev, "%s: "
		    "handle(0x%04x), wwid(0x%016llx), pd_count(%d), type(%s)\n",
		    r_level, raid_device->handle,
		    (unsigned long long)raid_device->wwid,
		    raid_device->num_pds, ds);
		_scsih_change_queue_depth(sdev, qdepth);
		return 0;
	}

	/* non-raid handling */
	spin_lock_irqsave(&ioc->sas_device_lock, flags);
	sas_device = mpt2sas_scsih_sas_device_find_by_sas_address(ioc,
	   sas_device_priv_data->sas_target->sas_address);
	spin_unlock_irqrestore(&ioc->sas_device_lock, flags);
	if (sas_device) {
		if (sas_target_priv_data->flags &
		    MPT_TARGET_FLAGS_RAID_COMPONENT) {
			mpt2sas_config_get_volume_handle(ioc,
			    sas_device->handle, &sas_device->volume_handle);
			mpt2sas_config_get_volume_wwid(ioc,
			    sas_device->volume_handle,
			    &sas_device->volume_wwid);
		}
		if (sas_device->device_info & MPI2_SAS_DEVICE_INFO_SSP_TARGET) {
			qdepth = MPT2SAS_SAS_QUEUE_DEPTH;
			ssp_target = 1;
			ds = "SSP";
		} else {
			qdepth = MPT2SAS_SATA_QUEUE_DEPTH;
			if (sas_device->device_info &
			    MPI2_SAS_DEVICE_INFO_STP_TARGET)
				ds = "STP";
			else if (sas_device->device_info &
			    MPI2_SAS_DEVICE_INFO_SATA_DEVICE)
				ds = "SATA";
		}

		sdev_printk(KERN_INFO, sdev, "%s: handle(0x%04x), "
		    "sas_addr(0x%016llx), device_name(0x%016llx)\n",
		    ds, sas_device->handle,
		    (unsigned long long)sas_device->sas_address,
		    (unsigned long long)sas_device->device_name);
		sdev_printk(KERN_INFO, sdev, "%s: "
		    "enclosure_logical_id(0x%016llx), slot(%d)\n", ds,
		    (unsigned long long) sas_device->enclosure_logical_id,
		    sas_device->slot);

		if (!ssp_target)
			_scsih_display_sata_capabilities(ioc, sas_device, sdev);
	}

	_scsih_change_queue_depth(sdev, qdepth);

	if (ssp_target)
		sas_read_port_mode_page(sdev);
	return 0;
}

/**
 * _scsih_bios_param - fetch head, sector, cylinder info for a disk
 * @sdev: scsi device struct
 * @bdev: pointer to block device context
 * @capacity: device size (in 512 byte sectors)
 * @params: three element array to place output:
 *              params[0] number of heads (max 255)
 *              params[1] number of sectors (max 63)
 *              params[2] number of cylinders
 *
 * Return nothing.
 */
static int
_scsih_bios_param(struct scsi_device *sdev, struct block_device *bdev,
    sector_t capacity, int params[])
{
	int		heads;
	int		sectors;
	sector_t	cylinders;
	ulong 		dummy;

	heads = 64;
	sectors = 32;

	dummy = heads * sectors;
	cylinders = capacity;
	sector_div(cylinders, dummy);

	/*
	 * Handle extended translation size for logical drives
	 * > 1Gb
	 */
	if ((ulong)capacity >= 0x200000) {
		heads = 255;
		sectors = 63;
		dummy = heads * sectors;
		cylinders = capacity;
		sector_div(cylinders, dummy);
	}

	/* return result */
	params[0] = heads;
	params[1] = sectors;
	params[2] = cylinders;

	return 0;
}

/**
 * _scsih_response_code - translation of device response code
 * @ioc: per adapter object
 * @response_code: response code returned by the device
 *
 * Return nothing.
 */
static void
_scsih_response_code(struct MPT2SAS_ADAPTER *ioc, u8 response_code)
{
	char *desc;

	switch (response_code) {
	case MPI2_SCSITASKMGMT_RSP_TM_COMPLETE:
		desc = "task management request completed";
		break;
	case MPI2_SCSITASKMGMT_RSP_INVALID_FRAME:
		desc = "invalid frame";
		break;
	case MPI2_SCSITASKMGMT_RSP_TM_NOT_SUPPORTED:
		desc = "task management request not supported";
		break;
	case MPI2_SCSITASKMGMT_RSP_TM_FAILED:
		desc = "task management request failed";
		break;
	case MPI2_SCSITASKMGMT_RSP_TM_SUCCEEDED:
		desc = "task management request succeeded";
		break;
	case MPI2_SCSITASKMGMT_RSP_TM_INVALID_LUN:
		desc = "invalid lun";
		break;
	case 0xA:
		desc = "overlapped tag attempted";
		break;
	case MPI2_SCSITASKMGMT_RSP_IO_QUEUED_ON_IOC:
		desc = "task queued, however not sent to target";
		break;
	default:
		desc = "unknown";
		break;
	}
	printk(MPT2SAS_WARN_FMT "response_code(0x%01x): %s\n",
		ioc->name, response_code, desc);
}

/**
 * _scsih_tm_done - tm completion routine
 * @ioc: per adapter object
 * @smid: system request message index
 * @msix_index: MSIX table index supplied by the OS
 * @reply: reply message frame(lower 32bit addr)
 * Context: none.
 *
 * The callback handler when using scsih_issue_tm.
 *
 * Return 1 meaning mf should be freed from _base_interrupt
 *        0 means the mf is freed from this function.
 */
static u8
_scsih_tm_done(struct MPT2SAS_ADAPTER *ioc, u16 smid, u8 msix_index, u32 reply)
{
	MPI2DefaultReply_t *mpi_reply;

	if (ioc->tm_cmds.status == MPT2_CMD_NOT_USED)
		return 1;
	if (ioc->tm_cmds.smid != smid)
		return 1;
	ioc->tm_cmds.status |= MPT2_CMD_COMPLETE;
	mpi_reply =  mpt2sas_base_get_reply_virt_addr(ioc, reply);
	if (mpi_reply) {
		memcpy(ioc->tm_cmds.reply, mpi_reply, mpi_reply->MsgLength*4);
		ioc->tm_cmds.status |= MPT2_CMD_REPLY_VALID;
	}
	ioc->tm_cmds.status &= ~MPT2_CMD_PENDING;
	complete(&ioc->tm_cmds.done);
	return 1;
}

/**
 * mpt2sas_scsih_set_tm_flag - set per target tm_busy
 * @ioc: per adapter object
 * @handle: device handle
 *
 * During taskmangement request, we need to freeze the device queue.
 */
void
mpt2sas_scsih_set_tm_flag(struct MPT2SAS_ADAPTER *ioc, u16 handle)
{
	struct MPT2SAS_DEVICE *sas_device_priv_data;
	struct scsi_device *sdev;
	u8 skip = 0;

	shost_for_each_device(sdev, ioc->shost) {
		if (skip)
			continue;
		sas_device_priv_data = sdev->hostdata;
		if (!sas_device_priv_data)
			continue;
		if (sas_device_priv_data->sas_target->handle == handle) {
			sas_device_priv_data->sas_target->tm_busy = 1;
			skip = 1;
			ioc->ignore_loginfos = 1;
		}
	}
}

/**
 * mpt2sas_scsih_clear_tm_flag - clear per target tm_busy
 * @ioc: per adapter object
 * @handle: device handle
 *
 * During taskmangement request, we need to freeze the device queue.
 */
void
mpt2sas_scsih_clear_tm_flag(struct MPT2SAS_ADAPTER *ioc, u16 handle)
{
	struct MPT2SAS_DEVICE *sas_device_priv_data;
	struct scsi_device *sdev;
	u8 skip = 0;

	shost_for_each_device(sdev, ioc->shost) {
		if (skip)
			continue;
		sas_device_priv_data = sdev->hostdata;
		if (!sas_device_priv_data)
			continue;
		if (sas_device_priv_data->sas_target->handle == handle) {
			sas_device_priv_data->sas_target->tm_busy = 0;
			skip = 1;
			ioc->ignore_loginfos = 0;
		}
	}
}

/**
 * mpt2sas_scsih_issue_tm - main routine for sending tm requests
 * @ioc: per adapter struct
 * @device_handle: device handle
 * @lun: lun number
 * @type: MPI2_SCSITASKMGMT_TASKTYPE__XXX (defined in mpi2_init.h)
 * @smid_task: smid assigned to the task
 * @timeout: timeout in seconds
 * Context: The calling function needs to acquire the tm_cmds.mutex
 *
 * A generic API for sending task management requests to firmware.
 *
 * The ioc->tm_cmds.status flag should be MPT2_CMD_NOT_USED before calling
 * this API.
 *
 * The callback index is set inside `ioc->tm_cb_idx`.
 *
 * Return nothing.
 */
void
mpt2sas_scsih_issue_tm(struct MPT2SAS_ADAPTER *ioc, u16 handle, uint lun,
    u8 type, u16 smid_task, ulong timeout)
{
	Mpi2SCSITaskManagementRequest_t *mpi_request;
	Mpi2SCSITaskManagementReply_t *mpi_reply;
	u16 smid = 0;
	u32 ioc_state;
	unsigned long timeleft;

	if (ioc->tm_cmds.status != MPT2_CMD_NOT_USED) {
		printk(MPT2SAS_INFO_FMT "%s: tm_cmd busy!!!\n",
		    __func__, ioc->name);
		return;
	}

	if (ioc->shost_recovery) {
		printk(MPT2SAS_INFO_FMT "%s: host reset in progress!\n",
		    __func__, ioc->name);
		return;
	}

	ioc_state = mpt2sas_base_get_iocstate(ioc, 0);
	if (ioc_state & MPI2_DOORBELL_USED) {
		dhsprintk(ioc, printk(MPT2SAS_DEBUG_FMT "unexpected doorbell "
		    "active!\n", ioc->name));
		goto issue_host_reset;
	}

	if ((ioc_state & MPI2_IOC_STATE_MASK) == MPI2_IOC_STATE_FAULT) {
		mpt2sas_base_fault_info(ioc, ioc_state &
		    MPI2_DOORBELL_DATA_MASK);
		goto issue_host_reset;
	}

	smid = mpt2sas_base_get_smid_hpr(ioc, ioc->tm_cb_idx);
	if (!smid) {
		printk(MPT2SAS_ERR_FMT "%s: failed obtaining a smid\n",
		    ioc->name, __func__);
		return;
	}

	dtmprintk(ioc, printk(MPT2SAS_INFO_FMT "sending tm: handle(0x%04x),"
	    " task_type(0x%02x), smid(%d)\n", ioc->name, handle, type,
	    smid_task));
	ioc->tm_cmds.status = MPT2_CMD_PENDING;
	mpi_request = mpt2sas_base_get_msg_frame(ioc, smid);
	ioc->tm_cmds.smid = smid;
	memset(mpi_request, 0, sizeof(Mpi2SCSITaskManagementRequest_t));
	mpi_request->Function = MPI2_FUNCTION_SCSI_TASK_MGMT;
	mpi_request->DevHandle = cpu_to_le16(handle);
	mpi_request->TaskType = type;
	mpi_request->TaskMID = cpu_to_le16(smid_task);
	mpi_request->VP_ID = 0;  /* TODO */
	mpi_request->VF_ID = 0;
	int_to_scsilun(lun, (struct scsi_lun *)mpi_request->LUN);
	mpt2sas_scsih_set_tm_flag(ioc, handle);
	init_completion(&ioc->tm_cmds.done);
	mpt2sas_base_put_smid_hi_priority(ioc, smid);
	timeleft = wait_for_completion_timeout(&ioc->tm_cmds.done, timeout*HZ);
	mpt2sas_scsih_clear_tm_flag(ioc, handle);
	if (!(ioc->tm_cmds.status & MPT2_CMD_COMPLETE)) {
		printk(MPT2SAS_ERR_FMT "%s: timeout\n",
		    ioc->name, __func__);
		_debug_dump_mf(mpi_request,
		    sizeof(Mpi2SCSITaskManagementRequest_t)/4);
		if (!(ioc->tm_cmds.status & MPT2_CMD_RESET))
			goto issue_host_reset;
	}

	if (ioc->tm_cmds.status & MPT2_CMD_REPLY_VALID) {
		mpi_reply = ioc->tm_cmds.reply;
		dtmprintk(ioc, printk(MPT2SAS_INFO_FMT "complete tm: "
		    "ioc_status(0x%04x), loginfo(0x%08x), term_count(0x%08x)\n",
		    ioc->name, le16_to_cpu(mpi_reply->IOCStatus),
		    le32_to_cpu(mpi_reply->IOCLogInfo),
		    le32_to_cpu(mpi_reply->TerminationCount)));
		if (ioc->logging_level & MPT_DEBUG_TM)
			_scsih_response_code(ioc, mpi_reply->ResponseCode);
	}
	return;
 issue_host_reset:
	mpt2sas_base_hard_reset_handler(ioc, CAN_SLEEP, FORCE_BIG_HAMMER);
}

/**
 * _scsih_abort - eh threads main abort routine
 * @sdev: scsi device struct
 *
 * Returns SUCCESS if command aborted else FAILED
 */
static int
_scsih_abort(struct scsi_cmnd *scmd)
{
	struct MPT2SAS_ADAPTER *ioc = shost_priv(scmd->device->host);
	struct MPT2SAS_DEVICE *sas_device_priv_data;
	u16 smid;
	u16 handle;
	int r;
	struct scsi_cmnd *scmd_lookup;

	printk(MPT2SAS_INFO_FMT "attempting task abort! scmd(%p)\n",
	    ioc->name, scmd);
	scsi_print_command(scmd);

	sas_device_priv_data = scmd->device->hostdata;
	if (!sas_device_priv_data || !sas_device_priv_data->sas_target) {
		printk(MPT2SAS_INFO_FMT "device been deleted! scmd(%p)\n",
		    ioc->name, scmd);
		scmd->result = DID_NO_CONNECT << 16;
		scmd->scsi_done(scmd);
		r = SUCCESS;
		goto out;
	}

	/* search for the command */
	smid = _scsih_scsi_lookup_find_by_scmd(ioc, scmd);
	if (!smid) {
		scmd->result = DID_RESET << 16;
		r = SUCCESS;
		goto out;
	}

	/* for hidden raid components and volumes this is not supported */
	if (sas_device_priv_data->sas_target->flags &
	    MPT_TARGET_FLAGS_RAID_COMPONENT ||
	    sas_device_priv_data->sas_target->flags & MPT_TARGET_FLAGS_VOLUME) {
		scmd->result = DID_RESET << 16;
		r = FAILED;
		goto out;
	}

	mpt2sas_halt_firmware(ioc);

	mutex_lock(&ioc->tm_cmds.mutex);
	handle = sas_device_priv_data->sas_target->handle;
	mpt2sas_scsih_issue_tm(ioc, handle, sas_device_priv_data->lun,
	    MPI2_SCSITASKMGMT_TASKTYPE_ABORT_TASK, smid, 30);

	/* sanity check - see whether command actually completed */
	scmd_lookup = _scsih_scsi_lookup_get(ioc, smid);
	if (scmd_lookup && (scmd_lookup->serial_number == scmd->serial_number))
		r = FAILED;
	else
		r = SUCCESS;
	ioc->tm_cmds.status = MPT2_CMD_NOT_USED;
	mutex_unlock(&ioc->tm_cmds.mutex);

 out:
	printk(MPT2SAS_INFO_FMT "task abort: %s scmd(%p)\n",
	    ioc->name, ((r == SUCCESS) ? "SUCCESS" : "FAILED"), scmd);
	return r;
}

/**
 * _scsih_dev_reset - eh threads main device reset routine
 * @sdev: scsi device struct
 *
 * Returns SUCCESS if command aborted else FAILED
 */
static int
_scsih_dev_reset(struct scsi_cmnd *scmd)
{
	struct MPT2SAS_ADAPTER *ioc = shost_priv(scmd->device->host);
	struct MPT2SAS_DEVICE *sas_device_priv_data;
	struct _sas_device *sas_device;
	unsigned long flags;
	u16	handle;
	int r;

	printk(MPT2SAS_INFO_FMT "attempting device reset! scmd(%p)\n",
	    ioc->name, scmd);
	scsi_print_command(scmd);

	sas_device_priv_data = scmd->device->hostdata;
	if (!sas_device_priv_data || !sas_device_priv_data->sas_target) {
		printk(MPT2SAS_INFO_FMT "device been deleted! scmd(%p)\n",
		    ioc->name, scmd);
		scmd->result = DID_NO_CONNECT << 16;
		scmd->scsi_done(scmd);
		r = SUCCESS;
		goto out;
	}

	/* for hidden raid components obtain the volume_handle */
	handle = 0;
	if (sas_device_priv_data->sas_target->flags &
	    MPT_TARGET_FLAGS_RAID_COMPONENT) {
		spin_lock_irqsave(&ioc->sas_device_lock, flags);
		sas_device = _scsih_sas_device_find_by_handle(ioc,
		   sas_device_priv_data->sas_target->handle);
		if (sas_device)
			handle = sas_device->volume_handle;
		spin_unlock_irqrestore(&ioc->sas_device_lock, flags);
	} else
		handle = sas_device_priv_data->sas_target->handle;

	if (!handle) {
		scmd->result = DID_RESET << 16;
		r = FAILED;
		goto out;
	}

	mutex_lock(&ioc->tm_cmds.mutex);
	mpt2sas_scsih_issue_tm(ioc, handle, 0,
	    MPI2_SCSITASKMGMT_TASKTYPE_LOGICAL_UNIT_RESET, scmd->device->lun,
	    30);

	/*
	 *  sanity check see whether all commands to this device been
	 *  completed
	 */
	if (_scsih_scsi_lookup_find_by_lun(ioc, scmd->device->id,
	    scmd->device->lun, scmd->device->channel))
		r = FAILED;
	else
		r = SUCCESS;
	ioc->tm_cmds.status = MPT2_CMD_NOT_USED;
	mutex_unlock(&ioc->tm_cmds.mutex);

 out:
	printk(MPT2SAS_INFO_FMT "device reset: %s scmd(%p)\n",
	    ioc->name, ((r == SUCCESS) ? "SUCCESS" : "FAILED"), scmd);
	return r;
}

/**
 * _scsih_target_reset - eh threads main target reset routine
 * @sdev: scsi device struct
 *
 * Returns SUCCESS if command aborted else FAILED
 */
static int
_scsih_target_reset(struct scsi_cmnd *scmd)
{
	struct MPT2SAS_ADAPTER *ioc = shost_priv(scmd->device->host);
	struct MPT2SAS_DEVICE *sas_device_priv_data;
	struct _sas_device *sas_device;
	unsigned long flags;
	u16	handle;
	int r;

	printk(MPT2SAS_INFO_FMT "attempting target reset! scmd(%p)\n",
	    ioc->name, scmd);
	scsi_print_command(scmd);

	sas_device_priv_data = scmd->device->hostdata;
	if (!sas_device_priv_data || !sas_device_priv_data->sas_target) {
		printk(MPT2SAS_INFO_FMT "target been deleted! scmd(%p)\n",
		    ioc->name, scmd);
		scmd->result = DID_NO_CONNECT << 16;
		scmd->scsi_done(scmd);
		r = SUCCESS;
		goto out;
	}

	/* for hidden raid components obtain the volume_handle */
	handle = 0;
	if (sas_device_priv_data->sas_target->flags &
	    MPT_TARGET_FLAGS_RAID_COMPONENT) {
		spin_lock_irqsave(&ioc->sas_device_lock, flags);
		sas_device = _scsih_sas_device_find_by_handle(ioc,
		   sas_device_priv_data->sas_target->handle);
		if (sas_device)
			handle = sas_device->volume_handle;
		spin_unlock_irqrestore(&ioc->sas_device_lock, flags);
	} else
		handle = sas_device_priv_data->sas_target->handle;

	if (!handle) {
		scmd->result = DID_RESET << 16;
		r = FAILED;
		goto out;
	}

	mutex_lock(&ioc->tm_cmds.mutex);
	mpt2sas_scsih_issue_tm(ioc, handle, 0,
	    MPI2_SCSITASKMGMT_TASKTYPE_TARGET_RESET, 0, 30);

	/*
	 *  sanity check see whether all commands to this target been
	 *  completed
	 */
	if (_scsih_scsi_lookup_find_by_target(ioc, scmd->device->id,
	    scmd->device->channel))
		r = FAILED;
	else
		r = SUCCESS;
	ioc->tm_cmds.status = MPT2_CMD_NOT_USED;
	mutex_unlock(&ioc->tm_cmds.mutex);

 out:
	printk(MPT2SAS_INFO_FMT "target reset: %s scmd(%p)\n",
	    ioc->name, ((r == SUCCESS) ? "SUCCESS" : "FAILED"), scmd);
	return r;
}

/**
 * _scsih_host_reset - eh threads main host reset routine
 * @sdev: scsi device struct
 *
 * Returns SUCCESS if command aborted else FAILED
 */
static int
_scsih_host_reset(struct scsi_cmnd *scmd)
{
	struct MPT2SAS_ADAPTER *ioc = shost_priv(scmd->device->host);
	int r, retval;

	printk(MPT2SAS_INFO_FMT "attempting host reset! scmd(%p)\n",
	    ioc->name, scmd);
	scsi_print_command(scmd);

	retval = mpt2sas_base_hard_reset_handler(ioc, CAN_SLEEP,
	    FORCE_BIG_HAMMER);
	r = (retval < 0) ? FAILED : SUCCESS;
	printk(MPT2SAS_INFO_FMT "host reset: %s scmd(%p)\n",
	    ioc->name, ((r == SUCCESS) ? "SUCCESS" : "FAILED"), scmd);

	return r;
}

/**
 * _scsih_fw_event_add - insert and queue up fw_event
 * @ioc: per adapter object
 * @fw_event: object describing the event
 * Context: This function will acquire ioc->fw_event_lock.
 *
 * This adds the firmware event object into link list, then queues it up to
 * be processed from user context.
 *
 * Return nothing.
 */
static void
_scsih_fw_event_add(struct MPT2SAS_ADAPTER *ioc, struct fw_event_work *fw_event)
{
	unsigned long flags;

	if (ioc->firmware_event_thread == NULL)
		return;

	spin_lock_irqsave(&ioc->fw_event_lock, flags);
	list_add_tail(&fw_event->list, &ioc->fw_event_list);
	INIT_WORK(&fw_event->work, _firmware_event_work);
	queue_work(ioc->firmware_event_thread, &fw_event->work);
	spin_unlock_irqrestore(&ioc->fw_event_lock, flags);
}

/**
 * _scsih_fw_event_free - delete fw_event
 * @ioc: per adapter object
 * @fw_event: object describing the event
 * Context: This function will acquire ioc->fw_event_lock.
 *
 * This removes firmware event object from link list, frees associated memory.
 *
 * Return nothing.
 */
static void
_scsih_fw_event_free(struct MPT2SAS_ADAPTER *ioc, struct fw_event_work
    *fw_event)
{
	unsigned long flags;

	spin_lock_irqsave(&ioc->fw_event_lock, flags);
	list_del(&fw_event->list);
	kfree(fw_event->event_data);
	kfree(fw_event);
	spin_unlock_irqrestore(&ioc->fw_event_lock, flags);
}

/**
 * _scsih_fw_event_add - requeue an event
 * @ioc: per adapter object
 * @fw_event: object describing the event
 * Context: This function will acquire ioc->fw_event_lock.
 *
 * Return nothing.
 */
static void
_scsih_fw_event_requeue(struct MPT2SAS_ADAPTER *ioc, struct fw_event_work
    *fw_event, unsigned long delay)
{
	unsigned long flags;
	if (ioc->firmware_event_thread == NULL)
		return;

	spin_lock_irqsave(&ioc->fw_event_lock, flags);
	queue_work(ioc->firmware_event_thread, &fw_event->work);
	spin_unlock_irqrestore(&ioc->fw_event_lock, flags);
}

/**
 * _scsih_fw_event_off - turn flag off preventing event handling
 * @ioc: per adapter object
 *
 * Used to prevent handling of firmware events during adapter reset
 * driver unload.
 *
 * Return nothing.
 */
static void
_scsih_fw_event_off(struct MPT2SAS_ADAPTER *ioc)
{
	unsigned long flags;

	spin_lock_irqsave(&ioc->fw_event_lock, flags);
	ioc->fw_events_off = 1;
	spin_unlock_irqrestore(&ioc->fw_event_lock, flags);

}

/**
 * _scsih_fw_event_on - turn flag on allowing firmware event handling
 * @ioc: per adapter object
 *
 * Returns nothing.
 */
static void
_scsih_fw_event_on(struct MPT2SAS_ADAPTER *ioc)
{
	unsigned long flags;

	spin_lock_irqsave(&ioc->fw_event_lock, flags);
	ioc->fw_events_off = 0;
	spin_unlock_irqrestore(&ioc->fw_event_lock, flags);
}

/**
 * _scsih_ublock_io_device - set the device state to SDEV_RUNNING
 * @ioc: per adapter object
 * @handle: device handle
 *
 * During device pull we need to appropiately set the sdev state.
 */
static void
_scsih_ublock_io_device(struct MPT2SAS_ADAPTER *ioc, u16 handle)
{
	struct MPT2SAS_DEVICE *sas_device_priv_data;
	struct scsi_device *sdev;

	shost_for_each_device(sdev, ioc->shost) {
		sas_device_priv_data = sdev->hostdata;
		if (!sas_device_priv_data)
			continue;
		if (!sas_device_priv_data->block)
			continue;
		if (sas_device_priv_data->sas_target->handle == handle) {
			dewtprintk(ioc, sdev_printk(KERN_INFO, sdev,
			    MPT2SAS_INFO_FMT "SDEV_RUNNING: "
			    "handle(0x%04x)\n", ioc->name, handle));
			sas_device_priv_data->block = 0;
			scsi_internal_device_unblock(sdev);
		}
	}
}

/**
 * _scsih_block_io_device - set the device state to SDEV_BLOCK
 * @ioc: per adapter object
 * @handle: device handle
 *
 * During device pull we need to appropiately set the sdev state.
 */
static void
_scsih_block_io_device(struct MPT2SAS_ADAPTER *ioc, u16 handle)
{
	struct MPT2SAS_DEVICE *sas_device_priv_data;
	struct scsi_device *sdev;

	shost_for_each_device(sdev, ioc->shost) {
		sas_device_priv_data = sdev->hostdata;
		if (!sas_device_priv_data)
			continue;
		if (sas_device_priv_data->block)
			continue;
		if (sas_device_priv_data->sas_target->handle == handle) {
			dewtprintk(ioc, sdev_printk(KERN_INFO, sdev,
			    MPT2SAS_INFO_FMT "SDEV_BLOCK: "
			    "handle(0x%04x)\n", ioc->name, handle));
			sas_device_priv_data->block = 1;
			scsi_internal_device_block(sdev);
		}
	}
}

/**
 * _scsih_block_io_to_children_attached_to_ex
 * @ioc: per adapter object
 * @sas_expander: the sas_device object
 *
 * This routine set sdev state to SDEV_BLOCK for all devices
 * attached to this expander. This function called when expander is
 * pulled.
 */
static void
_scsih_block_io_to_children_attached_to_ex(struct MPT2SAS_ADAPTER *ioc,
    struct _sas_node *sas_expander)
{
	struct _sas_port *mpt2sas_port;
	struct _sas_device *sas_device;
	struct _sas_node *expander_sibling;
	unsigned long flags;

	if (!sas_expander)
		return;

	list_for_each_entry(mpt2sas_port,
	   &sas_expander->sas_port_list, port_list) {
		if (mpt2sas_port->remote_identify.device_type ==
		    SAS_END_DEVICE) {
			spin_lock_irqsave(&ioc->sas_device_lock, flags);
			sas_device =
			    mpt2sas_scsih_sas_device_find_by_sas_address(ioc,
			   mpt2sas_port->remote_identify.sas_address);
			spin_unlock_irqrestore(&ioc->sas_device_lock, flags);
			if (!sas_device)
				continue;
			_scsih_block_io_device(ioc, sas_device->handle);
		}
	}

	list_for_each_entry(mpt2sas_port,
	   &sas_expander->sas_port_list, port_list) {

		if (mpt2sas_port->remote_identify.device_type ==
		    MPI2_SAS_DEVICE_INFO_EDGE_EXPANDER ||
		    mpt2sas_port->remote_identify.device_type ==
		    MPI2_SAS_DEVICE_INFO_FANOUT_EXPANDER) {

			spin_lock_irqsave(&ioc->sas_node_lock, flags);
			expander_sibling =
			    mpt2sas_scsih_expander_find_by_sas_address(
			    ioc, mpt2sas_port->remote_identify.sas_address);
			spin_unlock_irqrestore(&ioc->sas_node_lock, flags);
			_scsih_block_io_to_children_attached_to_ex(ioc,
			    expander_sibling);
		}
	}
}

/**
 * _scsih_block_io_to_children_attached_directly
 * @ioc: per adapter object
 * @event_data: topology change event data
 *
 * This routine set sdev state to SDEV_BLOCK for all devices
 * direct attached during device pull.
 */
static void
_scsih_block_io_to_children_attached_directly(struct MPT2SAS_ADAPTER *ioc,
    Mpi2EventDataSasTopologyChangeList_t *event_data)
{
	int i;
	u16 handle;
	u16 reason_code;
	u8 phy_number;
	u8 link_rate;

	for (i = 0; i < event_data->NumEntries; i++) {
		handle = le16_to_cpu(event_data->PHY[i].AttachedDevHandle);
		if (!handle)
			continue;
		phy_number = event_data->StartPhyNum + i;
		reason_code = event_data->PHY[i].PhyStatus &
		    MPI2_EVENT_SAS_TOPO_RC_MASK;
		if (reason_code == MPI2_EVENT_SAS_TOPO_RC_DELAY_NOT_RESPONDING)
			_scsih_block_io_device(ioc, handle);
		if (reason_code == MPI2_EVENT_SAS_TOPO_RC_PHY_CHANGED) {
			link_rate = event_data->PHY[i].LinkRate >> 4;
			if (link_rate >= MPI2_SAS_NEG_LINK_RATE_1_5)
				_scsih_ublock_io_device(ioc, handle);
		}
	}
}

/**
 * _scsih_tm_tr_send - send task management request
 * @ioc: per adapter object
 * @handle: device handle
 * Context: interrupt time.
 *
 * This code is to initiate the device removal handshake protocal
 * with controller firmware.  This function will issue target reset
 * using high priority request queue.  It will send a sas iounit
 * controll request (MPI2_SAS_OP_REMOVE_DEVICE) from this completion.
 *
 * This is designed to send muliple task management request at the same
 * time to the fifo. If the fifo is full, we will append the request,
 * and process it in a future completion.
 */
static void
_scsih_tm_tr_send(struct MPT2SAS_ADAPTER *ioc, u16 handle)
{
	Mpi2SCSITaskManagementRequest_t *mpi_request;
	struct MPT2SAS_TARGET *sas_target_priv_data;
	u16 smid;
	struct _sas_device *sas_device;
	unsigned long flags;
	struct _tr_list *delayed_tr;

	if (ioc->shost_recovery) {
		printk(MPT2SAS_INFO_FMT "%s: host reset in progress!\n",
		    __func__, ioc->name);
		return;
	}

	spin_lock_irqsave(&ioc->sas_device_lock, flags);
	sas_device = _scsih_sas_device_find_by_handle(ioc, handle);
	spin_unlock_irqrestore(&ioc->sas_device_lock, flags);

	/* skip is hidden raid component */
	if (sas_device && sas_device->hidden_raid_component)
		return;

	smid = mpt2sas_base_get_smid_hpr(ioc, ioc->tm_tr_cb_idx);
	if (!smid) {
		delayed_tr = kzalloc(sizeof(*delayed_tr), GFP_ATOMIC);
		if (!delayed_tr)
			return;
		INIT_LIST_HEAD(&delayed_tr->list);
		delayed_tr->handle = handle;
		delayed_tr->state = MPT2SAS_REQ_SAS_CNTRL;
		list_add_tail(&delayed_tr->list,
		    &ioc->delayed_tr_list);
		if (sas_device && sas_device->starget) {
			dewtprintk(ioc, starget_printk(KERN_INFO,
			    sas_device->starget, "DELAYED:tr:handle(0x%04x), "
			    "(open)\n", handle));
		} else {
			dewtprintk(ioc, printk(MPT2SAS_INFO_FMT
			    "DELAYED:tr:handle(0x%04x), (open)\n",
			    ioc->name, handle));
		}
		return;
	}

	if (sas_device) {
		sas_device->state |= MPTSAS_STATE_TR_SEND;
		sas_device->state |= MPT2SAS_REQ_SAS_CNTRL;
		if (sas_device->starget && sas_device->starget->hostdata) {
			sas_target_priv_data = sas_device->starget->hostdata;
			sas_target_priv_data->tm_busy = 1;
			dewtprintk(ioc, starget_printk(KERN_INFO,
			    sas_device->starget, "tr:handle(0x%04x), (open)\n",
			    handle));
		}
	} else {
		dewtprintk(ioc, printk(MPT2SAS_INFO_FMT
		    "tr:handle(0x%04x), (open)\n", ioc->name, handle));
	}

	mpi_request = mpt2sas_base_get_msg_frame(ioc, smid);
	memset(mpi_request, 0, sizeof(Mpi2SCSITaskManagementRequest_t));
	mpi_request->Function = MPI2_FUNCTION_SCSI_TASK_MGMT;
	mpi_request->DevHandle = cpu_to_le16(handle);
	mpi_request->TaskType = MPI2_SCSITASKMGMT_TASKTYPE_TARGET_RESET;
	mpt2sas_base_put_smid_hi_priority(ioc, smid);
}



/**
 * _scsih_sas_control_complete - completion routine
 * @ioc: per adapter object
 * @smid: system request message index
 * @msix_index: MSIX table index supplied by the OS
 * @reply: reply message frame(lower 32bit addr)
 * Context: interrupt time.
 *
 * This is the sas iounit controll completion routine.
 * This code is part of the code to initiate the device removal
 * handshake protocal with controller firmware.
 *
 * Return 1 meaning mf should be freed from _base_interrupt
 *        0 means the mf is freed from this function.
 */
static u8
_scsih_sas_control_complete(struct MPT2SAS_ADAPTER *ioc, u16 smid,
    u8 msix_index, u32 reply)
{
	unsigned long flags;
	u16 handle;
	struct _sas_device *sas_device;
	Mpi2SasIoUnitControlReply_t *mpi_reply =
	    mpt2sas_base_get_reply_virt_addr(ioc, reply);

	handle = le16_to_cpu(mpi_reply->DevHandle);

	spin_lock_irqsave(&ioc->sas_device_lock, flags);
	sas_device = _scsih_sas_device_find_by_handle(ioc, handle);
	spin_unlock_irqrestore(&ioc->sas_device_lock, flags);

	if (sas_device) {
		sas_device->state |= MPTSAS_STATE_CNTRL_COMPLETE;
		if (sas_device->starget)
			dewtprintk(ioc, starget_printk(KERN_INFO,
			    sas_device->starget,
			    "sc_complete:handle(0x%04x), "
			    "ioc_status(0x%04x), loginfo(0x%08x)\n",
			    handle, le16_to_cpu(mpi_reply->IOCStatus),
			    le32_to_cpu(mpi_reply->IOCLogInfo)));
	} else {
		dewtprintk(ioc, printk(MPT2SAS_INFO_FMT
		    "sc_complete:handle(0x%04x), "
		    "ioc_status(0x%04x), loginfo(0x%08x)\n",
		    ioc->name, handle, le16_to_cpu(mpi_reply->IOCStatus),
		    le32_to_cpu(mpi_reply->IOCLogInfo)));
	}

	return 1;
}

/**
 * _scsih_tm_tr_complete -
 * @ioc: per adapter object
 * @smid: system request message index
 * @msix_index: MSIX table index supplied by the OS
 * @reply: reply message frame(lower 32bit addr)
 * Context: interrupt time.
 *
 * This is the target reset completion routine.
 * This code is part of the code to initiate the device removal
 * handshake protocal with controller firmware.
 * It will send a sas iounit controll request (MPI2_SAS_OP_REMOVE_DEVICE)
 *
 * Return 1 meaning mf should be freed from _base_interrupt
 *        0 means the mf is freed from this function.
 */
static u8
_scsih_tm_tr_complete(struct MPT2SAS_ADAPTER *ioc, u16 smid, u8 msix_index,
    u32 reply)
{
	unsigned long flags;
	u16 handle;
	struct _sas_device *sas_device;
	Mpi2SCSITaskManagementReply_t *mpi_reply =
	    mpt2sas_base_get_reply_virt_addr(ioc, reply);
	Mpi2SasIoUnitControlRequest_t *mpi_request;
	u16 smid_sas_ctrl;
	struct MPT2SAS_TARGET *sas_target_priv_data;
	struct _tr_list *delayed_tr;
	u8 rc;

	handle = le16_to_cpu(mpi_reply->DevHandle);
	spin_lock_irqsave(&ioc->sas_device_lock, flags);
	sas_device = _scsih_sas_device_find_by_handle(ioc, handle);
	spin_unlock_irqrestore(&ioc->sas_device_lock, flags);

	if (sas_device) {
		sas_device->state |= MPTSAS_STATE_TR_COMPLETE;
		if (sas_device->starget) {
			dewtprintk(ioc, starget_printk(KERN_INFO,
			    sas_device->starget, "tr_complete:handle(0x%04x), "
			    "(%s) ioc_status(0x%04x), loginfo(0x%08x), "
			    "completed(%d)\n", sas_device->handle,
			    (sas_device->state & MPT2SAS_REQ_SAS_CNTRL) ?
			    "open" : "active",
			    le16_to_cpu(mpi_reply->IOCStatus),
			    le32_to_cpu(mpi_reply->IOCLogInfo),
			    le32_to_cpu(mpi_reply->TerminationCount)));
			if (sas_device->starget->hostdata) {
				sas_target_priv_data =
				    sas_device->starget->hostdata;
				sas_target_priv_data->tm_busy = 0;
			}
		}
	} else {
		dewtprintk(ioc, printk(MPT2SAS_INFO_FMT
		    "tr_complete:handle(0x%04x), (open) ioc_status(0x%04x), "
		    "loginfo(0x%08x), completed(%d)\n", ioc->name,
		    handle, le16_to_cpu(mpi_reply->IOCStatus),
		    le32_to_cpu(mpi_reply->IOCLogInfo),
		    le32_to_cpu(mpi_reply->TerminationCount)));
	}

	if (!list_empty(&ioc->delayed_tr_list)) {
		delayed_tr = list_entry(ioc->delayed_tr_list.next,
		    struct _tr_list, list);
		mpt2sas_base_free_smid(ioc, smid);
		if (delayed_tr->state & MPT2SAS_REQ_SAS_CNTRL)
			_scsih_tm_tr_send(ioc, delayed_tr->handle);
		list_del(&delayed_tr->list);
		kfree(delayed_tr);
		rc = 0; /* tells base_interrupt not to free mf */
	} else
		rc = 1;

	if (sas_device && !(sas_device->state & MPT2SAS_REQ_SAS_CNTRL))
		return rc;

	if (ioc->shost_recovery) {
		printk(MPT2SAS_INFO_FMT "%s: host reset in progress!\n",
		    __func__, ioc->name);
		return rc;
	}

	smid_sas_ctrl = mpt2sas_base_get_smid(ioc, ioc->tm_sas_control_cb_idx);
	if (!smid_sas_ctrl) {
		printk(MPT2SAS_ERR_FMT "%s: failed obtaining a smid\n",
		    ioc->name, __func__);
		return rc;
	}

	if (sas_device)
		sas_device->state |= MPTSAS_STATE_CNTRL_SEND;

	mpi_request = mpt2sas_base_get_msg_frame(ioc, smid_sas_ctrl);
	memset(mpi_request, 0, sizeof(Mpi2SasIoUnitControlRequest_t));
	mpi_request->Function = MPI2_FUNCTION_SAS_IO_UNIT_CONTROL;
	mpi_request->Operation = MPI2_SAS_OP_REMOVE_DEVICE;
	mpi_request->DevHandle = mpi_reply->DevHandle;
	mpt2sas_base_put_smid_default(ioc, smid_sas_ctrl);
	return rc;
}

/**
 * _scsih_check_topo_delete_events - sanity check on topo events
 * @ioc: per adapter object
 * @event_data: the event data payload
 *
 * This routine added to better handle cable breaker.
 *
 * This handles the case where driver recieves multiple expander
 * add and delete events in a single shot.  When there is a delete event
 * the routine will void any pending add events waiting in the event queue.
 *
 * Return nothing.
 */
static void
_scsih_check_topo_delete_events(struct MPT2SAS_ADAPTER *ioc,
    Mpi2EventDataSasTopologyChangeList_t *event_data)
{
	struct fw_event_work *fw_event;
	Mpi2EventDataSasTopologyChangeList_t *local_event_data;
	u16 expander_handle;
	struct _sas_node *sas_expander;
	unsigned long flags;
	int i, reason_code;
	u16 handle;

	for (i = 0 ; i < event_data->NumEntries; i++) {
		if (event_data->PHY[i].PhyStatus &
		    MPI2_EVENT_SAS_TOPO_PHYSTATUS_VACANT)
			continue;
		handle = le16_to_cpu(event_data->PHY[i].AttachedDevHandle);
		if (!handle)
			continue;
		reason_code = event_data->PHY[i].PhyStatus &
		    MPI2_EVENT_SAS_TOPO_RC_MASK;
		if (reason_code == MPI2_EVENT_SAS_TOPO_RC_TARG_NOT_RESPONDING)
			_scsih_tm_tr_send(ioc, handle);
	}

	expander_handle = le16_to_cpu(event_data->ExpanderDevHandle);
	if (expander_handle < ioc->sas_hba.num_phys) {
		_scsih_block_io_to_children_attached_directly(ioc, event_data);
		return;
	}

	if (event_data->ExpStatus == MPI2_EVENT_SAS_TOPO_ES_DELAY_NOT_RESPONDING
	 || event_data->ExpStatus == MPI2_EVENT_SAS_TOPO_ES_NOT_RESPONDING) {
		spin_lock_irqsave(&ioc->sas_node_lock, flags);
		sas_expander = mpt2sas_scsih_expander_find_by_handle(ioc,
		    expander_handle);
		spin_unlock_irqrestore(&ioc->sas_node_lock, flags);
		_scsih_block_io_to_children_attached_to_ex(ioc, sas_expander);
	} else if (event_data->ExpStatus == MPI2_EVENT_SAS_TOPO_ES_RESPONDING)
		_scsih_block_io_to_children_attached_directly(ioc, event_data);

	if (event_data->ExpStatus != MPI2_EVENT_SAS_TOPO_ES_NOT_RESPONDING)
		return;

	/* mark ignore flag for pending events */
	spin_lock_irqsave(&ioc->fw_event_lock, flags);
	list_for_each_entry(fw_event, &ioc->fw_event_list, list) {
		if (fw_event->event != MPI2_EVENT_SAS_TOPOLOGY_CHANGE_LIST ||
		    fw_event->ignore)
			continue;
		local_event_data = fw_event->event_data;
		if (local_event_data->ExpStatus ==
		    MPI2_EVENT_SAS_TOPO_ES_ADDED ||
		    local_event_data->ExpStatus ==
		    MPI2_EVENT_SAS_TOPO_ES_RESPONDING) {
			if (le16_to_cpu(local_event_data->ExpanderDevHandle) ==
			    expander_handle) {
				dewtprintk(ioc, printk(MPT2SAS_DEBUG_FMT
				    "setting ignoring flag\n", ioc->name));
				fw_event->ignore = 1;
			}
		}
	}
	spin_unlock_irqrestore(&ioc->fw_event_lock, flags);
}

/**
 * _scsih_flush_running_cmds - completing outstanding commands.
 * @ioc: per adapter object
 *
 * The flushing out of all pending scmd commands following host reset,
 * where all IO is dropped to the floor.
 *
 * Return nothing.
 */
static void
_scsih_flush_running_cmds(struct MPT2SAS_ADAPTER *ioc)
{
	struct scsi_cmnd *scmd;
	u16 smid;
	u16 count = 0;

	for (smid = 1; smid <= ioc->scsiio_depth; smid++) {
		scmd = _scsih_scsi_lookup_get(ioc, smid);
		if (!scmd)
			continue;
		count++;
		mpt2sas_base_free_smid(ioc, smid);
		scsi_dma_unmap(scmd);
		scmd->result = DID_RESET << 16;
		scmd->scsi_done(scmd);
	}
	dtmprintk(ioc, printk(MPT2SAS_INFO_FMT "completing %d cmds\n",
	    ioc->name, count));
}

/**
 * _scsih_setup_eedp - setup MPI request for EEDP transfer
 * @scmd: pointer to scsi command object
 * @mpi_request: pointer to the SCSI_IO reqest message frame
 *
 * Supporting protection 1 and 3.
 *
 * Returns nothing
 */
static void
_scsih_setup_eedp(struct scsi_cmnd *scmd, Mpi2SCSIIORequest_t *mpi_request)
{
	u16 eedp_flags;
	unsigned char prot_op = scsi_get_prot_op(scmd);
	unsigned char prot_type = scsi_get_prot_type(scmd);

	if (prot_type == SCSI_PROT_DIF_TYPE0 ||
	   prot_type == SCSI_PROT_DIF_TYPE2 ||
	   prot_op == SCSI_PROT_NORMAL)
		return;

	if (prot_op ==  SCSI_PROT_READ_STRIP)
		eedp_flags = MPI2_SCSIIO_EEDPFLAGS_CHECK_REMOVE_OP;
	else if (prot_op ==  SCSI_PROT_WRITE_INSERT)
		eedp_flags = MPI2_SCSIIO_EEDPFLAGS_INSERT_OP;
	else
		return;

	mpi_request->EEDPBlockSize = scmd->device->sector_size;

	switch (prot_type) {
	case SCSI_PROT_DIF_TYPE1:

		/*
		* enable ref/guard checking
		* auto increment ref tag
		*/
		mpi_request->EEDPFlags = eedp_flags |
		    MPI2_SCSIIO_EEDPFLAGS_INC_PRI_REFTAG |
		    MPI2_SCSIIO_EEDPFLAGS_CHECK_REFTAG |
		    MPI2_SCSIIO_EEDPFLAGS_CHECK_GUARD;
		mpi_request->CDB.EEDP32.PrimaryReferenceTag =
		    cpu_to_be32(scsi_get_lba(scmd));

		break;

	case SCSI_PROT_DIF_TYPE3:

		/*
		* enable guard checking
		*/
		mpi_request->EEDPFlags = eedp_flags |
		    MPI2_SCSIIO_EEDPFLAGS_CHECK_GUARD;

		break;
	}
}

/**
 * _scsih_eedp_error_handling - return sense code for EEDP errors
 * @scmd: pointer to scsi command object
 * @ioc_status: ioc status
 *
 * Returns nothing
 */
static void
_scsih_eedp_error_handling(struct scsi_cmnd *scmd, u16 ioc_status)
{
	u8 ascq;
	u8 sk;
	u8 host_byte;

	switch (ioc_status) {
	case MPI2_IOCSTATUS_EEDP_GUARD_ERROR:
		ascq = 0x01;
		break;
	case MPI2_IOCSTATUS_EEDP_APP_TAG_ERROR:
		ascq = 0x02;
		break;
	case MPI2_IOCSTATUS_EEDP_REF_TAG_ERROR:
		ascq = 0x03;
		break;
	default:
		ascq = 0x00;
		break;
	}

	if (scmd->sc_data_direction == DMA_TO_DEVICE) {
		sk = ILLEGAL_REQUEST;
		host_byte = DID_ABORT;
	} else {
		sk = ABORTED_COMMAND;
		host_byte = DID_OK;
	}

	scsi_build_sense_buffer(0, scmd->sense_buffer, sk, 0x10, ascq);
	scmd->result = DRIVER_SENSE << 24 | (host_byte << 16) |
	    SAM_STAT_CHECK_CONDITION;
}

/**
 * _scsih_qcmd - main scsi request entry point
 * @scmd: pointer to scsi command object
 * @done: function pointer to be invoked on completion
 *
 * The callback index is set inside `ioc->scsi_io_cb_idx`.
 *
 * Returns 0 on success.  If there's a failure, return either:
 * SCSI_MLQUEUE_DEVICE_BUSY if the device queue is full, or
 * SCSI_MLQUEUE_HOST_BUSY if the entire host queue is full
 */
static int
_scsih_qcmd(struct scsi_cmnd *scmd, void (*done)(struct scsi_cmnd *))
{
	struct MPT2SAS_ADAPTER *ioc = shost_priv(scmd->device->host);
	struct MPT2SAS_DEVICE *sas_device_priv_data;
	struct MPT2SAS_TARGET *sas_target_priv_data;
	Mpi2SCSIIORequest_t *mpi_request;
	u32 mpi_control;
	u16 smid;

	scmd->scsi_done = done;
	sas_device_priv_data = scmd->device->hostdata;
	if (!sas_device_priv_data) {
		scmd->result = DID_NO_CONNECT << 16;
		scmd->scsi_done(scmd);
		return 0;
	}

	sas_target_priv_data = sas_device_priv_data->sas_target;
	if (!sas_target_priv_data || sas_target_priv_data->handle ==
	    MPT2SAS_INVALID_DEVICE_HANDLE || sas_target_priv_data->deleted) {
		scmd->result = DID_NO_CONNECT << 16;
		scmd->scsi_done(scmd);
		return 0;
	}

	/* see if we are busy with task managment stuff */
	if (sas_device_priv_data->block || sas_target_priv_data->tm_busy)
		return SCSI_MLQUEUE_DEVICE_BUSY;
	else if (ioc->shost_recovery || ioc->ioc_link_reset_in_progress)
		return SCSI_MLQUEUE_HOST_BUSY;

	if (scmd->sc_data_direction == DMA_FROM_DEVICE)
		mpi_control = MPI2_SCSIIO_CONTROL_READ;
	else if (scmd->sc_data_direction == DMA_TO_DEVICE)
		mpi_control = MPI2_SCSIIO_CONTROL_WRITE;
	else
		mpi_control = MPI2_SCSIIO_CONTROL_NODATATRANSFER;

	/* set tags */
	if (!(sas_device_priv_data->flags & MPT_DEVICE_FLAGS_INIT)) {
		if (scmd->device->tagged_supported) {
			if (scmd->device->ordered_tags)
				mpi_control |= MPI2_SCSIIO_CONTROL_ORDEREDQ;
			else
				mpi_control |= MPI2_SCSIIO_CONTROL_SIMPLEQ;
		} else
/* MPI Revision I (UNIT = 0xA) - removed MPI2_SCSIIO_CONTROL_UNTAGGED */
/*			mpi_control |= MPI2_SCSIIO_CONTROL_UNTAGGED;
 */
			mpi_control |= (0x500);

	} else
		mpi_control |= MPI2_SCSIIO_CONTROL_SIMPLEQ;

	if ((sas_device_priv_data->flags & MPT_DEVICE_TLR_ON))
		mpi_control |= MPI2_SCSIIO_CONTROL_TLR_ON;

	smid = mpt2sas_base_get_smid_scsiio(ioc, ioc->scsi_io_cb_idx, scmd);
	if (!smid) {
		printk(MPT2SAS_ERR_FMT "%s: failed obtaining a smid\n",
		    ioc->name, __func__);
		goto out;
	}
	mpi_request = mpt2sas_base_get_msg_frame(ioc, smid);
	memset(mpi_request, 0, sizeof(Mpi2SCSIIORequest_t));
	_scsih_setup_eedp(scmd, mpi_request);
	mpi_request->Function = MPI2_FUNCTION_SCSI_IO_REQUEST;
	if (sas_device_priv_data->sas_target->flags &
	    MPT_TARGET_FLAGS_RAID_COMPONENT)
		mpi_request->Function = MPI2_FUNCTION_RAID_SCSI_IO_PASSTHROUGH;
	else
		mpi_request->Function = MPI2_FUNCTION_SCSI_IO_REQUEST;
	mpi_request->DevHandle =
	    cpu_to_le16(sas_device_priv_data->sas_target->handle);
	mpi_request->DataLength = cpu_to_le32(scsi_bufflen(scmd));
	mpi_request->Control = cpu_to_le32(mpi_control);
	mpi_request->IoFlags = cpu_to_le16(scmd->cmd_len);
	mpi_request->MsgFlags = MPI2_SCSIIO_MSGFLAGS_SYSTEM_SENSE_ADDR;
	mpi_request->SenseBufferLength = SCSI_SENSE_BUFFERSIZE;
	mpi_request->SenseBufferLowAddress =
	    (u32)mpt2sas_base_get_sense_buffer_dma(ioc, smid);
	mpi_request->SGLOffset0 = offsetof(Mpi2SCSIIORequest_t, SGL) / 4;
	mpi_request->SGLFlags = cpu_to_le16(MPI2_SCSIIO_SGLFLAGS_TYPE_MPI +
	    MPI2_SCSIIO_SGLFLAGS_SYSTEM_ADDR);
	mpi_request->VF_ID = 0; /* TODO */
	mpi_request->VP_ID = 0;
	int_to_scsilun(sas_device_priv_data->lun, (struct scsi_lun *)
	    mpi_request->LUN);
	memcpy(mpi_request->CDB.CDB32, scmd->cmnd, scmd->cmd_len);

	if (!mpi_request->DataLength) {
		mpt2sas_base_build_zero_len_sge(ioc, &mpi_request->SGL);
	} else {
		if (_scsih_build_scatter_gather(ioc, scmd, smid)) {
			mpt2sas_base_free_smid(ioc, smid);
			goto out;
		}
	}

	mpt2sas_base_put_smid_scsi_io(ioc, smid,
	    sas_device_priv_data->sas_target->handle);
	return 0;

 out:
	return SCSI_MLQUEUE_HOST_BUSY;
}

/**
 * _scsih_normalize_sense - normalize descriptor and fixed format sense data
 * @sense_buffer: sense data returned by target
 * @data: normalized skey/asc/ascq
 *
 * Return nothing.
 */
static void
_scsih_normalize_sense(char *sense_buffer, struct sense_info *data)
{
	if ((sense_buffer[0] & 0x7F) >= 0x72) {
		/* descriptor format */
		data->skey = sense_buffer[1] & 0x0F;
		data->asc = sense_buffer[2];
		data->ascq = sense_buffer[3];
	} else {
		/* fixed format */
		data->skey = sense_buffer[2] & 0x0F;
		data->asc = sense_buffer[12];
		data->ascq = sense_buffer[13];
	}
}

#ifdef CONFIG_SCSI_MPT2SAS_LOGGING
/**
 * _scsih_scsi_ioc_info - translated non-succesfull SCSI_IO request
 * @ioc: per adapter object
 * @scmd: pointer to scsi command object
 * @mpi_reply: reply mf payload returned from firmware
 *
 * scsi_status - SCSI Status code returned from target device
 * scsi_state - state info associated with SCSI_IO determined by ioc
 * ioc_status - ioc supplied status info
 *
 * Return nothing.
 */
static void
_scsih_scsi_ioc_info(struct MPT2SAS_ADAPTER *ioc, struct scsi_cmnd *scmd,
    Mpi2SCSIIOReply_t *mpi_reply, u16 smid)
{
	u32 response_info;
	u8 *response_bytes;
	u16 ioc_status = le16_to_cpu(mpi_reply->IOCStatus) &
	    MPI2_IOCSTATUS_MASK;
	u8 scsi_state = mpi_reply->SCSIState;
	u8 scsi_status = mpi_reply->SCSIStatus;
	char *desc_ioc_state = NULL;
	char *desc_scsi_status = NULL;
	char *desc_scsi_state = ioc->tmp_string;
	u32 log_info = le32_to_cpu(mpi_reply->IOCLogInfo);

	if (log_info == 0x31170000)
		return;

	switch (ioc_status) {
	case MPI2_IOCSTATUS_SUCCESS:
		desc_ioc_state = "success";
		break;
	case MPI2_IOCSTATUS_INVALID_FUNCTION:
		desc_ioc_state = "invalid function";
		break;
	case MPI2_IOCSTATUS_SCSI_RECOVERED_ERROR:
		desc_ioc_state = "scsi recovered error";
		break;
	case MPI2_IOCSTATUS_SCSI_INVALID_DEVHANDLE:
		desc_ioc_state = "scsi invalid dev handle";
		break;
	case MPI2_IOCSTATUS_SCSI_DEVICE_NOT_THERE:
		desc_ioc_state = "scsi device not there";
		break;
	case MPI2_IOCSTATUS_SCSI_DATA_OVERRUN:
		desc_ioc_state = "scsi data overrun";
		break;
	case MPI2_IOCSTATUS_SCSI_DATA_UNDERRUN:
		desc_ioc_state = "scsi data underrun";
		break;
	case MPI2_IOCSTATUS_SCSI_IO_DATA_ERROR:
		desc_ioc_state = "scsi io data error";
		break;
	case MPI2_IOCSTATUS_SCSI_PROTOCOL_ERROR:
		desc_ioc_state = "scsi protocol error";
		break;
	case MPI2_IOCSTATUS_SCSI_TASK_TERMINATED:
		desc_ioc_state = "scsi task terminated";
		break;
	case MPI2_IOCSTATUS_SCSI_RESIDUAL_MISMATCH:
		desc_ioc_state = "scsi residual mismatch";
		break;
	case MPI2_IOCSTATUS_SCSI_TASK_MGMT_FAILED:
		desc_ioc_state = "scsi task mgmt failed";
		break;
	case MPI2_IOCSTATUS_SCSI_IOC_TERMINATED:
		desc_ioc_state = "scsi ioc terminated";
		break;
	case MPI2_IOCSTATUS_SCSI_EXT_TERMINATED:
		desc_ioc_state = "scsi ext terminated";
		break;
	case MPI2_IOCSTATUS_EEDP_GUARD_ERROR:
		desc_ioc_state = "eedp guard error";
		break;
	case MPI2_IOCSTATUS_EEDP_REF_TAG_ERROR:
		desc_ioc_state = "eedp ref tag error";
		break;
	case MPI2_IOCSTATUS_EEDP_APP_TAG_ERROR:
		desc_ioc_state = "eedp app tag error";
		break;
	default:
		desc_ioc_state = "unknown";
		break;
	}

	switch (scsi_status) {
	case MPI2_SCSI_STATUS_GOOD:
		desc_scsi_status = "good";
		break;
	case MPI2_SCSI_STATUS_CHECK_CONDITION:
		desc_scsi_status = "check condition";
		break;
	case MPI2_SCSI_STATUS_CONDITION_MET:
		desc_scsi_status = "condition met";
		break;
	case MPI2_SCSI_STATUS_BUSY:
		desc_scsi_status = "busy";
		break;
	case MPI2_SCSI_STATUS_INTERMEDIATE:
		desc_scsi_status = "intermediate";
		break;
	case MPI2_SCSI_STATUS_INTERMEDIATE_CONDMET:
		desc_scsi_status = "intermediate condmet";
		break;
	case MPI2_SCSI_STATUS_RESERVATION_CONFLICT:
		desc_scsi_status = "reservation conflict";
		break;
	case MPI2_SCSI_STATUS_COMMAND_TERMINATED:
		desc_scsi_status = "command terminated";
		break;
	case MPI2_SCSI_STATUS_TASK_SET_FULL:
		desc_scsi_status = "task set full";
		break;
	case MPI2_SCSI_STATUS_ACA_ACTIVE:
		desc_scsi_status = "aca active";
		break;
	case MPI2_SCSI_STATUS_TASK_ABORTED:
		desc_scsi_status = "task aborted";
		break;
	default:
		desc_scsi_status = "unknown";
		break;
	}

	desc_scsi_state[0] = '\0';
	if (!scsi_state)
		desc_scsi_state = " ";
	if (scsi_state & MPI2_SCSI_STATE_RESPONSE_INFO_VALID)
		strcat(desc_scsi_state, "response info ");
	if (scsi_state & MPI2_SCSI_STATE_TERMINATED)
		strcat(desc_scsi_state, "state terminated ");
	if (scsi_state & MPI2_SCSI_STATE_NO_SCSI_STATUS)
		strcat(desc_scsi_state, "no status ");
	if (scsi_state & MPI2_SCSI_STATE_AUTOSENSE_FAILED)
		strcat(desc_scsi_state, "autosense failed ");
	if (scsi_state & MPI2_SCSI_STATE_AUTOSENSE_VALID)
		strcat(desc_scsi_state, "autosense valid ");

	scsi_print_command(scmd);
	printk(MPT2SAS_WARN_FMT "\tdev handle(0x%04x), "
	    "ioc_status(%s)(0x%04x), smid(%d)\n", ioc->name,
	    le16_to_cpu(mpi_reply->DevHandle), desc_ioc_state,
		ioc_status, smid);
	printk(MPT2SAS_WARN_FMT "\trequest_len(%d), underflow(%d), "
	    "resid(%d)\n", ioc->name, scsi_bufflen(scmd), scmd->underflow,
	    scsi_get_resid(scmd));
	printk(MPT2SAS_WARN_FMT "\ttag(%d), transfer_count(%d), "
	    "sc->result(0x%08x)\n", ioc->name, le16_to_cpu(mpi_reply->TaskTag),
	    le32_to_cpu(mpi_reply->TransferCount), scmd->result);
	printk(MPT2SAS_WARN_FMT "\tscsi_status(%s)(0x%02x), "
	    "scsi_state(%s)(0x%02x)\n", ioc->name, desc_scsi_status,
	    scsi_status, desc_scsi_state, scsi_state);

	if (scsi_state & MPI2_SCSI_STATE_AUTOSENSE_VALID) {
		struct sense_info data;
		_scsih_normalize_sense(scmd->sense_buffer, &data);
		printk(MPT2SAS_WARN_FMT "\t[sense_key,asc,ascq]: "
		    "[0x%02x,0x%02x,0x%02x]\n", ioc->name, data.skey,
		    data.asc, data.ascq);
	}

	if (scsi_state & MPI2_SCSI_STATE_RESPONSE_INFO_VALID) {
		response_info = le32_to_cpu(mpi_reply->ResponseInfo);
		response_bytes = (u8 *)&response_info;
		_scsih_response_code(ioc, response_bytes[0]);
	}
}
#endif

/**
 * _scsih_smart_predicted_fault - illuminate Fault LED
 * @ioc: per adapter object
 * @handle: device handle
 *
 * Return nothing.
 */
static void
_scsih_smart_predicted_fault(struct MPT2SAS_ADAPTER *ioc, u16 handle)
{
	Mpi2SepReply_t mpi_reply;
	Mpi2SepRequest_t mpi_request;
	struct scsi_target *starget;
	struct MPT2SAS_TARGET *sas_target_priv_data;
	Mpi2EventNotificationReply_t *event_reply;
	Mpi2EventDataSasDeviceStatusChange_t *event_data;
	struct _sas_device *sas_device;
	ssize_t sz;
	unsigned long flags;

	/* only handle non-raid devices */
	spin_lock_irqsave(&ioc->sas_device_lock, flags);
	sas_device = _scsih_sas_device_find_by_handle(ioc, handle);
	if (!sas_device) {
		spin_unlock_irqrestore(&ioc->sas_device_lock, flags);
		return;
	}
	starget = sas_device->starget;
	sas_target_priv_data = starget->hostdata;

	if ((sas_target_priv_data->flags & MPT_TARGET_FLAGS_RAID_COMPONENT) ||
	   ((sas_target_priv_data->flags & MPT_TARGET_FLAGS_VOLUME))) {
		spin_unlock_irqrestore(&ioc->sas_device_lock, flags);
		return;
	}
	starget_printk(KERN_WARNING, starget, "predicted fault\n");
	spin_unlock_irqrestore(&ioc->sas_device_lock, flags);

	if (ioc->pdev->subsystem_vendor == PCI_VENDOR_ID_IBM) {
		memset(&mpi_request, 0, sizeof(Mpi2SepRequest_t));
		mpi_request.Function = MPI2_FUNCTION_SCSI_ENCLOSURE_PROCESSOR;
		mpi_request.Action = MPI2_SEP_REQ_ACTION_WRITE_STATUS;
		mpi_request.SlotStatus =
		    MPI2_SEP_REQ_SLOTSTATUS_PREDICTED_FAULT;
		mpi_request.DevHandle = cpu_to_le16(handle);
		mpi_request.Flags = MPI2_SEP_REQ_FLAGS_DEVHANDLE_ADDRESS;
		if ((mpt2sas_base_scsi_enclosure_processor(ioc, &mpi_reply,
		    &mpi_request)) != 0) {
			printk(MPT2SAS_ERR_FMT "failure at %s:%d/%s()!\n",
			    ioc->name, __FILE__, __LINE__, __func__);
			return;
		}

		if (mpi_reply.IOCStatus || mpi_reply.IOCLogInfo) {
			dewtprintk(ioc, printk(MPT2SAS_INFO_FMT
			    "enclosure_processor: ioc_status (0x%04x), "
			    "loginfo(0x%08x)\n", ioc->name,
			    le16_to_cpu(mpi_reply.IOCStatus),
			    le32_to_cpu(mpi_reply.IOCLogInfo)));
			return;
		}
	}

	/* insert into event log */
	sz = offsetof(Mpi2EventNotificationReply_t, EventData) +
	     sizeof(Mpi2EventDataSasDeviceStatusChange_t);
	event_reply = kzalloc(sz, GFP_KERNEL);
	if (!event_reply) {
		printk(MPT2SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		return;
	}

	event_reply->Function = MPI2_FUNCTION_EVENT_NOTIFICATION;
	event_reply->Event =
	    cpu_to_le16(MPI2_EVENT_SAS_DEVICE_STATUS_CHANGE);
	event_reply->MsgLength = sz/4;
	event_reply->EventDataLength =
	    cpu_to_le16(sizeof(Mpi2EventDataSasDeviceStatusChange_t)/4);
	event_data = (Mpi2EventDataSasDeviceStatusChange_t *)
	    event_reply->EventData;
	event_data->ReasonCode = MPI2_EVENT_SAS_DEV_STAT_RC_SMART_DATA;
	event_data->ASC = 0x5D;
	event_data->DevHandle = cpu_to_le16(handle);
	event_data->SASAddress = cpu_to_le64(sas_target_priv_data->sas_address);
	mpt2sas_ctl_add_to_event_log(ioc, event_reply);
	kfree(event_reply);
}

/**
 * _scsih_io_done - scsi request callback
 * @ioc: per adapter object
 * @smid: system request message index
 * @msix_index: MSIX table index supplied by the OS
 * @reply: reply message frame(lower 32bit addr)
 *
 * Callback handler when using _scsih_qcmd.
 *
 * Return 1 meaning mf should be freed from _base_interrupt
 *        0 means the mf is freed from this function.
 */
static u8
_scsih_io_done(struct MPT2SAS_ADAPTER *ioc, u16 smid, u8 msix_index, u32 reply)
{
	Mpi2SCSIIORequest_t *mpi_request;
	Mpi2SCSIIOReply_t *mpi_reply;
	struct scsi_cmnd *scmd;
	u16 ioc_status;
	u32 xfer_cnt;
	u8 scsi_state;
	u8 scsi_status;
	u32 log_info;
	struct MPT2SAS_DEVICE *sas_device_priv_data;
	u32 response_code = 0;

	mpi_reply = mpt2sas_base_get_reply_virt_addr(ioc, reply);
	scmd = _scsih_scsi_lookup_get(ioc, smid);
	if (scmd == NULL)
		return 1;

	mpi_request = mpt2sas_base_get_msg_frame(ioc, smid);

	if (mpi_reply == NULL) {
		scmd->result = DID_OK << 16;
		goto out;
	}

	sas_device_priv_data = scmd->device->hostdata;
	if (!sas_device_priv_data || !sas_device_priv_data->sas_target ||
	     sas_device_priv_data->sas_target->deleted) {
		scmd->result = DID_NO_CONNECT << 16;
		goto out;
	}

	/* turning off TLR */
	scsi_state = mpi_reply->SCSIState;
	if (scsi_state & MPI2_SCSI_STATE_RESPONSE_INFO_VALID)
		response_code =
		    le32_to_cpu(mpi_reply->ResponseInfo) & 0xFF;
	if (!sas_device_priv_data->tlr_snoop_check) {
		sas_device_priv_data->tlr_snoop_check++;
		if ((sas_device_priv_data->flags & MPT_DEVICE_TLR_ON) &&
		    response_code == MPI2_SCSITASKMGMT_RSP_INVALID_FRAME)
			sas_device_priv_data->flags &=
			    ~MPT_DEVICE_TLR_ON;
	}

	xfer_cnt = le32_to_cpu(mpi_reply->TransferCount);
	scsi_set_resid(scmd, scsi_bufflen(scmd) - xfer_cnt);
	ioc_status = le16_to_cpu(mpi_reply->IOCStatus);
	if (ioc_status & MPI2_IOCSTATUS_FLAG_LOG_INFO_AVAILABLE)
		log_info =  le32_to_cpu(mpi_reply->IOCLogInfo);
	else
		log_info = 0;
	ioc_status &= MPI2_IOCSTATUS_MASK;
	scsi_status = mpi_reply->SCSIStatus;

	if (ioc_status == MPI2_IOCSTATUS_SCSI_DATA_UNDERRUN && xfer_cnt == 0 &&
	    (scsi_status == MPI2_SCSI_STATUS_BUSY ||
	     scsi_status == MPI2_SCSI_STATUS_RESERVATION_CONFLICT ||
	     scsi_status == MPI2_SCSI_STATUS_TASK_SET_FULL)) {
		ioc_status = MPI2_IOCSTATUS_SUCCESS;
	}

	if (scsi_state & MPI2_SCSI_STATE_AUTOSENSE_VALID) {
		struct sense_info data;
		const void *sense_data = mpt2sas_base_get_sense_buffer(ioc,
		    smid);
		u32 sz = min_t(u32, SCSI_SENSE_BUFFERSIZE,
		    le32_to_cpu(mpi_reply->SenseCount));
		memcpy(scmd->sense_buffer, sense_data, sz);
		_scsih_normalize_sense(scmd->sense_buffer, &data);
		/* failure prediction threshold exceeded */
		if (data.asc == 0x5D)
			_scsih_smart_predicted_fault(ioc,
			    le16_to_cpu(mpi_reply->DevHandle));
	}

	switch (ioc_status) {
	case MPI2_IOCSTATUS_BUSY:
	case MPI2_IOCSTATUS_INSUFFICIENT_RESOURCES:
		scmd->result = SAM_STAT_BUSY;
		break;

	case MPI2_IOCSTATUS_SCSI_DEVICE_NOT_THERE:
		scmd->result = DID_NO_CONNECT << 16;
		break;

	case MPI2_IOCSTATUS_SCSI_IOC_TERMINATED:
		if (sas_device_priv_data->block) {
			scmd->result = DID_TRANSPORT_DISRUPTED << 16;
			goto out;
		}
	case MPI2_IOCSTATUS_SCSI_TASK_TERMINATED:
	case MPI2_IOCSTATUS_SCSI_EXT_TERMINATED:
		scmd->result = DID_RESET << 16;
		break;

	case MPI2_IOCSTATUS_SCSI_RESIDUAL_MISMATCH:
		if ((xfer_cnt == 0) || (scmd->underflow > xfer_cnt))
			scmd->result = DID_SOFT_ERROR << 16;
		else
			scmd->result = (DID_OK << 16) | scsi_status;
		break;

	case MPI2_IOCSTATUS_SCSI_DATA_UNDERRUN:
		scmd->result = (DID_OK << 16) | scsi_status;

		if ((scsi_state & MPI2_SCSI_STATE_AUTOSENSE_VALID))
			break;

		if (xfer_cnt < scmd->underflow) {
			if (scsi_status == SAM_STAT_BUSY)
				scmd->result = SAM_STAT_BUSY;
			else
				scmd->result = DID_SOFT_ERROR << 16;
		} else if (scsi_state & (MPI2_SCSI_STATE_AUTOSENSE_FAILED |
		     MPI2_SCSI_STATE_NO_SCSI_STATUS))
			scmd->result = DID_SOFT_ERROR << 16;
		else if (scsi_state & MPI2_SCSI_STATE_TERMINATED)
			scmd->result = DID_RESET << 16;
		else if (!xfer_cnt && scmd->cmnd[0] == REPORT_LUNS) {
			mpi_reply->SCSIState = MPI2_SCSI_STATE_AUTOSENSE_VALID;
			mpi_reply->SCSIStatus = SAM_STAT_CHECK_CONDITION;
			scmd->result = (DRIVER_SENSE << 24) |
			    SAM_STAT_CHECK_CONDITION;
			scmd->sense_buffer[0] = 0x70;
			scmd->sense_buffer[2] = ILLEGAL_REQUEST;
			scmd->sense_buffer[12] = 0x20;
			scmd->sense_buffer[13] = 0;
		}
		break;

	case MPI2_IOCSTATUS_SCSI_DATA_OVERRUN:
		scsi_set_resid(scmd, 0);
	case MPI2_IOCSTATUS_SCSI_RECOVERED_ERROR:
	case MPI2_IOCSTATUS_SUCCESS:
		scmd->result = (DID_OK << 16) | scsi_status;
		if (response_code ==
		    MPI2_SCSITASKMGMT_RSP_INVALID_FRAME ||
		    (scsi_state & (MPI2_SCSI_STATE_AUTOSENSE_FAILED |
		     MPI2_SCSI_STATE_NO_SCSI_STATUS)))
			scmd->result = DID_SOFT_ERROR << 16;
		else if (scsi_state & MPI2_SCSI_STATE_TERMINATED)
			scmd->result = DID_RESET << 16;
		break;

	case MPI2_IOCSTATUS_EEDP_GUARD_ERROR:
	case MPI2_IOCSTATUS_EEDP_REF_TAG_ERROR:
	case MPI2_IOCSTATUS_EEDP_APP_TAG_ERROR:
		_scsih_eedp_error_handling(scmd, ioc_status);
		break;
	case MPI2_IOCSTATUS_SCSI_PROTOCOL_ERROR:
	case MPI2_IOCSTATUS_INVALID_FUNCTION:
	case MPI2_IOCSTATUS_INVALID_SGL:
	case MPI2_IOCSTATUS_INTERNAL_ERROR:
	case MPI2_IOCSTATUS_INVALID_FIELD:
	case MPI2_IOCSTATUS_INVALID_STATE:
	case MPI2_IOCSTATUS_SCSI_IO_DATA_ERROR:
	case MPI2_IOCSTATUS_SCSI_TASK_MGMT_FAILED:
	default:
		scmd->result = DID_SOFT_ERROR << 16;
		break;

	}

#ifdef CONFIG_SCSI_MPT2SAS_LOGGING
	if (scmd->result && (ioc->logging_level & MPT_DEBUG_REPLY))
		_scsih_scsi_ioc_info(ioc , scmd, mpi_reply, smid);
#endif

 out:
	scsi_dma_unmap(scmd);
	scmd->scsi_done(scmd);
	return 1;
}

/**
 * _scsih_sas_host_refresh - refreshing sas host object contents
 * @ioc: per adapter object
 * Context: user
 *
 * During port enable, fw will send topology events for every device. Its
 * possible that the handles may change from the previous setting, so this
 * code keeping handles updating if changed.
 *
 * Return nothing.
 */
static void
_scsih_sas_host_refresh(struct MPT2SAS_ADAPTER *ioc)
{
	u16 sz;
	u16 ioc_status;
	int i;
	Mpi2ConfigReply_t mpi_reply;
	Mpi2SasIOUnitPage0_t *sas_iounit_pg0 = NULL;
	u16 attached_handle;

	dtmprintk(ioc, printk(MPT2SAS_INFO_FMT
	    "updating handles for sas_host(0x%016llx)\n",
	    ioc->name, (unsigned long long)ioc->sas_hba.sas_address));

	sz = offsetof(Mpi2SasIOUnitPage0_t, PhyData) + (ioc->sas_hba.num_phys
	    * sizeof(Mpi2SasIOUnit0PhyData_t));
	sas_iounit_pg0 = kzalloc(sz, GFP_KERNEL);
	if (!sas_iounit_pg0) {
		printk(MPT2SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		return;
	}

	if ((mpt2sas_config_get_sas_iounit_pg0(ioc, &mpi_reply,
	    sas_iounit_pg0, sz)) != 0)
		goto out;
	ioc_status = le16_to_cpu(mpi_reply.IOCStatus) & MPI2_IOCSTATUS_MASK;
	if (ioc_status != MPI2_IOCSTATUS_SUCCESS)
		goto out;
	for (i = 0; i < ioc->sas_hba.num_phys ; i++) {
		if (i == 0)
			ioc->sas_hba.handle = le16_to_cpu(sas_iounit_pg0->
			    PhyData[0].ControllerDevHandle);
		ioc->sas_hba.phy[i].handle = ioc->sas_hba.handle;
		attached_handle = le16_to_cpu(sas_iounit_pg0->PhyData[i].
		    AttachedDevHandle);
		mpt2sas_transport_update_links(ioc, ioc->sas_hba.sas_address,
		    attached_handle, i, sas_iounit_pg0->PhyData[i].
		    NegotiatedLinkRate >> 4);
	}
 out:
	kfree(sas_iounit_pg0);
}

/**
 * _scsih_sas_host_add - create sas host object
 * @ioc: per adapter object
 *
 * Creating host side data object, stored in ioc->sas_hba
 *
 * Return nothing.
 */
static void
_scsih_sas_host_add(struct MPT2SAS_ADAPTER *ioc)
{
	int i;
	Mpi2ConfigReply_t mpi_reply;
	Mpi2SasIOUnitPage0_t *sas_iounit_pg0 = NULL;
	Mpi2SasIOUnitPage1_t *sas_iounit_pg1 = NULL;
	Mpi2SasPhyPage0_t phy_pg0;
	Mpi2SasDevicePage0_t sas_device_pg0;
	Mpi2SasEnclosurePage0_t enclosure_pg0;
	u16 ioc_status;
	u16 sz;
	u16 device_missing_delay;

	mpt2sas_config_get_number_hba_phys(ioc, &ioc->sas_hba.num_phys);
	if (!ioc->sas_hba.num_phys) {
		printk(MPT2SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		return;
	}

	/* sas_iounit page 0 */
	sz = offsetof(Mpi2SasIOUnitPage0_t, PhyData) + (ioc->sas_hba.num_phys *
	    sizeof(Mpi2SasIOUnit0PhyData_t));
	sas_iounit_pg0 = kzalloc(sz, GFP_KERNEL);
	if (!sas_iounit_pg0) {
		printk(MPT2SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		return;
	}
	if ((mpt2sas_config_get_sas_iounit_pg0(ioc, &mpi_reply,
	    sas_iounit_pg0, sz))) {
		printk(MPT2SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		goto out;
	}
	ioc_status = le16_to_cpu(mpi_reply.IOCStatus) &
	    MPI2_IOCSTATUS_MASK;
	if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
		printk(MPT2SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		goto out;
	}

	/* sas_iounit page 1 */
	sz = offsetof(Mpi2SasIOUnitPage1_t, PhyData) + (ioc->sas_hba.num_phys *
	    sizeof(Mpi2SasIOUnit1PhyData_t));
	sas_iounit_pg1 = kzalloc(sz, GFP_KERNEL);
	if (!sas_iounit_pg1) {
		printk(MPT2SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		goto out;
	}
	if ((mpt2sas_config_get_sas_iounit_pg1(ioc, &mpi_reply,
	    sas_iounit_pg1, sz))) {
		printk(MPT2SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		goto out;
	}
	ioc_status = le16_to_cpu(mpi_reply.IOCStatus) &
	    MPI2_IOCSTATUS_MASK;
	if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
		printk(MPT2SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		goto out;
	}

	ioc->io_missing_delay =
	    le16_to_cpu(sas_iounit_pg1->IODeviceMissingDelay);
	device_missing_delay =
	    le16_to_cpu(sas_iounit_pg1->ReportDeviceMissingDelay);
	if (device_missing_delay & MPI2_SASIOUNIT1_REPORT_MISSING_UNIT_16)
		ioc->device_missing_delay = (device_missing_delay &
		    MPI2_SASIOUNIT1_REPORT_MISSING_TIMEOUT_MASK) * 16;
	else
		ioc->device_missing_delay = device_missing_delay &
		    MPI2_SASIOUNIT1_REPORT_MISSING_TIMEOUT_MASK;

	ioc->sas_hba.parent_dev = &ioc->shost->shost_gendev;
	ioc->sas_hba.phy = kcalloc(ioc->sas_hba.num_phys,
	    sizeof(struct _sas_phy), GFP_KERNEL);
	if (!ioc->sas_hba.phy) {
		printk(MPT2SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		goto out;
	}
	for (i = 0; i < ioc->sas_hba.num_phys ; i++) {
		if ((mpt2sas_config_get_phy_pg0(ioc, &mpi_reply, &phy_pg0,
		    i))) {
			printk(MPT2SAS_ERR_FMT "failure at %s:%d/%s()!\n",
			    ioc->name, __FILE__, __LINE__, __func__);
			goto out;
		}
		ioc_status = le16_to_cpu(mpi_reply.IOCStatus) &
		    MPI2_IOCSTATUS_MASK;
		if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
			printk(MPT2SAS_ERR_FMT "failure at %s:%d/%s()!\n",
			    ioc->name, __FILE__, __LINE__, __func__);
			goto out;
		}

		if (i == 0)
			ioc->sas_hba.handle = le16_to_cpu(sas_iounit_pg0->
			    PhyData[0].ControllerDevHandle);
		ioc->sas_hba.phy[i].handle = ioc->sas_hba.handle;
		ioc->sas_hba.phy[i].phy_id = i;
		mpt2sas_transport_add_host_phy(ioc, &ioc->sas_hba.phy[i],
		    phy_pg0, ioc->sas_hba.parent_dev);
	}
	if ((mpt2sas_config_get_sas_device_pg0(ioc, &mpi_reply, &sas_device_pg0,
	    MPI2_SAS_DEVICE_PGAD_FORM_HANDLE, ioc->sas_hba.handle))) {
		printk(MPT2SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		goto out;
	}
	ioc->sas_hba.enclosure_handle =
	    le16_to_cpu(sas_device_pg0.EnclosureHandle);
	ioc->sas_hba.sas_address = le64_to_cpu(sas_device_pg0.SASAddress);
	printk(MPT2SAS_INFO_FMT "host_add: handle(0x%04x), "
	    "sas_addr(0x%016llx), phys(%d)\n", ioc->name, ioc->sas_hba.handle,
	    (unsigned long long) ioc->sas_hba.sas_address,
	    ioc->sas_hba.num_phys) ;

	if (ioc->sas_hba.enclosure_handle) {
		if (!(mpt2sas_config_get_enclosure_pg0(ioc, &mpi_reply,
		    &enclosure_pg0,
		   MPI2_SAS_ENCLOS_PGAD_FORM_HANDLE,
		   ioc->sas_hba.enclosure_handle))) {
			ioc->sas_hba.enclosure_logical_id =
			    le64_to_cpu(enclosure_pg0.EnclosureLogicalID);
		}
	}

 out:
	kfree(sas_iounit_pg1);
	kfree(sas_iounit_pg0);
}

/**
 * _scsih_expander_add -  creating expander object
 * @ioc: per adapter object
 * @handle: expander handle
 *
 * Creating expander object, stored in ioc->sas_expander_list.
 *
 * Return 0 for success, else error.
 */
static int
_scsih_expander_add(struct MPT2SAS_ADAPTER *ioc, u16 handle)
{
	struct _sas_node *sas_expander;
	Mpi2ConfigReply_t mpi_reply;
	Mpi2ExpanderPage0_t expander_pg0;
	Mpi2ExpanderPage1_t expander_pg1;
	Mpi2SasEnclosurePage0_t enclosure_pg0;
	u32 ioc_status;
	u16 parent_handle;
	__le64 sas_address, sas_address_parent = 0;
	int i;
	unsigned long flags;
	struct _sas_port *mpt2sas_port = NULL;
	int rc = 0;

	if (!handle)
		return -1;

	if (ioc->shost_recovery)
		return -1;

	if ((mpt2sas_config_get_expander_pg0(ioc, &mpi_reply, &expander_pg0,
	    MPI2_SAS_EXPAND_PGAD_FORM_HNDL, handle))) {
		printk(MPT2SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		return -1;
	}

	ioc_status = le16_to_cpu(mpi_reply.IOCStatus) &
	    MPI2_IOCSTATUS_MASK;
	if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
		printk(MPT2SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		return -1;
	}

	/* handle out of order topology events */
	parent_handle = le16_to_cpu(expander_pg0.ParentDevHandle);
	if (_scsih_get_sas_address(ioc, parent_handle, &sas_address_parent)
	    != 0) {
		printk(MPT2SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		return -1;
	}
	if (sas_address_parent != ioc->sas_hba.sas_address) {
		spin_lock_irqsave(&ioc->sas_node_lock, flags);
		sas_expander = mpt2sas_scsih_expander_find_by_sas_address(ioc,
		    sas_address_parent);
		spin_unlock_irqrestore(&ioc->sas_node_lock, flags);
		if (!sas_expander) {
			rc = _scsih_expander_add(ioc, parent_handle);
			if (rc != 0)
				return rc;
		}
	}

	spin_lock_irqsave(&ioc->sas_node_lock, flags);
	sas_address = le64_to_cpu(expander_pg0.SASAddress);
	sas_expander = mpt2sas_scsih_expander_find_by_sas_address(ioc,
	    sas_address);
	spin_unlock_irqrestore(&ioc->sas_node_lock, flags);

	if (sas_expander)
		return 0;

	sas_expander = kzalloc(sizeof(struct _sas_node),
	    GFP_KERNEL);
	if (!sas_expander) {
		printk(MPT2SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		return -1;
	}

	sas_expander->handle = handle;
	sas_expander->num_phys = expander_pg0.NumPhys;
	sas_expander->sas_address_parent = sas_address_parent;
	sas_expander->sas_address = sas_address;

	printk(MPT2SAS_INFO_FMT "expander_add: handle(0x%04x),"
	    " parent(0x%04x), sas_addr(0x%016llx), phys(%d)\n", ioc->name,
	    handle, parent_handle, (unsigned long long)
	    sas_expander->sas_address, sas_expander->num_phys);

	if (!sas_expander->num_phys)
		goto out_fail;
	sas_expander->phy = kcalloc(sas_expander->num_phys,
	    sizeof(struct _sas_phy), GFP_KERNEL);
	if (!sas_expander->phy) {
		printk(MPT2SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		rc = -1;
		goto out_fail;
	}

	INIT_LIST_HEAD(&sas_expander->sas_port_list);
	mpt2sas_port = mpt2sas_transport_port_add(ioc, handle,
	    sas_address_parent);
	if (!mpt2sas_port) {
		printk(MPT2SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		rc = -1;
		goto out_fail;
	}
	sas_expander->parent_dev = &mpt2sas_port->rphy->dev;

	for (i = 0 ; i < sas_expander->num_phys ; i++) {
		if ((mpt2sas_config_get_expander_pg1(ioc, &mpi_reply,
		    &expander_pg1, i, handle))) {
			printk(MPT2SAS_ERR_FMT "failure at %s:%d/%s()!\n",
			    ioc->name, __FILE__, __LINE__, __func__);
			rc = -1;
			goto out_fail;
		}
		sas_expander->phy[i].handle = handle;
		sas_expander->phy[i].phy_id = i;

		if ((mpt2sas_transport_add_expander_phy(ioc,
		    &sas_expander->phy[i], expander_pg1,
		    sas_expander->parent_dev))) {
			printk(MPT2SAS_ERR_FMT "failure at %s:%d/%s()!\n",
			    ioc->name, __FILE__, __LINE__, __func__);
			rc = -1;
			goto out_fail;
		}
	}

	if (sas_expander->enclosure_handle) {
		if (!(mpt2sas_config_get_enclosure_pg0(ioc, &mpi_reply,
		    &enclosure_pg0, MPI2_SAS_ENCLOS_PGAD_FORM_HANDLE,
		   sas_expander->enclosure_handle))) {
			sas_expander->enclosure_logical_id =
			    le64_to_cpu(enclosure_pg0.EnclosureLogicalID);
		}
	}

	_scsih_expander_node_add(ioc, sas_expander);
	 return 0;

 out_fail:

	if (mpt2sas_port)
		mpt2sas_transport_port_remove(ioc, sas_expander->sas_address,
		    sas_address_parent);
	kfree(sas_expander);
	return rc;
}

/**
 * _scsih_expander_remove - removing expander object
 * @ioc: per adapter object
 * @sas_address: expander sas_address
 *
 * Return nothing.
 */
static void
_scsih_expander_remove(struct MPT2SAS_ADAPTER *ioc, u64 sas_address)
{
	struct _sas_node *sas_expander;
	unsigned long flags;

	if (ioc->shost_recovery)
		return;

	spin_lock_irqsave(&ioc->sas_node_lock, flags);
	sas_expander = mpt2sas_scsih_expander_find_by_sas_address(ioc,
	    sas_address);
	spin_unlock_irqrestore(&ioc->sas_node_lock, flags);
	_scsih_expander_node_remove(ioc, sas_expander);
}

/**
 * _scsih_add_device -  creating sas device object
 * @ioc: per adapter object
 * @handle: sas device handle
 * @phy_num: phy number end device attached to
 * @is_pd: is this hidden raid component
 *
 * Creating end device object, stored in ioc->sas_device_list.
 *
 * Returns 0 for success, non-zero for failure.
 */
static int
_scsih_add_device(struct MPT2SAS_ADAPTER *ioc, u16 handle, u8 phy_num, u8 is_pd)
{
	Mpi2ConfigReply_t mpi_reply;
	Mpi2SasDevicePage0_t sas_device_pg0;
	Mpi2SasEnclosurePage0_t enclosure_pg0;
	struct _sas_device *sas_device;
	u32 ioc_status;
	__le64 sas_address;
	u32 device_info;
	unsigned long flags;

	if ((mpt2sas_config_get_sas_device_pg0(ioc, &mpi_reply, &sas_device_pg0,
	    MPI2_SAS_DEVICE_PGAD_FORM_HANDLE, handle))) {
		printk(MPT2SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		return -1;
	}

	ioc_status = le16_to_cpu(mpi_reply.IOCStatus) &
	    MPI2_IOCSTATUS_MASK;
	if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
		printk(MPT2SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		return -1;
	}

	/* check if device is present */
	if (!(le16_to_cpu(sas_device_pg0.Flags) &
	    MPI2_SAS_DEVICE0_FLAGS_DEVICE_PRESENT)) {
		printk(MPT2SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		printk(MPT2SAS_ERR_FMT "Flags = 0x%04x\n",
		    ioc->name, le16_to_cpu(sas_device_pg0.Flags));
		return -1;
	}

	/* check if there were any issus with discovery */
	if (sas_device_pg0.AccessStatus ==
	    MPI2_SAS_DEVICE0_ASTATUS_SATA_INIT_FAILED) {
		printk(MPT2SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		printk(MPT2SAS_ERR_FMT "AccessStatus = 0x%02x\n",
		    ioc->name, sas_device_pg0.AccessStatus);
		return -1;
	}

	/* check if this is end device */
	device_info = le32_to_cpu(sas_device_pg0.DeviceInfo);
	if (!(_scsih_is_end_device(device_info))) {
		printk(MPT2SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		return -1;
	}

	sas_address = le64_to_cpu(sas_device_pg0.SASAddress);

	spin_lock_irqsave(&ioc->sas_device_lock, flags);
	sas_device = mpt2sas_scsih_sas_device_find_by_sas_address(ioc,
	    sas_address);
	spin_unlock_irqrestore(&ioc->sas_device_lock, flags);

	if (sas_device) {
		_scsih_ublock_io_device(ioc, handle);
		return 0;
	}

	sas_device = kzalloc(sizeof(struct _sas_device),
	    GFP_KERNEL);
	if (!sas_device) {
		printk(MPT2SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		return -1;
	}

	sas_device->handle = handle;
	if (_scsih_get_sas_address(ioc, le16_to_cpu
		(sas_device_pg0.ParentDevHandle),
		&sas_device->sas_address_parent) != 0)
		printk(MPT2SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
	sas_device->enclosure_handle =
	    le16_to_cpu(sas_device_pg0.EnclosureHandle);
	sas_device->slot =
	    le16_to_cpu(sas_device_pg0.Slot);
	sas_device->device_info = device_info;
	sas_device->sas_address = sas_address;
	sas_device->hidden_raid_component = is_pd;

	/* get enclosure_logical_id */
	if (sas_device->enclosure_handle && !(mpt2sas_config_get_enclosure_pg0(
	   ioc, &mpi_reply, &enclosure_pg0, MPI2_SAS_ENCLOS_PGAD_FORM_HANDLE,
	   sas_device->enclosure_handle)))
		sas_device->enclosure_logical_id =
		    le64_to_cpu(enclosure_pg0.EnclosureLogicalID);

	/* get device name */
	sas_device->device_name = le64_to_cpu(sas_device_pg0.DeviceName);

	if (ioc->wait_for_port_enable_to_complete)
		_scsih_sas_device_init_add(ioc, sas_device);
	else
		_scsih_sas_device_add(ioc, sas_device);

	return 0;
}

/**
 * _scsih_remove_device -  removing sas device object
 * @ioc: per adapter object
 * @sas_device: the sas_device object
 *
 * Return nothing.
 */
static void
_scsih_remove_device(struct MPT2SAS_ADAPTER *ioc, struct _sas_device
    *sas_device)
{
	struct MPT2SAS_TARGET *sas_target_priv_data;
	Mpi2SasIoUnitControlReply_t mpi_reply;
	Mpi2SasIoUnitControlRequest_t mpi_request;
	u16 device_handle, handle;

	if (!sas_device)
		return;

	handle = sas_device->handle;
	dewtprintk(ioc, printk(MPT2SAS_INFO_FMT "%s: enter: handle(0x%04x),"
	    " sas_addr(0x%016llx)\n", ioc->name, __func__, handle,
	    (unsigned long long) sas_device->sas_address));

	if (sas_device->starget && sas_device->starget->hostdata) {
		sas_target_priv_data = sas_device->starget->hostdata;
		sas_target_priv_data->deleted = 1;
	}

	if (ioc->remove_host || ioc->shost_recovery || !handle)
		goto out;

	if ((sas_device->state & MPTSAS_STATE_TR_COMPLETE)) {
		dewtprintk(ioc, printk(MPT2SAS_INFO_FMT "\tskip "
		   "target_reset handle(0x%04x)\n", ioc->name,
		   handle));
		goto skip_tr;
	}

	/* Target Reset to flush out all the outstanding IO */
	device_handle = (sas_device->hidden_raid_component) ?
	    sas_device->volume_handle : handle;
	if (device_handle) {
		dewtprintk(ioc, printk(MPT2SAS_INFO_FMT "issue target reset: "
		    "handle(0x%04x)\n", ioc->name, device_handle));
		mutex_lock(&ioc->tm_cmds.mutex);
		mpt2sas_scsih_issue_tm(ioc, device_handle, 0,
		    MPI2_SCSITASKMGMT_TASKTYPE_TARGET_RESET, 0, 10);
		ioc->tm_cmds.status = MPT2_CMD_NOT_USED;
		mutex_unlock(&ioc->tm_cmds.mutex);
		dewtprintk(ioc, printk(MPT2SAS_INFO_FMT "issue target reset "
		    "done: handle(0x%04x)\n", ioc->name, device_handle));
		if (ioc->shost_recovery)
			goto out;
	}
 skip_tr:

	if ((sas_device->state & MPTSAS_STATE_CNTRL_COMPLETE)) {
		dewtprintk(ioc, printk(MPT2SAS_INFO_FMT "\tskip "
		   "sas_cntrl handle(0x%04x)\n", ioc->name, handle));
		goto out;
	}

	/* SAS_IO_UNIT_CNTR - send REMOVE_DEVICE */
	dewtprintk(ioc, printk(MPT2SAS_INFO_FMT "sas_iounit: handle"
	    "(0x%04x)\n", ioc->name, handle));
	memset(&mpi_request, 0, sizeof(Mpi2SasIoUnitControlRequest_t));
	mpi_request.Function = MPI2_FUNCTION_SAS_IO_UNIT_CONTROL;
	mpi_request.Operation = MPI2_SAS_OP_REMOVE_DEVICE;
	mpi_request.DevHandle = handle;
	mpi_request.VF_ID = 0; /* TODO */
	mpi_request.VP_ID = 0;
	if ((mpt2sas_base_sas_iounit_control(ioc, &mpi_reply,
	    &mpi_request)) != 0) {
		printk(MPT2SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
	}

	dewtprintk(ioc, printk(MPT2SAS_INFO_FMT "sas_iounit: ioc_status"
	    "(0x%04x), loginfo(0x%08x)\n", ioc->name,
	    le16_to_cpu(mpi_reply.IOCStatus),
	    le32_to_cpu(mpi_reply.IOCLogInfo)));

 out:

	_scsih_ublock_io_device(ioc, handle);

	mpt2sas_transport_port_remove(ioc, sas_device->sas_address,
	    sas_device->sas_address_parent);

	printk(MPT2SAS_INFO_FMT "removing handle(0x%04x), sas_addr"
	    "(0x%016llx)\n", ioc->name, handle,
	    (unsigned long long) sas_device->sas_address);
	_scsih_sas_device_remove(ioc, sas_device);

	dewtprintk(ioc, printk(MPT2SAS_INFO_FMT "%s: exit: handle"
	    "(0x%04x)\n", ioc->name, __func__, handle));
}

#ifdef CONFIG_SCSI_MPT2SAS_LOGGING
/**
 * _scsih_sas_topology_change_event_debug - debug for topology event
 * @ioc: per adapter object
 * @event_data: event data payload
 * Context: user.
 */
static void
_scsih_sas_topology_change_event_debug(struct MPT2SAS_ADAPTER *ioc,
    Mpi2EventDataSasTopologyChangeList_t *event_data)
{
	int i;
	u16 handle;
	u16 reason_code;
	u8 phy_number;
	char *status_str = NULL;
	char link_rate[25];

	switch (event_data->ExpStatus) {
	case MPI2_EVENT_SAS_TOPO_ES_ADDED:
		status_str = "add";
		break;
	case MPI2_EVENT_SAS_TOPO_ES_NOT_RESPONDING:
		status_str = "remove";
		break;
	case MPI2_EVENT_SAS_TOPO_ES_RESPONDING:
		status_str =  "responding";
		break;
	case MPI2_EVENT_SAS_TOPO_ES_DELAY_NOT_RESPONDING:
		status_str = "remove delay";
		break;
	default:
		status_str = "unknown status";
		break;
	}
	printk(MPT2SAS_DEBUG_FMT "sas topology change: (%s)\n",
	    ioc->name, status_str);
	printk(KERN_DEBUG "\thandle(0x%04x), enclosure_handle(0x%04x) "
	    "start_phy(%02d), count(%d)\n",
	    le16_to_cpu(event_data->ExpanderDevHandle),
	    le16_to_cpu(event_data->EnclosureHandle),
	    event_data->StartPhyNum, event_data->NumEntries);
	for (i = 0; i < event_data->NumEntries; i++) {
		handle = le16_to_cpu(event_data->PHY[i].AttachedDevHandle);
		if (!handle)
			continue;
		phy_number = event_data->StartPhyNum + i;
		reason_code = event_data->PHY[i].PhyStatus &
		    MPI2_EVENT_SAS_TOPO_RC_MASK;
		switch (reason_code) {
		case MPI2_EVENT_SAS_TOPO_RC_TARG_ADDED:
			snprintf(link_rate, 25, ": add, link(0x%02x)",
			    (event_data->PHY[i].LinkRate >> 4));
			status_str = link_rate;
			break;
		case MPI2_EVENT_SAS_TOPO_RC_TARG_NOT_RESPONDING:
			status_str = ": remove";
			break;
		case MPI2_EVENT_SAS_TOPO_RC_DELAY_NOT_RESPONDING:
			status_str = ": remove_delay";
			break;
		case MPI2_EVENT_SAS_TOPO_RC_PHY_CHANGED:
			snprintf(link_rate, 25, ": link(0x%02x)",
			    (event_data->PHY[i].LinkRate >> 4));
			status_str = link_rate;
			break;
		case MPI2_EVENT_SAS_TOPO_RC_NO_CHANGE:
			status_str = ": responding";
			break;
		default:
			status_str = ": unknown";
			break;
		}
		printk(KERN_DEBUG "\tphy(%02d), attached_handle(0x%04x)%s\n",
		    phy_number, handle, status_str);
	}
}
#endif

/**
 * _scsih_sas_topology_change_event - handle topology changes
 * @ioc: per adapter object
 * @fw_event: The fw_event_work object
 * Context: user.
 *
 */
static void
_scsih_sas_topology_change_event(struct MPT2SAS_ADAPTER *ioc,
    struct fw_event_work *fw_event)
{
	int i;
	u16 parent_handle, handle;
	u16 reason_code;
	u8 phy_number;
	struct _sas_node *sas_expander;
	struct _sas_device *sas_device;
	u64 sas_address;
	unsigned long flags;
	u8 link_rate;
	Mpi2EventDataSasTopologyChangeList_t *event_data = fw_event->event_data;

#ifdef CONFIG_SCSI_MPT2SAS_LOGGING
	if (ioc->logging_level & MPT_DEBUG_EVENT_WORK_TASK)
		_scsih_sas_topology_change_event_debug(ioc, event_data);
#endif

	if (ioc->shost_recovery)
		return;

	if (!ioc->sas_hba.num_phys)
		_scsih_sas_host_add(ioc);
	else
		_scsih_sas_host_refresh(ioc);

	if (fw_event->ignore) {
		dewtprintk(ioc, printk(MPT2SAS_DEBUG_FMT "ignoring expander "
		    "event\n", ioc->name));
		return;
	}

	parent_handle = le16_to_cpu(event_data->ExpanderDevHandle);

	/* handle expander add */
	if (event_data->ExpStatus == MPI2_EVENT_SAS_TOPO_ES_ADDED)
		if (_scsih_expander_add(ioc, parent_handle) != 0)
			return;

	spin_lock_irqsave(&ioc->sas_node_lock, flags);
	sas_expander = mpt2sas_scsih_expander_find_by_handle(ioc,
	    parent_handle);
	spin_unlock_irqrestore(&ioc->sas_node_lock, flags);
	if (sas_expander)
		sas_address = sas_expander->sas_address;
	else if (parent_handle < ioc->sas_hba.num_phys)
		sas_address = ioc->sas_hba.sas_address;
	else
		return;

	/* handle siblings events */
	for (i = 0; i < event_data->NumEntries; i++) {
		if (fw_event->ignore) {
			dewtprintk(ioc, printk(MPT2SAS_DEBUG_FMT "ignoring "
			    "expander event\n", ioc->name));
			return;
		}
		if (ioc->shost_recovery)
			return;
		phy_number = event_data->StartPhyNum + i;
		reason_code = event_data->PHY[i].PhyStatus &
		    MPI2_EVENT_SAS_TOPO_RC_MASK;
		if ((event_data->PHY[i].PhyStatus &
		    MPI2_EVENT_SAS_TOPO_PHYSTATUS_VACANT) && (reason_code !=
		    MPI2_EVENT_SAS_TOPO_RC_TARG_NOT_RESPONDING))
			continue;
		handle = le16_to_cpu(event_data->PHY[i].AttachedDevHandle);
		if (!handle)
			continue;
		link_rate = event_data->PHY[i].LinkRate >> 4;
		switch (reason_code) {
		case MPI2_EVENT_SAS_TOPO_RC_PHY_CHANGED:
		case MPI2_EVENT_SAS_TOPO_RC_TARG_ADDED:

			mpt2sas_transport_update_links(ioc, sas_address,
			    handle, phy_number, link_rate);

			if (link_rate < MPI2_SAS_NEG_LINK_RATE_1_5)
				break;
			if (reason_code == MPI2_EVENT_SAS_TOPO_RC_TARG_ADDED) {
				_scsih_add_device(ioc, handle, phy_number, 0);
			}
			break;
		case MPI2_EVENT_SAS_TOPO_RC_TARG_NOT_RESPONDING:

			spin_lock_irqsave(&ioc->sas_device_lock, flags);
			sas_device = _scsih_sas_device_find_by_handle(ioc,
			    handle);
			if (!sas_device) {
				spin_unlock_irqrestore(&ioc->sas_device_lock,
				    flags);
				break;
			}
			spin_unlock_irqrestore(&ioc->sas_device_lock, flags);
			_scsih_remove_device(ioc, sas_device);
			break;
		}
	}

	/* handle expander removal */
	if (event_data->ExpStatus == MPI2_EVENT_SAS_TOPO_ES_NOT_RESPONDING &&
	    sas_expander)
		_scsih_expander_remove(ioc, sas_address);

}

#ifdef CONFIG_SCSI_MPT2SAS_LOGGING
/**
 * _scsih_sas_device_status_change_event_debug - debug for device event
 * @event_data: event data payload
 * Context: user.
 *
 * Return nothing.
 */
static void
_scsih_sas_device_status_change_event_debug(struct MPT2SAS_ADAPTER *ioc,
    Mpi2EventDataSasDeviceStatusChange_t *event_data)
{
	char *reason_str = NULL;

	switch (event_data->ReasonCode) {
	case MPI2_EVENT_SAS_DEV_STAT_RC_SMART_DATA:
		reason_str = "smart data";
		break;
	case MPI2_EVENT_SAS_DEV_STAT_RC_UNSUPPORTED:
		reason_str = "unsupported device discovered";
		break;
	case MPI2_EVENT_SAS_DEV_STAT_RC_INTERNAL_DEVICE_RESET:
		reason_str = "internal device reset";
		break;
	case MPI2_EVENT_SAS_DEV_STAT_RC_TASK_ABORT_INTERNAL:
		reason_str = "internal task abort";
		break;
	case MPI2_EVENT_SAS_DEV_STAT_RC_ABORT_TASK_SET_INTERNAL:
		reason_str = "internal task abort set";
		break;
	case MPI2_EVENT_SAS_DEV_STAT_RC_CLEAR_TASK_SET_INTERNAL:
		reason_str = "internal clear task set";
		break;
	case MPI2_EVENT_SAS_DEV_STAT_RC_QUERY_TASK_INTERNAL:
		reason_str = "internal query task";
		break;
	case MPI2_EVENT_SAS_DEV_STAT_RC_SATA_INIT_FAILURE:
		reason_str = "sata init failure";
		break;
	case MPI2_EVENT_SAS_DEV_STAT_RC_CMP_INTERNAL_DEV_RESET:
		reason_str = "internal device reset complete";
		break;
	case MPI2_EVENT_SAS_DEV_STAT_RC_CMP_TASK_ABORT_INTERNAL:
		reason_str = "internal task abort complete";
		break;
	case MPI2_EVENT_SAS_DEV_STAT_RC_ASYNC_NOTIFICATION:
		reason_str = "internal async notification";
		break;
	case MPI2_EVENT_SAS_DEV_STAT_RC_EXPANDER_REDUCED_FUNCTIONALITY:
		reason_str = "expander reduced functionality";
		break;
	case MPI2_EVENT_SAS_DEV_STAT_RC_CMP_EXPANDER_REDUCED_FUNCTIONALITY:
		reason_str = "expander reduced functionality complete";
		break;
	default:
		reason_str = "unknown reason";
		break;
	}
	printk(MPT2SAS_DEBUG_FMT "device status change: (%s)\n"
	    "\thandle(0x%04x), sas address(0x%016llx)", ioc->name,
	    reason_str, le16_to_cpu(event_data->DevHandle),
	    (unsigned long long)le64_to_cpu(event_data->SASAddress));
	if (event_data->ReasonCode == MPI2_EVENT_SAS_DEV_STAT_RC_SMART_DATA)
		printk(MPT2SAS_DEBUG_FMT ", ASC(0x%x), ASCQ(0x%x)\n", ioc->name,
		    event_data->ASC, event_data->ASCQ);
	printk(KERN_INFO "\n");
}
#endif

/**
 * _scsih_sas_device_status_change_event - handle device status change
 * @ioc: per adapter object
 * @fw_event: The fw_event_work object
 * Context: user.
 *
 * Return nothing.
 */
static void
_scsih_sas_device_status_change_event(struct MPT2SAS_ADAPTER *ioc,
    struct fw_event_work *fw_event)
{
#ifdef CONFIG_SCSI_MPT2SAS_LOGGING
	if (ioc->logging_level & MPT_DEBUG_EVENT_WORK_TASK)
		_scsih_sas_device_status_change_event_debug(ioc,
		     fw_event->event_data);
#endif
}

#ifdef CONFIG_SCSI_MPT2SAS_LOGGING
/**
 * _scsih_sas_enclosure_dev_status_change_event_debug - debug for enclosure event
 * @ioc: per adapter object
 * @event_data: event data payload
 * Context: user.
 *
 * Return nothing.
 */
static void
_scsih_sas_enclosure_dev_status_change_event_debug(struct MPT2SAS_ADAPTER *ioc,
    Mpi2EventDataSasEnclDevStatusChange_t *event_data)
{
	char *reason_str = NULL;

	switch (event_data->ReasonCode) {
	case MPI2_EVENT_SAS_ENCL_RC_ADDED:
		reason_str = "enclosure add";
		break;
	case MPI2_EVENT_SAS_ENCL_RC_NOT_RESPONDING:
		reason_str = "enclosure remove";
		break;
	default:
		reason_str = "unknown reason";
		break;
	}

	printk(MPT2SAS_DEBUG_FMT "enclosure status change: (%s)\n"
	    "\thandle(0x%04x), enclosure logical id(0x%016llx)"
	    " number slots(%d)\n", ioc->name, reason_str,
	    le16_to_cpu(event_data->EnclosureHandle),
	    (unsigned long long)le64_to_cpu(event_data->EnclosureLogicalID),
	    le16_to_cpu(event_data->StartSlot));
}
#endif

/**
 * _scsih_sas_enclosure_dev_status_change_event - handle enclosure events
 * @ioc: per adapter object
 * @fw_event: The fw_event_work object
 * Context: user.
 *
 * Return nothing.
 */
static void
_scsih_sas_enclosure_dev_status_change_event(struct MPT2SAS_ADAPTER *ioc,
    struct fw_event_work *fw_event)
{
#ifdef CONFIG_SCSI_MPT2SAS_LOGGING
	if (ioc->logging_level & MPT_DEBUG_EVENT_WORK_TASK)
		_scsih_sas_enclosure_dev_status_change_event_debug(ioc,
		     fw_event->event_data);
#endif
}

/**
 * _scsih_sas_broadcast_primative_event - handle broadcast events
 * @ioc: per adapter object
 * @fw_event: The fw_event_work object
 * Context: user.
 *
 * Return nothing.
 */
static void
_scsih_sas_broadcast_primative_event(struct MPT2SAS_ADAPTER *ioc,
    struct fw_event_work *fw_event)
{
	struct scsi_cmnd *scmd;
	u16 smid, handle;
	u32 lun;
	struct MPT2SAS_DEVICE *sas_device_priv_data;
	u32 termination_count;
	u32 query_count;
	Mpi2SCSITaskManagementReply_t *mpi_reply;
#ifdef CONFIG_SCSI_MPT2SAS_LOGGING
	Mpi2EventDataSasBroadcastPrimitive_t *event_data = fw_event->event_data;
#endif
	dewtprintk(ioc, printk(MPT2SAS_DEBUG_FMT "broadcast primative: "
	    "phy number(%d), width(%d)\n", ioc->name, event_data->PhyNum,
	    event_data->PortWidth));
	dtmprintk(ioc, printk(MPT2SAS_DEBUG_FMT "%s: enter\n", ioc->name,
	    __func__));

	mutex_lock(&ioc->tm_cmds.mutex);
	termination_count = 0;
	query_count = 0;
	mpi_reply = ioc->tm_cmds.reply;
	for (smid = 1; smid <= ioc->scsiio_depth; smid++) {
		scmd = _scsih_scsi_lookup_get(ioc, smid);
		if (!scmd)
			continue;
		sas_device_priv_data = scmd->device->hostdata;
		if (!sas_device_priv_data || !sas_device_priv_data->sas_target)
			continue;
		 /* skip hidden raid components */
		if (sas_device_priv_data->sas_target->flags &
		    MPT_TARGET_FLAGS_RAID_COMPONENT)
			continue;
		 /* skip volumes */
		if (sas_device_priv_data->sas_target->flags &
		    MPT_TARGET_FLAGS_VOLUME)
			continue;

		handle = sas_device_priv_data->sas_target->handle;
		lun = sas_device_priv_data->lun;
		query_count++;

		mpt2sas_scsih_issue_tm(ioc, handle, lun,
		    MPI2_SCSITASKMGMT_TASKTYPE_QUERY_TASK, smid, 30);
		ioc->tm_cmds.status = MPT2_CMD_NOT_USED;

		if ((mpi_reply->IOCStatus == MPI2_IOCSTATUS_SUCCESS) &&
		    (mpi_reply->ResponseCode ==
		     MPI2_SCSITASKMGMT_RSP_TM_SUCCEEDED ||
		     mpi_reply->ResponseCode ==
		     MPI2_SCSITASKMGMT_RSP_IO_QUEUED_ON_IOC))
			continue;

		mpt2sas_scsih_issue_tm(ioc, handle, lun,
		    MPI2_SCSITASKMGMT_TASKTYPE_ABRT_TASK_SET, 0, 30);
		ioc->tm_cmds.status = MPT2_CMD_NOT_USED;
		termination_count += le32_to_cpu(mpi_reply->TerminationCount);
	}
	ioc->broadcast_aen_busy = 0;
	mutex_unlock(&ioc->tm_cmds.mutex);

	dtmprintk(ioc, printk(MPT2SAS_DEBUG_FMT
	    "%s - exit, query_count = %d termination_count = %d\n",
	    ioc->name, __func__, query_count, termination_count));
}

/**
 * _scsih_sas_discovery_event - handle discovery events
 * @ioc: per adapter object
 * @fw_event: The fw_event_work object
 * Context: user.
 *
 * Return nothing.
 */
static void
_scsih_sas_discovery_event(struct MPT2SAS_ADAPTER *ioc,
    struct fw_event_work *fw_event)
{
	Mpi2EventDataSasDiscovery_t *event_data = fw_event->event_data;

#ifdef CONFIG_SCSI_MPT2SAS_LOGGING
	if (ioc->logging_level & MPT_DEBUG_EVENT_WORK_TASK) {
		printk(MPT2SAS_DEBUG_FMT "discovery event: (%s)", ioc->name,
		    (event_data->ReasonCode == MPI2_EVENT_SAS_DISC_RC_STARTED) ?
		    "start" : "stop");
	if (event_data->DiscoveryStatus)
		printk("discovery_status(0x%08x)",
		    le32_to_cpu(event_data->DiscoveryStatus));
	printk("\n");
	}
#endif

	if (event_data->ReasonCode == MPI2_EVENT_SAS_DISC_RC_STARTED &&
	    !ioc->sas_hba.num_phys)
		_scsih_sas_host_add(ioc);
}

/**
 * _scsih_reprobe_lun - reprobing lun
 * @sdev: scsi device struct
 * @no_uld_attach: sdev->no_uld_attach flag setting
 *
 **/
static void
_scsih_reprobe_lun(struct scsi_device *sdev, void *no_uld_attach)
{
	int rc;

	sdev->no_uld_attach = no_uld_attach ? 1 : 0;
	sdev_printk(KERN_INFO, sdev, "%s raid component\n",
	    sdev->no_uld_attach ? "hidding" : "exposing");
	rc = scsi_device_reprobe(sdev);
}

/**
 * _scsih_reprobe_target - reprobing target
 * @starget: scsi target struct
 * @no_uld_attach: sdev->no_uld_attach flag setting
 *
 * Note: no_uld_attach flag determines whether the disk device is attached
 * to block layer. A value of `1` means to not attach.
 **/
static void
_scsih_reprobe_target(struct scsi_target *starget, int no_uld_attach)
{
	struct MPT2SAS_TARGET *sas_target_priv_data = starget->hostdata;

	if (no_uld_attach)
		sas_target_priv_data->flags |= MPT_TARGET_FLAGS_RAID_COMPONENT;
	else
		sas_target_priv_data->flags &= ~MPT_TARGET_FLAGS_RAID_COMPONENT;

	starget_for_each_device(starget, no_uld_attach ? (void *)1 : NULL,
	    _scsih_reprobe_lun);
}
/**
 * _scsih_sas_volume_add - add new volume
 * @ioc: per adapter object
 * @element: IR config element data
 * Context: user.
 *
 * Return nothing.
 */
static void
_scsih_sas_volume_add(struct MPT2SAS_ADAPTER *ioc,
    Mpi2EventIrConfigElement_t *element)
{
	struct _raid_device *raid_device;
	unsigned long flags;
	u64 wwid;
	u16 handle = le16_to_cpu(element->VolDevHandle);
	int rc;

	mpt2sas_config_get_volume_wwid(ioc, handle, &wwid);
	if (!wwid) {
		printk(MPT2SAS_ERR_FMT
		    "failure at %s:%d/%s()!\n", ioc->name,
		    __FILE__, __LINE__, __func__);
		return;
	}

	spin_lock_irqsave(&ioc->raid_device_lock, flags);
	raid_device = _scsih_raid_device_find_by_wwid(ioc, wwid);
	spin_unlock_irqrestore(&ioc->raid_device_lock, flags);

	if (raid_device)
		return;

	raid_device = kzalloc(sizeof(struct _raid_device), GFP_KERNEL);
	if (!raid_device) {
		printk(MPT2SAS_ERR_FMT
		    "failure at %s:%d/%s()!\n", ioc->name,
		    __FILE__, __LINE__, __func__);
		return;
	}

	raid_device->id = ioc->sas_id++;
	raid_device->channel = RAID_CHANNEL;
	raid_device->handle = handle;
	raid_device->wwid = wwid;
	_scsih_raid_device_add(ioc, raid_device);
	if (!ioc->wait_for_port_enable_to_complete) {
		rc = scsi_add_device(ioc->shost, RAID_CHANNEL,
		    raid_device->id, 0);
		if (rc)
			_scsih_raid_device_remove(ioc, raid_device);
	} else
		_scsih_determine_boot_device(ioc, raid_device, 1);
}

/**
 * _scsih_sas_volume_delete - delete volume
 * @ioc: per adapter object
 * @element: IR config element data
 * Context: user.
 *
 * Return nothing.
 */
static void
_scsih_sas_volume_delete(struct MPT2SAS_ADAPTER *ioc,
    Mpi2EventIrConfigElement_t *element)
{
	struct _raid_device *raid_device;
	u16 handle = le16_to_cpu(element->VolDevHandle);
	unsigned long flags;
	struct MPT2SAS_TARGET *sas_target_priv_data;

	spin_lock_irqsave(&ioc->raid_device_lock, flags);
	raid_device = _scsih_raid_device_find_by_handle(ioc, handle);
	spin_unlock_irqrestore(&ioc->raid_device_lock, flags);
	if (!raid_device)
		return;
	if (raid_device->starget) {
		sas_target_priv_data = raid_device->starget->hostdata;
		sas_target_priv_data->deleted = 1;
		scsi_remove_target(&raid_device->starget->dev);
	}
	_scsih_raid_device_remove(ioc, raid_device);
}

/**
 * _scsih_sas_pd_expose - expose pd component to /dev/sdX
 * @ioc: per adapter object
 * @element: IR config element data
 * Context: user.
 *
 * Return nothing.
 */
static void
_scsih_sas_pd_expose(struct MPT2SAS_ADAPTER *ioc,
    Mpi2EventIrConfigElement_t *element)
{
	struct _sas_device *sas_device;
	unsigned long flags;
	u16 handle = le16_to_cpu(element->PhysDiskDevHandle);

	spin_lock_irqsave(&ioc->sas_device_lock, flags);
	sas_device = _scsih_sas_device_find_by_handle(ioc, handle);
	spin_unlock_irqrestore(&ioc->sas_device_lock, flags);
	if (!sas_device)
		return;

	/* exposing raid component */
	sas_device->volume_handle = 0;
	sas_device->volume_wwid = 0;
	sas_device->hidden_raid_component = 0;
	_scsih_reprobe_target(sas_device->starget, 0);
}

/**
 * _scsih_sas_pd_hide - hide pd component from /dev/sdX
 * @ioc: per adapter object
 * @element: IR config element data
 * Context: user.
 *
 * Return nothing.
 */
static void
_scsih_sas_pd_hide(struct MPT2SAS_ADAPTER *ioc,
    Mpi2EventIrConfigElement_t *element)
{
	struct _sas_device *sas_device;
	unsigned long flags;
	u16 handle = le16_to_cpu(element->PhysDiskDevHandle);

	spin_lock_irqsave(&ioc->sas_device_lock, flags);
	sas_device = _scsih_sas_device_find_by_handle(ioc, handle);
	spin_unlock_irqrestore(&ioc->sas_device_lock, flags);
	if (!sas_device)
		return;

	/* hiding raid component */
	mpt2sas_config_get_volume_handle(ioc, handle,
	    &sas_device->volume_handle);
	mpt2sas_config_get_volume_wwid(ioc, sas_device->volume_handle,
	    &sas_device->volume_wwid);
	sas_device->hidden_raid_component = 1;
	_scsih_reprobe_target(sas_device->starget, 1);
}

/**
 * _scsih_sas_pd_delete - delete pd component
 * @ioc: per adapter object
 * @element: IR config element data
 * Context: user.
 *
 * Return nothing.
 */
static void
_scsih_sas_pd_delete(struct MPT2SAS_ADAPTER *ioc,
    Mpi2EventIrConfigElement_t *element)
{
	struct _sas_device *sas_device;
	unsigned long flags;
	u16 handle = le16_to_cpu(element->PhysDiskDevHandle);

	spin_lock_irqsave(&ioc->sas_device_lock, flags);
	sas_device = _scsih_sas_device_find_by_handle(ioc, handle);
	spin_unlock_irqrestore(&ioc->sas_device_lock, flags);
	if (!sas_device)
		return;
	_scsih_remove_device(ioc, sas_device);
}

/**
 * _scsih_sas_pd_add - remove pd component
 * @ioc: per adapter object
 * @element: IR config element data
 * Context: user.
 *
 * Return nothing.
 */
static void
_scsih_sas_pd_add(struct MPT2SAS_ADAPTER *ioc,
    Mpi2EventIrConfigElement_t *element)
{
	struct _sas_device *sas_device;
	unsigned long flags;
	u16 handle = le16_to_cpu(element->PhysDiskDevHandle);
	Mpi2ConfigReply_t mpi_reply;
	Mpi2SasDevicePage0_t sas_device_pg0;
	u32 ioc_status;
	u64 sas_address;
	u16 parent_handle;

	spin_lock_irqsave(&ioc->sas_device_lock, flags);
	sas_device = _scsih_sas_device_find_by_handle(ioc, handle);
	spin_unlock_irqrestore(&ioc->sas_device_lock, flags);
	if (sas_device) {
		sas_device->hidden_raid_component = 1;
		return;
	}

	if ((mpt2sas_config_get_sas_device_pg0(ioc, &mpi_reply, &sas_device_pg0,
	    MPI2_SAS_DEVICE_PGAD_FORM_HANDLE, handle))) {
		printk(MPT2SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		return;
	}

	ioc_status = le16_to_cpu(mpi_reply.IOCStatus) &
	    MPI2_IOCSTATUS_MASK;
	if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
		printk(MPT2SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		return;
	}

	parent_handle = le16_to_cpu(sas_device_pg0.ParentDevHandle);
	if (!_scsih_get_sas_address(ioc, parent_handle, &sas_address))
		mpt2sas_transport_update_links(ioc, sas_address, handle,
		    sas_device_pg0.PhyNum, MPI2_SAS_NEG_LINK_RATE_1_5);

	_scsih_add_device(ioc, handle, 0, 1);
}

#ifdef CONFIG_SCSI_MPT2SAS_LOGGING
/**
 * _scsih_sas_ir_config_change_event_debug - debug for IR Config Change events
 * @ioc: per adapter object
 * @event_data: event data payload
 * Context: user.
 *
 * Return nothing.
 */
static void
_scsih_sas_ir_config_change_event_debug(struct MPT2SAS_ADAPTER *ioc,
    Mpi2EventDataIrConfigChangeList_t *event_data)
{
	Mpi2EventIrConfigElement_t *element;
	u8 element_type;
	int i;
	char *reason_str = NULL, *element_str = NULL;

	element = (Mpi2EventIrConfigElement_t *)&event_data->ConfigElement[0];

	printk(MPT2SAS_DEBUG_FMT "raid config change: (%s), elements(%d)\n",
	    ioc->name, (le32_to_cpu(event_data->Flags) &
	    MPI2_EVENT_IR_CHANGE_FLAGS_FOREIGN_CONFIG) ?
	    "foreign" : "native", event_data->NumElements);
	for (i = 0; i < event_data->NumElements; i++, element++) {
		switch (element->ReasonCode) {
		case MPI2_EVENT_IR_CHANGE_RC_ADDED:
			reason_str = "add";
			break;
		case MPI2_EVENT_IR_CHANGE_RC_REMOVED:
			reason_str = "remove";
			break;
		case MPI2_EVENT_IR_CHANGE_RC_NO_CHANGE:
			reason_str = "no change";
			break;
		case MPI2_EVENT_IR_CHANGE_RC_HIDE:
			reason_str = "hide";
			break;
		case MPI2_EVENT_IR_CHANGE_RC_UNHIDE:
			reason_str = "unhide";
			break;
		case MPI2_EVENT_IR_CHANGE_RC_VOLUME_CREATED:
			reason_str = "volume_created";
			break;
		case MPI2_EVENT_IR_CHANGE_RC_VOLUME_DELETED:
			reason_str = "volume_deleted";
			break;
		case MPI2_EVENT_IR_CHANGE_RC_PD_CREATED:
			reason_str = "pd_created";
			break;
		case MPI2_EVENT_IR_CHANGE_RC_PD_DELETED:
			reason_str = "pd_deleted";
			break;
		default:
			reason_str = "unknown reason";
			break;
		}
		element_type = le16_to_cpu(element->ElementFlags) &
		    MPI2_EVENT_IR_CHANGE_EFLAGS_ELEMENT_TYPE_MASK;
		switch (element_type) {
		case MPI2_EVENT_IR_CHANGE_EFLAGS_VOLUME_ELEMENT:
			element_str = "volume";
			break;
		case MPI2_EVENT_IR_CHANGE_EFLAGS_VOLPHYSDISK_ELEMENT:
			element_str = "phys disk";
			break;
		case MPI2_EVENT_IR_CHANGE_EFLAGS_HOTSPARE_ELEMENT:
			element_str = "hot spare";
			break;
		default:
			element_str = "unknown element";
			break;
		}
		printk(KERN_DEBUG "\t(%s:%s), vol handle(0x%04x), "
		    "pd handle(0x%04x), pd num(0x%02x)\n", element_str,
		    reason_str, le16_to_cpu(element->VolDevHandle),
		    le16_to_cpu(element->PhysDiskDevHandle),
		    element->PhysDiskNum);
	}
}
#endif

/**
 * _scsih_sas_ir_config_change_event - handle ir configuration change events
 * @ioc: per adapter object
 * @fw_event: The fw_event_work object
 * Context: user.
 *
 * Return nothing.
 */
static void
_scsih_sas_ir_config_change_event(struct MPT2SAS_ADAPTER *ioc,
    struct fw_event_work *fw_event)
{
	Mpi2EventIrConfigElement_t *element;
	int i;
	u8 foreign_config;
	Mpi2EventDataIrConfigChangeList_t *event_data = fw_event->event_data;

#ifdef CONFIG_SCSI_MPT2SAS_LOGGING
	if (ioc->logging_level & MPT_DEBUG_EVENT_WORK_TASK)
		_scsih_sas_ir_config_change_event_debug(ioc, event_data);

#endif
	foreign_config = (le32_to_cpu(event_data->Flags) &
	    MPI2_EVENT_IR_CHANGE_FLAGS_FOREIGN_CONFIG) ? 1 : 0;

	element = (Mpi2EventIrConfigElement_t *)&event_data->ConfigElement[0];
	for (i = 0; i < event_data->NumElements; i++, element++) {

		switch (element->ReasonCode) {
		case MPI2_EVENT_IR_CHANGE_RC_VOLUME_CREATED:
		case MPI2_EVENT_IR_CHANGE_RC_ADDED:
			if (!foreign_config)
				_scsih_sas_volume_add(ioc, element);
			break;
		case MPI2_EVENT_IR_CHANGE_RC_VOLUME_DELETED:
		case MPI2_EVENT_IR_CHANGE_RC_REMOVED:
			if (!foreign_config)
				_scsih_sas_volume_delete(ioc, element);
			break;
		case MPI2_EVENT_IR_CHANGE_RC_PD_CREATED:
			_scsih_sas_pd_hide(ioc, element);
			break;
		case MPI2_EVENT_IR_CHANGE_RC_PD_DELETED:
			_scsih_sas_pd_expose(ioc, element);
			break;
		case MPI2_EVENT_IR_CHANGE_RC_HIDE:
			_scsih_sas_pd_add(ioc, element);
			break;
		case MPI2_EVENT_IR_CHANGE_RC_UNHIDE:
			_scsih_sas_pd_delete(ioc, element);
			break;
		}
	}
}

/**
 * _scsih_sas_ir_volume_event - IR volume event
 * @ioc: per adapter object
 * @fw_event: The fw_event_work object
 * Context: user.
 *
 * Return nothing.
 */
static void
_scsih_sas_ir_volume_event(struct MPT2SAS_ADAPTER *ioc,
    struct fw_event_work *fw_event)
{
	u64 wwid;
	unsigned long flags;
	struct _raid_device *raid_device;
	u16 handle;
	u32 state;
	int rc;
	struct MPT2SAS_TARGET *sas_target_priv_data;
	Mpi2EventDataIrVolume_t *event_data = fw_event->event_data;

	if (event_data->ReasonCode != MPI2_EVENT_IR_VOLUME_RC_STATE_CHANGED)
		return;

	handle = le16_to_cpu(event_data->VolDevHandle);
	state = le32_to_cpu(event_data->NewValue);
	dewtprintk(ioc, printk(MPT2SAS_DEBUG_FMT "%s: handle(0x%04x), "
	    "old(0x%08x), new(0x%08x)\n", ioc->name, __func__,  handle,
	    le32_to_cpu(event_data->PreviousValue), state));

	spin_lock_irqsave(&ioc->raid_device_lock, flags);
	raid_device = _scsih_raid_device_find_by_handle(ioc, handle);
	spin_unlock_irqrestore(&ioc->raid_device_lock, flags);

	switch (state) {
	case MPI2_RAID_VOL_STATE_MISSING:
	case MPI2_RAID_VOL_STATE_FAILED:
		if (!raid_device)
			break;
		if (raid_device->starget) {
			sas_target_priv_data = raid_device->starget->hostdata;
			sas_target_priv_data->deleted = 1;
			scsi_remove_target(&raid_device->starget->dev);
		}
		_scsih_raid_device_remove(ioc, raid_device);
		break;

	case MPI2_RAID_VOL_STATE_ONLINE:
	case MPI2_RAID_VOL_STATE_DEGRADED:
	case MPI2_RAID_VOL_STATE_OPTIMAL:
		if (raid_device)
			break;

		mpt2sas_config_get_volume_wwid(ioc, handle, &wwid);
		if (!wwid) {
			printk(MPT2SAS_ERR_FMT
			    "failure at %s:%d/%s()!\n", ioc->name,
			    __FILE__, __LINE__, __func__);
			break;
		}

		raid_device = kzalloc(sizeof(struct _raid_device), GFP_KERNEL);
		if (!raid_device) {
			printk(MPT2SAS_ERR_FMT
			    "failure at %s:%d/%s()!\n", ioc->name,
			    __FILE__, __LINE__, __func__);
			break;
		}

		raid_device->id = ioc->sas_id++;
		raid_device->channel = RAID_CHANNEL;
		raid_device->handle = handle;
		raid_device->wwid = wwid;
		_scsih_raid_device_add(ioc, raid_device);
		rc = scsi_add_device(ioc->shost, RAID_CHANNEL,
		    raid_device->id, 0);
		if (rc)
			_scsih_raid_device_remove(ioc, raid_device);
		break;

	case MPI2_RAID_VOL_STATE_INITIALIZING:
	default:
		break;
	}
}

/**
 * _scsih_sas_ir_physical_disk_event - PD event
 * @ioc: per adapter object
 * @fw_event: The fw_event_work object
 * Context: user.
 *
 * Return nothing.
 */
static void
_scsih_sas_ir_physical_disk_event(struct MPT2SAS_ADAPTER *ioc,
    struct fw_event_work *fw_event)
{
	u16 handle, parent_handle;
	u32 state;
	struct _sas_device *sas_device;
	unsigned long flags;
	Mpi2ConfigReply_t mpi_reply;
	Mpi2SasDevicePage0_t sas_device_pg0;
	u32 ioc_status;
	Mpi2EventDataIrPhysicalDisk_t *event_data = fw_event->event_data;
	u64 sas_address;

	if (event_data->ReasonCode != MPI2_EVENT_IR_PHYSDISK_RC_STATE_CHANGED)
		return;

	handle = le16_to_cpu(event_data->PhysDiskDevHandle);
	state = le32_to_cpu(event_data->NewValue);

	dewtprintk(ioc, printk(MPT2SAS_DEBUG_FMT "%s: handle(0x%04x), "
	    "old(0x%08x), new(0x%08x)\n", ioc->name, __func__,  handle,
	    le32_to_cpu(event_data->PreviousValue), state));

	spin_lock_irqsave(&ioc->sas_device_lock, flags);
	sas_device = _scsih_sas_device_find_by_handle(ioc, handle);
	spin_unlock_irqrestore(&ioc->sas_device_lock, flags);

	switch (state) {
	case MPI2_RAID_PD_STATE_ONLINE:
	case MPI2_RAID_PD_STATE_DEGRADED:
	case MPI2_RAID_PD_STATE_REBUILDING:
	case MPI2_RAID_PD_STATE_OPTIMAL:
		if (sas_device) {
			sas_device->hidden_raid_component = 1;
			return;
		}

		if ((mpt2sas_config_get_sas_device_pg0(ioc, &mpi_reply,
		    &sas_device_pg0, MPI2_SAS_DEVICE_PGAD_FORM_HANDLE,
		    handle))) {
			printk(MPT2SAS_ERR_FMT "failure at %s:%d/%s()!\n",
			    ioc->name, __FILE__, __LINE__, __func__);
			return;
		}

		ioc_status = le16_to_cpu(mpi_reply.IOCStatus) &
		    MPI2_IOCSTATUS_MASK;
		if (ioc_status != MPI2_IOCSTATUS_SUCCESS) {
			printk(MPT2SAS_ERR_FMT "failure at %s:%d/%s()!\n",
			    ioc->name, __FILE__, __LINE__, __func__);
			return;
		}

		parent_handle = le16_to_cpu(sas_device_pg0.ParentDevHandle);
		if (!_scsih_get_sas_address(ioc, parent_handle, &sas_address))
			mpt2sas_transport_update_links(ioc, sas_address, handle,
			    sas_device_pg0.PhyNum, MPI2_SAS_NEG_LINK_RATE_1_5);

		_scsih_add_device(ioc, handle, 0, 1);

		break;

	case MPI2_RAID_PD_STATE_OFFLINE:
	case MPI2_RAID_PD_STATE_NOT_CONFIGURED:
	case MPI2_RAID_PD_STATE_NOT_COMPATIBLE:
	case MPI2_RAID_PD_STATE_HOT_SPARE:
	default:
		break;
	}
}

#ifdef CONFIG_SCSI_MPT2SAS_LOGGING
/**
 * _scsih_sas_ir_operation_status_event_debug - debug for IR op event
 * @ioc: per adapter object
 * @event_data: event data payload
 * Context: user.
 *
 * Return nothing.
 */
static void
_scsih_sas_ir_operation_status_event_debug(struct MPT2SAS_ADAPTER *ioc,
    Mpi2EventDataIrOperationStatus_t *event_data)
{
	char *reason_str = NULL;

	switch (event_data->RAIDOperation) {
	case MPI2_EVENT_IR_RAIDOP_RESYNC:
		reason_str = "resync";
		break;
	case MPI2_EVENT_IR_RAIDOP_ONLINE_CAP_EXPANSION:
		reason_str = "online capacity expansion";
		break;
	case MPI2_EVENT_IR_RAIDOP_CONSISTENCY_CHECK:
		reason_str = "consistency check";
		break;
	case MPI2_EVENT_IR_RAIDOP_BACKGROUND_INIT:
		reason_str = "background init";
		break;
	case MPI2_EVENT_IR_RAIDOP_MAKE_DATA_CONSISTENT:
		reason_str = "make data consistent";
		break;
	}

	if (!reason_str)
		return;

	printk(MPT2SAS_INFO_FMT "raid operational status: (%s)"
	    "\thandle(0x%04x), percent complete(%d)\n",
	    ioc->name, reason_str,
	    le16_to_cpu(event_data->VolDevHandle),
	    event_data->PercentComplete);
}
#endif

/**
 * _scsih_sas_ir_operation_status_event - handle RAID operation events
 * @ioc: per adapter object
 * @fw_event: The fw_event_work object
 * Context: user.
 *
 * Return nothing.
 */
static void
_scsih_sas_ir_operation_status_event(struct MPT2SAS_ADAPTER *ioc,
    struct fw_event_work *fw_event)
{
#ifdef CONFIG_SCSI_MPT2SAS_LOGGING
	if (ioc->logging_level & MPT_DEBUG_EVENT_WORK_TASK)
		_scsih_sas_ir_operation_status_event_debug(ioc,
		     fw_event->event_data);
#endif
}

/**
 * _scsih_task_set_full - handle task set full
 * @ioc: per adapter object
 * @fw_event: The fw_event_work object
 * Context: user.
 *
 * Throttle back qdepth.
 */
static void
_scsih_task_set_full(struct MPT2SAS_ADAPTER *ioc, struct fw_event_work
	*fw_event)
{
	unsigned long flags;
	struct _sas_device *sas_device;
	static struct _raid_device *raid_device;
	struct scsi_device *sdev;
	int depth;
	u16 current_depth;
	u16 handle;
	int id, channel;
	u64 sas_address;
	Mpi2EventDataTaskSetFull_t *event_data = fw_event->event_data;

	current_depth = le16_to_cpu(event_data->CurrentDepth);
	handle = le16_to_cpu(event_data->DevHandle);
	spin_lock_irqsave(&ioc->sas_device_lock, flags);
	sas_device = _scsih_sas_device_find_by_handle(ioc, handle);
	if (!sas_device) {
		spin_unlock_irqrestore(&ioc->sas_device_lock, flags);
		return;
	}
	spin_unlock_irqrestore(&ioc->sas_device_lock, flags);
	id = sas_device->id;
	channel = sas_device->channel;
	sas_address = sas_device->sas_address;

	/* if hidden raid component, then change to volume characteristics */
	if (sas_device->hidden_raid_component && sas_device->volume_handle) {
		spin_lock_irqsave(&ioc->raid_device_lock, flags);
		raid_device = _scsih_raid_device_find_by_handle(
		    ioc, sas_device->volume_handle);
		spin_unlock_irqrestore(&ioc->raid_device_lock, flags);
		if (raid_device) {
			id = raid_device->id;
			channel = raid_device->channel;
			handle = raid_device->handle;
			sas_address = raid_device->wwid;
		}
	}

	if (ioc->logging_level & MPT_DEBUG_TASK_SET_FULL)
		starget_printk(KERN_DEBUG, sas_device->starget, "task set "
		    "full: handle(0x%04x), sas_addr(0x%016llx), depth(%d)\n",
		    handle, (unsigned long long)sas_address, current_depth);

	shost_for_each_device(sdev, ioc->shost) {
		if (sdev->id == id && sdev->channel == channel) {
			if (current_depth > sdev->queue_depth) {
				if (ioc->logging_level &
				    MPT_DEBUG_TASK_SET_FULL)
					sdev_printk(KERN_INFO, sdev, "strange "
					    "observation, the queue depth is"
					    " (%d) meanwhile fw queue depth "
					    "is (%d)\n", sdev->queue_depth,
					    current_depth);
				continue;
			}
			depth = scsi_track_queue_full(sdev,
			    current_depth - 1);
			if (depth > 0)
				sdev_printk(KERN_INFO, sdev, "Queue depth "
				    "reduced to (%d)\n", depth);
			else if (depth < 0)
				sdev_printk(KERN_INFO, sdev, "Tagged Command "
				    "Queueing is being disabled\n");
			else if (depth == 0)
				if (ioc->logging_level &
				     MPT_DEBUG_TASK_SET_FULL)
					sdev_printk(KERN_INFO, sdev,
					     "Queue depth not changed yet\n");
		}
	}
}

/**
 * _scsih_mark_responding_sas_device - mark a sas_devices as responding
 * @ioc: per adapter object
 * @sas_address: sas address
 * @slot: enclosure slot id
 * @handle: device handle
 *
 * After host reset, find out whether devices are still responding.
 * Used in _scsi_remove_unresponsive_sas_devices.
 *
 * Return nothing.
 */
static void
_scsih_mark_responding_sas_device(struct MPT2SAS_ADAPTER *ioc, u64 sas_address,
    u16 slot, u16 handle)
{
	struct MPT2SAS_TARGET *sas_target_priv_data;
	struct scsi_target *starget;
	struct _sas_device *sas_device;
	unsigned long flags;

	spin_lock_irqsave(&ioc->sas_device_lock, flags);
	list_for_each_entry(sas_device, &ioc->sas_device_list, list) {
		if (sas_device->sas_address == sas_address &&
		    sas_device->slot == slot && sas_device->starget) {
			sas_device->responding = 1;
			sas_device->state = 0;
			starget = sas_device->starget;
			sas_target_priv_data = starget->hostdata;
			sas_target_priv_data->tm_busy = 0;
			starget_printk(KERN_INFO, sas_device->starget,
			    "handle(0x%04x), sas_addr(0x%016llx), enclosure "
			    "logical id(0x%016llx), slot(%d)\n", handle,
			    (unsigned long long)sas_device->sas_address,
			    (unsigned long long)
			    sas_device->enclosure_logical_id,
			    sas_device->slot);
			if (sas_device->handle == handle)
				goto out;
			printk(KERN_INFO "\thandle changed from(0x%04x)!!!\n",
			    sas_device->handle);
			sas_device->handle = handle;
			sas_target_priv_data->handle = handle;
			goto out;
		}
	}
 out:
	spin_unlock_irqrestore(&ioc->sas_device_lock, flags);
}

/**
 * _scsih_search_responding_sas_devices -
 * @ioc: per adapter object
 *
 * After host reset, find out whether devices are still responding.
 * If not remove.
 *
 * Return nothing.
 */
static void
_scsih_search_responding_sas_devices(struct MPT2SAS_ADAPTER *ioc)
{
	Mpi2SasDevicePage0_t sas_device_pg0;
	Mpi2ConfigReply_t mpi_reply;
	u16 ioc_status;
	__le64 sas_address;
	u16 handle;
	u32 device_info;
	u16 slot;

	printk(MPT2SAS_INFO_FMT "%s\n", ioc->name, __func__);

	if (list_empty(&ioc->sas_device_list))
		return;

	handle = 0xFFFF;
	while (!(mpt2sas_config_get_sas_device_pg0(ioc, &mpi_reply,
	    &sas_device_pg0, MPI2_SAS_DEVICE_PGAD_FORM_GET_NEXT_HANDLE,
	    handle))) {
		ioc_status = le16_to_cpu(mpi_reply.IOCStatus) &
		    MPI2_IOCSTATUS_MASK;
		if (ioc_status == MPI2_IOCSTATUS_CONFIG_INVALID_PAGE)
			break;
		handle = le16_to_cpu(sas_device_pg0.DevHandle);
		device_info = le32_to_cpu(sas_device_pg0.DeviceInfo);
		if (!(_scsih_is_end_device(device_info)))
			continue;
		sas_address = le64_to_cpu(sas_device_pg0.SASAddress);
		slot = le16_to_cpu(sas_device_pg0.Slot);
		_scsih_mark_responding_sas_device(ioc, sas_address, slot,
		    handle);
	}
}

/**
 * _scsih_mark_responding_raid_device - mark a raid_device as responding
 * @ioc: per adapter object
 * @wwid: world wide identifier for raid volume
 * @handle: device handle
 *
 * After host reset, find out whether devices are still responding.
 * Used in _scsi_remove_unresponsive_raid_devices.
 *
 * Return nothing.
 */
static void
_scsih_mark_responding_raid_device(struct MPT2SAS_ADAPTER *ioc, u64 wwid,
    u16 handle)
{
	struct MPT2SAS_TARGET *sas_target_priv_data;
	struct scsi_target *starget;
	struct _raid_device *raid_device;
	unsigned long flags;

	spin_lock_irqsave(&ioc->raid_device_lock, flags);
	list_for_each_entry(raid_device, &ioc->raid_device_list, list) {
		if (raid_device->wwid == wwid && raid_device->starget) {
			raid_device->responding = 1;
			starget_printk(KERN_INFO, raid_device->starget,
			    "handle(0x%04x), wwid(0x%016llx)\n", handle,
			    (unsigned long long)raid_device->wwid);
			if (raid_device->handle == handle)
				goto out;
			printk(KERN_INFO "\thandle changed from(0x%04x)!!!\n",
			    raid_device->handle);
			raid_device->handle = handle;
			starget = raid_device->starget;
			sas_target_priv_data = starget->hostdata;
			sas_target_priv_data->handle = handle;
			goto out;
		}
	}
 out:
	spin_unlock_irqrestore(&ioc->raid_device_lock, flags);
}

/**
 * _scsih_search_responding_raid_devices -
 * @ioc: per adapter object
 *
 * After host reset, find out whether devices are still responding.
 * If not remove.
 *
 * Return nothing.
 */
static void
_scsih_search_responding_raid_devices(struct MPT2SAS_ADAPTER *ioc)
{
	Mpi2RaidVolPage1_t volume_pg1;
	Mpi2ConfigReply_t mpi_reply;
	u16 ioc_status;
	u16 handle;

	printk(MPT2SAS_INFO_FMT "%s\n", ioc->name, __func__);

	if (list_empty(&ioc->raid_device_list))
		return;

	handle = 0xFFFF;
	while (!(mpt2sas_config_get_raid_volume_pg1(ioc, &mpi_reply,
	    &volume_pg1, MPI2_RAID_VOLUME_PGAD_FORM_GET_NEXT_HANDLE, handle))) {
		ioc_status = le16_to_cpu(mpi_reply.IOCStatus) &
		    MPI2_IOCSTATUS_MASK;
		if (ioc_status == MPI2_IOCSTATUS_CONFIG_INVALID_PAGE)
			break;
		handle = le16_to_cpu(volume_pg1.DevHandle);
		_scsih_mark_responding_raid_device(ioc,
		    le64_to_cpu(volume_pg1.WWID), handle);
	}
}

/**
 * _scsih_mark_responding_expander - mark a expander as responding
 * @ioc: per adapter object
 * @sas_address: sas address
 * @handle:
 *
 * After host reset, find out whether devices are still responding.
 * Used in _scsi_remove_unresponsive_expanders.
 *
 * Return nothing.
 */
static void
_scsih_mark_responding_expander(struct MPT2SAS_ADAPTER *ioc, u64 sas_address,
     u16 handle)
{
	struct _sas_node *sas_expander;
	unsigned long flags;
	int i;

	spin_lock_irqsave(&ioc->sas_node_lock, flags);
	list_for_each_entry(sas_expander, &ioc->sas_expander_list, list) {
		if (sas_expander->sas_address != sas_address)
			continue;
		sas_expander->responding = 1;
		if (sas_expander->handle == handle)
			goto out;
		printk(KERN_INFO "\texpander(0x%016llx): handle changed"
		    " from(0x%04x) to (0x%04x)!!!\n",
		    (unsigned long long)sas_expander->sas_address,
		    sas_expander->handle, handle);
		sas_expander->handle = handle;
		for (i = 0 ; i < sas_expander->num_phys ; i++)
			sas_expander->phy[i].handle = handle;
		goto out;
	}
 out:
	spin_unlock_irqrestore(&ioc->sas_node_lock, flags);
}

/**
 * _scsih_search_responding_expanders -
 * @ioc: per adapter object
 *
 * After host reset, find out whether devices are still responding.
 * If not remove.
 *
 * Return nothing.
 */
static void
_scsih_search_responding_expanders(struct MPT2SAS_ADAPTER *ioc)
{
	Mpi2ExpanderPage0_t expander_pg0;
	Mpi2ConfigReply_t mpi_reply;
	u16 ioc_status;
	__le64 sas_address;
	u16 handle;

	printk(MPT2SAS_INFO_FMT "%s\n", ioc->name, __func__);

	if (list_empty(&ioc->sas_expander_list))
		return;

	handle = 0xFFFF;
	while (!(mpt2sas_config_get_expander_pg0(ioc, &mpi_reply, &expander_pg0,
	    MPI2_SAS_EXPAND_PGAD_FORM_GET_NEXT_HNDL, handle))) {

		ioc_status = le16_to_cpu(mpi_reply.IOCStatus) &
		    MPI2_IOCSTATUS_MASK;
		if (ioc_status == MPI2_IOCSTATUS_CONFIG_INVALID_PAGE)
			break;

		handle = le16_to_cpu(expander_pg0.DevHandle);
		sas_address = le64_to_cpu(expander_pg0.SASAddress);
		printk(KERN_INFO "\texpander present: handle(0x%04x), "
		    "sas_addr(0x%016llx)\n", handle,
		    (unsigned long long)sas_address);
		_scsih_mark_responding_expander(ioc, sas_address, handle);
	}

}

/**
 * _scsih_remove_unresponding_devices - removing unresponding devices
 * @ioc: per adapter object
 *
 * Return nothing.
 */
static void
_scsih_remove_unresponding_devices(struct MPT2SAS_ADAPTER *ioc)
{
	struct _sas_device *sas_device, *sas_device_next;
	struct _sas_node *sas_expander;
	struct _raid_device *raid_device, *raid_device_next;


	list_for_each_entry_safe(sas_device, sas_device_next,
	    &ioc->sas_device_list, list) {
		if (sas_device->responding) {
			sas_device->responding = 0;
			continue;
		}
		if (sas_device->starget)
			starget_printk(KERN_INFO, sas_device->starget,
			    "removing: handle(0x%04x), sas_addr(0x%016llx), "
			    "enclosure logical id(0x%016llx), slot(%d)\n",
			    sas_device->handle,
			    (unsigned long long)sas_device->sas_address,
			    (unsigned long long)
			    sas_device->enclosure_logical_id,
			    sas_device->slot);
		/* invalidate the device handle */
		sas_device->handle = 0;
		_scsih_remove_device(ioc, sas_device);
	}

	list_for_each_entry_safe(raid_device, raid_device_next,
	    &ioc->raid_device_list, list) {
		if (raid_device->responding) {
			raid_device->responding = 0;
			continue;
		}
		if (raid_device->starget) {
			starget_printk(KERN_INFO, raid_device->starget,
			    "removing: handle(0x%04x), wwid(0x%016llx)\n",
			      raid_device->handle,
			    (unsigned long long)raid_device->wwid);
			scsi_remove_target(&raid_device->starget->dev);
		}
		_scsih_raid_device_remove(ioc, raid_device);
	}

 retry_expander_search:
	sas_expander = NULL;
	list_for_each_entry(sas_expander, &ioc->sas_expander_list, list) {
		if (sas_expander->responding) {
			sas_expander->responding = 0;
			continue;
		}
		_scsih_expander_remove(ioc, sas_expander->sas_address);
		goto retry_expander_search;
	}
}

/**
 * mpt2sas_scsih_reset_handler - reset callback handler (for scsih)
 * @ioc: per adapter object
 * @reset_phase: phase
 *
 * The handler for doing any required cleanup or initialization.
 *
 * The reset phase can be MPT2_IOC_PRE_RESET, MPT2_IOC_AFTER_RESET,
 * MPT2_IOC_DONE_RESET
 *
 * Return nothing.
 */
void
mpt2sas_scsih_reset_handler(struct MPT2SAS_ADAPTER *ioc, int reset_phase)
{
	switch (reset_phase) {
	case MPT2_IOC_PRE_RESET:
		dtmprintk(ioc, printk(MPT2SAS_DEBUG_FMT "%s: "
		    "MPT2_IOC_PRE_RESET\n", ioc->name, __func__));
		_scsih_fw_event_off(ioc);
		break;
	case MPT2_IOC_AFTER_RESET:
		dtmprintk(ioc, printk(MPT2SAS_DEBUG_FMT "%s: "
		    "MPT2_IOC_AFTER_RESET\n", ioc->name, __func__));
		if (ioc->tm_cmds.status & MPT2_CMD_PENDING) {
			ioc->tm_cmds.status |= MPT2_CMD_RESET;
			mpt2sas_base_free_smid(ioc, ioc->tm_cmds.smid);
			complete(&ioc->tm_cmds.done);
		}
		_scsih_fw_event_on(ioc);
		_scsih_flush_running_cmds(ioc);
		break;
	case MPT2_IOC_DONE_RESET:
		dtmprintk(ioc, printk(MPT2SAS_DEBUG_FMT "%s: "
		    "MPT2_IOC_DONE_RESET\n", ioc->name, __func__));
		_scsih_sas_host_refresh(ioc);
		_scsih_search_responding_sas_devices(ioc);
		_scsih_search_responding_raid_devices(ioc);
		_scsih_search_responding_expanders(ioc);
		break;
	case MPT2_IOC_RUNNING:
		dtmprintk(ioc, printk(MPT2SAS_DEBUG_FMT "%s: "
		    "MPT2_IOC_RUNNING\n", ioc->name, __func__));
		_scsih_remove_unresponding_devices(ioc);
		break;
	}
}

/**
 * _firmware_event_work - delayed task for processing firmware events
 * @ioc: per adapter object
 * @work: equal to the fw_event_work object
 * Context: user.
 *
 * Return nothing.
 */
static void
_firmware_event_work(struct work_struct *work)
{
	struct fw_event_work *fw_event = container_of(work,
	    struct fw_event_work, work);
	unsigned long flags;
	struct MPT2SAS_ADAPTER *ioc = fw_event->ioc;

	/* the queue is being flushed so ignore this event */
	spin_lock_irqsave(&ioc->fw_event_lock, flags);
	if (ioc->fw_events_off || ioc->remove_host) {
		spin_unlock_irqrestore(&ioc->fw_event_lock, flags);
		_scsih_fw_event_free(ioc, fw_event);
		return;
	}
	spin_unlock_irqrestore(&ioc->fw_event_lock, flags);

	if (ioc->shost_recovery) {
		_scsih_fw_event_requeue(ioc, fw_event, 1000);
		return;
	}

	switch (fw_event->event) {
	case MPI2_EVENT_SAS_TOPOLOGY_CHANGE_LIST:
		_scsih_sas_topology_change_event(ioc, fw_event);
		break;
	case MPI2_EVENT_SAS_DEVICE_STATUS_CHANGE:
		_scsih_sas_device_status_change_event(ioc,
		    fw_event);
		break;
	case MPI2_EVENT_SAS_DISCOVERY:
		_scsih_sas_discovery_event(ioc,
		    fw_event);
		break;
	case MPI2_EVENT_SAS_BROADCAST_PRIMITIVE:
		_scsih_sas_broadcast_primative_event(ioc,
		    fw_event);
		break;
	case MPI2_EVENT_SAS_ENCL_DEVICE_STATUS_CHANGE:
		_scsih_sas_enclosure_dev_status_change_event(ioc,
		    fw_event);
		break;
	case MPI2_EVENT_IR_CONFIGURATION_CHANGE_LIST:
		_scsih_sas_ir_config_change_event(ioc, fw_event);
		break;
	case MPI2_EVENT_IR_VOLUME:
		_scsih_sas_ir_volume_event(ioc, fw_event);
		break;
	case MPI2_EVENT_IR_PHYSICAL_DISK:
		_scsih_sas_ir_physical_disk_event(ioc, fw_event);
		break;
	case MPI2_EVENT_IR_OPERATION_STATUS:
		_scsih_sas_ir_operation_status_event(ioc, fw_event);
		break;
	case MPI2_EVENT_TASK_SET_FULL:
		_scsih_task_set_full(ioc, fw_event);
		break;
	}
	_scsih_fw_event_free(ioc, fw_event);
}

/**
 * mpt2sas_scsih_event_callback - firmware event handler (called at ISR time)
 * @ioc: per adapter object
 * @msix_index: MSIX table index supplied by the OS
 * @reply: reply message frame(lower 32bit addr)
 * Context: interrupt.
 *
 * This function merely adds a new work task into ioc->firmware_event_thread.
 * The tasks are worked from _firmware_event_work in user context.
 *
 * Return 1 meaning mf should be freed from _base_interrupt
 *        0 means the mf is freed from this function.
 */
u8
mpt2sas_scsih_event_callback(struct MPT2SAS_ADAPTER *ioc, u8 msix_index,
	u32 reply)
{
	struct fw_event_work *fw_event;
	Mpi2EventNotificationReply_t *mpi_reply;
	unsigned long flags;
	u16 event;

	/* events turned off due to host reset or driver unloading */
	spin_lock_irqsave(&ioc->fw_event_lock, flags);
	if (ioc->fw_events_off || ioc->remove_host) {
		spin_unlock_irqrestore(&ioc->fw_event_lock, flags);
		return 1;
	}
	spin_unlock_irqrestore(&ioc->fw_event_lock, flags);

	mpi_reply = mpt2sas_base_get_reply_virt_addr(ioc, reply);
	event = le16_to_cpu(mpi_reply->Event);

	switch (event) {
	/* handle these */
	case MPI2_EVENT_SAS_BROADCAST_PRIMITIVE:
	{
		Mpi2EventDataSasBroadcastPrimitive_t *baen_data =
		    (Mpi2EventDataSasBroadcastPrimitive_t *)
		    mpi_reply->EventData;

		if (baen_data->Primitive !=
		    MPI2_EVENT_PRIMITIVE_ASYNCHRONOUS_EVENT ||
		    ioc->broadcast_aen_busy)
			return 1;
		ioc->broadcast_aen_busy = 1;
		break;
	}

	case MPI2_EVENT_SAS_TOPOLOGY_CHANGE_LIST:
		_scsih_check_topo_delete_events(ioc,
		    (Mpi2EventDataSasTopologyChangeList_t *)
		    mpi_reply->EventData);
		break;

	case MPI2_EVENT_SAS_DEVICE_STATUS_CHANGE:
	case MPI2_EVENT_IR_OPERATION_STATUS:
	case MPI2_EVENT_SAS_DISCOVERY:
	case MPI2_EVENT_SAS_ENCL_DEVICE_STATUS_CHANGE:
	case MPI2_EVENT_IR_VOLUME:
	case MPI2_EVENT_IR_PHYSICAL_DISK:
	case MPI2_EVENT_IR_CONFIGURATION_CHANGE_LIST:
	case MPI2_EVENT_TASK_SET_FULL:
		break;

	default: /* ignore the rest */
		return 1;
	}

	fw_event = kzalloc(sizeof(struct fw_event_work), GFP_ATOMIC);
	if (!fw_event) {
		printk(MPT2SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		return 1;
	}
	fw_event->event_data =
	    kzalloc(mpi_reply->EventDataLength*4, GFP_ATOMIC);
	if (!fw_event->event_data) {
		printk(MPT2SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		kfree(fw_event);
		return 1;
	}

	memcpy(fw_event->event_data, mpi_reply->EventData,
	    mpi_reply->EventDataLength*4);
	fw_event->ioc = ioc;
	fw_event->VF_ID = mpi_reply->VF_ID;
	fw_event->VP_ID = mpi_reply->VP_ID;
	fw_event->event = event;
	_scsih_fw_event_add(ioc, fw_event);
	return 1;
}

/* shost template */
static struct scsi_host_template scsih_driver_template = {
	.module				= THIS_MODULE,
	.name				= "Fusion MPT SAS Host",
	.proc_name			= MPT2SAS_DRIVER_NAME,
	.queuecommand			= _scsih_qcmd,
	.target_alloc			= _scsih_target_alloc,
	.slave_alloc			= _scsih_slave_alloc,
	.slave_configure		= _scsih_slave_configure,
	.target_destroy			= _scsih_target_destroy,
	.slave_destroy			= _scsih_slave_destroy,
	.change_queue_depth 		= _scsih_change_queue_depth,
	.change_queue_type		= _scsih_change_queue_type,
	.eh_abort_handler		= _scsih_abort,
	.eh_device_reset_handler	= _scsih_dev_reset,
	.eh_target_reset_handler	= _scsih_target_reset,
	.eh_host_reset_handler		= _scsih_host_reset,
	.bios_param			= _scsih_bios_param,
	.can_queue			= 1,
	.this_id			= -1,
	.sg_tablesize			= MPT2SAS_SG_DEPTH,
	.max_sectors			= 8192,
	.cmd_per_lun			= 7,
	.use_clustering			= ENABLE_CLUSTERING,
	.shost_attrs			= mpt2sas_host_attrs,
	.sdev_attrs			= mpt2sas_dev_attrs,
};

/**
 * _scsih_expander_node_remove - removing expander device from list.
 * @ioc: per adapter object
 * @sas_expander: the sas_device object
 * Context: Calling function should acquire ioc->sas_node_lock.
 *
 * Removing object and freeing associated memory from the
 * ioc->sas_expander_list.
 *
 * Return nothing.
 */
static void
_scsih_expander_node_remove(struct MPT2SAS_ADAPTER *ioc,
    struct _sas_node *sas_expander)
{
	struct _sas_port *mpt2sas_port;
	struct _sas_device *sas_device;
	struct _sas_node *expander_sibling;
	unsigned long flags;

	if (!sas_expander)
		return;

	/* remove sibling ports attached to this expander */
 retry_device_search:
	list_for_each_entry(mpt2sas_port,
	   &sas_expander->sas_port_list, port_list) {
		if (mpt2sas_port->remote_identify.device_type ==
		    SAS_END_DEVICE) {
			spin_lock_irqsave(&ioc->sas_device_lock, flags);
			sas_device =
			    mpt2sas_scsih_sas_device_find_by_sas_address(ioc,
			   mpt2sas_port->remote_identify.sas_address);
			spin_unlock_irqrestore(&ioc->sas_device_lock, flags);
			if (!sas_device)
				continue;
			_scsih_remove_device(ioc, sas_device);
			if (ioc->shost_recovery)
				return;
			goto retry_device_search;
		}
	}

 retry_expander_search:
	list_for_each_entry(mpt2sas_port,
	   &sas_expander->sas_port_list, port_list) {

		if (mpt2sas_port->remote_identify.device_type ==
		    MPI2_SAS_DEVICE_INFO_EDGE_EXPANDER ||
		    mpt2sas_port->remote_identify.device_type ==
		    MPI2_SAS_DEVICE_INFO_FANOUT_EXPANDER) {

			spin_lock_irqsave(&ioc->sas_node_lock, flags);
			expander_sibling =
			    mpt2sas_scsih_expander_find_by_sas_address(
			    ioc, mpt2sas_port->remote_identify.sas_address);
			spin_unlock_irqrestore(&ioc->sas_node_lock, flags);
			if (!expander_sibling)
				continue;
			_scsih_expander_remove(ioc,
			    expander_sibling->sas_address);
			if (ioc->shost_recovery)
				return;
			goto retry_expander_search;
		}
	}

	mpt2sas_transport_port_remove(ioc, sas_expander->sas_address,
	    sas_expander->sas_address_parent);

	printk(MPT2SAS_INFO_FMT "expander_remove: handle"
	   "(0x%04x), sas_addr(0x%016llx)\n", ioc->name,
	    sas_expander->handle, (unsigned long long)
	    sas_expander->sas_address);

	list_del(&sas_expander->list);
	kfree(sas_expander->phy);
	kfree(sas_expander);
}

/**
 * _scsih_remove - detach and remove add host
 * @pdev: PCI device struct
 *
 * Return nothing.
 */
static void __devexit
_scsih_remove(struct pci_dev *pdev)
{
	struct Scsi_Host *shost = pci_get_drvdata(pdev);
	struct MPT2SAS_ADAPTER *ioc = shost_priv(shost);
	struct _sas_port *mpt2sas_port;
	struct _sas_device *sas_device;
	struct _sas_node *expander_sibling;
	struct workqueue_struct	*wq;
	unsigned long flags;

	ioc->remove_host = 1;
	_scsih_fw_event_off(ioc);

	spin_lock_irqsave(&ioc->fw_event_lock, flags);
	wq = ioc->firmware_event_thread;
	ioc->firmware_event_thread = NULL;
	spin_unlock_irqrestore(&ioc->fw_event_lock, flags);
	if (wq)
		destroy_workqueue(wq);

	/* free ports attached to the sas_host */
 retry_again:
	list_for_each_entry(mpt2sas_port,
	   &ioc->sas_hba.sas_port_list, port_list) {
		if (mpt2sas_port->remote_identify.device_type ==
		    SAS_END_DEVICE) {
			sas_device =
			    mpt2sas_scsih_sas_device_find_by_sas_address(ioc,
			   mpt2sas_port->remote_identify.sas_address);
			if (sas_device) {
				_scsih_remove_device(ioc, sas_device);
				goto retry_again;
			}
		} else {
			expander_sibling =
			    mpt2sas_scsih_expander_find_by_sas_address(ioc,
			    mpt2sas_port->remote_identify.sas_address);
			if (expander_sibling) {
				_scsih_expander_remove(ioc,
				    expander_sibling->sas_address);
				goto retry_again;
			}
		}
	}

	/* free phys attached to the sas_host */
	if (ioc->sas_hba.num_phys) {
		kfree(ioc->sas_hba.phy);
		ioc->sas_hba.phy = NULL;
		ioc->sas_hba.num_phys = 0;
	}

	sas_remove_host(shost);
	mpt2sas_base_detach(ioc);
	list_del(&ioc->list);
	scsi_remove_host(shost);
	scsi_host_put(shost);
}

/**
 * _scsih_probe_boot_devices - reports 1st device
 * @ioc: per adapter object
 *
 * If specified in bios page 2, this routine reports the 1st
 * device scsi-ml or sas transport for persistent boot device
 * purposes.  Please refer to function _scsih_determine_boot_device()
 */
static void
_scsih_probe_boot_devices(struct MPT2SAS_ADAPTER *ioc)
{
	u8 is_raid;
	void *device;
	struct _sas_device *sas_device;
	struct _raid_device *raid_device;
	u16 handle;
	u64 sas_address_parent;
	u64 sas_address;
	unsigned long flags;
	int rc;

	device = NULL;
	if (ioc->req_boot_device.device) {
		device =  ioc->req_boot_device.device;
		is_raid = ioc->req_boot_device.is_raid;
	} else if (ioc->req_alt_boot_device.device) {
		device =  ioc->req_alt_boot_device.device;
		is_raid = ioc->req_alt_boot_device.is_raid;
	} else if (ioc->current_boot_device.device) {
		device =  ioc->current_boot_device.device;
		is_raid = ioc->current_boot_device.is_raid;
	}

	if (!device)
		return;

	if (is_raid) {
		raid_device = device;
		rc = scsi_add_device(ioc->shost, RAID_CHANNEL,
		    raid_device->id, 0);
		if (rc)
			_scsih_raid_device_remove(ioc, raid_device);
	} else {
		sas_device = device;
		handle = sas_device->handle;
		sas_address_parent = sas_device->sas_address_parent;
		sas_address = sas_device->sas_address;
		spin_lock_irqsave(&ioc->sas_device_lock, flags);
		list_move_tail(&sas_device->list, &ioc->sas_device_list);
		spin_unlock_irqrestore(&ioc->sas_device_lock, flags);
		if (!mpt2sas_transport_port_add(ioc, sas_device->handle,
		    sas_device->sas_address_parent)) {
			_scsih_sas_device_remove(ioc, sas_device);
		} else if (!sas_device->starget) {
			mpt2sas_transport_port_remove(ioc, sas_address,
			    sas_address_parent);
			_scsih_sas_device_remove(ioc, sas_device);
		}
	}
}

/**
 * _scsih_probe_raid - reporting raid volumes to scsi-ml
 * @ioc: per adapter object
 *
 * Called during initial loading of the driver.
 */
static void
_scsih_probe_raid(struct MPT2SAS_ADAPTER *ioc)
{
	struct _raid_device *raid_device, *raid_next;
	int rc;

	list_for_each_entry_safe(raid_device, raid_next,
	    &ioc->raid_device_list, list) {
		if (raid_device->starget)
			continue;
		rc = scsi_add_device(ioc->shost, RAID_CHANNEL,
		    raid_device->id, 0);
		if (rc)
			_scsih_raid_device_remove(ioc, raid_device);
	}
}

/**
 * _scsih_probe_sas - reporting sas devices to sas transport
 * @ioc: per adapter object
 *
 * Called during initial loading of the driver.
 */
static void
_scsih_probe_sas(struct MPT2SAS_ADAPTER *ioc)
{
	struct _sas_device *sas_device, *next;
	unsigned long flags;

	/* SAS Device List */
	list_for_each_entry_safe(sas_device, next, &ioc->sas_device_init_list,
	    list) {
		spin_lock_irqsave(&ioc->sas_device_lock, flags);
		list_move_tail(&sas_device->list, &ioc->sas_device_list);
		spin_unlock_irqrestore(&ioc->sas_device_lock, flags);

		if (!mpt2sas_transport_port_add(ioc, sas_device->handle,
		    sas_device->sas_address_parent)) {
			_scsih_sas_device_remove(ioc, sas_device);
		} else if (!sas_device->starget) {
			mpt2sas_transport_port_remove(ioc,
			    sas_device->sas_address,
			    sas_device->sas_address_parent);
			_scsih_sas_device_remove(ioc, sas_device);
		}
	}
}

/**
 * _scsih_probe_devices - probing for devices
 * @ioc: per adapter object
 *
 * Called during initial loading of the driver.
 */
static void
_scsih_probe_devices(struct MPT2SAS_ADAPTER *ioc)
{
	u16 volume_mapping_flags =
	    le16_to_cpu(ioc->ioc_pg8.IRVolumeMappingFlags) &
	    MPI2_IOCPAGE8_IRFLAGS_MASK_VOLUME_MAPPING_MODE;

	if (!(ioc->facts.ProtocolFlags & MPI2_IOCFACTS_PROTOCOL_SCSI_INITIATOR))
		return;  /* return when IOC doesn't support initiator mode */

	_scsih_probe_boot_devices(ioc);

	if (ioc->ir_firmware) {
		if ((volume_mapping_flags &
		     MPI2_IOCPAGE8_IRFLAGS_HIGH_VOLUME_MAPPING)) {
			_scsih_probe_sas(ioc);
			_scsih_probe_raid(ioc);
		} else {
			_scsih_probe_raid(ioc);
			_scsih_probe_sas(ioc);
		}
	} else
		_scsih_probe_sas(ioc);
}

/**
 * _scsih_probe - attach and add scsi host
 * @pdev: PCI device struct
 * @id: pci device id
 *
 * Returns 0 success, anything else error.
 */
static int
_scsih_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct MPT2SAS_ADAPTER *ioc;
	struct Scsi_Host *shost;

	shost = scsi_host_alloc(&scsih_driver_template,
	    sizeof(struct MPT2SAS_ADAPTER));
	if (!shost)
		return -ENODEV;

	/* init local params */
	ioc = shost_priv(shost);
	memset(ioc, 0, sizeof(struct MPT2SAS_ADAPTER));
	INIT_LIST_HEAD(&ioc->list);
	list_add_tail(&ioc->list, &mpt2sas_ioc_list);
	ioc->shost = shost;
	ioc->id = mpt_ids++;
	sprintf(ioc->name, "%s%d", MPT2SAS_DRIVER_NAME, ioc->id);
	ioc->pdev = pdev;
	ioc->scsi_io_cb_idx = scsi_io_cb_idx;
	ioc->tm_cb_idx = tm_cb_idx;
	ioc->ctl_cb_idx = ctl_cb_idx;
	ioc->base_cb_idx = base_cb_idx;
	ioc->transport_cb_idx = transport_cb_idx;
	ioc->config_cb_idx = config_cb_idx;
	ioc->tm_tr_cb_idx = tm_tr_cb_idx;
	ioc->tm_sas_control_cb_idx = tm_sas_control_cb_idx;
	ioc->logging_level = logging_level;
	/* misc semaphores and spin locks */
	spin_lock_init(&ioc->ioc_reset_in_progress_lock);
	spin_lock_init(&ioc->scsi_lookup_lock);
	spin_lock_init(&ioc->sas_device_lock);
	spin_lock_init(&ioc->sas_node_lock);
	spin_lock_init(&ioc->fw_event_lock);
	spin_lock_init(&ioc->raid_device_lock);

	INIT_LIST_HEAD(&ioc->sas_device_list);
	INIT_LIST_HEAD(&ioc->sas_device_init_list);
	INIT_LIST_HEAD(&ioc->sas_expander_list);
	INIT_LIST_HEAD(&ioc->fw_event_list);
	INIT_LIST_HEAD(&ioc->raid_device_list);
	INIT_LIST_HEAD(&ioc->sas_hba.sas_port_list);
	INIT_LIST_HEAD(&ioc->delayed_tr_list);

	/* init shost parameters */
	shost->max_cmd_len = 16;
	shost->max_lun = max_lun;
	shost->transportt = mpt2sas_transport_template;
	shost->unique_id = ioc->id;

	if ((scsi_add_host(shost, &pdev->dev))) {
		printk(MPT2SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		list_del(&ioc->list);
		goto out_add_shost_fail;
	}

	scsi_host_set_prot(shost, SHOST_DIF_TYPE1_PROTECTION
	    | SHOST_DIF_TYPE3_PROTECTION);
	scsi_host_set_guard(shost, SHOST_DIX_GUARD_CRC);

	/* event thread */
	snprintf(ioc->firmware_event_name, sizeof(ioc->firmware_event_name),
	    "fw_event%d", ioc->id);
	ioc->firmware_event_thread = create_singlethread_workqueue(
	    ioc->firmware_event_name);
	if (!ioc->firmware_event_thread) {
		printk(MPT2SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		goto out_thread_fail;
	}

	ioc->wait_for_port_enable_to_complete = 1;
	if ((mpt2sas_base_attach(ioc))) {
		printk(MPT2SAS_ERR_FMT "failure at %s:%d/%s()!\n",
		    ioc->name, __FILE__, __LINE__, __func__);
		goto out_attach_fail;
	}

	ioc->wait_for_port_enable_to_complete = 0;
	_scsih_probe_devices(ioc);
	return 0;

 out_attach_fail:
	destroy_workqueue(ioc->firmware_event_thread);
 out_thread_fail:
	list_del(&ioc->list);
	scsi_remove_host(shost);
 out_add_shost_fail:
	return -ENODEV;
}

#ifdef CONFIG_PM
/**
 * _scsih_suspend - power management suspend main entry point
 * @pdev: PCI device struct
 * @state: PM state change to (usually PCI_D3)
 *
 * Returns 0 success, anything else error.
 */
static int
_scsih_suspend(struct pci_dev *pdev, pm_message_t state)
{
	struct Scsi_Host *shost = pci_get_drvdata(pdev);
	struct MPT2SAS_ADAPTER *ioc = shost_priv(shost);
	u32 device_state;

	mpt2sas_base_stop_watchdog(ioc);
	flush_scheduled_work();
	scsi_block_requests(shost);
	device_state = pci_choose_state(pdev, state);
	printk(MPT2SAS_INFO_FMT "pdev=0x%p, slot=%s, entering "
	    "operating state [D%d]\n", ioc->name, pdev,
	    pci_name(pdev), device_state);

	mpt2sas_base_free_resources(ioc);
	pci_save_state(pdev);
	pci_disable_device(pdev);
	pci_set_power_state(pdev, device_state);
	return 0;
}

/**
 * _scsih_resume - power management resume main entry point
 * @pdev: PCI device struct
 *
 * Returns 0 success, anything else error.
 */
static int
_scsih_resume(struct pci_dev *pdev)
{
	struct Scsi_Host *shost = pci_get_drvdata(pdev);
	struct MPT2SAS_ADAPTER *ioc = shost_priv(shost);
	u32 device_state = pdev->current_state;
	int r;

	printk(MPT2SAS_INFO_FMT "pdev=0x%p, slot=%s, previous "
	    "operating state [D%d]\n", ioc->name, pdev,
	    pci_name(pdev), device_state);

	pci_set_power_state(pdev, PCI_D0);
	pci_enable_wake(pdev, PCI_D0, 0);
	pci_restore_state(pdev);
	ioc->pdev = pdev;
	r = mpt2sas_base_map_resources(ioc);
	if (r)
		return r;

	mpt2sas_base_hard_reset_handler(ioc, CAN_SLEEP, SOFT_RESET);
	scsi_unblock_requests(shost);
	mpt2sas_base_start_watchdog(ioc);
	return 0;
}
#endif /* CONFIG_PM */


static struct pci_driver scsih_driver = {
	.name		= MPT2SAS_DRIVER_NAME,
	.id_table	= scsih_pci_table,
	.probe		= _scsih_probe,
	.remove		= __devexit_p(_scsih_remove),
#ifdef CONFIG_PM
	.suspend	= _scsih_suspend,
	.resume		= _scsih_resume,
#endif
};


/**
 * _scsih_init - main entry point for this driver.
 *
 * Returns 0 success, anything else error.
 */
static int __init
_scsih_init(void)
{
	int error;

	mpt_ids = 0;
	printk(KERN_INFO "%s version %s loaded\n", MPT2SAS_DRIVER_NAME,
	    MPT2SAS_DRIVER_VERSION);

	mpt2sas_transport_template =
	    sas_attach_transport(&mpt2sas_transport_functions);
	if (!mpt2sas_transport_template)
		return -ENODEV;

	mpt2sas_base_initialize_callback_handler();

	 /* queuecommand callback hander */
	scsi_io_cb_idx = mpt2sas_base_register_callback_handler(_scsih_io_done);

	/* task managment callback handler */
	tm_cb_idx = mpt2sas_base_register_callback_handler(_scsih_tm_done);

	/* base internal commands callback handler */
	base_cb_idx = mpt2sas_base_register_callback_handler(mpt2sas_base_done);

	/* transport internal commands callback handler */
	transport_cb_idx = mpt2sas_base_register_callback_handler(
	    mpt2sas_transport_done);

	/* configuration page API internal commands callback handler */
	config_cb_idx = mpt2sas_base_register_callback_handler(
	    mpt2sas_config_done);

	/* ctl module callback handler */
	ctl_cb_idx = mpt2sas_base_register_callback_handler(mpt2sas_ctl_done);

	tm_tr_cb_idx = mpt2sas_base_register_callback_handler(
	    _scsih_tm_tr_complete);
	tm_sas_control_cb_idx = mpt2sas_base_register_callback_handler(
	    _scsih_sas_control_complete);

	mpt2sas_ctl_init();

	error = pci_register_driver(&scsih_driver);
	if (error)
		sas_release_transport(mpt2sas_transport_template);

	return error;
}

/**
 * _scsih_exit - exit point for this driver (when it is a module).
 *
 * Returns 0 success, anything else error.
 */
static void __exit
_scsih_exit(void)
{
	printk(KERN_INFO "mpt2sas version %s unloading\n",
	    MPT2SAS_DRIVER_VERSION);

	pci_unregister_driver(&scsih_driver);

	sas_release_transport(mpt2sas_transport_template);
	mpt2sas_base_release_callback_handler(scsi_io_cb_idx);
	mpt2sas_base_release_callback_handler(tm_cb_idx);
	mpt2sas_base_release_callback_handler(base_cb_idx);
	mpt2sas_base_release_callback_handler(transport_cb_idx);
	mpt2sas_base_release_callback_handler(config_cb_idx);
	mpt2sas_base_release_callback_handler(ctl_cb_idx);

	mpt2sas_base_release_callback_handler(tm_tr_cb_idx);
	mpt2sas_base_release_callback_handler(tm_sas_control_cb_idx);

	mpt2sas_ctl_exit();
}

module_init(_scsih_init);
module_exit(_scsih_exit);
