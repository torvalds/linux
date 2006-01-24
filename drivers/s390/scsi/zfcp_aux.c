/*
 *
 * linux/drivers/s390/scsi/zfcp_aux.c
 *
 * FCP adapter driver for IBM eServer zSeries
 *
 * (C) Copyright IBM Corp. 2002, 2004
 *
 * Author(s): Martin Peschke <mpeschke@de.ibm.com>
 *            Raimund Schroeder <raimund.schroeder@de.ibm.com>
 *            Aron Zeh
 *            Wolfgang Taphorn
 *            Stefan Bader <stefan.bader@de.ibm.com>
 *            Heiko Carstens <heiko.carstens@de.ibm.com>
 *            Andreas Herrmann <aherrman@de.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#define ZFCP_AUX_REVISION "$Revision: 1.145 $"

#include "zfcp_ext.h"

/* accumulated log level (module parameter) */
static u32 loglevel = ZFCP_LOG_LEVEL_DEFAULTS;
static char *device;
/*********************** FUNCTION PROTOTYPES *********************************/

/* written against the module interface */
static int __init  zfcp_module_init(void);

/* FCP related */
static void zfcp_ns_gid_pn_handler(unsigned long);

/* miscellaneous */
static inline int zfcp_sg_list_alloc(struct zfcp_sg_list *, size_t);
static inline void zfcp_sg_list_free(struct zfcp_sg_list *);
static inline int zfcp_sg_list_copy_from_user(struct zfcp_sg_list *,
					      void __user *, size_t);
static inline int zfcp_sg_list_copy_to_user(void __user *,
					    struct zfcp_sg_list *, size_t);

static long zfcp_cfdc_dev_ioctl(struct file *, unsigned int, unsigned long);

#define ZFCP_CFDC_IOC_MAGIC                     0xDD
#define ZFCP_CFDC_IOC \
	_IOWR(ZFCP_CFDC_IOC_MAGIC, 0, struct zfcp_cfdc_sense_data)


static struct file_operations zfcp_cfdc_fops = {
	.unlocked_ioctl = zfcp_cfdc_dev_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = zfcp_cfdc_dev_ioctl
#endif
};

static struct miscdevice zfcp_cfdc_misc = {
	.minor = ZFCP_CFDC_DEV_MINOR,
	.name = ZFCP_CFDC_DEV_NAME,
	.fops = &zfcp_cfdc_fops
};

/*********************** KERNEL/MODULE PARAMETERS  ***************************/

/* declare driver module init/cleanup functions */
module_init(zfcp_module_init);

MODULE_AUTHOR("Heiko Carstens <heiko.carstens@de.ibm.com>, "
	      "Andreas Herrman <aherrman@de.ibm.com>, "
	      "Martin Peschke <mpeschke@de.ibm.com>, "
	      "Raimund Schroeder <raimund.schroeder@de.ibm.com>, "
	      "Wolfgang Taphorn <taphorn@de.ibm.com>, "
	      "Aron Zeh <arzeh@de.ibm.com>, "
	      "IBM Deutschland Entwicklung GmbH");
MODULE_DESCRIPTION
    ("FCP (SCSI over Fibre Channel) HBA driver for IBM eServer zSeries");
MODULE_LICENSE("GPL");

module_param(device, charp, 0400);
MODULE_PARM_DESC(device, "specify initial device");

module_param(loglevel, uint, 0400);
MODULE_PARM_DESC(loglevel,
		 "log levels, 8 nibbles: "
		 "FC ERP QDIO CIO Config FSF SCSI Other, "
		 "levels: 0=none 1=normal 2=devel 3=trace");

/****************************************************************/
/************** Functions without logging ***********************/
/****************************************************************/

void
_zfcp_hex_dump(char *addr, int count)
{
	int i;
	for (i = 0; i < count; i++) {
		printk("%02x", addr[i]);
		if ((i % 4) == 3)
			printk(" ");
		if ((i % 32) == 31)
			printk("\n");
	}
	if (((i-1) % 32) != 31)
		printk("\n");
}

/****************************************************************/
/************** Uncategorised Functions *************************/
/****************************************************************/

#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_OTHER

/**
 * zfcp_device_setup - setup function
 * @str: pointer to parameter string
 *
 * Parse "device=..." parameter string.
 */
static int __init
zfcp_device_setup(char *devstr)
{
	char *tmp, *str;
	size_t len;

	if (!devstr)
		return 0;

	len = strlen(devstr) + 1;
	str = (char *) kmalloc(len, GFP_KERNEL);
	if (!str)
		goto err_out;
	memcpy(str, devstr, len);

	tmp = strchr(str, ',');
	if (!tmp)
		goto err_out;
	*tmp++ = '\0';
	strncpy(zfcp_data.init_busid, str, BUS_ID_SIZE);
	zfcp_data.init_busid[BUS_ID_SIZE-1] = '\0';

	zfcp_data.init_wwpn = simple_strtoull(tmp, &tmp, 0);
	if (*tmp++ != ',')
		goto err_out;
	if (*tmp == '\0')
		goto err_out;

	zfcp_data.init_fcp_lun = simple_strtoull(tmp, &tmp, 0);
	if (*tmp != '\0')
		goto err_out;
	kfree(str);
	return 1;

 err_out:
	ZFCP_LOG_NORMAL("Parse error for device parameter string %s\n", str);
	kfree(str);
	return 0;
}

static void __init
zfcp_init_device_configure(void)
{
	struct zfcp_adapter *adapter;
	struct zfcp_port *port;
	struct zfcp_unit *unit;

	down(&zfcp_data.config_sema);
	read_lock_irq(&zfcp_data.config_lock);
	adapter = zfcp_get_adapter_by_busid(zfcp_data.init_busid);
	if (adapter)
		zfcp_adapter_get(adapter);
	read_unlock_irq(&zfcp_data.config_lock);

	if (adapter == NULL)
		goto out_adapter;
	port = zfcp_port_enqueue(adapter, zfcp_data.init_wwpn, 0, 0);
	if (!port)
		goto out_port;
	unit = zfcp_unit_enqueue(port, zfcp_data.init_fcp_lun);
	if (!unit)
		goto out_unit;
	up(&zfcp_data.config_sema);
	ccw_device_set_online(adapter->ccw_device);
	zfcp_erp_wait(adapter);
	down(&zfcp_data.config_sema);
	zfcp_unit_put(unit);
 out_unit:
	zfcp_port_put(port);
 out_port:
	zfcp_adapter_put(adapter);
 out_adapter:
	up(&zfcp_data.config_sema);
	return;
}

static int __init
zfcp_module_init(void)
{

	int retval = 0;

	atomic_set(&zfcp_data.loglevel, loglevel);

	/* initialize adapter list */
	INIT_LIST_HEAD(&zfcp_data.adapter_list_head);

	/* initialize adapters to be removed list head */
	INIT_LIST_HEAD(&zfcp_data.adapter_remove_lh);

	zfcp_transport_template = fc_attach_transport(&zfcp_transport_functions);
	if (!zfcp_transport_template)
		return -ENODEV;

	retval = misc_register(&zfcp_cfdc_misc);
	if (retval != 0) {
		ZFCP_LOG_INFO("registration of misc device "
			      "zfcp_cfdc failed\n");
		goto out;
	}

	ZFCP_LOG_TRACE("major/minor for zfcp_cfdc: %d/%d\n",
		       ZFCP_CFDC_DEV_MAJOR, zfcp_cfdc_misc.minor);

	/* Initialise proc semaphores */
	sema_init(&zfcp_data.config_sema, 1);

	/* initialise configuration rw lock */
	rwlock_init(&zfcp_data.config_lock);

	/* save address of data structure managing the driver module */
	zfcp_data.scsi_host_template.module = THIS_MODULE;

	/* setup dynamic I/O */
	retval = zfcp_ccw_register();
	if (retval) {
		ZFCP_LOG_NORMAL("registration with common I/O layer failed\n");
		goto out_ccw_register;
	}

	if (zfcp_device_setup(device))
		zfcp_init_device_configure();

	goto out;

 out_ccw_register:
	misc_deregister(&zfcp_cfdc_misc);
 out:
	return retval;
}

