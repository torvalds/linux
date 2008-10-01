/*
 * zfcp device driver
 *
 * sysfs attributes.
 *
 * Copyright IBM Corporation 2008
 */

#include "zfcp_ext.h"

#define ZFCP_DEV_ATTR(_feat, _name, _mode, _show, _store) \
struct device_attribute dev_attr_##_feat##_##_name = __ATTR(_name, _mode,\
							    _show, _store)
#define ZFCP_DEFINE_ATTR(_feat_def, _feat, _name, _format, _value)	       \
static ssize_t zfcp_sysfs_##_feat##_##_name##_show(struct device *dev,	       \
						   struct device_attribute *at,\
						   char *buf)		       \
{									       \
	struct _feat_def *_feat = dev_get_drvdata(dev);			       \
									       \
	return sprintf(buf, _format, _value);				       \
}									       \
static ZFCP_DEV_ATTR(_feat, _name, S_IRUGO,				       \
		     zfcp_sysfs_##_feat##_##_name##_show, NULL);

ZFCP_DEFINE_ATTR(zfcp_adapter, adapter, status, "0x%08x\n",
		 atomic_read(&adapter->status));
ZFCP_DEFINE_ATTR(zfcp_adapter, adapter, peer_wwnn, "0x%016llx\n",
		 (unsigned long long) adapter->peer_wwnn);
ZFCP_DEFINE_ATTR(zfcp_adapter, adapter, peer_wwpn, "0x%016llx\n",
		 (unsigned long long) adapter->peer_wwpn);
ZFCP_DEFINE_ATTR(zfcp_adapter, adapter, peer_d_id, "0x%06x\n",
		 adapter->peer_d_id);
ZFCP_DEFINE_ATTR(zfcp_adapter, adapter, card_version, "0x%04x\n",
		 adapter->hydra_version);
ZFCP_DEFINE_ATTR(zfcp_adapter, adapter, lic_version, "0x%08x\n",
		 adapter->fsf_lic_version);
ZFCP_DEFINE_ATTR(zfcp_adapter, adapter, hardware_version, "0x%08x\n",
		 adapter->hardware_version);
ZFCP_DEFINE_ATTR(zfcp_adapter, adapter, in_recovery, "%d\n",
		 (atomic_read(&adapter->status) &
		  ZFCP_STATUS_COMMON_ERP_INUSE) != 0);

ZFCP_DEFINE_ATTR(zfcp_port, port, status, "0x%08x\n",
		 atomic_read(&port->status));
ZFCP_DEFINE_ATTR(zfcp_port, port, in_recovery, "%d\n",
		 (atomic_read(&port->status) &
		  ZFCP_STATUS_COMMON_ERP_INUSE) != 0);
ZFCP_DEFINE_ATTR(zfcp_port, port, access_denied, "%d\n",
		 (atomic_read(&port->status) &
		  ZFCP_STATUS_COMMON_ACCESS_DENIED) != 0);

ZFCP_DEFINE_ATTR(zfcp_unit, unit, status, "0x%08x\n",
		 atomic_read(&unit->status));
ZFCP_DEFINE_ATTR(zfcp_unit, unit, in_recovery, "%d\n",
		 (atomic_read(&unit->status) &
		  ZFCP_STATUS_COMMON_ERP_INUSE) != 0);
ZFCP_DEFINE_ATTR(zfcp_unit, unit, access_denied, "%d\n",
		 (atomic_read(&unit->status) &
		  ZFCP_STATUS_COMMON_ACCESS_DENIED) != 0);
ZFCP_DEFINE_ATTR(zfcp_unit, unit, access_shared, "%d\n",
		 (atomic_read(&unit->status) &
		  ZFCP_STATUS_UNIT_SHARED) != 0);
ZFCP_DEFINE_ATTR(zfcp_unit, unit, access_readonly, "%d\n",
		 (atomic_read(&unit->status) &
		  ZFCP_STATUS_UNIT_READONLY) != 0);

#define ZFCP_SYSFS_FAILED(_feat_def, _feat, _adapter, _mod_id, _reopen_id)     \
static ssize_t zfcp_sysfs_##_feat##_failed_show(struct device *dev,	       \
						struct device_attribute *attr, \
						char *buf)		       \
{									       \
	struct _feat_def *_feat = dev_get_drvdata(dev);			       \
									       \
	if (atomic_read(&_feat->status) & ZFCP_STATUS_COMMON_ERP_FAILED)       \
		return sprintf(buf, "1\n");				       \
	else								       \
		return sprintf(buf, "0\n");				       \
}									       \
static ssize_t zfcp_sysfs_##_feat##_failed_store(struct device *dev,	       \
						 struct device_attribute *attr,\
						 const char *buf, size_t count)\
{									       \
	struct _feat_def *_feat = dev_get_drvdata(dev);			       \
	unsigned long val;						       \
	int retval = 0;							       \
									       \
	down(&zfcp_data.config_sema);					       \
	if (atomic_read(&_feat->status) & ZFCP_STATUS_COMMON_REMOVE) {	       \
		retval = -EBUSY;					       \
		goto out;						       \
	}								       \
									       \
	if (strict_strtoul(buf, 0, &val) || val != 0) {			       \
		retval = -EINVAL;					       \
		goto out;						       \
	}								       \
									       \
	zfcp_erp_modify_##_feat##_status(_feat, _mod_id, NULL,		       \
					 ZFCP_STATUS_COMMON_RUNNING, ZFCP_SET);\
	zfcp_erp_##_feat##_reopen(_feat, ZFCP_STATUS_COMMON_ERP_FAILED,	       \
				  _reopen_id, NULL);			       \
	zfcp_erp_wait(_adapter);					       \
