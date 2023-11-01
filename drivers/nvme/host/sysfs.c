// SPDX-License-Identifier: GPL-2.0
/*
 * Sysfs interface for the NVMe core driver.
 *
 * Copyright (c) 2011-2014, Intel Corporation.
 */

#include <linux/nvme-auth.h>

#include "nvme.h"
#include "fabrics.h"

static ssize_t nvme_sysfs_reset(struct device *dev,
				struct device_attribute *attr, const char *buf,
				size_t count)
{
	struct nvme_ctrl *ctrl = dev_get_drvdata(dev);
	int ret;

	ret = nvme_reset_ctrl_sync(ctrl);
	if (ret < 0)
		return ret;
	return count;
}
static DEVICE_ATTR(reset_controller, S_IWUSR, NULL, nvme_sysfs_reset);

static ssize_t nvme_sysfs_rescan(struct device *dev,
				struct device_attribute *attr, const char *buf,
				size_t count)
{
	struct nvme_ctrl *ctrl = dev_get_drvdata(dev);

	nvme_queue_scan(ctrl);
	return count;
}
static DEVICE_ATTR(rescan_controller, S_IWUSR, NULL, nvme_sysfs_rescan);

static inline struct nvme_ns_head *dev_to_ns_head(struct device *dev)
{
	struct gendisk *disk = dev_to_disk(dev);

	if (disk->fops == &nvme_bdev_ops)
		return nvme_get_ns_from_dev(dev)->head;
	else
		return disk->private_data;
}

static ssize_t wwid_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct nvme_ns_head *head = dev_to_ns_head(dev);
	struct nvme_ns_ids *ids = &head->ids;
	struct nvme_subsystem *subsys = head->subsys;
	int serial_len = sizeof(subsys->serial);
	int model_len = sizeof(subsys->model);

	if (!uuid_is_null(&ids->uuid))
		return sysfs_emit(buf, "uuid.%pU\n", &ids->uuid);

	if (memchr_inv(ids->nguid, 0, sizeof(ids->nguid)))
		return sysfs_emit(buf, "eui.%16phN\n", ids->nguid);

	if (memchr_inv(ids->eui64, 0, sizeof(ids->eui64)))
		return sysfs_emit(buf, "eui.%8phN\n", ids->eui64);

	while (serial_len > 0 && (subsys->serial[serial_len - 1] == ' ' ||
				  subsys->serial[serial_len - 1] == '\0'))
		serial_len--;
	while (model_len > 0 && (subsys->model[model_len - 1] == ' ' ||
				 subsys->model[model_len - 1] == '\0'))
		model_len--;

	return sysfs_emit(buf, "nvme.%04x-%*phN-%*phN-%08x\n", subsys->vendor_id,
		serial_len, subsys->serial, model_len, subsys->model,
		head->ns_id);
}
static DEVICE_ATTR_RO(wwid);

static ssize_t nguid_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	return sysfs_emit(buf, "%pU\n", dev_to_ns_head(dev)->ids.nguid);
}
static DEVICE_ATTR_RO(nguid);

static ssize_t uuid_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct nvme_ns_ids *ids = &dev_to_ns_head(dev)->ids;

	/* For backward compatibility expose the NGUID to userspace if
	 * we have no UUID set
	 */
	if (uuid_is_null(&ids->uuid)) {
		dev_warn_once(dev,
			"No UUID available providing old NGUID\n");
		return sysfs_emit(buf, "%pU\n", ids->nguid);
	}
	return sysfs_emit(buf, "%pU\n", &ids->uuid);
}
static DEVICE_ATTR_RO(uuid);

static ssize_t eui_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	return sysfs_emit(buf, "%8ph\n", dev_to_ns_head(dev)->ids.eui64);
}
static DEVICE_ATTR_RO(eui);

static ssize_t nsid_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	return sysfs_emit(buf, "%d\n", dev_to_ns_head(dev)->ns_id);
}
static DEVICE_ATTR_RO(nsid);