/*
 * function:    zfcp_cfdc_dev_ioctl
 *
 * purpose:     Handle control file upload/download transaction via IOCTL
 *		interface
 *
 * returns:     0           - Operation completed successfuly
 *              -ENOTTY     - Unknown IOCTL command
 *              -EINVAL     - Invalid sense data record
 *              -ENXIO      - The FCP adapter is not available
 *              -EOPNOTSUPP - The FCP adapter does not have CFDC support
 *              -ENOMEM     - Insufficient memory
 *              -EFAULT     - User space memory I/O operation fault
 *              -EPERM      - Cannot create or queue FSF request or create SBALs
 *              -ERESTARTSYS- Received signal (is mapped to EAGAIN by VFS)
 */
static long
zfcp_cfdc_dev_ioctl(struct file *file, unsigned int command,
		    unsigned long buffer)
{
	struct zfcp_cfdc_sense_data *sense_data, __user *sense_data_user;
	struct zfcp_adapter *adapter = NULL;
	struct zfcp_fsf_req *fsf_req = NULL;
	struct zfcp_sg_list *sg_list = NULL;
	u32 fsf_command, option;
	char *bus_id = NULL;
	int retval = 0;

	sense_data = kmalloc(sizeof(struct zfcp_cfdc_sense_data), GFP_KERNEL);
	if (sense_data == NULL) {
		retval = -ENOMEM;
		goto out;
	}

	sg_list = kmalloc(sizeof(struct zfcp_sg_list), GFP_KERNEL);
	if (sg_list == NULL) {
		retval = -ENOMEM;
		goto out;
	}
	memset(sg_list, 0, sizeof(*sg_list));

	if (command != ZFCP_CFDC_IOC) {
		ZFCP_LOG_INFO("IOC request code 0x%x invalid\n", command);
		retval = -ENOTTY;
		goto out;
	}

	if ((sense_data_user = (void __user *) buffer) == NULL) {
		ZFCP_LOG_INFO("sense data record is required\n");
		retval = -EINVAL;
		goto out;
	}

	retval = copy_from_user(sense_data, sense_data_user,
				sizeof(struct zfcp_cfdc_sense_data));
	if (retval) {
		retval = -EFAULT;
		goto out;
	}

	if (sense_data->signature != ZFCP_CFDC_SIGNATURE) {
		ZFCP_LOG_INFO("invalid sense data request signature 0x%08x\n",
			      ZFCP_CFDC_SIGNATURE);
		retval = -EINVAL;
		goto out;
	}

	switch (sense_data->command) {

	case ZFCP_CFDC_CMND_DOWNLOAD_NORMAL:
		fsf_command = FSF_QTCB_DOWNLOAD_CONTROL_FILE;
		option = FSF_CFDC_OPTION_NORMAL_MODE;
		break;

	case ZFCP_CFDC_CMND_DOWNLOAD_FORCE:
		fsf_command = FSF_QTCB_DOWNLOAD_CONTROL_FILE;
		option = FSF_CFDC_OPTION_FORCE;
		break;

	case ZFCP_CFDC_CMND_FULL_ACCESS:
		fsf_command = FSF_QTCB_DOWNLOAD_CONTROL_FILE;
		option = FSF_CFDC_OPTION_FULL_ACCESS;
		break;

	case ZFCP_CFDC_CMND_RESTRICTED_ACCESS:
		fsf_command = FSF_QTCB_DOWNLOAD_CONTROL_FILE;
		option = FSF_CFDC_OPTION_RESTRICTED_ACCESS;
		break;

	case ZFCP_CFDC_CMND_UPLOAD:
		fsf_command = FSF_QTCB_UPLOAD_CONTROL_FILE;
		option = 0;
		break;

	default:
		ZFCP_LOG_INFO("invalid command code 0x%08x\n",
			      sense_data->command);
		retval = -EINVAL;
		goto out;
	}

	bus_id = kmalloc(BUS_ID_SIZE, GFP_KERNEL);
	if (bus_id == NULL) {
		retval = -ENOMEM;
		goto out;
	}
	snprintf(bus_id, BUS_ID_SIZE, "%d.%d.%04x",
		(sense_data->devno >> 24),
		(sense_data->devno >> 16) & 0xFF,
		(sense_data->devno & 0xFFFF));

	read_lock_irq(&zfcp_data.config_lock);
	adapter = zfcp_get_adapter_by_busid(bus_id);
	if (adapter)
		zfcp_adapter_get(adapter);
	read_unlock_irq(&zfcp_data.config_lock);

	kfree(bus_id);

	if (adapter == NULL) {
		ZFCP_LOG_INFO("invalid adapter\n");
		retval = -ENXIO;
		goto out;
	}

	if (sense_data->command & ZFCP_CFDC_WITH_CONTROL_FILE) {
		retval = zfcp_sg_list_alloc(sg_list,
					    ZFCP_CFDC_MAX_CONTROL_FILE_SIZE);
		if (retval) {
			retval = -ENOMEM;
			goto out;
		}
	}

	if ((sense_data->command & ZFCP_CFDC_DOWNLOAD) &&
	    (sense_data->command & ZFCP_CFDC_WITH_CONTROL_FILE)) {
		retval = zfcp_sg_list_copy_from_user(
			sg_list, &sense_data_user->control_file,
			ZFCP_CFDC_MAX_CONTROL_FILE_SIZE);
		if (retval) {
			retval = -EFAULT;
			goto out;
		}
	}

	retval = zfcp_fsf_control_file(adapter, &fsf_req, fsf_command,
				       option, sg_list);
	if (retval)
		goto out;

	if ((fsf_req->qtcb->prefix.prot_status != FSF_PROT_GOOD) &&
	    (fsf_req->qtcb->prefix.prot_status != FSF_PROT_FSF_STATUS_PRESENTED)) {
		retval = -ENXIO;
		goto out;
	}

	sense_data->fsf_status = fsf_req->qtcb->header.fsf_status;
	memcpy(&sense_data->fsf_status_qual,
	       &fsf_req->qtcb->header.fsf_status_qual,
	       sizeof(union fsf_status_qual));
	memcpy(&sense_data->payloads, &fsf_req->qtcb->bottom.support.els, 256);

	retval = copy_to_user(sense_data_user, sense_data,
		sizeof(struct zfcp_cfdc_sense_data));
	if (retval) {
		retval = -EFAULT;
		goto out;
	}

	if (sense_data->command & ZFCP_CFDC_UPLOAD) {
		retval = zfcp_sg_list_copy_to_user(
			&sense_data_user->control_file, sg_list,
			ZFCP_CFDC_MAX_CONTROL_FILE_SIZE);
		if (retval) {
			retval = -EFAULT;
			goto out;
		}
	}

 out:
	if (fsf_req != NULL)
		zfcp_fsf_req_free(fsf_req);

	if ((adapter != NULL) && (retval != -ENXIO))
		zfcp_adapter_put(adapter);

	if (sg_list != NULL) {
		zfcp_sg_list_free(sg_list);
		kfree(sg_list);
	}

	kfree(sense_data);

	return retval;
}


/**
 * zfcp_sg_list_alloc - create a scatter-gather list of the specified size
 * @sg_list: structure describing a scatter gather list
 * @size: size of scatter-gather list
 * Return: 0 on success, else -ENOMEM
 *
 * In sg_list->sg a pointer to the created scatter-gather list is returned,
 * or NULL if we run out of memory. sg_list->count specifies the number of
 * elements of the scatter-gather list. The maximum size of a single element
 * in the scatter-gather list is PAGE_SIZE.
 */
