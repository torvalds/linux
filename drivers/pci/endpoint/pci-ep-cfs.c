/**
 * configfs to configure the PCI endpoint
 *
 * Copyright (C) 2017 Texas Instruments
 * Author: Kishon Vijay Abraham I <kishon@ti.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 of
 * the License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/module.h>
#include <linux/slab.h>

#include <linux/pci-epc.h>
#include <linux/pci-epf.h>
#include <linux/pci-ep-cfs.h>

static struct config_group *functions_group;
static struct config_group *controllers_group;

struct pci_epf_group {
	struct config_group group;
	struct pci_epf *epf;
};

struct pci_epc_group {
	struct config_group group;
	struct pci_epc *epc;
	bool start;
	unsigned long function_num_map;
};

static inline struct pci_epf_group *to_pci_epf_group(struct config_item *item)
{
	return container_of(to_config_group(item), struct pci_epf_group, group);
}

static inline struct pci_epc_group *to_pci_epc_group(struct config_item *item)
{
	return container_of(to_config_group(item), struct pci_epc_group, group);
}

static ssize_t pci_epc_start_store(struct config_item *item, const char *page,
				   size_t len)
{
	int ret;
	bool start;
	struct pci_epc *epc;
	struct pci_epc_group *epc_group = to_pci_epc_group(item);

	epc = epc_group->epc;

	ret = kstrtobool(page, &start);
	if (ret)
		return ret;

	if (!start) {
		pci_epc_stop(epc);
		return len;
	}

	ret = pci_epc_start(epc);
	if (ret) {
		dev_err(&epc->dev, "failed to start endpoint controller\n");
		return -EINVAL;
	}

	epc_group->start = start;

	return len;
}

static ssize_t pci_epc_start_show(struct config_item *item, char *page)
{
	return sprintf(page, "%d\n",
		       to_pci_epc_group(item)->start);
}

CONFIGFS_ATTR(pci_epc_, start);

static struct configfs_attribute *pci_epc_attrs[] = {
	&pci_epc_attr_start,
	NULL,
};

static int pci_epc_epf_link(struct config_item *epc_item,
			    struct config_item *epf_item)
{
	int ret;
	u32 func_no = 0;
	struct pci_epc *epc;
	struct pci_epf *epf;
	struct pci_epf_group *epf_group = to_pci_epf_group(epf_item);
	struct pci_epc_group *epc_group = to_pci_epc_group(epc_item);

	epc = epc_group->epc;
	epf = epf_group->epf;
	ret = pci_epc_add_epf(epc, epf);
	if (ret)
		goto err_add_epf;

	func_no = find_first_zero_bit(&epc_group->function_num_map,
				      BITS_PER_LONG);
	if (func_no >= BITS_PER_LONG)
		return -EINVAL;

	set_bit(func_no, &epc_group->function_num_map);
	epf->func_no = func_no;

	ret = pci_epf_bind(epf);
	if (ret)
		goto err_epf_bind;

	return 0;

err_epf_bind:
	pci_epc_remove_epf(epc, epf);

err_add_epf:
	clear_bit(func_no, &epc_group->function_num_map);

	return ret;
}

static void pci_epc_epf_unlink(struct config_item *epc_item,
			       struct config_item *epf_item)
{
	struct pci_epc *epc;
	struct pci_epf *epf;
	struct pci_epf_group *epf_group = to_pci_epf_group(epf_item);
	struct pci_epc_group *epc_group = to_pci_epc_group(epc_item);

	WARN_ON_ONCE(epc_group->start);

	epc = epc_group->epc;
	epf = epf_group->epf;
	clear_bit(epf->func_no, &epc_group->function_num_map);
	pci_epf_unbind(epf);
	pci_epc_remove_epf(epc, epf);
}

static struct configfs_item_operations pci_epc_item_ops = {
	.allow_link	= pci_epc_epf_link,
	.drop_link	= pci_epc_epf_unlink,
};

static struct config_item_type pci_epc_type = {
	.ct_item_ops	= &pci_epc_item_ops,
	.ct_attrs	= pci_epc_attrs,
	.ct_owner	= THIS_MODULE,
};

struct config_group *pci_ep_cfs_add_epc_group(const char *name)
{
	int ret;
	struct pci_epc *epc;
	struct config_group *group;
	struct pci_epc_group *epc_group;

	epc_group = kzalloc(sizeof(*epc_group), GFP_KERNEL);
	if (!epc_group) {
		ret = -ENOMEM;
		goto err;
	}

	group = &epc_group->group;

	config_group_init_type_name(group, name, &pci_epc_type);
	ret = configfs_register_group(controllers_group, group);
	if (ret) {
		pr_err("failed to register configfs group for %s\n", name);
		goto err_register_group;
	}

	epc = pci_epc_get(name);
	if (IS_ERR(epc)) {
		ret = PTR_ERR(epc);
		goto err_epc_get;
	}

	epc_group->epc = epc;

	return group;

err_epc_get:
	configfs_unregister_group(group);

err_register_group:
	kfree(epc_group);

err:
	return ERR_PTR(ret);
}
EXPORT_SYMBOL(pci_ep_cfs_add_epc_group);

void pci_ep_cfs_remove_epc_group(struct config_group *group)
{
	struct pci_epc_group *epc_group;

	if (!group)
		return;

	epc_group = container_of(group, struct pci_epc_group, group);
	pci_epc_put(epc_group->epc);
	configfs_unregister_group(&epc_group->group);
	kfree(epc_group);
}
EXPORT_SYMBOL(pci_ep_cfs_remove_epc_group);

#define PCI_EPF_HEADER_R(_name)						       \
static ssize_t pci_epf_##_name##_show(struct config_item *item,	char *page)    \
{									       \
	struct pci_epf *epf = to_pci_epf_group(item)->epf;		       \
	if (WARN_ON_ONCE(!epf->header))					       \
		return -EINVAL;						       \
	return sprintf(page, "0x%04x\n", epf->header->_name);		       \
}

#define PCI_EPF_HEADER_W_u32(_name)					       \
static ssize_t pci_epf_##_name##_store(struct config_item *item,	       \
				       const char *page, size_t len)	       \
{									       \
	u32 val;							       \
	int ret;							       \
	struct pci_epf *epf = to_pci_epf_group(item)->epf;		       \
	if (WARN_ON_ONCE(!epf->header))					       \
		return -EINVAL;						       \
	ret = kstrtou32(page, 0, &val);					       \
	if (ret)							       \
		return ret;						       \
	epf->header->_name = val;					       \
	return len;							       \
}

#define PCI_EPF_HEADER_W_u16(_name)					       \
static ssize_t pci_epf_##_name##_store(struct config_item *item,	       \
				       const char *page, size_t len)	       \
{									       \
	u16 val;							       \
	int ret;							       \
	struct pci_epf *epf = to_pci_epf_group(item)->epf;		       \
	if (WARN_ON_ONCE(!epf->header))					       \
		return -EINVAL;						       \
	ret = kstrtou16(page, 0, &val);					       \
	if (ret)							       \
		return ret;						       \
	epf->header->_name = val;					       \
	return len;							       \
}

#define PCI_EPF_HEADER_W_u8(_name)					       \
static ssize_t pci_epf_##_name##_store(struct config_item *item,	       \
				       const char *page, size_t len)	       \
{									       \
	u8 val;								       \
	int ret;							       \
	struct pci_epf *epf = to_pci_epf_group(item)->epf;		       \
	if (WARN_ON_ONCE(!epf->header))					       \
		return -EINVAL;						       \
	ret = kstrtou8(page, 0, &val);					       \
	if (ret)							       \
		return ret;						       \
	epf->header->_name = val;					       \
	return len;							       \
}

static ssize_t pci_epf_msi_interrupts_store(struct config_item *item,
					    const char *page, size_t len)
{
	u8 val;
	int ret;

	ret = kstrtou8(page, 0, &val);
	if (ret)
		return ret;

	to_pci_epf_group(item)->epf->msi_interrupts = val;

	return len;
}

static ssize_t pci_epf_msi_interrupts_show(struct config_item *item,
					   char *page)
{
	return sprintf(page, "%d\n",
		       to_pci_epf_group(item)->epf->msi_interrupts);
}

PCI_EPF_HEADER_R(vendorid)
PCI_EPF_HEADER_W_u16(vendorid)

PCI_EPF_HEADER_R(deviceid)
PCI_EPF_HEADER_W_u16(deviceid)

PCI_EPF_HEADER_R(revid)
PCI_EPF_HEADER_W_u8(revid)

PCI_EPF_HEADER_R(progif_code)
PCI_EPF_HEADER_W_u8(progif_code)

PCI_EPF_HEADER_R(subclass_code)
PCI_EPF_HEADER_W_u8(subclass_code)

PCI_EPF_HEADER_R(baseclass_code)
PCI_EPF_HEADER_W_u8(baseclass_code)

PCI_EPF_HEADER_R(cache_line_size)
PCI_EPF_HEADER_W_u8(cache_line_size)

PCI_EPF_HEADER_R(subsys_vendor_id)
PCI_EPF_HEADER_W_u16(subsys_vendor_id)

PCI_EPF_HEADER_R(subsys_id)
PCI_EPF_HEADER_W_u16(subsys_id)

PCI_EPF_HEADER_R(interrupt_pin)
PCI_EPF_HEADER_W_u8(interrupt_pin)

CONFIGFS_ATTR(pci_epf_, vendorid);
CONFIGFS_ATTR(pci_epf_, deviceid);
CONFIGFS_ATTR(pci_epf_, revid);
CONFIGFS_ATTR(pci_epf_, progif_code);
CONFIGFS_ATTR(pci_epf_, subclass_code);
CONFIGFS_ATTR(pci_epf_, baseclass_code);
CONFIGFS_ATTR(pci_epf_, cache_line_size);
CONFIGFS_ATTR(pci_epf_, subsys_vendor_id);
CONFIGFS_ATTR(pci_epf_, subsys_id);
CONFIGFS_ATTR(pci_epf_, interrupt_pin);
CONFIGFS_ATTR(pci_epf_, msi_interrupts);

static struct configfs_attribute *pci_epf_attrs[] = {
	&pci_epf_attr_vendorid,
	&pci_epf_attr_deviceid,
	&pci_epf_attr_revid,
	&pci_epf_attr_progif_code,
	&pci_epf_attr_subclass_code,
	&pci_epf_attr_baseclass_code,
	&pci_epf_attr_cache_line_size,
	&pci_epf_attr_subsys_vendor_id,
	&pci_epf_attr_subsys_id,
	&pci_epf_attr_interrupt_pin,
	&pci_epf_attr_msi_interrupts,
	NULL,
};

static void pci_epf_release(struct config_item *item)
{
	struct pci_epf_group *epf_group = to_pci_epf_group(item);

	pci_epf_destroy(epf_group->epf);
	kfree(epf_group);
}

static struct configfs_item_operations pci_epf_ops = {
	.release		= pci_epf_release,
};

static struct config_item_type pci_epf_type = {
	.ct_item_ops	= &pci_epf_ops,
	.ct_attrs	= pci_epf_attrs,
	.ct_owner	= THIS_MODULE,
};

static struct config_group *pci_epf_make(struct config_group *group,
					 const char *name)
{
	struct pci_epf_group *epf_group;
	struct pci_epf *epf;

	epf_group = kzalloc(sizeof(*epf_group), GFP_KERNEL);
	if (!epf_group)
		return ERR_PTR(-ENOMEM);

	config_group_init_type_name(&epf_group->group, name, &pci_epf_type);

	epf = pci_epf_create(group->cg_item.ci_name);
	if (IS_ERR(epf)) {
		pr_err("failed to create endpoint function device\n");
		return ERR_PTR(-EINVAL);
	}

	epf_group->epf = epf;

	return &epf_group->group;
}

static void pci_epf_drop(struct config_group *group, struct config_item *item)
{
	config_item_put(item);
}

static struct configfs_group_operations pci_epf_group_ops = {
	.make_group     = &pci_epf_make,
	.drop_item      = &pci_epf_drop,
};

static struct config_item_type pci_epf_group_type = {
	.ct_group_ops	= &pci_epf_group_ops,
	.ct_owner	= THIS_MODULE,
};

struct config_group *pci_ep_cfs_add_epf_group(const char *name)
{
	struct config_group *group;

	group = configfs_register_default_group(functions_group, name,
						&pci_epf_group_type);
	if (IS_ERR(group))
		pr_err("failed to register configfs group for %s function\n",
		       name);

	return group;
}
EXPORT_SYMBOL(pci_ep_cfs_add_epf_group);

void pci_ep_cfs_remove_epf_group(struct config_group *group)
{
	if (IS_ERR_OR_NULL(group))
		return;

	configfs_unregister_default_group(group);
}
EXPORT_SYMBOL(pci_ep_cfs_remove_epf_group);

static struct config_item_type pci_functions_type = {
	.ct_owner	= THIS_MODULE,
};

static struct config_item_type pci_controllers_type = {
	.ct_owner	= THIS_MODULE,
};

static struct config_item_type pci_ep_type = {
	.ct_owner	= THIS_MODULE,
};

static struct configfs_subsystem pci_ep_cfs_subsys = {
	.su_group = {
		.cg_item = {
			.ci_namebuf = "pci_ep",
			.ci_type = &pci_ep_type,
		},
	},
	.su_mutex = __MUTEX_INITIALIZER(pci_ep_cfs_subsys.su_mutex),
};

static int __init pci_ep_cfs_init(void)
{
	int ret;
	struct config_group *root = &pci_ep_cfs_subsys.su_group;

	config_group_init(root);

	ret = configfs_register_subsystem(&pci_ep_cfs_subsys);
	if (ret) {
		pr_err("Error %d while registering subsystem %s\n",
		       ret, root->cg_item.ci_namebuf);
		goto err;
	}

	functions_group = configfs_register_default_group(root, "functions",
							  &pci_functions_type);
	if (IS_ERR(functions_group)) {
		ret = PTR_ERR(functions_group);
		pr_err("Error %d while registering functions group\n",
		       ret);
		goto err_functions_group;
	}

	controllers_group =
		configfs_register_default_group(root, "controllers",
						&pci_controllers_type);
	if (IS_ERR(controllers_group)) {
		ret = PTR_ERR(controllers_group);
		pr_err("Error %d while registering controllers group\n",
		       ret);
		goto err_controllers_group;
	}

	return 0;

err_controllers_group:
	configfs_unregister_default_group(functions_group);

err_functions_group:
	configfs_unregister_subsystem(&pci_ep_cfs_subsys);

err:
	return ret;
}
module_init(pci_ep_cfs_init);

static void __exit pci_ep_cfs_exit(void)
{
	configfs_unregister_default_group(controllers_group);
	configfs_unregister_default_group(functions_group);
	configfs_unregister_subsystem(&pci_ep_cfs_subsys);
}
module_exit(pci_ep_cfs_exit);

MODULE_DESCRIPTION("PCI EP CONFIGFS");
MODULE_AUTHOR("Kishon Vijay Abraham I <kishon@ti.com>");
MODULE_LICENSE("GPL v2");