static struct attribute *nvme_ns_id_attrs[] = {
	&dev_attr_wwid.attr,
	&dev_attr_uuid.attr,
	&dev_attr_nguid.attr,
	&dev_attr_eui.attr,
	&dev_attr_nsid.attr,
#ifdef CONFIG_NVME_MULTIPATH
	&dev_attr_ana_grpid.attr,
	&dev_attr_ana_state.attr,
#endif
	NULL,
};

static umode_t nvme_ns_id_attrs_are_visible(struct kobject *kobj,
		struct attribute *a, int n)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct nvme_ns_ids *ids = &dev_to_ns_head(dev)->ids;

	if (a == &dev_attr_uuid.attr) {
		if (uuid_is_null(&ids->uuid) &&
		    !memchr_inv(ids->nguid, 0, sizeof(ids->nguid)))
			return 0;
	}
	if (a == &dev_attr_nguid.attr) {
		if (!memchr_inv(ids->nguid, 0, sizeof(ids->nguid)))
			return 0;
	}
	if (a == &dev_attr_eui.attr) {
		if (!memchr_inv(ids->eui64, 0, sizeof(ids->eui64)))
			return 0;
	}
#ifdef CONFIG_NVME_MULTIPATH
	if (a == &dev_attr_ana_grpid.attr || a == &dev_attr_ana_state.attr) {
		if (dev_to_disk(dev)->fops != &nvme_bdev_ops) /* per-path attr */
			return 0;
		if (!nvme_ctrl_use_ana(nvme_get_ns_from_dev(dev)->ctrl))
			return 0;
	}
#endif
	return a->mode;
}

static const struct attribute_group nvme_ns_id_attr_group = {
	.attrs		= nvme_ns_id_attrs,
	.is_visible	= nvme_ns_id_attrs_are_visible,
};

const struct attribute_group *nvme_ns_id_attr_groups[] = {
	&nvme_ns_id_attr_group,
	NULL,
};

#define nvme_show_str_function(field)						\
static ssize_t  field##_show(struct device *dev,				\
			    struct device_attribute *attr, char *buf)		\
{										\
        struct nvme_ctrl *ctrl = dev_get_drvdata(dev);				\
        return sysfs_emit(buf, "%.*s\n",					\
		(int)sizeof(ctrl->subsys->field), ctrl->subsys->field);		\
}										\
static DEVICE_ATTR(field, S_IRUGO, field##_show, NULL);

nvme_show_str_function(model);
nvme_show_str_function(serial);
nvme_show_str_function(firmware_rev);

#define nvme_show_int_function(field)						\
static ssize_t  field##_show(struct device *dev,				\
			    struct device_attribute *attr, char *buf)		\
{										\
        struct nvme_ctrl *ctrl = dev_get_drvdata(dev);				\
        return sysfs_emit(buf, "%d\n", ctrl->field);				\
}										\
static DEVICE_ATTR(field, S_IRUGO, field##_show, NULL);

nvme_show_int_function(cntlid);
nvme_show_int_function(numa_node);
nvme_show_int_function(queue_count);
nvme_show_int_function(sqsize);
nvme_show_int_function(kato);

static ssize_t nvme_sysfs_delete(struct device *dev,
				struct device_attribute *attr, const char *buf,
				size_t count)
{
	struct nvme_ctrl *ctrl = dev_get_drvdata(dev);

	if (!test_bit(NVME_CTRL_STARTED_ONCE, &ctrl->flags))
		return -EBUSY;

	if (device_remove_file_self(dev, attr))
		nvme_delete_ctrl_sync(ctrl);
	return count;
}
static DEVICE_ATTR(delete_controller, S_IWUSR, NULL, nvme_sysfs_delete);

static ssize_t nvme_sysfs_show_transport(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct nvme_ctrl *ctrl = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%s\n", ctrl->ops->name);
}
static DEVICE_ATTR(transport, S_IRUGO, nvme_sysfs_show_transport, NULL);

static ssize_t nvme_sysfs_show_state(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	struct nvme_ctrl *ctrl = dev_get_drvdata(dev);
	static const char *const state_name[] = {
		[NVME_CTRL_NEW]		= "new",
		[NVME_CTRL_LIVE]	= "live",
		[NVME_CTRL_RESETTING]	= "resetting",
		[NVME_CTRL_CONNECTING]	= "connecting",
		[NVME_CTRL_DELETING]	= "deleting",
		[NVME_CTRL_DELETING_NOIO]= "deleting (no IO)",
		[NVME_CTRL_DEAD]	= "dead",
	};

	if ((unsigned)ctrl->state < ARRAY_SIZE(state_name) &&
	    state_name[ctrl->state])
		return sysfs_emit(buf, "%s\n", state_name[ctrl->state]);

	return sysfs_emit(buf, "unknown state\n");
}

static DEVICE_ATTR(state, S_IRUGO, nvme_sysfs_show_state, NULL);

static ssize_t nvme_sysfs_show_subsysnqn(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct nvme_ctrl *ctrl = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%s\n", ctrl->subsys->subnqn);
}
static DEVICE_ATTR(subsysnqn, S_IRUGO, nvme_sysfs_show_subsysnqn, NULL);

static ssize_t nvme_sysfs_show_hostnqn(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct nvme_ctrl *ctrl = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%s\n", ctrl->opts->host->nqn);
}
static DEVICE_ATTR(hostnqn, S_IRUGO, nvme_sysfs_show_hostnqn, NULL);

static ssize_t nvme_sysfs_show_hostid(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct nvme_ctrl *ctrl = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%pU\n", &ctrl->opts->host->id);
}
static DEVICE_ATTR(hostid, S_IRUGO, nvme_sysfs_show_hostid, NULL);

static ssize_t nvme_sysfs_show_address(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct nvme_ctrl *ctrl = dev_get_drvdata(dev);

	return ctrl->ops->get_address(ctrl, buf, PAGE_SIZE);
}
static DEVICE_ATTR(address, S_IRUGO, nvme_sysfs_show_address, NULL);

static ssize_t nvme_ctrl_loss_tmo_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct nvme_ctrl *ctrl = dev_get_drvdata(dev);
	struct nvmf_ctrl_options *opts = ctrl->opts;

	if (ctrl->opts->max_reconnects == -1)
		return sysfs_emit(buf, "off\n");
	return sysfs_emit(buf, "%d\n",
			  opts->max_reconnects * opts->reconnect_delay);
}