static inline int
zfcp_sg_list_alloc(struct zfcp_sg_list *sg_list, size_t size)
{
	struct scatterlist *sg;
	unsigned int i;
	int retval = 0;
	void *address;

	BUG_ON(sg_list == NULL);

	sg_list->count = size >> PAGE_SHIFT;
	if (size & ~PAGE_MASK)
		sg_list->count++;
	sg_list->sg = kmalloc(sg_list->count * sizeof(struct scatterlist),
			      GFP_KERNEL);
	if (sg_list->sg == NULL) {
		sg_list->count = 0;
		retval = -ENOMEM;
		goto out;
	}
	memset(sg_list->sg, 0, sg_list->count * sizeof(struct scatterlist));

	for (i = 0, sg = sg_list->sg; i < sg_list->count; i++, sg++) {
		sg->length = min(size, PAGE_SIZE);
		sg->offset = 0;
		address = (void *) get_zeroed_page(GFP_KERNEL);
		if (address == NULL) {
			sg_list->count = i;
			zfcp_sg_list_free(sg_list);
			retval = -ENOMEM;
			goto out;
		}
		zfcp_address_to_sg(address, sg);
		size -= sg->length;
	}

 out:
	return retval;
}


/**
 * zfcp_sg_list_free - free memory of a scatter-gather list
 * @sg_list: structure describing a scatter-gather list
 *
 * Memory for each element in the scatter-gather list is freed.
 * Finally sg_list->sg is freed itself and sg_list->count is reset.
 */
static inline void
zfcp_sg_list_free(struct zfcp_sg_list *sg_list)
{
	struct scatterlist *sg;
	unsigned int i;

	BUG_ON(sg_list == NULL);

	for (i = 0, sg = sg_list->sg; i < sg_list->count; i++, sg++)
		free_page((unsigned long) zfcp_sg_to_address(sg));

	sg_list->count = 0;
	kfree(sg_list->sg);
}

/**
 * zfcp_sg_size - determine size of a scatter-gather list
 * @sg: array of (struct scatterlist)
 * @sg_count: elements in array
 * Return: size of entire scatter-gather list
 */
size_t
zfcp_sg_size(struct scatterlist *sg, unsigned int sg_count)
{
	unsigned int i;
	struct scatterlist *p;
	size_t size;

	size = 0;
	for (i = 0, p = sg; i < sg_count; i++, p++) {
		BUG_ON(p == NULL);
		size += p->length;
	}

	return size;
}


/**
 * zfcp_sg_list_copy_from_user -copy data from user space to scatter-gather list
 * @sg_list: structure describing a scatter-gather list
 * @user_buffer: pointer to buffer in user space
 * @size: number of bytes to be copied
 * Return: 0 on success, -EFAULT if copy_from_user fails.
 */
static inline int
zfcp_sg_list_copy_from_user(struct zfcp_sg_list *sg_list,
			    void __user *user_buffer,
                            size_t size)
{
	struct scatterlist *sg;
	unsigned int length;
	void *zfcp_buffer;
	int retval = 0;

	BUG_ON(sg_list == NULL);

	if (zfcp_sg_size(sg_list->sg, sg_list->count) < size)
		return -EFAULT;

	for (sg = sg_list->sg; size > 0; sg++) {
		length = min((unsigned int)size, sg->length);
		zfcp_buffer = zfcp_sg_to_address(sg);
		if (copy_from_user(zfcp_buffer, user_buffer, length)) {
			retval = -EFAULT;
			goto out;
		}
		user_buffer += length;
		size -= length;
	}

 out:
	return retval;
}


/**
 * zfcp_sg_list_copy_to_user - copy data from scatter-gather list to user space
 * @user_buffer: pointer to buffer in user space
 * @sg_list: structure describing a scatter-gather list
 * @size: number of bytes to be copied
 * Return: 0 on success, -EFAULT if copy_to_user fails
 */
static inline int
zfcp_sg_list_copy_to_user(void __user  *user_buffer,
			  struct zfcp_sg_list *sg_list,
                          size_t size)
{
	struct scatterlist *sg;
	unsigned int length;
	void *zfcp_buffer;
	int retval = 0;

	BUG_ON(sg_list == NULL);

	if (zfcp_sg_size(sg_list->sg, sg_list->count) < size)
		return -EFAULT;

	for (sg = sg_list->sg; size > 0; sg++) {
		length = min((unsigned int) size, sg->length);
		zfcp_buffer = zfcp_sg_to_address(sg);
		if (copy_to_user(user_buffer, zfcp_buffer, length)) {
			retval = -EFAULT;
			goto out;
		}
		user_buffer += length;
		size -= length;
	}

 out:
	return retval;
}


#undef ZFCP_LOG_AREA

/****************************************************************/
/****** Functions for configuration/set-up of structures ********/
/****************************************************************/

#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_CONFIG

/**
 * zfcp_get_unit_by_lun - find unit in unit list of port by FCP LUN
 * @port: pointer to port to search for unit
 * @fcp_lun: FCP LUN to search for
 * Traverse list of all units of a port and return pointer to a unit
 * with the given FCP LUN.
 */
struct zfcp_unit *
zfcp_get_unit_by_lun(struct zfcp_port *port, fcp_lun_t fcp_lun)
{
	struct zfcp_unit *unit;
	int found = 0;

	list_for_each_entry(unit, &port->unit_list_head, list) {
		if ((unit->fcp_lun == fcp_lun) &&
		    !atomic_test_mask(ZFCP_STATUS_COMMON_REMOVE, &unit->status))
		{
			found = 1;
			break;
		}
	}
	return found ? unit : NULL;
}

/**
 * zfcp_get_port_by_wwpn - find port in port list of adapter by wwpn
 * @adapter: pointer to adapter to search for port
 * @wwpn: wwpn to search for
 * Traverse list of all ports of an adapter and return pointer to a port
 * with the given wwpn.
 */
struct zfcp_port *
zfcp_get_port_by_wwpn(struct zfcp_adapter *adapter, wwn_t wwpn)
{
	struct zfcp_port *port;
	int found = 0;

	list_for_each_entry(port, &adapter->port_list_head, list) {
		if ((port->wwpn == wwpn) &&
		    !(atomic_read(&port->status) &
		      (ZFCP_STATUS_PORT_NO_WWPN | ZFCP_STATUS_COMMON_REMOVE))) {
			found = 1;
			break;
		}
	}
	return found ? port : NULL;
}

/**
 * zfcp_get_port_by_did - find port in port list of adapter by d_id
 * @adapter: pointer to adapter to search for port
 * @d_id: d_id to search for
 * Traverse list of all ports of an adapter and return pointer to a port
 * with the given d_id.
 */
struct zfcp_port *
zfcp_get_port_by_did(struct zfcp_adapter *adapter, u32 d_id)
{
	struct zfcp_port *port;
	int found = 0;

	list_for_each_entry(port, &adapter->port_list_head, list) {
		if ((port->d_id == d_id) &&
		    !atomic_test_mask(ZFCP_STATUS_COMMON_REMOVE, &port->status))
		{
			found = 1;
			break;
		}
	}
	return found ? port : NULL;
}

/**
 * zfcp_get_adapter_by_busid - find adpater in adapter list by bus_id
 * @bus_id: bus_id to search for
 * Traverse list of all adapters and return pointer to an adapter
 * with the given bus_id.
 */
struct zfcp_adapter *
zfcp_get_adapter_by_busid(char *bus_id)
{
	struct zfcp_adapter *adapter;
	int found = 0;

	list_for_each_entry(adapter, &zfcp_data.adapter_list_head, list) {
		if ((strncmp(bus_id, zfcp_get_busid_by_adapter(adapter),
			     BUS_ID_SIZE) == 0) &&
		    !atomic_test_mask(ZFCP_STATUS_COMMON_REMOVE,
				      &adapter->status)){
			found = 1;
			break;
		}
	}
	return found ? adapter : NULL;
}

/**
 * zfcp_unit_enqueue - enqueue unit to unit list of a port.
 * @port: pointer to port where unit is added
 * @fcp_lun: FCP LUN of unit to be enqueued
 * Return: pointer to enqueued unit on success, NULL on error
 * Locks: config_sema must be held to serialize changes to the unit list
 *
 * Sets up some unit internal structures and creates sysfs entry.
 */
