// SPDX-License-Identifier: GPL-2.0
/*
 * Configfs interface for the NVMe target.
 * Copyright (c) 2015-2016 HGST, a Western Digital Company.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/stat.h>
#include <linux/ctype.h>
#include <linux/pci.h>
#include <linux/pci-p2pdma.h>

#include "nvmet.h"

static const struct config_item_type nvmet_host_type;
static const struct config_item_type nvmet_subsys_type;

static LIST_HEAD(nvmet_ports_list);
struct list_head *nvmet_ports = &nvmet_ports_list;

static const struct nvmet_transport_name {
	u8		type;
	const char	*name;
} nvmet_transport_names[] = {
	{ NVMF_TRTYPE_RDMA,	"rdma" },
	{ NVMF_TRTYPE_FC,	"fc" },
	{ NVMF_TRTYPE_TCP,	"tcp" },
	{ NVMF_TRTYPE_LOOP,	"loop" },
};

/*
 * nvmet_port Generic ConfigFS definitions.
 * Used in any place in the ConfigFS tree that refers to an address.
 */
static ssize_t nvmet_addr_adrfam_show(struct config_item *item,
		char *page)
{
	switch (to_nvmet_port(item)->disc_addr.adrfam) {
	case NVMF_ADDR_FAMILY_IP4:
		return sprintf(page, "ipv4\n");
	case NVMF_ADDR_FAMILY_IP6:
		return sprintf(page, "ipv6\n");
	case NVMF_ADDR_FAMILY_IB:
		return sprintf(page, "ib\n");
	case NVMF_ADDR_FAMILY_FC:
		return sprintf(page, "fc\n");
	default:
		return sprintf(page, "\n");
	}
}

static ssize_t nvmet_addr_adrfam_store(struct config_item *item,
		const char *page, size_t count)
{
	struct nvmet_port *port = to_nvmet_port(item);

	if (port->enabled) {
		pr_err("Cannot modify address while enabled\n");
		pr_err("Disable the address before modifying\n");
		return -EACCES;
	}

	if (sysfs_streq(page, "ipv4")) {
		port->disc_addr.adrfam = NVMF_ADDR_FAMILY_IP4;
	} else if (sysfs_streq(page, "ipv6")) {
		port->disc_addr.adrfam = NVMF_ADDR_FAMILY_IP6;
	} else if (sysfs_streq(page, "ib")) {
		port->disc_addr.adrfam = NVMF_ADDR_FAMILY_IB;
	} else if (sysfs_streq(page, "fc")) {
		port->disc_addr.adrfam = NVMF_ADDR_FAMILY_FC;
	} else {
		pr_err("Invalid value '%s' for adrfam\n", page);
		return -EINVAL;
	}

	return count;
}

CONFIGFS_ATTR(nvmet_, addr_adrfam);

static ssize_t nvmet_addr_portid_show(struct config_item *item,
		char *page)
{
	struct nvmet_port *port = to_nvmet_port(item);

	return snprintf(page, PAGE_SIZE, "%d\n",
			le16_to_cpu(port->disc_addr.portid));
}

static ssize_t nvmet_addr_portid_store(struct config_item *item,
		const char *page, size_t count)
{
	struct nvmet_port *port = to_nvmet_port(item);
	u16 portid = 0;

	if (kstrtou16(page, 0, &portid)) {
		pr_err("Invalid value '%s' for portid\n", page);
		return -EINVAL;
	}

	if (port->enabled) {
		pr_err("Cannot modify address while enabled\n");
		pr_err("Disable the address before modifying\n");
		return -EACCES;
	}
	port->disc_addr.portid = cpu_to_le16(portid);
	return count;
}

CONFIGFS_ATTR(nvmet_, addr_portid);

static ssize_t nvmet_addr_traddr_show(struct config_item *item,
		char *page)
{
	struct nvmet_port *port = to_nvmet_port(item);

	return snprintf(page, PAGE_SIZE, "%s\n",
			port->disc_addr.traddr);
}

static ssize_t nvmet_addr_traddr_store(struct config_item *item,
		const char *page, size_t count)
{
	struct nvmet_port *port = to_nvmet_port(item);

	if (count > NVMF_TRADDR_SIZE) {
		pr_err("Invalid value '%s' for traddr\n", page);
		return -EINVAL;
	}

	if (port->enabled) {
		pr_err("Cannot modify address while enabled\n");
		pr_err("Disable the address before modifying\n");
		return -EACCES;
	}

	if (sscanf(page, "%s\n", port->disc_addr.traddr) != 1)
		return -EINVAL;
	return count;
}

CONFIGFS_ATTR(nvmet_, addr_traddr);

static ssize_t nvmet_addr_treq_show(struct config_item *item,
		char *page)
{
	switch (to_nvmet_port(item)->disc_addr.treq &
		NVME_TREQ_SECURE_CHANNEL_MASK) {
	case NVMF_TREQ_NOT_SPECIFIED:
		return sprintf(page, "not specified\n");
	case NVMF_TREQ_REQUIRED:
		return sprintf(page, "required\n");
	case NVMF_TREQ_NOT_REQUIRED:
		return sprintf(page, "not required\n");
	default:
		return sprintf(page, "\n");
	}
}

