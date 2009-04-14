#include <linux/moduleparam.h>
#include <linux/delay.h>
#include <linux/etherdevice.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <linux/kthread.h>
#include <linux/kfifo.h>

#include "host.h"
#include "decl.h"
#include "dev.h"
#include "wext.h"
#include "debugfs.h"
#include "scan.h"
#include "assoc.h"
#include "cmd.h"

static int mesh_get_default_parameters(struct device *dev,
				       struct mrvl_mesh_defaults *defs)
{
	struct lbs_private *priv = to_net_dev(dev)->ml_priv;
	struct cmd_ds_mesh_config cmd;
	int ret;

	memset(&cmd, 0, sizeof(struct cmd_ds_mesh_config));
	ret = lbs_mesh_config_send(priv, &cmd, CMD_ACT_MESH_CONFIG_GET,
				   CMD_TYPE_MESH_GET_DEFAULTS);

	if (ret)
		return -EOPNOTSUPP;

	memcpy(defs, &cmd.data[0], sizeof(struct mrvl_mesh_defaults));

	return 0;
}

/**
 * @brief Get function for sysfs attribute bootflag
 */
static ssize_t bootflag_get(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	struct mrvl_mesh_defaults defs;
	int ret;

	ret = mesh_get_default_parameters(dev, &defs);

	if (ret)
		return ret;

	return snprintf(buf, 12, "%d\n", le32_to_cpu(defs.bootflag));
}

/**
 * @brief Set function for sysfs attribute bootflag
 */
static ssize_t bootflag_set(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	struct lbs_private *priv = to_net_dev(dev)->ml_priv;
	struct cmd_ds_mesh_config cmd;
	uint32_t datum;
	int ret;

	memset(&cmd, 0, sizeof(cmd));
	ret = sscanf(buf, "%d", &datum);
	if ((ret != 1) || (datum > 1))
		return -EINVAL;

	*((__le32 *)&cmd.data[0]) = cpu_to_le32(!!datum);
	cmd.length = cpu_to_le16(sizeof(uint32_t));
	ret = lbs_mesh_config_send(priv, &cmd, CMD_ACT_MESH_CONFIG_SET,
				   CMD_TYPE_MESH_SET_BOOTFLAG);
	if (ret)
		return ret;

	return strlen(buf);
}

/**
 * @brief Get function for sysfs attribute boottime
 */
static ssize_t boottime_get(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	struct mrvl_mesh_defaults defs;
	int ret;

	ret = mesh_get_default_parameters(dev, &defs);

	if (ret)
		return ret;

	return snprintf(buf, 12, "%d\n", defs.boottime);
}

/**
 * @brief Set function for sysfs attribute boottime
 */
static ssize_t boottime_set(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct lbs_private *priv = to_net_dev(dev)->ml_priv;
	struct cmd_ds_mesh_config cmd;
	uint32_t datum;
	int ret;

	memset(&cmd, 0, sizeof(cmd));
	ret = sscanf(buf, "%d", &datum);
	if ((ret != 1) || (datum > 255))
		return -EINVAL;

	/* A too small boot time will result in the device booting into
	 * standalone (no-host) mode before the host can take control of it,
	 * so the change will be hard to revert.  This may be a desired
	 * feature (e.g to configure a very fast boot time for devices that
	 * will not be attached to a host), but dangerous.  So I'm enforcing a
	 * lower limit of 20 seconds:  remove and recompile the driver if this
	 * does not work for you.
	 */
	datum = (datum < 20) ? 20 : datum;
	cmd.data[0] = datum;
	cmd.length = cpu_to_le16(sizeof(uint8_t));
	ret = lbs_mesh_config_send(priv, &cmd, CMD_ACT_MESH_CONFIG_SET,
				   CMD_TYPE_MESH_SET_BOOTTIME);
	if (ret)
		return ret;

	return strlen(buf);
}

/**
 * @brief Get function for sysfs attribute channel
 */
static ssize_t channel_get(struct device *dev,
			   struct device_attribute *attr, char *buf)
{
	struct mrvl_mesh_defaults defs;
	int ret;

	ret = mesh_get_default_parameters(dev, &defs);

	if (ret)
		return ret;

	return snprintf(buf, 12, "%d\n", le16_to_cpu(defs.channel));
}

/**
 * @brief Set function for sysfs attribute channel
 */