struct zfcp_unit *
zfcp_unit_enqueue(struct zfcp_port *port, fcp_lun_t fcp_lun)
{
	struct zfcp_unit *unit, *tmp_unit;
	scsi_lun_t scsi_lun;
	int found;

	/*
	 * check that there is no unit with this FCP_LUN already in list
	 * and enqueue it.
	 * Note: Unlike for the adapter and the port, this is an error
	 */
	read_lock_irq(&zfcp_data.config_lock);
	unit = zfcp_get_unit_by_lun(port, fcp_lun);
	read_unlock_irq(&zfcp_data.config_lock);
	if (unit)
		return NULL;

	unit = kmalloc(sizeof (struct zfcp_unit), GFP_KERNEL);
	if (!unit)
		return NULL;
	memset(unit, 0, sizeof (struct zfcp_unit));

	/* initialise reference count stuff */
	atomic_set(&unit->refcount, 0);
	init_waitqueue_head(&unit->remove_wq);

	unit->port = port;
	unit->fcp_lun = fcp_lun;

	/* setup for sysfs registration */
	snprintf(unit->sysfs_device.bus_id, BUS_ID_SIZE, "0x%016llx", fcp_lun);
	unit->sysfs_device.parent = &port->sysfs_device;
	unit->sysfs_device.release = zfcp_sysfs_unit_release;
	dev_set_drvdata(&unit->sysfs_device, unit);

	/* mark unit unusable as long as sysfs registration is not complete */
	atomic_set_mask(ZFCP_STATUS_COMMON_REMOVE, &unit->status);

	if (device_register(&unit->sysfs_device)) {
		kfree(unit);
		return NULL;
	}

	if (zfcp_sysfs_unit_create_files(&unit->sysfs_device)) {
		device_unregister(&unit->sysfs_device);
		return NULL;
	}

	zfcp_unit_get(unit);

	scsi_lun = 0;
	found = 0;
	write_lock_irq(&zfcp_data.config_lock);
	list_for_each_entry(tmp_unit, &port->unit_list_head, list) {
		if (tmp_unit->scsi_lun != scsi_lun) {
			found = 1;
			break;
		}
		scsi_lun++;
	}
	unit->scsi_lun = scsi_lun;
	if (found)
		list_add_tail(&unit->list, &tmp_unit->list);
	else
		list_add_tail(&unit->list, &port->unit_list_head);
	atomic_clear_mask(ZFCP_STATUS_COMMON_REMOVE, &unit->status);
	atomic_set_mask(ZFCP_STATUS_COMMON_RUNNING, &unit->status);
	write_unlock_irq(&zfcp_data.config_lock);

	port->units++;
	zfcp_port_get(port);

	return unit;
}

void
zfcp_unit_dequeue(struct zfcp_unit *unit)
{
	zfcp_unit_wait(unit);
	write_lock_irq(&zfcp_data.config_lock);
	list_del(&unit->list);
	write_unlock_irq(&zfcp_data.config_lock);
	unit->port->units--;
	zfcp_port_put(unit->port);
	zfcp_sysfs_unit_remove_files(&unit->sysfs_device);
	device_unregister(&unit->sysfs_device);
}

static void *
zfcp_mempool_alloc(gfp_t gfp_mask, void *size)
{
	return kmalloc((size_t) size, gfp_mask);
}

static void
zfcp_mempool_free(void *element, void *size)
{
	kfree(element);
}

/*
 * Allocates a combined QTCB/fsf_req buffer for erp actions and fcp/SCSI
 * commands.
 * It also genrates fcp-nameserver request/response buffer and unsolicited 
 * status read fsf_req buffers.
 *
 * locks:       must only be called with zfcp_data.config_sema taken
 */
static int
zfcp_allocate_low_mem_buffers(struct zfcp_adapter *adapter)
{
	adapter->pool.fsf_req_erp =
		mempool_create(ZFCP_POOL_FSF_REQ_ERP_NR,
			       zfcp_mempool_alloc, zfcp_mempool_free, (void *)
			       sizeof(struct zfcp_fsf_req_pool_element));

	if (NULL == adapter->pool.fsf_req_erp)
		return -ENOMEM;

	adapter->pool.fsf_req_scsi =
		mempool_create(ZFCP_POOL_FSF_REQ_SCSI_NR,
			       zfcp_mempool_alloc, zfcp_mempool_free, (void *)
			       sizeof(struct zfcp_fsf_req_pool_element));

	if (NULL == adapter->pool.fsf_req_scsi)
		return -ENOMEM;

	adapter->pool.fsf_req_abort =
		mempool_create(ZFCP_POOL_FSF_REQ_ABORT_NR,
			       zfcp_mempool_alloc, zfcp_mempool_free, (void *)
			       sizeof(struct zfcp_fsf_req_pool_element));

	if (NULL == adapter->pool.fsf_req_abort)
		return -ENOMEM;

	adapter->pool.fsf_req_status_read =
		mempool_create(ZFCP_POOL_STATUS_READ_NR,
			       zfcp_mempool_alloc, zfcp_mempool_free,
			       (void *) sizeof(struct zfcp_fsf_req));

	if (NULL == adapter->pool.fsf_req_status_read)
		return -ENOMEM;

	adapter->pool.data_status_read =
		mempool_create(ZFCP_POOL_STATUS_READ_NR,
			       zfcp_mempool_alloc, zfcp_mempool_free,
			       (void *) sizeof(struct fsf_status_read_buffer));

	if (NULL == adapter->pool.data_status_read)
		return -ENOMEM;

	adapter->pool.data_gid_pn =
		mempool_create(ZFCP_POOL_DATA_GID_PN_NR,
			       zfcp_mempool_alloc, zfcp_mempool_free, (void *)
			       sizeof(struct zfcp_gid_pn_data));

	if (NULL == adapter->pool.data_gid_pn)
		return -ENOMEM;

	return 0;
}

/**
 * zfcp_free_low_mem_buffers - free memory pools of an adapter
 * @adapter: pointer to zfcp_adapter for which memory pools should be freed
 * locking:  zfcp_data.config_sema must be held
 */
static void
zfcp_free_low_mem_buffers(struct zfcp_adapter *adapter)
{
	if (adapter->pool.fsf_req_erp)
		mempool_destroy(adapter->pool.fsf_req_erp);
	if (adapter->pool.fsf_req_scsi)
		mempool_destroy(adapter->pool.fsf_req_scsi);
	if (adapter->pool.fsf_req_abort)
		mempool_destroy(adapter->pool.fsf_req_abort);
	if (adapter->pool.fsf_req_status_read)
		mempool_destroy(adapter->pool.fsf_req_status_read);
	if (adapter->pool.data_status_read)
		mempool_destroy(adapter->pool.data_status_read);
	if (adapter->pool.data_gid_pn)
		mempool_destroy(adapter->pool.data_gid_pn);
}

void
zfcp_dummy_release(struct device *dev)
{
	return;
}

/*
 * Enqueues an adapter at the end of the adapter list in the driver data.
 * All adapter internal structures are set up.
 * Proc-fs entries are also created.
 *
 * returns:	0             if a new adapter was successfully enqueued
 *              ZFCP_KNOWN    if an adapter with this devno was already present
 *		-ENOMEM       if alloc failed
 * locks:	config_sema must be held to serialise changes to the adapter list
 */