static ssize_t nvmet_addr_treq_store(struct config_item *item,
		const char *page, size_t count)
{
	struct nvmet_port *port = to_nvmet_port(item);
	u8 treq = port->disc_addr.treq & ~NVME_TREQ_SECURE_CHANNEL_MASK;

	if (port->enabled) {
		pr_err("Cannot modify address while enabled\n");
		pr_err("Disable the address before modifying\n");
		return -EACCES;
	}

	if (sysfs_streq(page, "not specified")) {
		treq |= NVMF_TREQ_NOT_SPECIFIED;
	} else if (sysfs_streq(page, "required")) {
		treq |= NVMF_TREQ_REQUIRED;
	} else if (sysfs_streq(page, "not required")) {
		treq |= NVMF_TREQ_NOT_REQUIRED;
	} else {
		pr_err("Invalid value '%s' for treq\n", page);
		return -EINVAL;
	}
	port->disc_addr.treq = treq;

	return count;
}

CONFIGFS_ATTR(nvmet_, addr_treq);

static ssize_t nvmet_addr_trsvcid_show(struct config_item *item,
		char *page)
{
	struct nvmet_port *port = to_nvmet_port(item);

	return snprintf(page, PAGE_SIZE, "%s\n",
			port->disc_addr.trsvcid);
}

static ssize_t nvmet_addr_trsvcid_store(struct config_item *item,
		const char *page, size_t count)
{
	struct nvmet_port *port = to_nvmet_port(item);

	if (count > NVMF_TRSVCID_SIZE) {
		pr_err("Invalid value '%s' for trsvcid\n", page);
		return -EINVAL;
	}
	if (port->enabled) {
		pr_err("Cannot modify address while enabled\n");
		pr_err("Disable the address before modifying\n");
		return -EACCES;
	}

	if (sscanf(page, "%s\n", port->disc_addr.trsvcid) != 1)
		return -EINVAL;
	return count;
}

CONFIGFS_ATTR(nvmet_, addr_trsvcid);

static ssize_t nvmet_param_inline_data_size_show(struct config_item *item,
		char *page)
{
	struct nvmet_port *port = to_nvmet_port(item);

	return snprintf(page, PAGE_SIZE, "%d\n", port->inline_data_size);
}

static ssize_t nvmet_param_inline_data_size_store(struct config_item *item,
		const char *page, size_t count)
{
	struct nvmet_port *port = to_nvmet_port(item);
	int ret;

	if (port->enabled) {
		pr_err("Cannot modify inline_data_size while port enabled\n");
		pr_err("Disable the port before modifying\n");
		return -EACCES;
	}
	ret = kstrtoint(page, 0, &port->inline_data_size);
	if (ret) {
		pr_err("Invalid value '%s' for inline_data_size\n", page);
		return -EINVAL;
	}
	return count;
}

CONFIGFS_ATTR(nvmet_, param_inline_data_size);

static ssize_t nvmet_addr_trtype_show(struct config_item *item,
		char *page)
{
	struct nvmet_port *port = to_nvmet_port(item);
	int i;

	for (i = 0; i < ARRAY_SIZE(nvmet_transport_names); i++) {
		if (port->disc_addr.trtype != nvmet_transport_names[i].type)
			continue;
		return sprintf(page, "%s\n", nvmet_transport_names[i].name);
	}

	return sprintf(page, "\n");
}

static void nvmet_port_init_tsas_rdma(struct nvmet_port *port)
{
	port->disc_addr.tsas.rdma.qptype = NVMF_RDMA_QPTYPE_CONNECTED;
	port->disc_addr.tsas.rdma.prtype = NVMF_RDMA_PRTYPE_NOT_SPECIFIED;
	port->disc_addr.tsas.rdma.cms = NVMF_RDMA_CMS_RDMA_CM;
}

static ssize_t nvmet_addr_trtype_store(struct config_item *item,
		const char *page, size_t count)
{
	struct nvmet_port *port = to_nvmet_port(item);
	int i;

	if (port->enabled) {
		pr_err("Cannot modify address while enabled\n");
		pr_err("Disable the address before modifying\n");
		return -EACCES;
	}

	for (i = 0; i < ARRAY_SIZE(nvmet_transport_names); i++) {
		if (sysfs_streq(page, nvmet_transport_names[i].name))
			goto found;
	}

	pr_err("Invalid value '%s' for trtype\n", page);
	return -EINVAL;
found:
	memset(&port->disc_addr.tsas, 0, NVMF_TSAS_SIZE);
	port->disc_addr.trtype = nvmet_transport_names[i].type;
	if (port->disc_addr.trtype == NVMF_TRTYPE_RDMA)
		nvmet_port_init_tsas_rdma(port);
	return count;
}

CONFIGFS_ATTR(nvmet_, addr_trtype);

/*
 * Namespace structures & file operation functions below
 */
static ssize_t nvmet_ns_device_path_show(struct config_item *item, char *page)
{
	return sprintf(page, "%s\n", to_nvmet_ns(item)->device_path);
}

static ssize_t nvmet_ns_device_path_store(struct config_item *item,
		const char *page, size_t count)
{
	struct nvmet_ns *ns = to_nvmet_ns(item);
	struct nvmet_subsys *subsys = ns->subsys;
	size_t len;
	int ret;

	mutex_lock(&subsys->lock);
	ret = -EBUSY;
	if (ns->enabled)
		goto out_unlock;

	ret = -EINVAL;
	len = strcspn(page, "\n");
	if (!len)
		goto out_unlock;

	kfree(ns->device_path);
	ret = -ENOMEM;
	ns->device_path = kstrndup(page, len, GFP_KERNEL);
	if (!ns->device_path)
		goto out_unlock;

	mutex_unlock(&subsys->lock);
	return count;

out_unlock:
	mutex_unlock(&subsys->lock);
	return ret;
}