static ssize_t channel_set(struct device *dev, struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct lbs_private *priv = to_net_dev(dev)->ml_priv;
	struct cmd_ds_mesh_config cmd;
	uint32_t datum;
	int ret;

	memset(&cmd, 0, sizeof(cmd));
	ret = sscanf(buf, "%d", &datum);
	if (ret != 1 || datum < 1 || datum > 11)
		return -EINVAL;

	*((__le16 *)&cmd.data[0]) = cpu_to_le16(datum);
	cmd.length = cpu_to_le16(sizeof(uint16_t));
	ret = lbs_mesh_config_send(priv, &cmd, CMD_ACT_MESH_CONFIG_SET,
				   CMD_TYPE_MESH_SET_DEF_CHANNEL);
	if (ret)
		return ret;

	return strlen(buf);
}

/**
 * @brief Get function for sysfs attribute mesh_id
 */
static ssize_t mesh_id_get(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct mrvl_mesh_defaults defs;
	int maxlen;
	int ret;

	ret = mesh_get_default_parameters(dev, &defs);

	if (ret)
		return ret;

	if (defs.meshie.val.mesh_id_len > IW_ESSID_MAX_SIZE) {
		lbs_pr_err("inconsistent mesh ID length");
		defs.meshie.val.mesh_id_len = IW_ESSID_MAX_SIZE;
	}

	/* SSID not null terminated: reserve room for \0 + \n */
	maxlen = defs.meshie.val.mesh_id_len + 2;
	maxlen = (PAGE_SIZE > maxlen) ? maxlen : PAGE_SIZE;

	defs.meshie.val.mesh_id[defs.meshie.val.mesh_id_len] = '\0';

	return snprintf(buf, maxlen, "%s\n", defs.meshie.val.mesh_id);
}

/**
 * @brief Set function for sysfs attribute mesh_id
 */
static ssize_t mesh_id_set(struct device *dev, struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct cmd_ds_mesh_config cmd;
	struct mrvl_mesh_defaults defs;
	struct mrvl_meshie *ie;
	struct lbs_private *priv = to_net_dev(dev)->ml_priv;
	int len;
	int ret;

	if (count < 2 || count > IW_ESSID_MAX_SIZE + 1)
		return -EINVAL;

	memset(&cmd, 0, sizeof(struct cmd_ds_mesh_config));
	ie = (struct mrvl_meshie *) &cmd.data[0];

	/* fetch all other Information Element parameters */
	ret = mesh_get_default_parameters(dev, &defs);

	cmd.length = cpu_to_le16(sizeof(struct mrvl_meshie));

	/* transfer IE elements */
	memcpy(ie, &defs.meshie, sizeof(struct mrvl_meshie));

	len = count - 1;
	memcpy(ie->val.mesh_id, buf, len);
	/* SSID len */
	ie->val.mesh_id_len = len;
	/* IE len */
	ie->len = sizeof(struct mrvl_meshie_val) - IW_ESSID_MAX_SIZE + len;

	ret = lbs_mesh_config_send(priv, &cmd, CMD_ACT_MESH_CONFIG_SET,
				   CMD_TYPE_MESH_SET_MESH_IE);
	if (ret)
		return ret;

	return strlen(buf);
}

/**
 * @brief Get function for sysfs attribute protocol_id
 */
static ssize_t protocol_id_get(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct mrvl_mesh_defaults defs;
	int ret;

	ret = mesh_get_default_parameters(dev, &defs);

	if (ret)
		return ret;

	return snprintf(buf, 5, "%d\n", defs.meshie.val.active_protocol_id);
}

/**
 * @brief Set function for sysfs attribute protocol_id
 */
static ssize_t protocol_id_set(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct cmd_ds_mesh_config cmd;
	struct mrvl_mesh_defaults defs;
	struct mrvl_meshie *ie;
	struct lbs_private *priv = to_net_dev(dev)->ml_priv;
	uint32_t datum;
	int ret;

	memset(&cmd, 0, sizeof(cmd));
	ret = sscanf(buf, "%d", &datum);
	if ((ret != 1) || (datum > 255))
		return -EINVAL;

	/* fetch all other Information Element parameters */
	ret = mesh_get_default_parameters(dev, &defs);

	cmd.length = cpu_to_le16(sizeof(struct mrvl_meshie));

	/* transfer IE elements */
	ie = (struct mrvl_meshie *) &cmd.data[0];
	memcpy(ie, &defs.meshie, sizeof(struct mrvl_meshie));
	/* update protocol id */
	ie->val.active_protocol_id = datum;

	ret = lbs_mesh_config_send(priv, &cmd, CMD_ACT_MESH_CONFIG_SET,
				   CMD_TYPE_MESH_SET_MESH_IE);
	if (ret)
		return ret;

	return strlen(buf);
}

/**
 * @brief Get function for sysfs attribute metric_id
 */
static ssize_t metric_id_get(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mrvl_mesh_defaults defs;
	int ret;

	ret = mesh_get_default_parameters(dev, &defs);

	if (ret)
		return ret;

	return snprintf(buf, 5, "%d\n", defs.meshie.val.active_metric_id);
}

