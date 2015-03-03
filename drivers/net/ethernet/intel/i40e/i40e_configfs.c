/*******************************************************************************
 *
 * Intel Ethernet Controller XL710 Family Linux Driver
 * Copyright(c) 2013 - 2015 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Contact Information:
 * e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 ******************************************************************************/

#include <linux/configfs.h>
#include "i40e.h"

#if IS_ENABLED(CONFIG_I40E_CONFIGFS_FS)

/**
 * configfs structure for i40e
 *
 * This file adds code for configfs support for the i40e driver.  This sets
 * up a filesystem under /sys/kernel/config in which configuration changes
 * can be made for the driver's netdevs.
 *
 * The initialization in this code creates the "i40e" entry in the configfs
 * system.  After that, the user needs to use mkdir to create configurations
 * for specific netdev ports; for example "mkdir eth3".  This code will verify
 * that such a netdev exists and that it is owned by i40e.
 *
 **/

struct i40e_cfgfs_vsi {
	struct config_item item;
	struct i40e_vsi *vsi;
};

static inline struct i40e_cfgfs_vsi *to_i40e_cfgfs_vsi(struct config_item *item)
{
	return item ? container_of(item, struct i40e_cfgfs_vsi, item) : NULL;
}

static struct configfs_attribute i40e_cfgfs_vsi_attr_min_bw = {
	.ca_owner = THIS_MODULE,
	.ca_name = "min_bw",
	.ca_mode = S_IRUGO | S_IWUSR,
};

static struct configfs_attribute i40e_cfgfs_vsi_attr_max_bw = {
	.ca_owner = THIS_MODULE,
	.ca_name = "max_bw",
	.ca_mode = S_IRUGO | S_IWUSR,
};

static struct configfs_attribute i40e_cfgfs_vsi_attr_commit = {
	.ca_owner = THIS_MODULE,
	.ca_name = "commit",
	.ca_mode = S_IRUGO | S_IWUSR,
};

static struct configfs_attribute i40e_cfgfs_vsi_attr_port_count = {
	.ca_owner = THIS_MODULE,
	.ca_name = "ports",
	.ca_mode = S_IRUGO | S_IWUSR,
};

static struct configfs_attribute i40e_cfgfs_vsi_attr_part_count = {
	.ca_owner = THIS_MODULE,
	.ca_name = "partitions",
	.ca_mode = S_IRUGO | S_IWUSR,
};

static struct configfs_attribute *i40e_cfgfs_vsi_attrs[] = {
	&i40e_cfgfs_vsi_attr_min_bw,
	&i40e_cfgfs_vsi_attr_max_bw,
	&i40e_cfgfs_vsi_attr_commit,
	&i40e_cfgfs_vsi_attr_port_count,
	&i40e_cfgfs_vsi_attr_part_count,
	NULL,
};

/**
 * i40e_cfgfs_vsi_attr_show - Show a VSI's NPAR BW partition info
 * @item: A pointer back to the configfs item created on driver load
 * @attr: A pointer to this item's configuration attribute
 * @page: A pointer to the output buffer
 **/
static ssize_t i40e_cfgfs_vsi_attr_show(struct config_item *item,
					struct configfs_attribute *attr,
					char *page)
{
	struct i40e_cfgfs_vsi *i40e_cfgfs_vsi = to_i40e_cfgfs_vsi(item);
	struct i40e_pf *pf = i40e_cfgfs_vsi->vsi->back;
	ssize_t count;

	if (i40e_cfgfs_vsi->vsi != pf->vsi[pf->lan_vsi])
		return 0;

	if (strncmp(attr->ca_name, "min_bw", 6) == 0)
		count = sprintf(page, "%s %s %d%%\n",
				i40e_cfgfs_vsi->vsi->netdev->name,
				(pf->npar_min_bw & I40E_ALT_BW_RELATIVE_MASK) ?
				"Relative Min BW" : "Absolute Min BW",
				pf->npar_min_bw & I40E_ALT_BW_VALUE_MASK);
	else if (strncmp(attr->ca_name, "max_bw", 6) == 0)
		count = sprintf(page, "%s %s %d%%\n",
				i40e_cfgfs_vsi->vsi->netdev->name,
				(pf->npar_max_bw & I40E_ALT_BW_RELATIVE_MASK) ?
				"Relative Max BW" : "Absolute Max BW",
				pf->npar_max_bw & I40E_ALT_BW_VALUE_MASK);
	else if (strncmp(attr->ca_name, "ports", 5) == 0)
		count = sprintf(page, "%d\n",
				pf->hw.num_ports);
	else if (strncmp(attr->ca_name, "partitions", 10) == 0)
		count = sprintf(page, "%d\n",
				pf->hw.num_partitions);
	else
		return 0;

	return count;
}