CONFIGFS_ATTR(nvmet_ns_, device_path);

#ifdef CONFIG_PCI_P2PDMA
static ssize_t nvmet_ns_p2pmem_show(struct config_item *item, char *page)
{
	struct nvmet_ns *ns = to_nvmet_ns(item);

	return pci_p2pdma_enable_show(page, ns->p2p_dev, ns->use_p2pmem);
}

static ssize_t nvmet_ns_p2pmem_store(struct config_item *item,
		const char *page, size_t count)
{
	struct nvmet_ns *ns = to_nvmet_ns(item);
	struct pci_dev *p2p_dev = NULL;
	bool use_p2pmem;
	int ret = count;
	int error;

	mutex_lock(&ns->subsys->lock);
	if (ns->enabled) {
		ret = -EBUSY;
		goto out_unlock;
	}

	error = pci_p2pdma_enable_store(page, &p2p_dev, &use_p2pmem);
	if (error) {
		ret = error;
		goto out_unlock;
	}

	ns->use_p2pmem = use_p2pmem;
	pci_dev_put(ns->p2p_dev);
	ns->p2p_dev = p2p_dev;

out_unlock:
	mutex_unlock(&ns->subsys->lock);

	return ret;
}

CONFIGFS_ATTR(nvmet_ns_, p2pmem);
#endif /* CONFIG_PCI_P2PDMA */

static ssize_t nvmet_ns_device_uuid_show(struct config_item *item, char *page)
{
	return sprintf(page, "%pUb\n", &to_nvmet_ns(item)->uuid);
}

static ssize_t nvmet_ns_device_uuid_store(struct config_item *item,
					  const char *page, size_t count)
{
	struct nvmet_ns *ns = to_nvmet_ns(item);
	struct nvmet_subsys *subsys = ns->subsys;
	int ret = 0;


	mutex_lock(&subsys->lock);
	if (ns->enabled) {
		ret = -EBUSY;
		goto out_unlock;
	}


	if (uuid_parse(page, &ns->uuid))
		ret = -EINVAL;

out_unlock:
	mutex_unlock(&subsys->lock);
	return ret ? ret : count;
}

CONFIGFS_ATTR(nvmet_ns_, device_uuid);

static ssize_t nvmet_ns_device_nguid_show(struct config_item *item, char *page)
{
	return sprintf(page, "%pUb\n", &to_nvmet_ns(item)->nguid);
}

static ssize_t nvmet_ns_device_nguid_store(struct config_item *item,
		const char *page, size_t count)
{
	struct nvmet_ns *ns = to_nvmet_ns(item);
	struct nvmet_subsys *subsys = ns->subsys;
	u8 nguid[16];
	const char *p = page;
	int i;
	int ret = 0;

	mutex_lock(&subsys->lock);
	if (ns->enabled) {
		ret = -EBUSY;
		goto out_unlock;
	}

	for (i = 0; i < 16; i++) {
		if (p + 2 > page + count) {
			ret = -EINVAL;
			goto out_unlock;
		}
		if (!isxdigit(p[0]) || !isxdigit(p[1])) {
			ret = -EINVAL;
			goto out_unlock;
		}

		nguid[i] = (hex_to_bin(p[0]) << 4) | hex_to_bin(p[1]);
		p += 2;

		if (*p == '-' || *p == ':')
			p++;
	}

	memcpy(&ns->nguid, nguid, sizeof(nguid));
out_unlock:
	mutex_unlock(&subsys->lock);
	return ret ? ret : count;
}

CONFIGFS_ATTR(nvmet_ns_, device_nguid);

static ssize_t nvmet_ns_ana_grpid_show(struct config_item *item, char *page)
{
	return sprintf(page, "%u\n", to_nvmet_ns(item)->anagrpid);
}

static ssize_t nvmet_ns_ana_grpid_store(struct config_item *item,
		const char *page, size_t count)
{
	struct nvmet_ns *ns = to_nvmet_ns(item);
	u32 oldgrpid, newgrpid;
	int ret;

	ret = kstrtou32(page, 0, &newgrpid);
	if (ret)
		return ret;

	if (newgrpid < 1 || newgrpid > NVMET_MAX_ANAGRPS)
		return -EINVAL;

	down_write(&nvmet_ana_sem);
	oldgrpid = ns->anagrpid;
	nvmet_ana_group_enabled[newgrpid]++;
	ns->anagrpid = newgrpid;
	nvmet_ana_group_enabled[oldgrpid]--;
	nvmet_ana_chgcnt++;
	up_write(&nvmet_ana_sem);

	nvmet_send_ana_event(ns->subsys, NULL);
	return count;
}

CONFIGFS_ATTR(nvmet_ns_, ana_grpid);

static ssize_t nvmet_ns_enable_show(struct config_item *item, char *page)
{
	return sprintf(page, "%d\n", to_nvmet_ns(item)->enabled);
}