struct zfcp_adapter *
zfcp_adapter_enqueue(struct ccw_device *ccw_device)
{
	int retval = 0;
	struct zfcp_adapter *adapter;

	/*
	 * Note: It is safe to release the list_lock, as any list changes 
	 * are protected by the config_sema, which must be held to get here
	 */

	/* try to allocate new adapter data structure (zeroed) */
	adapter = kmalloc(sizeof (struct zfcp_adapter), GFP_KERNEL);
	if (!adapter) {
		ZFCP_LOG_INFO("error: allocation of base adapter "
			      "structure failed\n");
		goto out;
	}
	memset(adapter, 0, sizeof (struct zfcp_adapter));

	ccw_device->handler = NULL;

	/* save ccw_device pointer */
	adapter->ccw_device = ccw_device;

	retval = zfcp_qdio_allocate_queues(adapter);
	if (retval)
		goto queues_alloc_failed;

	retval = zfcp_qdio_allocate(adapter);
	if (retval)
		goto qdio_allocate_failed;

	retval = zfcp_allocate_low_mem_buffers(adapter);
	if (retval) {
		ZFCP_LOG_INFO("error: pool allocation failed\n");
		goto failed_low_mem_buffers;
	}

	/* initialise reference count stuff */
	atomic_set(&adapter->refcount, 0);
	init_waitqueue_head(&adapter->remove_wq);

	/* initialise list of ports */
	INIT_LIST_HEAD(&adapter->port_list_head);

	/* initialise list of ports to be removed */
	INIT_LIST_HEAD(&adapter->port_remove_lh);

	/* initialize list of fsf requests */
	spin_lock_init(&adapter->fsf_req_list_lock);
	INIT_LIST_HEAD(&adapter->fsf_req_list_head);

	/* initialize debug locks */

	spin_lock_init(&adapter->erp_dbf_lock);
	spin_lock_init(&adapter->hba_dbf_lock);
	spin_lock_init(&adapter->san_dbf_lock);
	spin_lock_init(&adapter->scsi_dbf_lock);

	/* initialize error recovery stuff */

	rwlock_init(&adapter->erp_lock);
	sema_init(&adapter->erp_ready_sem, 0);
	INIT_LIST_HEAD(&adapter->erp_ready_head);
	INIT_LIST_HEAD(&adapter->erp_running_head);

	/* initialize abort lock */
	rwlock_init(&adapter->abort_lock);

	/* initialise some erp stuff */
	init_waitqueue_head(&adapter->erp_thread_wqh);
	init_waitqueue_head(&adapter->erp_done_wqh);

	/* initialize lock of associated request queue */
	rwlock_init(&adapter->request_queue.queue_lock);

	/* intitialise SCSI ER timer */
	init_timer(&adapter->scsi_er_timer);

	/* set FC service class used per default */
	adapter->fc_service_class = ZFCP_FC_SERVICE_CLASS_DEFAULT;

	sprintf(adapter->name, "%s", zfcp_get_busid_by_adapter(adapter));
	ASCEBC(adapter->name, strlen(adapter->name));

	/* mark adapter unusable as long as sysfs registration is not complete */
	atomic_set_mask(ZFCP_STATUS_COMMON_REMOVE, &adapter->status);

	adapter->ccw_device = ccw_device;
	dev_set_drvdata(&ccw_device->dev, adapter);

	if (zfcp_sysfs_adapter_create_files(&ccw_device->dev))
		goto sysfs_failed;

	adapter->generic_services.parent = &adapter->ccw_device->dev;
	adapter->generic_services.release = zfcp_dummy_release;
	snprintf(adapter->generic_services.bus_id, BUS_ID_SIZE,
		 "generic_services");

	if (device_register(&adapter->generic_services))
		goto generic_services_failed;

	/* put allocated adapter at list tail */
	write_lock_irq(&zfcp_data.config_lock);
	atomic_clear_mask(ZFCP_STATUS_COMMON_REMOVE, &adapter->status);
	list_add_tail(&adapter->list, &zfcp_data.adapter_list_head);
	write_unlock_irq(&zfcp_data.config_lock);

	zfcp_data.adapters++;

	goto out;

 generic_services_failed:
	zfcp_sysfs_adapter_remove_files(&adapter->ccw_device->dev);
 sysfs_failed:
	dev_set_drvdata(&ccw_device->dev, NULL);
 failed_low_mem_buffers:
	zfcp_free_low_mem_buffers(adapter);
	if (qdio_free(ccw_device) != 0)
		ZFCP_LOG_NORMAL("bug: qdio_free for adapter %s failed\n",
				zfcp_get_busid_by_adapter(adapter));
 qdio_allocate_failed:
	zfcp_qdio_free_queues(adapter);
 queues_alloc_failed:
	kfree(adapter);
	adapter = NULL;
 out:
	return adapter;
}

/*
 * returns:	0 - struct zfcp_adapter  data structure successfully removed
 *		!0 - struct zfcp_adapter  data structure could not be removed
 *			(e.g. still used)
 * locks:	adapter list write lock is assumed to be held by caller
 *              adapter->fsf_req_list_lock is taken and released within this 
 *              function and must not be held on entry
 */
void
zfcp_adapter_dequeue(struct zfcp_adapter *adapter)
{
	int retval = 0;
	unsigned long flags;

	device_unregister(&adapter->generic_services);
	zfcp_sysfs_adapter_remove_files(&adapter->ccw_device->dev);
	dev_set_drvdata(&adapter->ccw_device->dev, NULL);
	/* sanity check: no pending FSF requests */
	spin_lock_irqsave(&adapter->fsf_req_list_lock, flags);
	retval = !list_empty(&adapter->fsf_req_list_head);
	spin_unlock_irqrestore(&adapter->fsf_req_list_lock, flags);
	if (retval) {
		ZFCP_LOG_NORMAL("bug: adapter %s (%p) still in use, "
				"%i requests outstanding\n",
				zfcp_get_busid_by_adapter(adapter), adapter,
				atomic_read(&adapter->fsf_reqs_active));
		retval = -EBUSY;
		goto out;
	}

	/* remove specified adapter data structure from list */
	write_lock_irq(&zfcp_data.config_lock);
	list_del(&adapter->list);
	write_unlock_irq(&zfcp_data.config_lock);

	/* decrease number of adapters in list */
	zfcp_data.adapters--;

	ZFCP_LOG_TRACE("adapter %s (%p) removed from list, "
		       "%i adapters still in list\n",
		       zfcp_get_busid_by_adapter(adapter),
		       adapter, zfcp_data.adapters);

	retval = qdio_free(adapter->ccw_device);
	if (retval)
		ZFCP_LOG_NORMAL("bug: qdio_free for adapter %s failed\n",
				zfcp_get_busid_by_adapter(adapter));

	zfcp_free_low_mem_buffers(adapter);
	/* free memory of adapter data structure and queues */
	zfcp_qdio_free_queues(adapter);
	kfree(adapter->fc_stats);
	kfree(adapter->stats_reset_data);
	ZFCP_LOG_TRACE("freeing adapter structure\n");
	kfree(adapter);
 out:
	return;
}

/**
 * zfcp_port_enqueue - enqueue port to port list of adapter
 * @adapter: adapter where remote port is added
 * @wwpn: WWPN of the remote port to be enqueued
 * @status: initial status for the port
 * @d_id: destination id of the remote port to be enqueued
 * Return: pointer to enqueued port on success, NULL on error
 * Locks: config_sema must be held to serialize changes to the port list
 *
 * All port internal structures are set up and the sysfs entry is generated.
 * d_id is used to enqueue ports with a well known address like the Directory
 * Service for nameserver lookup.
 */
