/*
 * zfcp device driver
 *
 * Tracking of manually configured LUNs and helper functions to
 * register the LUNs with the SCSI midlayer.
 *
 * Copyright IBM Corp. 2010
 */

#include "zfcp_def.h"
#include "zfcp_ext.h"

/**
 * zfcp_unit_scsi_scan - Register LUN with SCSI midlayer
 * @unit: The zfcp LUN/unit to register
 *
 * When the SCSI midlayer is not allowed to automatically scan and
 * attach SCSI devices, zfcp has to register the single devices with
 * the SCSI midlayer.
 */
void zfcp_unit_scsi_scan(struct zfcp_unit *unit)
{
	struct fc_rport *rport = unit->port->rport;
	unsigned int lun;

	lun = scsilun_to_int((struct scsi_lun *) &unit->fcp_lun);

	if (rport && rport->port_state == FC_PORTSTATE_ONLINE)
		scsi_scan_target(&rport->dev, 0, rport->scsi_target_id, lun, 1);
}

static void zfcp_unit_scsi_scan_work(struct work_struct *work)
{
	struct zfcp_unit *unit = container_of(work, struct zfcp_unit,
					      scsi_work);

	zfcp_unit_scsi_scan(unit);
	put_device(&unit->dev);
}

/**
 * zfcp_unit_queue_scsi_scan - Register configured units on port
 * @port: The zfcp_port where to register units
 *
 * After opening a port, all units configured on this port have to be
 * registered with the SCSI midlayer. This function should be called
 * after calling fc_remote_port_add, so that the fc_rport is already
 * ONLINE and the call to scsi_scan_target runs the same way as the
 * call in the FC transport class.
 */
void zfcp_unit_queue_scsi_scan(struct zfcp_port *port)
{
	struct zfcp_unit *unit;

	read_lock_irq(&port->unit_list_lock);
	list_for_each_entry(unit, &port->unit_list, list) {
		get_device(&unit->dev);
		if (scsi_queue_work(port->adapter->scsi_host,
				    &unit->scsi_work) <= 0)
			put_device(&unit->dev);
	}
	read_unlock_irq(&port->unit_list_lock);
}

static struct zfcp_unit *_zfcp_unit_find(struct zfcp_port *port, u64 fcp_lun)
{
	struct zfcp_unit *unit;

	list_for_each_entry(unit, &port->unit_list, list)
		if (unit->fcp_lun == fcp_lun) {
			get_device(&unit->dev);
			return unit;
		}

	return NULL;
}

/**
 * zfcp_unit_find - Find and return zfcp_unit with specified FCP LUN
 * @port: zfcp_port where to look for the unit
 * @fcp_lun: 64 Bit FCP LUN used to identify the zfcp_unit
 *
 * If zfcp_unit is found, a reference is acquired that has to be
 * released later.
 *
 * Returns: Pointer to the zfcp_unit, or NULL if there is no zfcp_unit
 *          with the specified FCP LUN.
 */
struct zfcp_unit *zfcp_unit_find(struct zfcp_port *port, u64 fcp_lun)
{
	struct zfcp_unit *unit;

	read_lock_irq(&port->unit_list_lock);
	unit = _zfcp_unit_find(port, fcp_lun);
	read_unlock_irq(&port->unit_list_lock);
	return unit;
}

/**
 * zfcp_unit_release - Drop reference to zfcp_port and free memory of zfcp_unit.
 * @dev: pointer to device in zfcp_unit
 */
static void zfcp_unit_release(struct device *dev)
{
	struct zfcp_unit *unit = container_of(dev, struct zfcp_unit, dev);

	put_device(&unit->port->dev);
	kfree(unit);
}

/**
 * zfcp_unit_enqueue - enqueue unit to unit list of a port.
 * @port: pointer to port where unit is added
 * @fcp_lun: FCP LUN of unit to be enqueued
 * Returns: 0 success
 *
 * Sets up some unit internal structures and creates sysfs entry.
 */