static ssize_t nvmet_ns_enable_store(struct config_item *item,
		const char *page, size_t count)
{
	struct nvmet_ns *ns = to_nvmet_ns(item);
	bool enable;
	int ret = 0;

	if (strtobool(page, &enable))
		return -EINVAL;

	if (enable)
		ret = nvmet_ns_enable(ns);
	else
		nvmet_ns_disable(ns);

	return ret ? ret : count;
}

CONFIGFS_ATTR(nvmet_ns_, enable);

static ssize_t nvmet_ns_buffered_io_show(struct config_item *item, char *page)
{
	return sprintf(page, "%d\n", to_nvmet_ns(item)->buffered_io);
}

static ssize_t nvmet_ns_buffered_io_store(struct config_item *item,
		const char *page, size_t count)
{
	struct nvmet_ns *ns = to_nvmet_ns(item);
	bool val;

	if (strtobool(page, &val))
		return -EINVAL;

	mutex_lock(&ns->subsys->lock);
	if (ns->enabled) {
		pr_err("disable ns before setting buffered_io value.\n");
		mutex_unlock(&ns->subsys->lock);
		return -EINVAL;
	}

	ns->buffered_io = val;
	mutex_unlock(&ns->subsys->lock);
	return count;
}

CONFIGFS_ATTR(nvmet_ns_, buffered_io);

static struct configfs_attribute *nvmet_ns_attrs[] = {
	&nvmet_ns_attr_device_path,
	&nvmet_ns_attr_device_nguid,
	&nvmet_ns_attr_device_uuid,
	&nvmet_ns_attr_ana_grpid,
	&nvmet_ns_attr_enable,
	&nvmet_ns_attr_buffered_io,
#ifdef CONFIG_PCI_P2PDMA
	&nvmet_ns_attr_p2pmem,
#endif
	NULL,
};

static void nvmet_ns_release(struct config_item *item)
{
	struct nvmet_ns *ns = to_nvmet_ns(item);

	nvmet_ns_free(ns);
}

static struct configfs_item_operations nvmet_ns_item_ops = {
	.release		= nvmet_ns_release,
};

static const struct config_item_type nvmet_ns_type = {
	.ct_item_ops		= &nvmet_ns_item_ops,
	.ct_attrs		= nvmet_ns_attrs,
	.ct_owner		= THIS_MODULE,
};

static struct config_group *nvmet_ns_make(struct config_group *group,
		const char *name)
{
	struct nvmet_subsys *subsys = namespaces_to_subsys(&group->cg_item);
	struct nvmet_ns *ns;
	int ret;
	u32 nsid;

	ret = kstrtou32(name, 0, &nsid);
	if (ret)
		goto out;

	ret = -EINVAL;
	if (nsid == 0 || nsid == NVME_NSID_ALL)
		goto out;

	ret = -ENOMEM;
	ns = nvmet_ns_alloc(subsys, nsid);
	if (!ns)
		goto out;
	config_group_init_type_name(&ns->group, name, &nvmet_ns_type);

	pr_info("adding nsid %d to subsystem %s\n", nsid, subsys->subsysnqn);

	return &ns->group;
out:
	return ERR_PTR(ret);
}

static struct configfs_group_operations nvmet_namespaces_group_ops = {
	.make_group		= nvmet_ns_make,
};

static const struct config_item_type nvmet_namespaces_type = {
	.ct_group_ops		= &nvmet_namespaces_group_ops,
	.ct_owner		= THIS_MODULE,
};

static int nvmet_port_subsys_allow_link(struct config_item *parent,
		struct config_item *target)
{
	struct nvmet_port *port = to_nvmet_port(parent->ci_parent);
	struct nvmet_subsys *subsys;
	struct nvmet_subsys_link *link, *p;
	int ret;

	if (target->ci_type != &nvmet_subsys_type) {
		pr_err("can only link subsystems into the subsystems dir.!\n");
		return -EINVAL;
	}
	subsys = to_subsys(target);
	link = kmalloc(sizeof(*link), GFP_KERNEL);
	if (!link)
		return -ENOMEM;
	link->subsys = subsys;

	down_write(&nvmet_config_sem);
	ret = -EEXIST;
	list_for_each_entry(p, &port->subsystems, entry) {
		if (p->subsys == subsys)
			goto out_free_link;
	}

	if (list_empty(&port->subsystems)) {
		ret = nvmet_enable_port(port);
		if (ret)
			goto out_free_link;
	}

	list_add_tail(&link->entry, &port->subsystems);
	nvmet_port_disc_changed(port, subsys);

	up_write(&nvmet_config_sem);
	return 0;

out_free_link:
	up_write(&nvmet_config_sem);
	kfree(link);
	return ret;
}

static void nvmet_port_subsys_drop_link(struct config_item *parent,
		struct config_item *target)
{
	struct nvmet_port *port = to_nvmet_port(parent->ci_parent);
	struct nvmet_subsys *subsys = to_subsys(target);
	struct nvmet_subsys_link *p;

	down_write(&nvmet_config_sem);
	list_for_each_entry(p, &port->subsystems, entry) {
		if (p->subsys == subsys)
			goto found;
	}
	up_write(&nvmet_config_sem);
	return;

found:
	list_del(&p->entry);
	nvmet_port_disc_changed(port, subsys);

	if (list_empty(&port->subsystems))
		nvmet_disable_port(port);
	up_write(&nvmet_config_sem);
	kfree(p);
}