static ssize_t nvme_ctrl_loss_tmo_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct nvme_ctrl *ctrl = dev_get_drvdata(dev);
	struct nvmf_ctrl_options *opts = ctrl->opts;
	int ctrl_loss_tmo, err;

	err = kstrtoint(buf, 10, &ctrl_loss_tmo);
	if (err)
		return -EINVAL;

	if (ctrl_loss_tmo < 0)
		opts->max_reconnects = -1;
	else
		opts->max_reconnects = DIV_ROUND_UP(ctrl_loss_tmo,
						opts->reconnect_delay);
	return count;
}
static DEVICE_ATTR(ctrl_loss_tmo, S_IRUGO | S_IWUSR,
	nvme_ctrl_loss_tmo_show, nvme_ctrl_loss_tmo_store);

static ssize_t nvme_ctrl_reconnect_delay_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct nvme_ctrl *ctrl = dev_get_drvdata(dev);

	if (ctrl->opts->reconnect_delay == -1)
		return sysfs_emit(buf, "off\n");
	return sysfs_emit(buf, "%d\n", ctrl->opts->reconnect_delay);
}

static ssize_t nvme_ctrl_reconnect_delay_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct nvme_ctrl *ctrl = dev_get_drvdata(dev);
	unsigned int v;
	int err;

	err = kstrtou32(buf, 10, &v);
	if (err)
		return err;

	ctrl->opts->reconnect_delay = v;
	return count;
}
static DEVICE_ATTR(reconnect_delay, S_IRUGO | S_IWUSR,
	nvme_ctrl_reconnect_delay_show, nvme_ctrl_reconnect_delay_store);

static ssize_t nvme_ctrl_fast_io_fail_tmo_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct nvme_ctrl *ctrl = dev_get_drvdata(dev);

	if (ctrl->opts->fast_io_fail_tmo == -1)
		return sysfs_emit(buf, "off\n");
	return sysfs_emit(buf, "%d\n", ctrl->opts->fast_io_fail_tmo);
}