out:									       \
	up(&zfcp_data.config_sema);					       \
	return retval ? retval : (ssize_t) count;			       \
}									       \
static ZFCP_DEV_ATTR(_feat, failed, S_IWUSR | S_IRUGO,			       \
		     zfcp_sysfs_##_feat##_failed_show,			       \
		     zfcp_sysfs_##_feat##_failed_store);

ZFCP_SYSFS_FAILED(zfcp_adapter, adapter, adapter, 44, 93);
ZFCP_SYSFS_FAILED(zfcp_port, port, port->adapter, 45, 96);
ZFCP_SYSFS_FAILED(zfcp_unit, unit, unit->port->adapter, 46, 97);

static ssize_t zfcp_sysfs_port_rescan_store(struct device *dev,
					    struct device_attribute *attr,
					    const char *buf, size_t count)
{
	struct zfcp_adapter *adapter = dev_get_drvdata(dev);
	int ret;

	if (atomic_read(&adapter->status) & ZFCP_STATUS_COMMON_REMOVE)
		return -EBUSY;

	ret = zfcp_scan_ports(adapter);
	return ret ? ret : (ssize_t) count;
}
static ZFCP_DEV_ATTR(adapter, port_rescan, S_IWUSR, NULL,
		     zfcp_sysfs_port_rescan_store);

static ssize_t zfcp_sysfs_port_remove_store(struct device *dev,
					    struct device_attribute *attr,
					    const char *buf, size_t count)
{
	struct zfcp_adapter *adapter = dev_get_drvdata(dev);
	struct zfcp_port *port;
	u64 wwpn;
	int retval = 0;

	down(&zfcp_data.config_sema);
	if (atomic_read(&adapter->status) & ZFCP_STATUS_COMMON_REMOVE) {
		retval = -EBUSY;
		goto out;
	}

	if (strict_strtoull(buf, 0, (unsigned long long *) &wwpn)) {
		retval = -EINVAL;
		goto out;
	}

	write_lock_irq(&zfcp_data.config_lock);
	port = zfcp_get_port_by_wwpn(adapter, wwpn);
	if (port && (atomic_read(&port->refcount) == 0)) {
		zfcp_port_get(port);
		atomic_set_mask(ZFCP_STATUS_COMMON_REMOVE, &port->status);
		list_move(&port->list, &adapter->port_remove_lh);
	} else
		port = NULL;
	write_unlock_irq(&zfcp_data.config_lock);

	if (!port) {
		retval = -ENXIO;
		goto out;
	}

	zfcp_erp_port_shutdown(port, 0, 92, NULL);
	zfcp_erp_wait(adapter);
	zfcp_port_put(port);
	zfcp_port_dequeue(port);
 out:
	up(&zfcp_data.config_sema);
	return retval ? retval : (ssize_t) count;
}
static ZFCP_DEV_ATTR(adapter, port_remove, S_IWUSR, NULL,
		     zfcp_sysfs_port_remove_store);

static struct attribute *zfcp_adapter_attrs[] = {
	&dev_attr_adapter_failed.attr,
	&dev_attr_adapter_in_recovery.attr,
	&dev_attr_adapter_port_remove.attr,
	&dev_attr_adapter_port_rescan.attr,
	&dev_attr_adapter_peer_wwnn.attr,
	&dev_attr_adapter_peer_wwpn.attr,
	&dev_attr_adapter_peer_d_id.attr,
	&dev_attr_adapter_card_version.attr,
	&dev_attr_adapter_lic_version.attr,
	&dev_attr_adapter_status.attr,
	&dev_attr_adapter_hardware_version.attr,
	NULL
};

struct attribute_group zfcp_sysfs_adapter_attrs = {
	.attrs = zfcp_adapter_attrs,
};

static ssize_t zfcp_sysfs_unit_add_store(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf, size_t count)
{
	struct zfcp_port *port = dev_get_drvdata(dev);
	struct zfcp_unit *unit;
	u64 fcp_lun;
	int retval = -EINVAL;

	down(&zfcp_data.config_sema);
	if (atomic_read(&port->status) & ZFCP_STATUS_COMMON_REMOVE) {
		retval = -EBUSY;
		goto out;
	}

	if (strict_strtoull(buf, 0, (unsigned long long *) &fcp_lun))
		goto out;

	unit = zfcp_unit_enqueue(port, fcp_lun);
	if (IS_ERR(unit))
		goto out;

	retval = 0;

	zfcp_erp_unit_reopen(unit, 0, 94, NULL);
	zfcp_erp_wait(unit->port->adapter);
	zfcp_unit_put(unit);
out:
	up(&zfcp_data.config_sema);
	return retval ? retval : (ssize_t) count;
}
static DEVICE_ATTR(unit_add, S_IWUSR, NULL, zfcp_sysfs_unit_add_store);

static ssize_t zfcp_sysfs_unit_remove_store(struct device *dev,
					    struct device_attribute *attr,
					    const char *buf, size_t count)
{
	struct zfcp_port *port = dev_get_drvdata(dev);
	struct zfcp_unit *unit;
	u64 fcp_lun;
	int retval = 0;

	down(&zfcp_data.config_sema);
	if (atomic_read(&port->status) & ZFCP_STATUS_COMMON_REMOVE) {
		retval = -EBUSY;
		goto out;
	}

	if (strict_strtoull(buf, 0, (unsigned long long *) &fcp_lun)) {
		retval = -EINVAL;
		goto out;
	}

	write_lock_irq(&zfcp_data.config_lock);
	unit = zfcp_get_unit_by_lun(port, fcp_lun);
	if (unit && (atomic_read(&unit->refcount) == 0)) {
		zfcp_unit_get(unit);
		atomic_set_mask(ZFCP_STATUS_COMMON_REMOVE, &unit->status);
		list_move(&unit->list, &port->unit_remove_lh);
	} else
		unit = NULL;

	write_unlock_irq(&zfcp_data.config_lock);

	if (!unit) {
		retval = -ENXIO;
		goto out;
	}

	zfcp_erp_unit_shutdown(unit, 0, 95, NULL);
	zfcp_erp_wait(unit->port->adapter);
	zfcp_unit_put(unit);
	zfcp_unit_dequeue(unit);
out:
	up(&zfcp_data.config_sema);
	return retval ? retval : (ssize_t) count;
}
static DEVICE_ATTR(unit_remove, S_IWUSR, NULL, zfcp_sysfs_unit_remove_store);

static struct attribute *zfcp_port_attrs[] = {
	&dev_attr_unit_add.attr,
	&dev_attr_unit_remove.attr,
	&dev_attr_port_failed.attr,
	&dev_attr_port_in_recovery.attr,
	&dev_attr_port_status.attr,
	&dev_attr_port_access_denied.attr,
	NULL
};

/**
 * zfcp_sysfs_port_attrs - sysfs attributes for all other ports
 */
struct attribute_group zfcp_sysfs_port_attrs = {
	.attrs = zfcp_port_attrs,
};

static struct attribute *zfcp_unit_attrs[] = {
	&dev_attr_unit_failed.attr,
	&dev_attr_unit_in_recovery.attr,
	&dev_attr_unit_status.attr,
	&dev_attr_unit_access_denied.attr,
	&dev_attr_unit_access_shared.attr,
	&dev_attr_unit_access_readonly.attr,
	NULL
};

struct attribute_group zfcp_sysfs_unit_attrs = {
	.attrs = zfcp_unit_attrs,
};

#define ZFCP_DEFINE_LATENCY_ATTR(_name) 				\
static ssize_t								\
zfcp_sysfs_unit_##_name##_latency_show(struct device *dev,		\
				       struct device_attribute *attr,	\
				       char *buf) {			\
	struct scsi_device *sdev = to_scsi_device(dev);			\
	struct zfcp_unit *unit = sdev->hostdata;			\
	struct zfcp_latencies *lat = &unit->latencies;			\
	struct zfcp_adapter *adapter = unit->port->adapter;		\
	unsigned long flags;						\
	unsigned long long fsum, fmin, fmax, csum, cmin, cmax, cc;	\
									\
	spin_lock_irqsave(&lat->lock, flags);				\
	fsum = lat->_name.fabric.sum * adapter->timer_ticks;		\
	fmin = lat->_name.fabric.min * adapter->timer_ticks;		\
	fmax = lat->_name.fabric.max * adapter->timer_ticks;		\
	csum = lat->_name.channel.sum * adapter->timer_ticks;		\
	cmin = lat->_name.channel.min * adapter->timer_ticks;		\
	cmax = lat->_name.channel.max * adapter->timer_ticks;		\
	cc  = lat->_name.counter;					\
	spin_unlock_irqrestore(&lat->lock, flags);			\
									\
	do_div(fsum, 1000);						\
	do_div(fmin, 1000);						\
	do_div(fmax, 1000);						\
	do_div(csum, 1000);						\
	do_div(cmin, 1000);						\
	do_div(cmax, 1000);						\
									\
	return sprintf(buf, "%llu %llu %llu %llu %llu %llu %llu\n",	\
		       fmin, fmax, fsum, cmin, cmax, csum, cc); 	\
}									\
static ssize_t								\
zfcp_sysfs_unit_##_name##_latency_store(struct device *dev,		\
					struct device_attribute *attr,	\
					const char *buf, size_t count)	\
{									\
	struct scsi_device *sdev = to_scsi_device(dev);			\
	struct zfcp_unit *unit = sdev->hostdata;			\
	struct zfcp_latencies *lat = &unit->latencies;			\
	unsigned long flags;						\
									\
	spin_lock_irqsave(&lat->lock, flags);				\
	lat->_name.fabric.sum = 0;					\
	lat->_name.fabric.min = 0xFFFFFFFF;				\
	lat->_name.fabric.max = 0;					\
	lat->_name.channel.sum = 0;					\
	lat->_name.channel.min = 0xFFFFFFFF;				\
	lat->_name.channel.max = 0;					\
	lat->_name.counter = 0;						\
	spin_unlock_irqrestore(&lat->lock, flags);			\
									\
	return (ssize_t) count;						\
}									\
static DEVICE_ATTR(_name##_latency, S_IWUSR | S_IRUGO,			\
		   zfcp_sysfs_unit_##_name##_latency_show,		\
		   zfcp_sysfs_unit_##_name##_latency_store);

ZFCP_DEFINE_LATENCY_ATTR(read);
ZFCP_DEFINE_LATENCY_ATTR(write);
ZFCP_DEFINE_LATENCY_ATTR(cmd);

#define ZFCP_DEFINE_SCSI_ATTR(_name, _format, _value)			\
static ssize_t zfcp_sysfs_scsi_##_name##_show(struct device *dev,	\
					      struct device_attribute *attr,\
					      char *buf)                 \
{                                                                        \
	struct scsi_device *sdev  = to_scsi_device(dev);		 \
	struct zfcp_unit *unit = sdev->hostdata;			 \
									 \
	return sprintf(buf, _format, _value);                            \
}                                                                        \
static DEVICE_ATTR(_name, S_IRUGO, zfcp_sysfs_scsi_##_name##_show, NULL);

ZFCP_DEFINE_SCSI_ATTR(hba_id, "%s\n",
	unit->port->adapter->ccw_device->dev.bus_id);
ZFCP_DEFINE_SCSI_ATTR(wwpn, "0x%016llx\n",
		      (unsigned long long) unit->port->wwpn);
ZFCP_DEFINE_SCSI_ATTR(fcp_lun, "0x%016llx\n",
		      (unsigned long long) unit->fcp_lun);

struct device_attribute *zfcp_sysfs_sdev_attrs[] = {
	&dev_attr_fcp_lun,
	&dev_attr_wwpn,
	&dev_attr_hba_id,
	&dev_attr_read_latency,
	&dev_attr_write_latency,
	&dev_attr_cmd_latency,
	NULL
};

static ssize_t zfcp_sysfs_adapter_util_show(struct device *dev,
					    struct device_attribute *attr,
					    char *buf)
{
	struct Scsi_Host *scsi_host = dev_to_shost(dev);
	struct fsf_qtcb_bottom_port *qtcb_port;
	struct zfcp_adapter *adapter;
	int retval;

	adapter = (struct zfcp_adapter *) scsi_host->hostdata[0];
	if (!(adapter->adapter_features & FSF_FEATURE_MEASUREMENT_DATA))
		return -EOPNOTSUPP;

	qtcb_port = kzalloc(sizeof(struct fsf_qtcb_bottom_port), GFP_KERNEL);
	if (!qtcb_port)
		return -ENOMEM;

	retval = zfcp_fsf_exchange_port_data_sync(adapter, qtcb_port);
	if (!retval)
		retval = sprintf(buf, "%u %u %u\n", qtcb_port->cp_util,
				 qtcb_port->cb_util, qtcb_port->a_util);
	kfree(qtcb_port);
	return retval;
}
static DEVICE_ATTR(utilization, S_IRUGO, zfcp_sysfs_adapter_util_show, NULL);

static int zfcp_sysfs_adapter_ex_config(struct device *dev,
					struct fsf_statistics_info *stat_inf)
{
	struct Scsi_Host *scsi_host = dev_to_shost(dev);
	struct fsf_qtcb_bottom_config *qtcb_config;
	struct zfcp_adapter *adapter;
	int retval;

	adapter = (struct zfcp_adapter *) scsi_host->hostdata[0];
	if (!(adapter->adapter_features & FSF_FEATURE_MEASUREMENT_DATA))
		return -EOPNOTSUPP;

	qtcb_config = kzalloc(sizeof(struct fsf_qtcb_bottom_config),
			      GFP_KERNEL);
	if (!qtcb_config)
		return -ENOMEM;

	retval = zfcp_fsf_exchange_config_data_sync(adapter, qtcb_config);
	if (!retval)
		*stat_inf = qtcb_config->stat_info;

	kfree(qtcb_config);
	return retval;
}

#define ZFCP_SHOST_ATTR(_name, _format, _arg...)			\
static ssize_t zfcp_sysfs_adapter_##_name##_show(struct device *dev,	\
						 struct device_attribute *attr,\
						 char *buf)		\
{									\
	struct fsf_statistics_info stat_info;				\
	int retval;							\
									\
	retval = zfcp_sysfs_adapter_ex_config(dev, &stat_info);		\
	if (retval)							\
		return retval;						\
									\
	return sprintf(buf, _format, ## _arg);				\
}									\
static DEVICE_ATTR(_name, S_IRUGO, zfcp_sysfs_adapter_##_name##_show, NULL);

ZFCP_SHOST_ATTR(requests, "%llu %llu %llu\n",
		(unsigned long long) stat_info.input_req,
		(unsigned long long) stat_info.output_req,
		(unsigned long long) stat_info.control_req);

ZFCP_SHOST_ATTR(megabytes, "%llu %llu\n",
		(unsigned long long) stat_info.input_mb,
		(unsigned long long) stat_info.output_mb);

ZFCP_SHOST_ATTR(seconds_active, "%llu\n",
		(unsigned long long) stat_info.seconds_act);

static ssize_t zfcp_sysfs_adapter_q_full_show(struct device *dev,
					      struct device_attribute *attr,
					      char *buf)
{
	struct Scsi_Host *scsi_host = class_to_shost(dev);
	struct zfcp_adapter *adapter =
		(struct zfcp_adapter *) scsi_host->hostdata[0];

	return sprintf(buf, "%d\n", atomic_read(&adapter->qdio_outb_full));
}
static DEVICE_ATTR(queue_full, S_IRUGO, zfcp_sysfs_adapter_q_full_show, NULL);

struct device_attribute *zfcp_sysfs_shost_attrs[] = {
	&dev_attr_utilization,
	&dev_attr_requests,
	&dev_attr_megabytes,
	&dev_attr_seconds_active,
	&dev_attr_queue_full,
	NULL
};