static struct configfs_item_operations nvmet_port_subsys_item_ops = {
	.allow_link		= nvmet_port_subsys_allow_link,
	.drop_link		= nvmet_port_subsys_drop_link,
};

static const struct config_item_type nvmet_port_subsys_type = {
	.ct_item_ops		= &nvmet_port_subsys_item_ops,
	.ct_owner		= THIS_MODULE,
};

static int nvmet_allowed_hosts_allow_link(struct config_item *parent,
		struct config_item *target)
{
	struct nvmet_subsys *subsys = to_subsys(parent->ci_parent);
	struct nvmet_host *host;
	struct nvmet_host_link *link, *p;
	int ret;

	if (target->ci_type != &nvmet_host_type) {
		pr_err("can only link hosts into the allowed_hosts directory!\n");
		return -EINVAL;
	}

	host = to_host(target);
	link = kmalloc(sizeof(*link), GFP_KERNEL);
	if (!link)
		return -ENOMEM;
	link->host = host;

	down_write(&nvmet_config_sem);
	ret = -EINVAL;
	if (subsys->allow_any_host) {
		pr_err("can't add hosts when allow_any_host is set!\n");
		goto out_free_link;
	}

	ret = -EEXIST;
	list_for_each_entry(p, &subsys->hosts, entry) {
		if (!strcmp(nvmet_host_name(p->host), nvmet_host_name(host)))
			goto out_free_link;
	}
	list_add_tail(&link->entry, &subsys->hosts);
	nvmet_subsys_disc_changed(subsys, host);

	up_write(&nvmet_config_sem);
	return 0;
out_free_link:
	up_write(&nvmet_config_sem);
	kfree(link);
	return ret;
}

static void nvmet_allowed_hosts_drop_link(struct config_item *parent,
		struct config_item *target)
{
	struct nvmet_subsys *subsys = to_subsys(parent->ci_parent);
	struct nvmet_host *host = to_host(target);
	struct nvmet_host_link *p;

	down_write(&nvmet_config_sem);
	list_for_each_entry(p, &subsys->hosts, entry) {
		if (!strcmp(nvmet_host_name(p->host), nvmet_host_name(host)))
			goto found;
	}
	up_write(&nvmet_config_sem);
	return;

found:
	list_del(&p->entry);
	nvmet_subsys_disc_changed(subsys, host);

	up_write(&nvmet_config_sem);
	kfree(p);
}

static struct configfs_item_operations nvmet_allowed_hosts_item_ops = {
	.allow_link		= nvmet_allowed_hosts_allow_link,
	.drop_link		= nvmet_allowed_hosts_drop_link,
};

static const struct config_item_type nvmet_allowed_hosts_type = {
	.ct_item_ops		= &nvmet_allowed_hosts_item_ops,
	.ct_owner		= THIS_MODULE,
};

static ssize_t nvmet_subsys_attr_allow_any_host_show(struct config_item *item,
		char *page)
{
	return snprintf(page, PAGE_SIZE, "%d\n",
		to_subsys(item)->allow_any_host);
}

static ssize_t nvmet_subsys_attr_allow_any_host_store(struct config_item *item,
		const char *page, size_t count)
{
	struct nvmet_subsys *subsys = to_subsys(item);
	bool allow_any_host;
	int ret = 0;

	if (strtobool(page, &allow_any_host))
		return -EINVAL;

	down_write(&nvmet_config_sem);
	if (allow_any_host && !list_empty(&subsys->hosts)) {
		pr_err("Can't set allow_any_host when explicit hosts are set!\n");
		ret = -EINVAL;
		goto out_unlock;
	}

	if (subsys->allow_any_host != allow_any_host) {
		subsys->allow_any_host = allow_any_host;
		nvmet_subsys_disc_changed(subsys, NULL);
	}

out_unlock:
	up_write(&nvmet_config_sem);
	return ret ? ret : count;
}

CONFIGFS_ATTR(nvmet_subsys_, attr_allow_any_host);

static ssize_t nvmet_subsys_attr_version_show(struct config_item *item,
					      char *page)
{
	struct nvmet_subsys *subsys = to_subsys(item);

	if (NVME_TERTIARY(subsys->ver))
		return snprintf(page, PAGE_SIZE, "%d.%d.%d\n",
				(int)NVME_MAJOR(subsys->ver),
				(int)NVME_MINOR(subsys->ver),
				(int)NVME_TERTIARY(subsys->ver));
	else
		return snprintf(page, PAGE_SIZE, "%d.%d\n",
				(int)NVME_MAJOR(subsys->ver),
				(int)NVME_MINOR(subsys->ver));
}

static ssize_t nvmet_subsys_attr_version_store(struct config_item *item,
					       const char *page, size_t count)
{
	struct nvmet_subsys *subsys = to_subsys(item);
	int major, minor, tertiary = 0;
	int ret;


	ret = sscanf(page, "%d.%d.%d\n", &major, &minor, &tertiary);
	if (ret != 2 && ret != 3)
		return -EINVAL;

	down_write(&nvmet_config_sem);
	subsys->ver = NVME_VS(major, minor, tertiary);
	up_write(&nvmet_config_sem);

	return count;
}
CONFIGFS_ATTR(nvmet_subsys_, attr_version);