static ssize_t nvme_ctrl_fast_io_fail_tmo_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct nvme_ctrl *ctrl = dev_get_drvdata(dev);
	struct nvmf_ctrl_options *opts = ctrl->opts;
	int fast_io_fail_tmo, err;

	err = kstrtoint(buf, 10, &fast_io_fail_tmo);
	if (err)
		return -EINVAL;

	if (fast_io_fail_tmo < 0)
		opts->fast_io_fail_tmo = -1;
	else
		opts->fast_io_fail_tmo = fast_io_fail_tmo;
	return count;
}
static DEVICE_ATTR(fast_io_fail_tmo, S_IRUGO | S_IWUSR,
	nvme_ctrl_fast_io_fail_tmo_show, nvme_ctrl_fast_io_fail_tmo_store);

static ssize_t cntrltype_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	static const char * const type[] = {
		[NVME_CTRL_IO] = "io\n",
		[NVME_CTRL_DISC] = "discovery\n",
		[NVME_CTRL_ADMIN] = "admin\n",
	};
	struct nvme_ctrl *ctrl = dev_get_drvdata(dev);

	if (ctrl->cntrltype > NVME_CTRL_ADMIN || !type[ctrl->cntrltype])
		return sysfs_emit(buf, "reserved\n");

	return sysfs_emit(buf, type[ctrl->cntrltype]);
}
static DEVICE_ATTR_RO(cntrltype);

static ssize_t dctype_show(struct device *dev,
			   struct device_attribute *attr, char *buf)
{
	static const char * const type[] = {
		[NVME_DCTYPE_NOT_REPORTED] = "none\n",
		[NVME_DCTYPE_DDC] = "ddc\n",
		[NVME_DCTYPE_CDC] = "cdc\n",
	};
	struct nvme_ctrl *ctrl = dev_get_drvdata(dev);

	if (ctrl->dctype > NVME_DCTYPE_CDC || !type[ctrl->dctype])
		return sysfs_emit(buf, "reserved\n");

	return sysfs_emit(buf, type[ctrl->dctype]);
}
static DEVICE_ATTR_RO(dctype);

#ifdef CONFIG_NVME_HOST_AUTH
static ssize_t nvme_ctrl_dhchap_secret_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct nvme_ctrl *ctrl = dev_get_drvdata(dev);
	struct nvmf_ctrl_options *opts = ctrl->opts;

	if (!opts->dhchap_secret)
		return sysfs_emit(buf, "none\n");
	return sysfs_emit(buf, "%s\n", opts->dhchap_secret);
}

static ssize_t nvme_ctrl_dhchap_secret_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct nvme_ctrl *ctrl = dev_get_drvdata(dev);
	struct nvmf_ctrl_options *opts = ctrl->opts;
	char *dhchap_secret;

	if (!ctrl->opts->dhchap_secret)
		return -EINVAL;
	if (count < 7)
		return -EINVAL;
	if (memcmp(buf, "DHHC-1:", 7))
		return -EINVAL;

	dhchap_secret = kzalloc(count + 1, GFP_KERNEL);
	if (!dhchap_secret)
		return -ENOMEM;
	memcpy(dhchap_secret, buf, count);
	nvme_auth_stop(ctrl);
	if (strcmp(dhchap_secret, opts->dhchap_secret)) {
		struct nvme_dhchap_key *key, *host_key;
		int ret;

		ret = nvme_auth_generate_key(dhchap_secret, &key);
		if (ret) {
			kfree(dhchap_secret);
			return ret;
		}
		kfree(opts->dhchap_secret);
		opts->dhchap_secret = dhchap_secret;
		host_key = ctrl->host_key;
		mutex_lock(&ctrl->dhchap_auth_mutex);
		ctrl->host_key = key;
		mutex_unlock(&ctrl->dhchap_auth_mutex);
		nvme_auth_free_key(host_key);
	} else
		kfree(dhchap_secret);
	/* Start re-authentication */
	dev_info(ctrl->device, "re-authenticating controller\n");
	queue_work(nvme_wq, &ctrl->dhchap_auth_work);

	return count;
}