int zfcp_unit_add(struct zfcp_port *port, u64 fcp_lun)
{
	struct zfcp_unit *unit;

	unit = zfcp_unit_find(port, fcp_lun);
	if (unit) {
		put_device(&unit->dev);
		return -EEXIST;
	}

	unit = kzalloc(sizeof(struct zfcp_unit), GFP_KERNEL);
	if (!unit)
		return -ENOMEM;

	unit->port = port;
	unit->fcp_lun = fcp_lun;
	unit->dev.parent = &port->dev;
	unit->dev.release = zfcp_unit_release;
	INIT_WORK(&unit->scsi_work, zfcp_unit_scsi_scan_work);

	if (dev_set_name(&unit->dev, "0x%016llx",
			 (unsigned long long) fcp_lun)) {
		kfree(unit);
		return -ENOMEM;
	}

	get_device(&port->dev);

	if (device_register(&unit->dev)) {
		put_device(&unit->dev);
		return -ENOMEM;
	}

	if (sysfs_create_group(&unit->dev.kobj, &zfcp_sysfs_unit_attrs)) {
		device_unregister(&unit->dev);
		return -EINVAL;
	}

	write_lock_irq(&port->unit_list_lock);
	list_add_tail(&unit->list, &port->unit_list);
	write_unlock_irq(&port->unit_list_lock);

	zfcp_unit_scsi_scan(unit);

	return 0;
}

/**
 * zfcp_unit_sdev - Return SCSI device for zfcp_unit
 * @unit: The zfcp_unit where to get the SCSI device for
 *
 * Returns: scsi_device pointer on success, NULL if there is no SCSI
 *          device for this zfcp_unit
 *
 * On success, the caller also holds a reference to the SCSI device
 * that must be released with scsi_device_put.
 */
struct scsi_device *zfcp_unit_sdev(struct zfcp_unit *unit)
{
	struct Scsi_Host *shost;
	struct zfcp_port *port;
	unsigned int lun;

	lun = scsilun_to_int((struct scsi_lun *) &unit->fcp_lun);
	port = unit->port;
	shost = port->adapter->scsi_host;
	return scsi_device_lookup(shost, 0, port->starget_id, lun);
}

/**
 * zfcp_unit_sdev_status - Return zfcp LUN status for SCSI device
 * @unit: The unit to lookup the SCSI device for
 *
 * Returns the zfcp LUN status field of the SCSI device if the SCSI device
 * for the zfcp_unit exists, 0 otherwise.
 */
unsigned int zfcp_unit_sdev_status(struct zfcp_unit *unit)
{
	unsigned int status = 0;
	struct scsi_device *sdev;
	struct zfcp_scsi_dev *zfcp_sdev;

	sdev = zfcp_unit_sdev(unit);
	if (sdev) {
		zfcp_sdev = sdev_to_zfcp(sdev);
		status = atomic_read(&zfcp_sdev->status);
		scsi_device_put(sdev);
	}

	return status;
}

/**
 * zfcp_unit_remove - Remove entry from list of configured units
 * @port: The port where to remove the unit from the configuration
 * @fcp_lun: The 64 bit LUN of the unit to remove
 *
 * Returns: -EINVAL if a unit with the specified LUN does not exist,
 *          0 on success.
 */
int zfcp_unit_remove(struct zfcp_port *port, u64 fcp_lun)
{
	struct zfcp_unit *unit;
	struct scsi_device *sdev;

	write_lock_irq(&port->unit_list_lock);
	unit = _zfcp_unit_find(port, fcp_lun);
	if (unit)
		list_del(&unit->list);
	write_unlock_irq(&port->unit_list_lock);

	if (!unit)
		return -EINVAL;

	sdev = zfcp_unit_sdev(unit);
	if (sdev) {
		scsi_remove_device(sdev);
		scsi_device_put(sdev);
	}

	put_device(&unit->dev);

	zfcp_device_unregister(&unit->dev, &zfcp_sysfs_unit_attrs);

	return 0;
}