static ssize_t nvmet_subsys_attr_serial_show(struct config_item *item,
					     char *page)
{
	struct nvmet_subsys *subsys = to_subsys(item);

	return snprintf(page, PAGE_SIZE, "%llx\n", subsys->serial);
}

static ssize_t nvmet_subsys_attr_serial_store(struct config_item *item,
					      const char *page, size_t count)
{
	struct nvmet_subsys *subsys = to_subsys(item);

	down_write(&nvmet_config_sem);
	sscanf(page, "%llx\n", &subsys->serial);
	up_write(&nvmet_config_sem);

	return count;
}
CONFIGFS_ATTR(nvmet_subsys_, attr_serial);

static struct configfs_attribute *nvmet_subsys_attrs[] = {
	&nvmet_subsys_attr_attr_allow_any_host,
	&nvmet_subsys_attr_attr_version,
	&nvmet_subsys_attr_attr_serial,
	NULL,
};

/*
 * Subsystem structures & folder operation functions below
 */
static void nvmet_subsys_release(struct config_item *item)
{
	struct nvmet_subsys *subsys = to_subsys(item);

	nvmet_subsys_del_ctrls(subsys);
	nvmet_subsys_put(subsys);
}

static struct configfs_item_operations nvmet_subsys_item_ops = {
	.release		= nvmet_subsys_release,
};

static const struct config_item_type nvmet_subsys_type = {
	.ct_item_ops		= &nvmet_subsys_item_ops,
	.ct_attrs		= nvmet_subsys_attrs,
	.ct_owner		= THIS_MODULE,
};

static struct config_group *nvmet_subsys_make(struct config_group *group,
		const char *name)
{
	struct nvmet_subsys *subsys;

	if (sysfs_streq(name, NVME_DISC_SUBSYS_NAME)) {
		pr_err("can't create discovery subsystem through configfs\n");
		return ERR_PTR(-EINVAL);
	}

	subsys = nvmet_subsys_alloc(name, NVME_NQN_NVME);
	if (IS_ERR(subsys))
		return ERR_CAST(subsys);

	config_group_init_type_name(&subsys->group, name, &nvmet_subsys_type);

	config_group_init_type_name(&subsys->namespaces_group,
			"namespaces", &nvmet_namespaces_type);
	configfs_add_default_group(&subsys->namespaces_group, &subsys->group);

	config_group_init_type_name(&subsys->allowed_hosts_group,
			"allowed_hosts", &nvmet_allowed_hosts_type);
	configfs_add_default_group(&subsys->allowed_hosts_group,
			&subsys->group);

	return &subsys->group;
}

static struct configfs_group_operations nvmet_subsystems_group_ops = {
	.make_group		= nvmet_subsys_make,
};

static const struct config_item_type nvmet_subsystems_type = {
	.ct_group_ops		= &nvmet_subsystems_group_ops,
	.ct_owner		= THIS_MODULE,
};

static ssize_t nvmet_referral_enable_show(struct config_item *item,
		char *page)
{
	return snprintf(page, PAGE_SIZE, "%d\n", to_nvmet_port(item)->enabled);
}

static ssize_t nvmet_referral_enable_store(struct config_item *item,
		const char *page, size_t count)
{
	struct nvmet_port *parent = to_nvmet_port(item->ci_parent->ci_parent);
	struct nvmet_port *port = to_nvmet_port(item);
	bool enable;

	if (strtobool(page, &enable))
		goto inval;

	if (enable)
		nvmet_referral_enable(parent, port);
	else
		nvmet_referral_disable(parent, port);

	return count;
inval:
	pr_err("Invalid value '%s' for enable\n", page);
	return -EINVAL;
}

CONFIGFS_ATTR(nvmet_referral_, enable);

/*
 * Discovery Service subsystem definitions
 */
static struct configfs_attribute *nvmet_referral_attrs[] = {
	&nvmet_attr_addr_adrfam,
	&nvmet_attr_addr_portid,
	&nvmet_attr_addr_treq,
	&nvmet_attr_addr_traddr,
	&nvmet_attr_addr_trsvcid,
	&nvmet_attr_addr_trtype,
	&nvmet_referral_attr_enable,
	NULL,
};

static void nvmet_referral_release(struct config_item *item)
{
	struct nvmet_port *parent = to_nvmet_port(item->ci_parent->ci_parent);
	struct nvmet_port *port = to_nvmet_port(item);

	nvmet_referral_disable(parent, port);
	kfree(port);
}

static struct configfs_item_operations nvmet_referral_item_ops = {
	.release	= nvmet_referral_release,
};

static const struct config_item_type nvmet_referral_type = {
	.ct_owner	= THIS_MODULE,
	.ct_attrs	= nvmet_referral_attrs,
	.ct_item_ops	= &nvmet_referral_item_ops,
};

static struct config_group *nvmet_referral_make(
		struct config_group *group, const char *name)
{
	struct nvmet_port *port;

	port = kzalloc(sizeof(*port), GFP_KERNEL);
	if (!port)
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&port->entry);
	config_group_init_type_name(&port->group, name, &nvmet_referral_type);

	return &port->group;
}

static struct configfs_group_operations nvmet_referral_group_ops = {
	.make_group		= nvmet_referral_make,
};

static const struct config_item_type nvmet_referrals_type = {
	.ct_owner	= THIS_MODULE,
	.ct_group_ops	= &nvmet_referral_group_ops,
};