/**
 * @brief Set function for sysfs attribute metric_id
 */
static ssize_t metric_id_set(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t count)
{
	struct cmd_ds_mesh_config cmd;
	struct mrvl_mesh_defaults defs;
	struct mrvl_meshie *ie;
	struct lbs_private *priv = to_net_dev(dev)->ml_priv;
	uint32_t datum;
	int ret;

	memset(&cmd, 0, sizeof(cmd));
	ret = sscanf(buf, "%d", &datum);
	if ((ret != 1) || (datum > 255))
		return -EINVAL;

	/* fetch all other Information Element parameters */
	ret = mesh_get_default_parameters(dev, &defs);

	cmd.length = cpu_to_le16(sizeof(struct mrvl_meshie));

	/* transfer IE elements */
	ie = (struct mrvl_meshie *) &cmd.data[0];
	memcpy(ie, &defs.meshie, sizeof(struct mrvl_meshie));
	/* update metric id */
	ie->val.active_metric_id = datum;

	ret = lbs_mesh_config_send(priv, &cmd, CMD_ACT_MESH_CONFIG_SET,
				   CMD_TYPE_MESH_SET_MESH_IE);
	if (ret)
		return ret;

	return strlen(buf);
}

/**
 * @brief Get function for sysfs attribute capability
 */
static ssize_t capability_get(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mrvl_mesh_defaults defs;
	int ret;

	ret = mesh_get_default_parameters(dev, &defs);

	if (ret)
		return ret;

	return snprintf(buf, 5, "%d\n", defs.meshie.val.mesh_capability);
}

/**
 * @brief Set function for sysfs attribute capability
 */
static ssize_t capability_set(struct device *dev, struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct cmd_ds_mesh_config cmd;
	struct mrvl_mesh_defaults defs;
	struct mrvl_meshie *ie;
	struct lbs_private *priv = to_net_dev(dev)->ml_priv;
	uint32_t datum;
	int ret;

	memset(&cmd, 0, sizeof(cmd));
	ret = sscanf(buf, "%d", &datum);
	if ((ret != 1) || (datum > 255))
		return -EINVAL;

	/* fetch all other Information Element parameters */
	ret = mesh_get_default_parameters(dev, &defs);

	cmd.length = cpu_to_le16(sizeof(struct mrvl_meshie));

	/* transfer IE elements */
	ie = (struct mrvl_meshie *) &cmd.data[0];
	memcpy(ie, &defs.meshie, sizeof(struct mrvl_meshie));
	/* update value */
	ie->val.mesh_capability = datum;

	ret = lbs_mesh_config_send(priv, &cmd, CMD_ACT_MESH_CONFIG_SET,
				   CMD_TYPE_MESH_SET_MESH_IE);
	if (ret)
		return ret;

	return strlen(buf);
}


static DEVICE_ATTR(bootflag, 0644, bootflag_get, bootflag_set);
static DEVICE_ATTR(boottime, 0644, boottime_get, boottime_set);
static DEVICE_ATTR(channel, 0644, channel_get, channel_set);
static DEVICE_ATTR(mesh_id, 0644, mesh_id_get, mesh_id_set);
static DEVICE_ATTR(protocol_id, 0644, protocol_id_get, protocol_id_set);
static DEVICE_ATTR(metric_id, 0644, metric_id_get, metric_id_set);
static DEVICE_ATTR(capability, 0644, capability_get, capability_set);

static struct attribute *boot_opts_attrs[] = {
	&dev_attr_bootflag.attr,
	&dev_attr_boottime.attr,
	&dev_attr_channel.attr,
	NULL
};

static struct attribute_group boot_opts_group = {
	.name = "boot_options",
	.attrs = boot_opts_attrs,
};

static struct attribute *mesh_ie_attrs[] = {
	&dev_attr_mesh_id.attr,
	&dev_attr_protocol_id.attr,
	&dev_attr_metric_id.attr,
	&dev_attr_capability.attr,
	NULL
};

static struct attribute_group mesh_ie_group = {
	.name = "mesh_ie",
	.attrs = mesh_ie_attrs,
};

void lbs_persist_config_init(struct net_device *dev)
{
	int ret;
	ret = sysfs_create_group(&(dev->dev.kobj), &boot_opts_group);
	ret = sysfs_create_group(&(dev->dev.kobj), &mesh_ie_group);
}

void lbs_persist_config_remove(struct net_device *dev)
{
	sysfs_remove_group(&(dev->dev.kobj), &boot_opts_group);
	sysfs_remove_group(&(dev->dev.kobj), &mesh_ie_group);
}