/**
 * i40e_cfgfs_vsi_attr_store - Show a VSI's NPAR BW partition info
 * @item: A pointer back to the configfs item created on driver load
 * @attr: A pointer to this item's configuration attribute
 * @page: A pointer to the user input buffer holding the user input values
 **/
static ssize_t i40e_cfgfs_vsi_attr_store(struct config_item *item,
					 struct configfs_attribute *attr,
					 const char *page, size_t count)
{
	struct i40e_cfgfs_vsi *i40e_cfgfs_vsi = to_i40e_cfgfs_vsi(item);
	struct i40e_pf *pf = i40e_cfgfs_vsi->vsi->back;
	char *p = (char *)page;
	int rc;
	unsigned long tmp;

	if (i40e_cfgfs_vsi->vsi != pf->vsi[pf->lan_vsi])
		return 0;

	if (!p || (*p && (*p == '\n')))
		return -EINVAL;

	rc = kstrtoul(p, 10, &tmp);
	if (rc)
		return rc;
	if (tmp > 100)
		return -ERANGE;

	if (strncmp(attr->ca_name, "min_bw", 6) == 0) {
		if (tmp > (pf->npar_max_bw & I40E_ALT_BW_VALUE_MASK))
			return -ERANGE;
		/* Preserve the valid and relative BW bits - the rest is
		 * don't care.
		 */
		pf->npar_min_bw &= (I40E_ALT_BW_RELATIVE_MASK |
				    I40E_ALT_BW_VALID_MASK);
		pf->npar_min_bw |= (tmp & I40E_ALT_BW_VALUE_MASK);
		i40e_set_npar_bw_setting(pf);
	} else if (strncmp(attr->ca_name, "max_bw", 6) == 0) {
		if (tmp < 1 ||
		    tmp < (pf->npar_min_bw & I40E_ALT_BW_VALUE_MASK))
			return -ERANGE;
		/* Preserve the valid and relative BW bits - the rest is
		 * don't care.
		 */
		pf->npar_max_bw &= (I40E_ALT_BW_RELATIVE_MASK |
				    I40E_ALT_BW_VALID_MASK);
		pf->npar_max_bw |= (tmp & I40E_ALT_BW_VALUE_MASK);
		i40e_set_npar_bw_setting(pf);
	} else if (strncmp(attr->ca_name, "commit", 6) == 0 && tmp == 1) {
		if (i40e_commit_npar_bw_setting(pf))
			return -EIO;
	}

	return count;
}

/**
 * i40e_cfgfs_vsi_release - Free up the configuration item memory
 * @item: A pointer back to the configfs item created on driver load
 **/
static void i40e_cfgfs_vsi_release(struct config_item *item)
{
	kfree(to_i40e_cfgfs_vsi(item));
}

static struct configfs_item_operations i40e_cfgfs_vsi_item_ops = {
	.release		= i40e_cfgfs_vsi_release,
	.show_attribute		= i40e_cfgfs_vsi_attr_show,
	.store_attribute	= i40e_cfgfs_vsi_attr_store,
};

static struct config_item_type i40e_cfgfs_vsi_type = {
	.ct_item_ops	= &i40e_cfgfs_vsi_item_ops,
	.ct_attrs	= i40e_cfgfs_vsi_attrs,
	.ct_owner	= THIS_MODULE,
};

struct i40e_cfgfs_group {
	struct config_group group;
};

/**
 * to_i40e_cfgfs_group - Get the group pointer from the config item
 * @item: A pointer back to the configfs item created on driver load
 **/
static inline struct i40e_cfgfs_group *
to_i40e_cfgfs_group(struct config_item *item)
{
	return item ? container_of(to_config_group(item),
				   struct i40e_cfgfs_group, group) : NULL;
}