struct zfcp_port *
zfcp_port_enqueue(struct zfcp_adapter *adapter, wwn_t wwpn, u32 status,
		  u32 d_id)
{
	struct zfcp_port *port;
	int check_wwpn;

	check_wwpn = !(status & ZFCP_STATUS_PORT_NO_WWPN);
	/*
	 * check that there is no port with this WWPN already in list
	 */
	if (check_wwpn) {
		read_lock_irq(&zfcp_data.config_lock);
		port = zfcp_get_port_by_wwpn(adapter, wwpn);
		read_unlock_irq(&zfcp_data.config_lock);
		if (port)
			return NULL;
	}

	port = kmalloc(sizeof (struct zfcp_port), GFP_KERNEL);
	if (!port)
		return NULL;
	memset(port, 0, sizeof (struct zfcp_port));

	/* initialise reference count stuff */
	atomic_set(&port->refcount, 0);
	init_waitqueue_head(&port->remove_wq);

	INIT_LIST_HEAD(&port->unit_list_head);
	INIT_LIST_HEAD(&port->unit_remove_lh);

	port->adapter = adapter;

	if (check_wwpn)
		port->wwpn = wwpn;

	atomic_set_mask(status, &port->status);

	/* setup for sysfs registration */
	if (status & ZFCP_STATUS_PORT_WKA) {
		switch (d_id) {
		case ZFCP_DID_DIRECTORY_SERVICE:
			snprintf(port->sysfs_device.bus_id, BUS_ID_SIZE,
				 "directory");
			break;
		case ZFCP_DID_MANAGEMENT_SERVICE:
			snprintf(port->sysfs_device.bus_id, BUS_ID_SIZE,
				 "management");
			break;
		case ZFCP_DID_KEY_DISTRIBUTION_SERVICE:
			snprintf(port->sysfs_device.bus_id, BUS_ID_SIZE,
				 "key_distribution");
			break;
		case ZFCP_DID_ALIAS_SERVICE:
			snprintf(port->sysfs_device.bus_id, BUS_ID_SIZE,
				 "alias");
			break;
		case ZFCP_DID_TIME_SERVICE:
			snprintf(port->sysfs_device.bus_id, BUS_ID_SIZE,
				 "time");
			break;
		default:
			kfree(port);
			return NULL;
		}
		port->d_id = d_id;
		port->sysfs_device.parent = &adapter->generic_services;
	} else {
		snprintf(port->sysfs_device.bus_id,
			 BUS_ID_SIZE, "0x%016llx", wwpn);
		port->sysfs_device.parent = &adapter->ccw_device->dev;
	}
	port->sysfs_device.release = zfcp_sysfs_port_release;
	dev_set_drvdata(&port->sysfs_device, port);

	/* mark port unusable as long as sysfs registration is not complete */
	atomic_set_mask(ZFCP_STATUS_COMMON_REMOVE, &port->status);

	if (device_register(&port->sysfs_device)) {
		kfree(port);
		return NULL;
	}

	if (zfcp_sysfs_port_create_files(&port->sysfs_device, status)) {
		device_unregister(&port->sysfs_device);
		return NULL;
	}

	zfcp_port_get(port);

	write_lock_irq(&zfcp_data.config_lock);
	list_add_tail(&port->list, &adapter->port_list_head);
	atomic_clear_mask(ZFCP_STATUS_COMMON_REMOVE, &port->status);
	atomic_set_mask(ZFCP_STATUS_COMMON_RUNNING, &port->status);
	if (d_id == ZFCP_DID_DIRECTORY_SERVICE)
		if (!adapter->nameserver_port)
			adapter->nameserver_port = port;
	adapter->ports++;
	write_unlock_irq(&zfcp_data.config_lock);

	zfcp_adapter_get(adapter);

	return port;
}

void
zfcp_port_dequeue(struct zfcp_port *port)
{
	zfcp_port_wait(port);
	write_lock_irq(&zfcp_data.config_lock);
	list_del(&port->list);
	port->adapter->ports--;
	write_unlock_irq(&zfcp_data.config_lock);
	if (port->rport)
		fc_remote_port_delete(port->rport);
	port->rport = NULL;
	zfcp_adapter_put(port->adapter);
	zfcp_sysfs_port_remove_files(&port->sysfs_device,
				     atomic_read(&port->status));
	device_unregister(&port->sysfs_device);
}

/* Enqueues a nameserver port */
int
zfcp_nameserver_enqueue(struct zfcp_adapter *adapter)
{
	struct zfcp_port *port;

	port = zfcp_port_enqueue(adapter, 0, ZFCP_STATUS_PORT_WKA,
				 ZFCP_DID_DIRECTORY_SERVICE);
	if (!port) {
		ZFCP_LOG_INFO("error: enqueue of nameserver port for "
			      "adapter %s failed\n",
			      zfcp_get_busid_by_adapter(adapter));
		return -ENXIO;
	}
	zfcp_port_put(port);

	return 0;
}

#undef ZFCP_LOG_AREA

/****************************************************************/
/******* Fibre Channel Standard related Functions  **************/
/****************************************************************/

#define ZFCP_LOG_AREA                   ZFCP_LOG_AREA_FC

void
zfcp_fsf_incoming_els_rscn(struct zfcp_adapter *adapter,
			   struct fsf_status_read_buffer *status_buffer)
{
	struct fcp_rscn_head *fcp_rscn_head;
	struct fcp_rscn_element *fcp_rscn_element;
	struct zfcp_port *port;
	u16 i;
	u16 no_entries;
	u32 range_mask;
	unsigned long flags;

	fcp_rscn_head = (struct fcp_rscn_head *) status_buffer->payload;
	fcp_rscn_element = (struct fcp_rscn_element *) status_buffer->payload;

	/* see FC-FS */
	no_entries = (fcp_rscn_head->payload_len / 4);

	for (i = 1; i < no_entries; i++) {
		/* skip head and start with 1st element */
		fcp_rscn_element++;
		switch (fcp_rscn_element->addr_format) {
		case ZFCP_PORT_ADDRESS:
			range_mask = ZFCP_PORTS_RANGE_PORT;
			break;
		case ZFCP_AREA_ADDRESS:
			range_mask = ZFCP_PORTS_RANGE_AREA;
			break;
		case ZFCP_DOMAIN_ADDRESS:
			range_mask = ZFCP_PORTS_RANGE_DOMAIN;
			break;
		case ZFCP_FABRIC_ADDRESS:
			range_mask = ZFCP_PORTS_RANGE_FABRIC;
			break;
		default:
			ZFCP_LOG_INFO("incoming RSCN with unknown "
				      "address format\n");
			continue;
		}
		read_lock_irqsave(&zfcp_data.config_lock, flags);
		list_for_each_entry(port, &adapter->port_list_head, list) {
			if (atomic_test_mask
			    (ZFCP_STATUS_PORT_WKA, &port->status))
				continue;
			/* Do we know this port? If not skip it. */
			if (!atomic_test_mask
			    (ZFCP_STATUS_PORT_DID_DID, &port->status)) {
				ZFCP_LOG_INFO("incoming RSCN, trying to open "
					      "port 0x%016Lx\n", port->wwpn);
				zfcp_erp_port_reopen(port,
						     ZFCP_STATUS_COMMON_ERP_FAILED);
				continue;
			}

			/*
			 * FIXME: race: d_id might being invalidated
			 * (...DID_DID reset)
			 */
			if ((port->d_id & range_mask)
			    == (fcp_rscn_element->nport_did & range_mask)) {
				ZFCP_LOG_TRACE("reopen did 0x%08x\n",
					       fcp_rscn_element->nport_did);
				/*
				 * Unfortunately, an RSCN does not specify the
				 * type of change a target underwent. We assume
				 * that it makes sense to reopen the link.
				 * FIXME: Shall we try to find out more about
				 * the target and link state before closing it?
				 * How to accomplish this? (nameserver?)
				 * Where would such code be put in?
				 * (inside or outside erp)
				 */
				ZFCP_LOG_INFO("incoming RSCN, trying to open "
					      "port 0x%016Lx\n", port->wwpn);
				zfcp_test_link(port);
			}
		}
		read_unlock_irqrestore(&zfcp_data.config_lock, flags);
	}
}

static void
zfcp_fsf_incoming_els_plogi(struct zfcp_adapter *adapter,
			    struct fsf_status_read_buffer *status_buffer)
{
	logi *els_logi = (logi *) status_buffer->payload;
	struct zfcp_port *port;
	unsigned long flags;

	read_lock_irqsave(&zfcp_data.config_lock, flags);
	list_for_each_entry(port, &adapter->port_list_head, list) {
		if (port->wwpn == (*(wwn_t *) & els_logi->nport_wwn))
			break;
	}
	read_unlock_irqrestore(&zfcp_data.config_lock, flags);

	if (!port || (port->wwpn != (*(wwn_t *) & els_logi->nport_wwn))) {
		ZFCP_LOG_DEBUG("ignored incoming PLOGI for nonexisting port "
			       "with d_id 0x%08x on adapter %s\n",
			       status_buffer->d_id,
			       zfcp_get_busid_by_adapter(adapter));
	} else {
		zfcp_erp_port_forced_reopen(port, 0);
	}
}