static struct {
	enum nvme_ana_state	state;
	const char		*name;
} nvmet_ana_state_names[] = {
	{ NVME_ANA_OPTIMIZED,		"optimized" },
	{ NVME_ANA_NONOPTIMIZED,	"non-optimized" },
	{ NVME_ANA_INACCESSIBLE,	"inaccessible" },
	{ NVME_ANA_PERSISTENT_LOSS,	"persistent-loss" },
	{ NVME_ANA_CHANGE,		"change" },
};

static ssize_t nvmet_ana_group_ana_state_show(struct config_item *item,
		char *page)
{
	struct nvmet_ana_group *grp = to_ana_group(item);
	enum nvme_ana_state state = grp->port->ana_state[grp->grpid];
	int i;

	for (i = 0; i < ARRAY_SIZE(nvmet_ana_state_names); i++) {
		if (state != nvmet_ana_state_names[i].state)
			continue;
		return sprintf(page, "%s\n", nvmet_ana_state_names[i].name);
	}

	return sprintf(page, "\n");
}

static ssize_t nvmet_ana_group_ana_state_store(struct config_item *item,
		const char *page, size_t count)
{
	struct nvmet_ana_group *grp = to_ana_group(item);
	int i;

	for (i = 0; i < ARRAY_SIZE(nvmet_ana_state_names); i++) {
		if (sysfs_streq(page, nvmet_ana_state_names[i].name))
			goto found;
	}

	pr_err("Invalid value '%s' for ana_state\n", page);
	return -EINVAL;

found:
	down_write(&nvmet_ana_sem);
	grp->port->ana_state[grp->grpid] = nvmet_ana_state_names[i].state;
	nvmet_ana_chgcnt++;
	up_write(&nvmet_ana_sem);

	nvmet_port_send_ana_event(grp->port);
	return count;
}

CONFIGFS_ATTR(nvmet_ana_group_, ana_state);

static struct configfs_attribute *nvmet_ana_group_attrs[] = {
	&nvmet_ana_group_attr_ana_state,
	NULL,
};

static void nvmet_ana_group_release(struct config_item *item)
{
	struct nvmet_ana_group *grp = to_ana_group(item);

	if (grp == &grp->port->ana_default_group)
		return;

	down_write(&nvmet_ana_sem);
	grp->port->ana_state[grp->grpid] = NVME_ANA_INACCESSIBLE;
	nvmet_ana_group_enabled[grp->grpid]--;
	up_write(&nvmet_ana_sem);

	nvmet_port_send_ana_event(grp->port);
	kfree(grp);
}

static struct configfs_item_operations nvmet_ana_group_item_ops = {
	.release		= nvmet_ana_group_release,
};

static const struct config_item_type nvmet_ana_group_type = {
	.ct_item_ops		= &nvmet_ana_group_item_ops,
	.ct_attrs		= nvmet_ana_group_attrs,
	.ct_owner		= THIS_MODULE,
};

static struct config_group *nvmet_ana_groups_make_group(
		struct config_group *group, const char *name)
{
	struct nvmet_port *port = ana_groups_to_port(&group->cg_item);
	struct nvmet_ana_group *grp;
	u32 grpid;
	int ret;

	ret = kstrtou32(name, 0, &grpid);
	if (ret)
		goto out;

	ret = -EINVAL;
	if (grpid <= 1 || grpid > NVMET_MAX_ANAGRPS)
		goto out;

	ret = -ENOMEM;
	grp = kzalloc(sizeof(*grp), GFP_KERNEL);
	if (!grp)
		goto out;
	grp->port = port;
	grp->grpid = grpid;

	down_write(&nvmet_ana_sem);
	nvmet_ana_group_enabled[grpid]++;
	up_write(&nvmet_ana_sem);

	nvmet_port_send_ana_event(grp->port);

	config_group_init_type_name(&grp->group, name, &nvmet_ana_group_type);
	return &grp->group;
out:
	return ERR_PTR(ret);
}

static struct configfs_group_operations nvmet_ana_groups_group_ops = {
	.make_group		= nvmet_ana_groups_make_group,
};

static const struct config_item_type nvmet_ana_groups_type = {
	.ct_group_ops		= &nvmet_ana_groups_group_ops,
	.ct_owner		= THIS_MODULE,
};

/*
 * Ports definitions.
 */
static void nvmet_port_release(struct config_item *item)
{
	struct nvmet_port *port = to_nvmet_port(item);

	list_del(&port->global_entry);

	kfree(port->ana_state);
	kfree(port);
}

static struct configfs_attribute *nvmet_port_attrs[] = {
	&nvmet_attr_addr_adrfam,
	&nvmet_attr_addr_treq,
	&nvmet_attr_addr_traddr,
	&nvmet_attr_addr_trsvcid,
	&nvmet_attr_addr_trtype,
	&nvmet_attr_param_inline_data_size,
	NULL,
};

static struct configfs_item_operations nvmet_port_item_ops = {
	.release		= nvmet_port_release,
};

static const struct config_item_type nvmet_port_type = {
	.ct_attrs		= nvmet_port_attrs,
	.ct_item_ops		= &nvmet_port_item_ops,
	.ct_owner		= THIS_MODULE,
};