/**
 * i40e_cfgfs_group_make_item - Create the configfs item with group container
 * @group: A pointer to our configfs group
 * @name: A pointer to the nume of the device we're looking for
 **/
static struct config_item *
i40e_cfgfs_group_make_item(struct config_group *group, const char *name)
{
	struct i40e_cfgfs_vsi *i40e_cfgfs_vsi;
	struct net_device *netdev;
	struct i40e_netdev_priv *np;

	read_lock(&dev_base_lock);
	netdev = first_net_device(&init_net);
	while (netdev) {
		if (strncmp(netdev->name, name, sizeof(netdev->name)) == 0)
			break;
		netdev = next_net_device(netdev);
	}
	read_unlock(&dev_base_lock);

	if (!netdev)
		return ERR_PTR(-ENODEV);

	/* is this netdev owned by i40e? */
	if (netdev->netdev_ops->ndo_open != i40e_open)
		return ERR_PTR(-EACCES);

	i40e_cfgfs_vsi = kzalloc(sizeof(*i40e_cfgfs_vsi), GFP_KERNEL);
	if (!i40e_cfgfs_vsi)
		return ERR_PTR(-ENOMEM);

	np = netdev_priv(netdev);
	i40e_cfgfs_vsi->vsi = np->vsi;
	config_item_init_type_name(&i40e_cfgfs_vsi->item, name,
				   &i40e_cfgfs_vsi_type);

	return &i40e_cfgfs_vsi->item;
}

static struct configfs_attribute i40e_cfgfs_group_attr_description = {
	.ca_owner = THIS_MODULE,
	.ca_name = "description",
	.ca_mode = S_IRUGO,
};

static struct configfs_attribute *i40e_cfgfs_group_attrs[] = {
	&i40e_cfgfs_group_attr_description,
	NULL,
};

static ssize_t i40e_cfgfs_group_attr_show(struct config_item *item,
					  struct configfs_attribute *attr,
					  char *page)
{
	return sprintf(page,
"i40e\n"
"\n"
"This subsystem allows the modification of network port configurations.\n"
"To start, use the name of the network port to be configured in a 'mkdir'\n"
"command, e.g. 'mkdir eth3'.\n");
}

static void i40e_cfgfs_group_release(struct config_item *item)
{
	kfree(to_i40e_cfgfs_group(item));
}

static struct configfs_item_operations i40e_cfgfs_group_item_ops = {
	.release	= i40e_cfgfs_group_release,
	.show_attribute	= i40e_cfgfs_group_attr_show,
};

/* Note that, since no extra work is required on ->drop_item(),
 * no ->drop_item() is provided.
 */
static struct configfs_group_operations i40e_cfgfs_group_ops = {
	.make_item	= i40e_cfgfs_group_make_item,
};

static struct config_item_type i40e_cfgfs_group_type = {
	.ct_item_ops	= &i40e_cfgfs_group_item_ops,
	.ct_group_ops	= &i40e_cfgfs_group_ops,
	.ct_attrs	= i40e_cfgfs_group_attrs,
	.ct_owner	= THIS_MODULE,
};

static struct configfs_subsystem i40e_cfgfs_group_subsys = {
	.su_group = {
		.cg_item = {
			.ci_namebuf = "i40e",
			.ci_type = &i40e_cfgfs_group_type,
		},
	},
};

/**
 * i40e_configfs_init - Initialize configfs support for our driver
 **/
int i40e_configfs_init(void)
{
	int ret;
	struct configfs_subsystem *subsys;

	subsys = &i40e_cfgfs_group_subsys;

	config_group_init(&subsys->su_group);
	mutex_init(&subsys->su_mutex);
	ret = configfs_register_subsystem(subsys);
	if (ret) {
		pr_err("Error %d while registering configfs subsystem %s\n",
		       ret, subsys->su_group.cg_item.ci_namebuf);
		return ret;
	}

	return 0;
}

/**
 * i40e_configfs_init - Bail out - unregister configfs subsystem and release
 **/
void i40e_configfs_exit(void)
{
	configfs_unregister_subsystem(&i40e_cfgfs_group_subsys);
}
#endif /* IS_ENABLED(CONFIG_I40E_CONFIGFS_FS) */