static void
zfcp_fsf_incoming_els_logo(struct zfcp_adapter *adapter,
			   struct fsf_status_read_buffer *status_buffer)
{
	struct fcp_logo *els_logo = (struct fcp_logo *) status_buffer->payload;
	struct zfcp_port *port;
	unsigned long flags;

	read_lock_irqsave(&zfcp_data.config_lock, flags);
	list_for_each_entry(port, &adapter->port_list_head, list) {
		if (port->wwpn == els_logo->nport_wwpn)
			break;
	}
	read_unlock_irqrestore(&zfcp_data.config_lock, flags);

	if (!port || (port->wwpn != els_logo->nport_wwpn)) {
		ZFCP_LOG_DEBUG("ignored incoming LOGO for nonexisting port "
			       "with d_id 0x%08x on adapter %s\n",
			       status_buffer->d_id,
			       zfcp_get_busid_by_adapter(adapter));
	} else {
		zfcp_erp_port_forced_reopen(port, 0);
	}
}

static void
zfcp_fsf_incoming_els_unknown(struct zfcp_adapter *adapter,
			      struct fsf_status_read_buffer *status_buffer)
{
	ZFCP_LOG_NORMAL("warning: unknown incoming ELS 0x%08x "
			"for adapter %s\n", *(u32 *) (status_buffer->payload),
			zfcp_get_busid_by_adapter(adapter));

}

void
zfcp_fsf_incoming_els(struct zfcp_fsf_req *fsf_req)
{
	struct fsf_status_read_buffer *status_buffer;
	u32 els_type;
	struct zfcp_adapter *adapter;

	status_buffer = (struct fsf_status_read_buffer *) fsf_req->data;
	els_type = *(u32 *) (status_buffer->payload);
	adapter = fsf_req->adapter;

	zfcp_san_dbf_event_incoming_els(fsf_req);
	if (els_type == LS_PLOGI)
		zfcp_fsf_incoming_els_plogi(adapter, status_buffer);
	else if (els_type == LS_LOGO)
		zfcp_fsf_incoming_els_logo(adapter, status_buffer);
	else if ((els_type & 0xffff0000) == LS_RSCN)
		/* we are only concerned with the command, not the length */
		zfcp_fsf_incoming_els_rscn(adapter, status_buffer);
	else
		zfcp_fsf_incoming_els_unknown(adapter, status_buffer);
}


/**
 * zfcp_gid_pn_buffers_alloc - allocate buffers for GID_PN nameserver request
 * @gid_pn: pointer to return pointer to struct zfcp_gid_pn_data
 * @pool: pointer to mempool_t if non-null memory pool is used for allocation
 */
static int
zfcp_gid_pn_buffers_alloc(struct zfcp_gid_pn_data **gid_pn, mempool_t *pool)
{
	struct zfcp_gid_pn_data *data;

	if (pool != NULL) {
		data = mempool_alloc(pool, GFP_ATOMIC);
		if (likely(data != NULL)) {
			data->ct.pool = pool;
		}
	} else {
		data = kmalloc(sizeof(struct zfcp_gid_pn_data), GFP_ATOMIC);
	}

        if (NULL == data)
                return -ENOMEM;

	memset(data, 0, sizeof(*data));
        data->ct.req = &data->req;
        data->ct.resp = &data->resp;
	data->ct.req_count = data->ct.resp_count = 1;
	zfcp_address_to_sg(&data->ct_iu_req, &data->req);
        zfcp_address_to_sg(&data->ct_iu_resp, &data->resp);
        data->req.length = sizeof(struct ct_iu_gid_pn_req);
        data->resp.length = sizeof(struct ct_iu_gid_pn_resp);

	*gid_pn = data;
	return 0;
}

/**
 * zfcp_gid_pn_buffers_free - free buffers for GID_PN nameserver request
 * @gid_pn: pointer to struct zfcp_gid_pn_data which has to be freed
 */
static void
zfcp_gid_pn_buffers_free(struct zfcp_gid_pn_data *gid_pn)
{
        if ((gid_pn->ct.pool != 0))
		mempool_free(gid_pn, gid_pn->ct.pool);
	else
                kfree(gid_pn);

	return;
}

/**
 * zfcp_ns_gid_pn_request - initiate GID_PN nameserver request
 * @erp_action: pointer to zfcp_erp_action where GID_PN request is needed
 */
int
zfcp_ns_gid_pn_request(struct zfcp_erp_action *erp_action)
{
	int ret;
        struct ct_iu_gid_pn_req *ct_iu_req;
        struct zfcp_gid_pn_data *gid_pn;
        struct zfcp_adapter *adapter = erp_action->adapter;

	ret = zfcp_gid_pn_buffers_alloc(&gid_pn, adapter->pool.data_gid_pn);
	if (ret < 0) {
		ZFCP_LOG_INFO("error: buffer allocation for gid_pn nameserver "
			      "request failed for adapter %s\n",
			      zfcp_get_busid_by_adapter(adapter));
		goto out;
	}

	/* setup nameserver request */
        ct_iu_req = zfcp_sg_to_address(gid_pn->ct.req);
        ct_iu_req->header.revision = ZFCP_CT_REVISION;
        ct_iu_req->header.gs_type = ZFCP_CT_DIRECTORY_SERVICE;
        ct_iu_req->header.gs_subtype = ZFCP_CT_NAME_SERVER;
        ct_iu_req->header.options = ZFCP_CT_SYNCHRONOUS;
        ct_iu_req->header.cmd_rsp_code = ZFCP_CT_GID_PN;
        ct_iu_req->header.max_res_size = ZFCP_CT_MAX_SIZE;
	ct_iu_req->wwpn = erp_action->port->wwpn;

        /* setup parameters for send generic command */
        gid_pn->ct.port = adapter->nameserver_port;
	gid_pn->ct.handler = zfcp_ns_gid_pn_handler;
	gid_pn->ct.handler_data = (unsigned long) gid_pn;
        gid_pn->ct.timeout = ZFCP_NS_GID_PN_TIMEOUT;
        gid_pn->ct.timer = &erp_action->timer;
	gid_pn->port = erp_action->port;

	ret = zfcp_fsf_send_ct(&gid_pn->ct, adapter->pool.fsf_req_erp,
			       erp_action);
	if (ret) {
		ZFCP_LOG_INFO("error: initiation of gid_pn nameserver request "
                              "failed for adapter %s\n",
			      zfcp_get_busid_by_adapter(adapter));

                zfcp_gid_pn_buffers_free(gid_pn);
	}

 out:
	return ret;
}

/**
 * zfcp_ns_gid_pn_handler - handler for GID_PN nameserver request
 * @data: unsigned long, contains pointer to struct zfcp_gid_pn_data
 */