static DEVICE_ATTR(dhchap_secret, S_IRUGO | S_IWUSR,
	nvme_ctrl_dhchap_secret_show, nvme_ctrl_dhchap_secret_store);

static ssize_t nvme_ctrl_dhchap_ctrl_secret_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct nvme_ctrl *ctrl = dev_get_drvdata(dev);
	struct nvmf_ctrl_options *opts = ctrl->opts;

	if (!opts->dhchap_ctrl_secret)
		return sysfs_emit(buf, "none\n");
	return sysfs_emit(buf, "%s\n", opts->dhchap_ctrl_secret);
}

static ssize_t nvme_ctrl_dhchap_ctrl_secret_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct nvme_ctrl *ctrl = dev_get_drvdata(dev);
	struct nvmf_ctrl_options *opts = ctrl->opts;
	char *dhchap_secret;

	if (!ctrl->opts->dhchap_ctrl_secret)
		return -EINVAL;
	if (count < 7)
		return -EINVAL;
	if (memcmp(buf, "DHHC-1:", 7))
		return -EINVAL;

	dhchap_secret = kzalloc(count + 1, GFP_KERNEL);
	if (!dhchap_secret)
		return -ENOMEM;
	memcpy(dhchap_secret, buf, count);
	nvme_auth_stop(ctrl);
	if (strcmp(dhchap_secret, opts->dhchap_ctrl_secret)) {
		struct nvme_dhchap_key *key, *ctrl_key;
		int ret;

		ret = nvme_auth_generate_key(dhchap_secret, &key);
		if (ret) {
			kfree(dhchap_secret);
			return ret;
		}
		kfree(opts->dhchap_ctrl_secret);
		opts->dhchap_ctrl_secret = dhchap_secret;
		ctrl_key = ctrl->ctrl_key;
		mutex_lock(&ctrl->dhchap_auth_mutex);
		ctrl->ctrl_key = key;
		mutex_unlock(&ctrl->dhchap_auth_mutex);
		nvme_auth_free_key(ctrl_key);
	} else
		kfree(dhchap_secret);
	/* Start re-authentication */
	dev_info(ctrl->device, "re-authenticating controller\n");
	queue_work(nvme_wq, &ctrl->dhchap_auth_work);

	return count;
}

static DEVICE_ATTR(dhchap_ctrl_secret, S_IRUGO | S_IWUSR,
	nvme_ctrl_dhchap_ctrl_secret_show, nvme_ctrl_dhchap_ctrl_secret_store);
#endif

#ifdef CONFIG_NVME_TCP_TLS
static ssize_t tls_key_show(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	struct nvme_ctrl *ctrl = dev_get_drvdata(dev);

	if (!ctrl->tls_key)
		return 0;
	return sysfs_emit(buf, "%08x", key_serial(ctrl->tls_key));
}
static DEVICE_ATTR_RO(tls_key);
#endif

static struct attribute *nvme_dev_attrs[] = {
	&dev_attr_reset_controller.attr,
	&dev_attr_rescan_controller.attr,
	&dev_attr_model.attr,
	&dev_attr_serial.attr,
	&dev_attr_firmware_rev.attr,
	&dev_attr_cntlid.attr,
	&dev_attr_delete_controller.attr,
	&dev_attr_transport.attr,
	&dev_attr_subsysnqn.attr,
	&dev_attr_address.attr,
	&dev_attr_state.attr,
	&dev_attr_numa_node.attr,
	&dev_attr_queue_count.attr,
	&dev_attr_sqsize.attr,
	&dev_attr_hostnqn.attr,
	&dev_attr_hostid.attr,
	&dev_attr_ctrl_loss_tmo.attr,
	&dev_attr_reconnect_delay.attr,
	&dev_attr_fast_io_fail_tmo.attr,
	&dev_attr_kato.attr,
	&dev_attr_cntrltype.attr,
	&dev_attr_dctype.attr,
#ifdef CONFIG_NVME_HOST_AUTH
	&dev_attr_dhchap_secret.attr,
	&dev_attr_dhchap_ctrl_secret.attr,
#endif
#ifdef CONFIG_NVME_TCP_TLS
	&dev_attr_tls_key.attr,
#endif
	NULL
};