static struct config_group *nvmet_ports_make(struct config_group *group,
		const char *name)
{
	struct nvmet_port *port;
	u16 portid;
	u32 i;

	if (kstrtou16(name, 0, &portid))
		return ERR_PTR(-EINVAL);

	port = kzalloc(sizeof(*port), GFP_KERNEL);
	if (!port)
		return ERR_PTR(-ENOMEM);

	port->ana_state = kcalloc(NVMET_MAX_ANAGRPS + 1,
			sizeof(*port->ana_state), GFP_KERNEL);
	if (!port->ana_state) {
		kfree(port);
		return ERR_PTR(-ENOMEM);
	}

	for (i = 1; i <= NVMET_MAX_ANAGRPS; i++) {
		if (i == NVMET_DEFAULT_ANA_GRPID)
			port->ana_state[1] = NVME_ANA_OPTIMIZED;
		else
			port->ana_state[i] = NVME_ANA_INACCESSIBLE;
	}

	list_add(&port->global_entry, &nvmet_ports_list);

	INIT_LIST_HEAD(&port->entry);
	INIT_LIST_HEAD(&port->subsystems);
	INIT_LIST_HEAD(&port->referrals);
	port->inline_data_size = -1;	/* < 0 == let the transport choose */

	port->disc_addr.portid = cpu_to_le16(portid);
	port->disc_addr.treq = NVMF_TREQ_DISABLE_SQFLOW;
	config_group_init_type_name(&port->group, name, &nvmet_port_type);

	config_group_init_type_name(&port->subsys_group,
			"subsystems", &nvmet_port_subsys_type);
	configfs_add_default_group(&port->subsys_group, &port->group);

	config_group_init_type_name(&port->referrals_group,
			"referrals", &nvmet_referrals_type);
	configfs_add_default_group(&port->referrals_group, &port->group);

	config_group_init_type_name(&port->ana_groups_group,
			"ana_groups", &nvmet_ana_groups_type);
	configfs_add_default_group(&port->ana_groups_group, &port->group);

	port->ana_default_group.port = port;
	port->ana_default_group.grpid = NVMET_DEFAULT_ANA_GRPID;
	config_group_init_type_name(&port->ana_default_group.group,
			__stringify(NVMET_DEFAULT_ANA_GRPID),
			&nvmet_ana_group_type);
	configfs_add_default_group(&port->ana_default_group.group,
			&port->ana_groups_group);

	return &port->group;
}

static struct configfs_group_operations nvmet_ports_group_ops = {
	.make_group		= nvmet_ports_make,
};

static const struct config_item_type nvmet_ports_type = {
	.ct_group_ops		= &nvmet_ports_group_ops,
	.ct_owner		= THIS_MODULE,
};

static struct config_group nvmet_subsystems_group;
static struct config_group nvmet_ports_group;

static void nvmet_host_release(struct config_item *item)
{
	struct nvmet_host *host = to_host(item);

	kfree(host);
}

static struct configfs_item_operations nvmet_host_item_ops = {
	.release		= nvmet_host_release,
};

static const struct config_item_type nvmet_host_type = {
	.ct_item_ops		= &nvmet_host_item_ops,
	.ct_owner		= THIS_MODULE,
};

static struct config_group *nvmet_hosts_make_group(struct config_group *group,
		const char *name)
{
	struct nvmet_host *host;

	host = kzalloc(sizeof(*host), GFP_KERNEL);
	if (!host)
		return ERR_PTR(-ENOMEM);

	config_group_init_type_name(&host->group, name, &nvmet_host_type);

	return &host->group;
}

static struct configfs_group_operations nvmet_hosts_group_ops = {
	.make_group		= nvmet_hosts_make_group,
};

static const struct config_item_type nvmet_hosts_type = {
	.ct_group_ops		= &nvmet_hosts_group_ops,
	.ct_owner		= THIS_MODULE,
};

static struct config_group nvmet_hosts_group;

static const struct config_item_type nvmet_root_type = {
	.ct_owner		= THIS_MODULE,
};

static struct configfs_subsystem nvmet_configfs_subsystem = {
	.su_group = {
		.cg_item = {
			.ci_namebuf	= "nvmet",
			.ci_type	= &nvmet_root_type,
		},
	},
};

int __init nvmet_init_configfs(void)
{
	int ret;

	config_group_init(&nvmet_configfs_subsystem.su_group);
	mutex_init(&nvmet_configfs_subsystem.su_mutex);

	config_group_init_type_name(&nvmet_subsystems_group,
			"subsystems", &nvmet_subsystems_type);
	configfs_add_default_group(&nvmet_subsystems_group,
			&nvmet_configfs_subsystem.su_group);

	config_group_init_type_name(&nvmet_ports_group,
			"ports", &nvmet_ports_type);
	configfs_add_default_group(&nvmet_ports_group,
			&nvmet_configfs_subsystem.su_group);

	config_group_init_type_name(&nvmet_hosts_group,
			"hosts", &nvmet_hosts_type);
	configfs_add_default_group(&nvmet_hosts_group,
			&nvmet_configfs_subsystem.su_group);

	ret = configfs_register_subsystem(&nvmet_configfs_subsystem);
	if (ret) {
		pr_err("configfs_register_subsystem: %d\n", ret);
		return ret;
	}

	return 0;
}

void __exit nvmet_exit_configfs(void)
{
	configfs_unregister_subsystem(&nvmet_configfs_subsystem);
}