static void zfcp_ns_gid_pn_handler(unsigned long data)
{
	struct zfcp_port *port;
        struct zfcp_send_ct *ct;
	struct ct_iu_gid_pn_req *ct_iu_req;
	struct ct_iu_gid_pn_resp *ct_iu_resp;
        struct zfcp_gid_pn_data *gid_pn;


	gid_pn = (struct zfcp_gid_pn_data *) data;
	port = gid_pn->port;
        ct = &gid_pn->ct;
	ct_iu_req = zfcp_sg_to_address(ct->req);
	ct_iu_resp = zfcp_sg_to_address(ct->resp);

	if (ct->status != 0)
		goto failed;

	if (zfcp_check_ct_response(&ct_iu_resp->header)) {
		/* FIXME: do we need some specific erp entry points */
		atomic_set_mask(ZFCP_STATUS_PORT_INVALID_WWPN, &port->status);
		goto failed;
	}
	/* paranoia */
	if (ct_iu_req->wwpn != port->wwpn) {
		ZFCP_LOG_NORMAL("bug: wwpn 0x%016Lx returned by nameserver "
				"lookup does not match expected wwpn 0x%016Lx "
				"for adapter %s\n", ct_iu_req->wwpn, port->wwpn,
				zfcp_get_busid_by_port(port));
		goto mismatch;
	}

	/* looks like a valid d_id */
        port->d_id = ct_iu_resp->d_id & ZFCP_DID_MASK;
	atomic_set_mask(ZFCP_STATUS_PORT_DID_DID, &port->status);
	ZFCP_LOG_DEBUG("adapter %s:  wwpn=0x%016Lx ---> d_id=0x%08x\n",
		       zfcp_get_busid_by_port(port), port->wwpn, port->d_id);
	goto out;

 mismatch:
	ZFCP_LOG_DEBUG("CT IUs do not match:\n");
	ZFCP_HEX_DUMP(ZFCP_LOG_LEVEL_DEBUG, (char *) ct_iu_req,
		      sizeof(struct ct_iu_gid_pn_req));
	ZFCP_HEX_DUMP(ZFCP_LOG_LEVEL_DEBUG, (char *) ct_iu_resp,
		      sizeof(struct ct_iu_gid_pn_resp));

 failed:
	ZFCP_LOG_NORMAL("warning: failed gid_pn nameserver request for wwpn "
			"0x%016Lx for adapter %s\n",
			port->wwpn, zfcp_get_busid_by_port(port));
 out:
        zfcp_gid_pn_buffers_free(gid_pn);
	return;
}

/* reject CT_IU reason codes acc. to FC-GS-4 */
static const struct zfcp_rc_entry zfcp_ct_rc[] = {
	{0x01, "invalid command code"},
	{0x02, "invalid version level"},
	{0x03, "logical error"},
	{0x04, "invalid CT_IU size"},
	{0x05, "logical busy"},
	{0x07, "protocol error"},
	{0x09, "unable to perform command request"},
	{0x0b, "command not supported"},
	{0x0d, "server not available"},
	{0x0e, "session could not be established"},
	{0xff, "vendor specific error"},
	{0, NULL},
};

/* LS_RJT reason codes acc. to FC-FS */
static const struct zfcp_rc_entry zfcp_ls_rjt_rc[] = {
	{0x01, "invalid LS_Command code"},
	{0x03, "logical error"},
	{0x05, "logical busy"},
	{0x07, "protocol error"},
	{0x09, "unable to perform command request"},
	{0x0b, "command not supported"},
	{0x0e, "command already in progress"},
	{0xff, "vendor specific error"},
	{0, NULL},
};

/* reject reason codes according to FC-PH/FC-FS */
static const struct zfcp_rc_entry zfcp_p_rjt_rc[] = {
	{0x01, "invalid D_ID"},
	{0x02, "invalid S_ID"},
	{0x03, "Nx_Port not available, temporary"},
	{0x04, "Nx_Port not available, permament"},
	{0x05, "class not supported"},
	{0x06, "delimiter usage error"},
	{0x07, "TYPE not supported"},
	{0x08, "invalid Link_Control"},
	{0x09, "invalid R_CTL field"},
	{0x0a, "invalid F_CTL field"},
	{0x0b, "invalid OX_ID"},
	{0x0c, "invalid RX_ID"},
	{0x0d, "invalid SEQ_ID"},
	{0x0e, "invalid DF_CTL"},
	{0x0f, "invalid SEQ_CNT"},
	{0x10, "invalid parameter field"},
	{0x11, "exchange error"},
	{0x12, "protocol error"},
	{0x13, "incorrect length"},
	{0x14, "unsupported ACK"},
	{0x15, "class of service not supported by entity at FFFFFE"},
	{0x16, "login required"},
	{0x17, "excessive sequences attempted"},
	{0x18, "unable to establish exchange"},
	{0x1a, "fabric path not available"},
	{0x1b, "invalid VC_ID (class 4)"},
	{0x1c, "invalid CS_CTL field"},
	{0x1d, "insufficient resources for VC (class 4)"},
	{0x1f, "invalid class of service"},
	{0x20, "preemption request rejected"},
	{0x21, "preemption not enabled"},
	{0x22, "multicast error"},
	{0x23, "multicast error terminate"},
	{0x24, "process login required"},
	{0xff, "vendor specific reject"},
	{0, NULL},
};

/**
 * zfcp_rc_description - return description for given reaon code
 * @code: reason code
 * @rc_table: table of reason codes and descriptions
 */
static inline const char *
zfcp_rc_description(u8 code, const struct zfcp_rc_entry *rc_table)
{
	const char *descr = "unknown reason code";

	do {
		if (code == rc_table->code) {
			descr = rc_table->description;
			break;
		}
		rc_table++;
	} while (rc_table->code && rc_table->description);

	return descr;
}

/**
 * zfcp_check_ct_response - evaluate reason code for CT_IU
 * @rjt: response payload to an CT_IU request
 * Return: 0 for accept CT_IU, 1 for reject CT_IU or invlid response code
 */
int
zfcp_check_ct_response(struct ct_hdr *rjt)
{
	if (rjt->cmd_rsp_code == ZFCP_CT_ACCEPT)
		return 0;

	if (rjt->cmd_rsp_code != ZFCP_CT_REJECT) {
		ZFCP_LOG_NORMAL("error: invalid Generic Service command/"
				"response code (0x%04hx)\n",
				rjt->cmd_rsp_code);
		return 1;
	}

	ZFCP_LOG_INFO("Generic Service command rejected\n");
	ZFCP_LOG_INFO("%s (0x%02x, 0x%02x, 0x%02x)\n",
		      zfcp_rc_description(rjt->reason_code, zfcp_ct_rc),
		      (u32) rjt->reason_code, (u32) rjt->reason_code_expl,
		      (u32) rjt->vendor_unique);

	return 1;
}

/**
 * zfcp_print_els_rjt - print reject parameter and description for ELS reject
 * @rjt_par: reject parameter acc. to FC-PH/FC-FS
 * @rc_table: table of reason codes and descriptions
 */
static inline void
zfcp_print_els_rjt(struct zfcp_ls_rjt_par *rjt_par,
		   const struct zfcp_rc_entry *rc_table)
{
	ZFCP_LOG_INFO("%s (%02x %02x %02x %02x)\n",
		      zfcp_rc_description(rjt_par->reason_code, rc_table),
		      (u32) rjt_par->action, (u32) rjt_par->reason_code,
		      (u32) rjt_par->reason_expl, (u32) rjt_par->vendor_unique);
}

/**
 * zfcp_fsf_handle_els_rjt - evaluate status qualifier/reason code on ELS reject
 * @sq: status qualifier word
 * @rjt_par: reject parameter as described in FC-PH and FC-FS
 * Return: -EROMTEIO for LS_RJT, -EREMCHG for invalid D_ID, -EIO else
 */
int
zfcp_handle_els_rjt(u32 sq, struct zfcp_ls_rjt_par *rjt_par)
{
	int ret = -EIO;

	if (sq == FSF_IOSTAT_NPORT_RJT) {
		ZFCP_LOG_INFO("ELS rejected (P_RJT)\n");
		zfcp_print_els_rjt(rjt_par, zfcp_p_rjt_rc);
		/* invalid d_id */
		if (rjt_par->reason_code == 0x01)
			ret = -EREMCHG;
	} else if (sq == FSF_IOSTAT_FABRIC_RJT) {
		ZFCP_LOG_INFO("ELS rejected (F_RJT)\n");
		zfcp_print_els_rjt(rjt_par, zfcp_p_rjt_rc);
		/* invalid d_id */
		if (rjt_par->reason_code == 0x01)
			ret = -EREMCHG;
	} else if (sq == FSF_IOSTAT_LS_RJT) {
		ZFCP_LOG_INFO("ELS rejected (LS_RJT)\n");
		zfcp_print_els_rjt(rjt_par, zfcp_ls_rjt_rc);
		ret = -EREMOTEIO;
	} else
		ZFCP_LOG_INFO("unexpected SQ: 0x%02x\n", sq);

	return ret;
}

#undef ZFCP_LOG_AREA