static umode_t nvme_dev_attrs_are_visible(struct kobject *kobj,
		struct attribute *a, int n)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct nvme_ctrl *ctrl = dev_get_drvdata(dev);

	if (a == &dev_attr_delete_controller.attr && !ctrl->ops->delete_ctrl)
		return 0;
	if (a == &dev_attr_address.attr && !ctrl->ops->get_address)
		return 0;
	if (a == &dev_attr_hostnqn.attr && !ctrl->opts)
		return 0;
	if (a == &dev_attr_hostid.attr && !ctrl->opts)
		return 0;
	if (a == &dev_attr_ctrl_loss_tmo.attr && !ctrl->opts)
		return 0;
	if (a == &dev_attr_reconnect_delay.attr && !ctrl->opts)
		return 0;
	if (a == &dev_attr_fast_io_fail_tmo.attr && !ctrl->opts)
		return 0;
#ifdef CONFIG_NVME_HOST_AUTH
	if (a == &dev_attr_dhchap_secret.attr && !ctrl->opts)
		return 0;
	if (a == &dev_attr_dhchap_ctrl_secret.attr && !ctrl->opts)
		return 0;
#endif
#ifdef CONFIG_NVME_TCP_TLS
	if (a == &dev_attr_tls_key.attr &&
	    (!ctrl->opts || strcmp(ctrl->opts->transport, "tcp")))
		return 0;
#endif

	return a->mode;
}

const struct attribute_group nvme_dev_attrs_group = {
	.attrs		= nvme_dev_attrs,
	.is_visible	= nvme_dev_attrs_are_visible,
};
EXPORT_SYMBOL_GPL(nvme_dev_attrs_group);

const struct attribute_group *nvme_dev_attr_groups[] = {
	&nvme_dev_attrs_group,
	NULL,
};

#define SUBSYS_ATTR_RO(_name, _mode, _show)			\
	struct device_attribute subsys_attr_##_name = \
		__ATTR(_name, _mode, _show, NULL)

static ssize_t nvme_subsys_show_nqn(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	struct nvme_subsystem *subsys =
		container_of(dev, struct nvme_subsystem, dev);

	return sysfs_emit(buf, "%s\n", subsys->subnqn);
}
static SUBSYS_ATTR_RO(subsysnqn, S_IRUGO, nvme_subsys_show_nqn);

static ssize_t nvme_subsys_show_type(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	struct nvme_subsystem *subsys =
		container_of(dev, struct nvme_subsystem, dev);

	switch (subsys->subtype) {
	case NVME_NQN_DISC:
		return sysfs_emit(buf, "discovery\n");
	case NVME_NQN_NVME:
		return sysfs_emit(buf, "nvm\n");
	default:
		return sysfs_emit(buf, "reserved\n");
	}
}
static SUBSYS_ATTR_RO(subsystype, S_IRUGO, nvme_subsys_show_type);

#define nvme_subsys_show_str_function(field)				\
static ssize_t subsys_##field##_show(struct device *dev,		\
			    struct device_attribute *attr, char *buf)	\
{									\
	struct nvme_subsystem *subsys =					\
		container_of(dev, struct nvme_subsystem, dev);		\
	return sysfs_emit(buf, "%.*s\n",				\
			   (int)sizeof(subsys->field), subsys->field);	\
}									\
static SUBSYS_ATTR_RO(field, S_IRUGO, subsys_##field##_show);

nvme_subsys_show_str_function(model);
nvme_subsys_show_str_function(serial);
nvme_subsys_show_str_function(firmware_rev);

static struct attribute *nvme_subsys_attrs[] = {
	&subsys_attr_model.attr,
	&subsys_attr_serial.attr,
	&subsys_attr_firmware_rev.attr,
	&subsys_attr_subsysnqn.attr,
	&subsys_attr_subsystype.attr,
#ifdef CONFIG_NVME_MULTIPATH
	&subsys_attr_iopolicy.attr,
#endif
	NULL,
};

static const struct attribute_group nvme_subsys_attrs_group = {
	.attrs = nvme_subsys_attrs,
};

const struct attribute_group *nvme_subsys_attrs_groups[] = {
	&nvme_subsys_attrs_group,
	NULL,
};
