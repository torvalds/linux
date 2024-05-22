// SPDX-License-Identifier: GPL-2.0
/*
 * Configfs interface for the NVMe target.
 * Copyright (c) 2015-2016 HGST, a Western Digital Company.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/kstrtox.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/stat.h>
#include <linux/ctype.h>
#include <linux/pci.h>
#include <linux/pci-p2pdma.h>
#ifdef CONFIG_NVME_TARGET_AUTH
#include <linux/nvme-auth.h>
#endif
#include <linux/nvme-keyring.h>
#include <crypto/hash.h>
#include <crypto/kpp.h>
#include <linux/nospec.h>

#include "nvmet.h"

static const struct config_item_type nvmet_host_type;
static const struct config_item_type nvmet_subsys_type;

static LIST_HEAD(nvmet_ports_list);
struct list_head *nvmet_ports = &nvmet_ports_list;

struct nvmet_type_name_map {
	u8		type;
	const char	*name;
};

static struct nvmet_type_name_map nvmet_transport[] = {
	{ NVMF_TRTYPE_RDMA,	"rdma" },
	{ NVMF_TRTYPE_FC,	"fc" },
	{ NVMF_TRTYPE_TCP,	"tcp" },
	{ NVMF_TRTYPE_LOOP,	"loop" },
};

static const struct nvmet_type_name_map nvmet_addr_family[] = {
	{ NVMF_ADDR_FAMILY_PCI,		"pcie" },
	{ NVMF_ADDR_FAMILY_IP4,		"ipv4" },
	{ NVMF_ADDR_FAMILY_IP6,		"ipv6" },
	{ NVMF_ADDR_FAMILY_IB,		"ib" },
	{ NVMF_ADDR_FAMILY_FC,		"fc" },
	{ NVMF_ADDR_FAMILY_LOOP,	"loop" },
};

static bool nvmet_is_port_enabled(struct nvmet_port *p, const char *caller)
{
	if (p->enabled)
		pr_err("Disable port '%u' before changing attribute in %s\n",
		       le16_to_cpu(p->disc_addr.portid), caller);
	return p->enabled;
}

/*
 * nvmet_port Generic ConfigFS definitions.
 * Used in any place in the ConfigFS tree that refers to an address.
 */
static ssize_t nvmet_addr_adrfam_show(struct config_item *item, char *page)
{
	u8 adrfam = to_nvmet_port(item)->disc_addr.adrfam;
	int i;

	for (i = 1; i < ARRAY_SIZE(nvmet_addr_family); i++) {
		if (nvmet_addr_family[i].type == adrfam)
			return snprintf(page, PAGE_SIZE, "%s\n",
					nvmet_addr_family[i].name);
	}

	return snprintf(page, PAGE_SIZE, "\n");
}

static ssize_t nvmet_addr_adrfam_store(struct config_item *item,
		const char *page, size_t count)
{
	struct nvmet_port *port = to_nvmet_port(item);
	int i;

	if (nvmet_is_port_enabled(port, __func__))
		return -EACCES;

	for (i = 1; i < ARRAY_SIZE(nvmet_addr_family); i++) {
		if (sysfs_streq(page, nvmet_addr_family[i].name))
			goto found;
	}

	pr_err("Invalid value '%s' for adrfam\n", page);
	return -EINVAL;

found:
	port->disc_addr.adrfam = nvmet_addr_family[i].type;
	return count;
}

CONFIGFS_ATTR(nvmet_, addr_adrfam);

static ssize_t nvmet_addr_portid_show(struct config_item *item,
		char *page)
{
	__le16 portid = to_nvmet_port(item)->disc_addr.portid;

	return snprintf(page, PAGE_SIZE, "%d\n", le16_to_cpu(portid));
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

	if (nvmet_is_port_enabled(port, __func__))
		return -EACCES;

	port->disc_addr.portid = cpu_to_le16(portid);
	return count;
}

CONFIGFS_ATTR(nvmet_, addr_portid);

static ssize_t nvmet_addr_traddr_show(struct config_item *item,
		char *page)
{
	struct nvmet_port *port = to_nvmet_port(item);

	return snprintf(page, PAGE_SIZE, "%s\n", port->disc_addr.traddr);
}

static ssize_t nvmet_addr_traddr_store(struct config_item *item,
		const char *page, size_t count)
{
	struct nvmet_port *port = to_nvmet_port(item);

	if (count > NVMF_TRADDR_SIZE) {
		pr_err("Invalid value '%s' for traddr\n", page);
		return -EINVAL;
	}

	if (nvmet_is_port_enabled(port, __func__))
		return -EACCES;

	if (sscanf(page, "%s\n", port->disc_addr.traddr) != 1)
		return -EINVAL;
	return count;
}

CONFIGFS_ATTR(nvmet_, addr_traddr);

static const struct nvmet_type_name_map nvmet_addr_treq[] = {
	{ NVMF_TREQ_NOT_SPECIFIED,	"not specified" },
	{ NVMF_TREQ_REQUIRED,		"required" },
	{ NVMF_TREQ_NOT_REQUIRED,	"not required" },
};

static inline u8 nvmet_port_disc_addr_treq_mask(struct nvmet_port *port)
{
	return (port->disc_addr.treq & ~NVME_TREQ_SECURE_CHANNEL_MASK);
}

static ssize_t nvmet_addr_treq_show(struct config_item *item, char *page)
{
	u8 treq = nvmet_port_disc_addr_treq_secure_channel(to_nvmet_port(item));
	int i;

	for (i = 0; i < ARRAY_SIZE(nvmet_addr_treq); i++) {
		if (treq == nvmet_addr_treq[i].type)
			return snprintf(page, PAGE_SIZE, "%s\n",
					nvmet_addr_treq[i].name);
	}

	return snprintf(page, PAGE_SIZE, "\n");
}

static ssize_t nvmet_addr_treq_store(struct config_item *item,
		const char *page, size_t count)
{
	struct nvmet_port *port = to_nvmet_port(item);
	u8 treq = nvmet_port_disc_addr_treq_mask(port);
	int i;

	if (nvmet_is_port_enabled(port, __func__))
		return -EACCES;

	for (i = 0; i < ARRAY_SIZE(nvmet_addr_treq); i++) {
		if (sysfs_streq(page, nvmet_addr_treq[i].name))
			goto found;
	}

	pr_err("Invalid value '%s' for treq\n", page);
	return -EINVAL;

found:
	if (port->disc_addr.trtype == NVMF_TRTYPE_TCP &&
	    port->disc_addr.tsas.tcp.sectype == NVMF_TCP_SECTYPE_TLS13) {
		switch (nvmet_addr_treq[i].type) {
		case NVMF_TREQ_NOT_SPECIFIED:
			pr_debug("treq '%s' not allowed for TLS1.3\n",
				 nvmet_addr_treq[i].name);
			return -EINVAL;
		case NVMF_TREQ_NOT_REQUIRED:
			pr_warn("Allow non-TLS connections while TLS1.3 is enabled\n");
			break;
		default:
			break;
		}
	}
	treq |= nvmet_addr_treq[i].type;
	port->disc_addr.treq = treq;
	return count;
}

CONFIGFS_ATTR(nvmet_, addr_treq);

static ssize_t nvmet_addr_trsvcid_show(struct config_item *item,
		char *page)
{
	struct nvmet_port *port = to_nvmet_port(item);

	return snprintf(page, PAGE_SIZE, "%s\n", port->disc_addr.trsvcid);
}

static ssize_t nvmet_addr_trsvcid_store(struct config_item *item,
		const char *page, size_t count)
{
	struct nvmet_port *port = to_nvmet_port(item);

	if (count > NVMF_TRSVCID_SIZE) {
		pr_err("Invalid value '%s' for trsvcid\n", page);
		return -EINVAL;
	}
	if (nvmet_is_port_enabled(port, __func__))
		return -EACCES;

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

	if (nvmet_is_port_enabled(port, __func__))
		return -EACCES;
	ret = kstrtoint(page, 0, &port->inline_data_size);
	if (ret) {
		pr_err("Invalid value '%s' for inline_data_size\n", page);
		return -EINVAL;
	}
	return count;
}

CONFIGFS_ATTR(nvmet_, param_inline_data_size);

static ssize_t nvmet_param_max_queue_size_show(struct config_item *item,
		char *page)
{
	struct nvmet_port *port = to_nvmet_port(item);

	return snprintf(page, PAGE_SIZE, "%d\n", port->max_queue_size);
}

static ssize_t nvmet_param_max_queue_size_store(struct config_item *item,
		const char *page, size_t count)
{
	struct nvmet_port *port = to_nvmet_port(item);
	int ret;

	if (nvmet_is_port_enabled(port, __func__))
		return -EACCES;
	ret = kstrtoint(page, 0, &port->max_queue_size);
	if (ret) {
		pr_err("Invalid value '%s' for max_queue_size\n", page);
		return -EINVAL;
	}
	return count;
}

CONFIGFS_ATTR(nvmet_, param_max_queue_size);

#ifdef CONFIG_BLK_DEV_INTEGRITY
static ssize_t nvmet_param_pi_enable_show(struct config_item *item,
		char *page)
{
	struct nvmet_port *port = to_nvmet_port(item);

	return snprintf(page, PAGE_SIZE, "%d\n", port->pi_enable);
}

static ssize_t nvmet_param_pi_enable_store(struct config_item *item,
		const char *page, size_t count)
{
	struct nvmet_port *port = to_nvmet_port(item);
	bool val;

	if (kstrtobool(page, &val))
		return -EINVAL;

	if (nvmet_is_port_enabled(port, __func__))
		return -EACCES;

	port->pi_enable = val;
	return count;
}

CONFIGFS_ATTR(nvmet_, param_pi_enable);
#endif

static ssize_t nvmet_addr_trtype_show(struct config_item *item,
		char *page)
{
	struct nvmet_port *port = to_nvmet_port(item);
	int i;

	for (i = 0; i < ARRAY_SIZE(nvmet_transport); i++) {
		if (port->disc_addr.trtype == nvmet_transport[i].type)
			return snprintf(page, PAGE_SIZE,
					"%s\n", nvmet_transport[i].name);
	}

	return sprintf(page, "\n");
}

static void nvmet_port_init_tsas_rdma(struct nvmet_port *port)
{
	port->disc_addr.tsas.rdma.qptype = NVMF_RDMA_QPTYPE_CONNECTED;
	port->disc_addr.tsas.rdma.prtype = NVMF_RDMA_PRTYPE_NOT_SPECIFIED;
	port->disc_addr.tsas.rdma.cms = NVMF_RDMA_CMS_RDMA_CM;
}

static void nvmet_port_init_tsas_tcp(struct nvmet_port *port, int sectype)
{
	port->disc_addr.tsas.tcp.sectype = sectype;
}

static ssize_t nvmet_addr_trtype_store(struct config_item *item,
		const char *page, size_t count)
{
	struct nvmet_port *port = to_nvmet_port(item);
	int i;

	if (nvmet_is_port_enabled(port, __func__))
		return -EACCES;

	for (i = 0; i < ARRAY_SIZE(nvmet_transport); i++) {
		if (sysfs_streq(page, nvmet_transport[i].name))
			goto found;
	}

	pr_err("Invalid value '%s' for trtype\n", page);
	return -EINVAL;

found:
	memset(&port->disc_addr.tsas, 0, NVMF_TSAS_SIZE);
	port->disc_addr.trtype = nvmet_transport[i].type;
	if (port->disc_addr.trtype == NVMF_TRTYPE_RDMA)
		nvmet_port_init_tsas_rdma(port);
	else if (port->disc_addr.trtype == NVMF_TRTYPE_TCP)
		nvmet_port_init_tsas_tcp(port, NVMF_TCP_SECTYPE_NONE);
	return count;
}

CONFIGFS_ATTR(nvmet_, addr_trtype);

static const struct nvmet_type_name_map nvmet_addr_tsas_tcp[] = {
	{ NVMF_TCP_SECTYPE_NONE,	"none" },
	{ NVMF_TCP_SECTYPE_TLS13,	"tls1.3" },
};

static const struct nvmet_type_name_map nvmet_addr_tsas_rdma[] = {
	{ NVMF_RDMA_QPTYPE_CONNECTED,	"connected" },
	{ NVMF_RDMA_QPTYPE_DATAGRAM,	"datagram"  },
};

static ssize_t nvmet_addr_tsas_show(struct config_item *item,
		char *page)
{
	struct nvmet_port *port = to_nvmet_port(item);
	int i;

	if (port->disc_addr.trtype == NVMF_TRTYPE_TCP) {
		for (i = 0; i < ARRAY_SIZE(nvmet_addr_tsas_tcp); i++) {
			if (port->disc_addr.tsas.tcp.sectype == nvmet_addr_tsas_tcp[i].type)
				return sprintf(page, "%s\n", nvmet_addr_tsas_tcp[i].name);
		}
	} else if (port->disc_addr.trtype == NVMF_TRTYPE_RDMA) {
		for (i = 0; i < ARRAY_SIZE(nvmet_addr_tsas_rdma); i++) {
			if (port->disc_addr.tsas.rdma.qptype == nvmet_addr_tsas_rdma[i].type)
				return sprintf(page, "%s\n", nvmet_addr_tsas_rdma[i].name);
		}
	}
	return sprintf(page, "reserved\n");
}

static ssize_t nvmet_addr_tsas_store(struct config_item *item,
		const char *page, size_t count)
{
	struct nvmet_port *port = to_nvmet_port(item);
	u8 treq = nvmet_port_disc_addr_treq_mask(port);
	u8 sectype;
	int i;

	if (nvmet_is_port_enabled(port, __func__))
		return -EACCES;

	if (port->disc_addr.trtype != NVMF_TRTYPE_TCP)
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(nvmet_addr_tsas_tcp); i++) {
		if (sysfs_streq(page, nvmet_addr_tsas_tcp[i].name)) {
			sectype = nvmet_addr_tsas_tcp[i].type;
			goto found;
		}
	}

	pr_err("Invalid value '%s' for tsas\n", page);
	return -EINVAL;

found:
	if (sectype == NVMF_TCP_SECTYPE_TLS13) {
		if (!IS_ENABLED(CONFIG_NVME_TARGET_TCP_TLS)) {
			pr_err("TLS is not supported\n");
			return -EINVAL;
		}
		if (!port->keyring) {
			pr_err("TLS keyring not configured\n");
			return -EINVAL;
		}
	}

	nvmet_port_init_tsas_tcp(port, sectype);
	/*
	 * If TLS is enabled TREQ should be set to 'required' per default
	 */
	if (sectype == NVMF_TCP_SECTYPE_TLS13) {
		u8 sc = nvmet_port_disc_addr_treq_secure_channel(port);

		if (sc == NVMF_TREQ_NOT_SPECIFIED)
			treq |= NVMF_TREQ_REQUIRED;
		else
			treq |= sc;
	} else {
		treq |= NVMF_TREQ_NOT_SPECIFIED;
	}
	port->disc_addr.treq = treq;
	return count;
}

CONFIGFS_ATTR(nvmet_, addr_tsas);

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
	ns->device_path = kmemdup_nul(page, len, GFP_KERNEL);
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
	newgrpid = array_index_nospec(newgrpid, NVMET_MAX_ANAGRPS);
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

	if (kstrtobool(page, &enable))
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

	if (kstrtobool(page, &val))
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

static ssize_t nvmet_ns_revalidate_size_store(struct config_item *item,
		const char *page, size_t count)
{
	struct nvmet_ns *ns = to_nvmet_ns(item);
	bool val;

	if (kstrtobool(page, &val))
		return -EINVAL;

	if (!val)
		return -EINVAL;

	mutex_lock(&ns->subsys->lock);
	if (!ns->enabled) {
		pr_err("enable ns before revalidate.\n");
		mutex_unlock(&ns->subsys->lock);
		return -EINVAL;
	}
	if (nvmet_ns_revalidate(ns))
		nvmet_ns_changed(ns->subsys, ns->nsid);
	mutex_unlock(&ns->subsys->lock);
	return count;
}

CONFIGFS_ATTR_WO(nvmet_ns_, revalidate_size);

static struct configfs_attribute *nvmet_ns_attrs[] = {
	&nvmet_ns_attr_device_path,
	&nvmet_ns_attr_device_nguid,
	&nvmet_ns_attr_device_uuid,
	&nvmet_ns_attr_ana_grpid,
	&nvmet_ns_attr_enable,
	&nvmet_ns_attr_buffered_io,
	&nvmet_ns_attr_revalidate_size,
#ifdef CONFIG_PCI_P2PDMA
	&nvmet_ns_attr_p2pmem,
#endif
	NULL,
};

bool nvmet_subsys_nsid_exists(struct nvmet_subsys *subsys, u32 nsid)
{
	struct config_item *ns_item;
	char name[12];

	snprintf(name, sizeof(name), "%u", nsid);
	mutex_lock(&subsys->namespaces_group.cg_subsys->su_mutex);
	ns_item = config_group_find_item(&subsys->namespaces_group, name);
	mutex_unlock(&subsys->namespaces_group.cg_subsys->su_mutex);
	return ns_item != NULL;
}

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
	if (nsid == 0 || nsid == NVME_NSID_ALL) {
		pr_err("invalid nsid %#x", nsid);
		goto out;
	}

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

#ifdef CONFIG_NVME_TARGET_PASSTHRU

static ssize_t nvmet_passthru_device_path_show(struct config_item *item,
		char *page)
{
	struct nvmet_subsys *subsys = to_subsys(item->ci_parent);

	return snprintf(page, PAGE_SIZE, "%s\n", subsys->passthru_ctrl_path);
}

static ssize_t nvmet_passthru_device_path_store(struct config_item *item,
		const char *page, size_t count)
{
	struct nvmet_subsys *subsys = to_subsys(item->ci_parent);
	size_t len;
	int ret;

	mutex_lock(&subsys->lock);

	ret = -EBUSY;
	if (subsys->passthru_ctrl)
		goto out_unlock;

	ret = -EINVAL;
	len = strcspn(page, "\n");
	if (!len)
		goto out_unlock;

	kfree(subsys->passthru_ctrl_path);
	ret = -ENOMEM;
	subsys->passthru_ctrl_path = kstrndup(page, len, GFP_KERNEL);
	if (!subsys->passthru_ctrl_path)
		goto out_unlock;

	mutex_unlock(&subsys->lock);

	return count;
out_unlock:
	mutex_unlock(&subsys->lock);
	return ret;
}
CONFIGFS_ATTR(nvmet_passthru_, device_path);

static ssize_t nvmet_passthru_enable_show(struct config_item *item,
		char *page)
{
	struct nvmet_subsys *subsys = to_subsys(item->ci_parent);

	return sprintf(page, "%d\n", subsys->passthru_ctrl ? 1 : 0);
}

static ssize_t nvmet_passthru_enable_store(struct config_item *item,
		const char *page, size_t count)
{
	struct nvmet_subsys *subsys = to_subsys(item->ci_parent);
	bool enable;
	int ret = 0;

	if (kstrtobool(page, &enable))
		return -EINVAL;

	if (enable)
		ret = nvmet_passthru_ctrl_enable(subsys);
	else
		nvmet_passthru_ctrl_disable(subsys);

	return ret ? ret : count;
}
CONFIGFS_ATTR(nvmet_passthru_, enable);

static ssize_t nvmet_passthru_admin_timeout_show(struct config_item *item,
		char *page)
{
	return sprintf(page, "%u\n", to_subsys(item->ci_parent)->admin_timeout);
}

static ssize_t nvmet_passthru_admin_timeout_store(struct config_item *item,
		const char *page, size_t count)
{
	struct nvmet_subsys *subsys = to_subsys(item->ci_parent);
	unsigned int timeout;

	if (kstrtouint(page, 0, &timeout))
		return -EINVAL;
	subsys->admin_timeout = timeout;
	return count;
}
CONFIGFS_ATTR(nvmet_passthru_, admin_timeout);

static ssize_t nvmet_passthru_io_timeout_show(struct config_item *item,
		char *page)
{
	return sprintf(page, "%u\n", to_subsys(item->ci_parent)->io_timeout);
}

static ssize_t nvmet_passthru_io_timeout_store(struct config_item *item,
		const char *page, size_t count)
{
	struct nvmet_subsys *subsys = to_subsys(item->ci_parent);
	unsigned int timeout;

	if (kstrtouint(page, 0, &timeout))
		return -EINVAL;
	subsys->io_timeout = timeout;
	return count;
}
CONFIGFS_ATTR(nvmet_passthru_, io_timeout);

static ssize_t nvmet_passthru_clear_ids_show(struct config_item *item,
		char *page)
{
	return sprintf(page, "%u\n", to_subsys(item->ci_parent)->clear_ids);
}

static ssize_t nvmet_passthru_clear_ids_store(struct config_item *item,
		const char *page, size_t count)
{
	struct nvmet_subsys *subsys = to_subsys(item->ci_parent);
	unsigned int clear_ids;

	if (kstrtouint(page, 0, &clear_ids))
		return -EINVAL;
	subsys->clear_ids = clear_ids;
	return count;
}
CONFIGFS_ATTR(nvmet_passthru_, clear_ids);

static struct configfs_attribute *nvmet_passthru_attrs[] = {
	&nvmet_passthru_attr_device_path,
	&nvmet_passthru_attr_enable,
	&nvmet_passthru_attr_admin_timeout,
	&nvmet_passthru_attr_io_timeout,
	&nvmet_passthru_attr_clear_ids,
	NULL,
};

static const struct config_item_type nvmet_passthru_type = {
	.ct_attrs		= nvmet_passthru_attrs,
	.ct_owner		= THIS_MODULE,
};

static void nvmet_add_passthru_group(struct nvmet_subsys *subsys)
{
	config_group_init_type_name(&subsys->passthru_group,
				    "passthru", &nvmet_passthru_type);
	configfs_add_default_group(&subsys->passthru_group,
				   &subsys->group);
}

#else /* CONFIG_NVME_TARGET_PASSTHRU */

static void nvmet_add_passthru_group(struct nvmet_subsys *subsys)
{
}

#endif /* CONFIG_NVME_TARGET_PASSTHRU */

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
	nvmet_port_del_ctrls(port, subsys);
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

	if (kstrtobool(page, &allow_any_host))
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
		return snprintf(page, PAGE_SIZE, "%llu.%llu.%llu\n",
				NVME_MAJOR(subsys->ver),
				NVME_MINOR(subsys->ver),
				NVME_TERTIARY(subsys->ver));

	return snprintf(page, PAGE_SIZE, "%llu.%llu\n",
			NVME_MAJOR(subsys->ver),
			NVME_MINOR(subsys->ver));
}

static ssize_t
nvmet_subsys_attr_version_store_locked(struct nvmet_subsys *subsys,
		const char *page, size_t count)
{
	int major, minor, tertiary = 0;
	int ret;

	if (subsys->subsys_discovered) {
		if (NVME_TERTIARY(subsys->ver))
			pr_err("Can't set version number. %llu.%llu.%llu is already assigned\n",
			       NVME_MAJOR(subsys->ver),
			       NVME_MINOR(subsys->ver),
			       NVME_TERTIARY(subsys->ver));
		else
			pr_err("Can't set version number. %llu.%llu is already assigned\n",
			       NVME_MAJOR(subsys->ver),
			       NVME_MINOR(subsys->ver));
		return -EINVAL;
	}

	/* passthru subsystems use the underlying controller's version */
	if (nvmet_is_passthru_subsys(subsys))
		return -EINVAL;

	ret = sscanf(page, "%d.%d.%d\n", &major, &minor, &tertiary);
	if (ret != 2 && ret != 3)
		return -EINVAL;

	subsys->ver = NVME_VS(major, minor, tertiary);

	return count;
}

static ssize_t nvmet_subsys_attr_version_store(struct config_item *item,
					       const char *page, size_t count)
{
	struct nvmet_subsys *subsys = to_subsys(item);
	ssize_t ret;

	down_write(&nvmet_config_sem);
	mutex_lock(&subsys->lock);
	ret = nvmet_subsys_attr_version_store_locked(subsys, page, count);
	mutex_unlock(&subsys->lock);
	up_write(&nvmet_config_sem);

	return ret;
}
CONFIGFS_ATTR(nvmet_subsys_, attr_version);

/* See Section 1.5 of NVMe 1.4 */
static bool nvmet_is_ascii(const char c)
{
	return c >= 0x20 && c <= 0x7e;
}

static ssize_t nvmet_subsys_attr_serial_show(struct config_item *item,
					     char *page)
{
	struct nvmet_subsys *subsys = to_subsys(item);

	return snprintf(page, PAGE_SIZE, "%.*s\n",
			NVMET_SN_MAX_SIZE, subsys->serial);
}

static ssize_t
nvmet_subsys_attr_serial_store_locked(struct nvmet_subsys *subsys,
		const char *page, size_t count)
{
	int pos, len = strcspn(page, "\n");

	if (subsys->subsys_discovered) {
		pr_err("Can't set serial number. %s is already assigned\n",
		       subsys->serial);
		return -EINVAL;
	}

	if (!len || len > NVMET_SN_MAX_SIZE) {
		pr_err("Serial Number can not be empty or exceed %d Bytes\n",
		       NVMET_SN_MAX_SIZE);
		return -EINVAL;
	}

	for (pos = 0; pos < len; pos++) {
		if (!nvmet_is_ascii(page[pos])) {
			pr_err("Serial Number must contain only ASCII strings\n");
			return -EINVAL;
		}
	}

	memcpy_and_pad(subsys->serial, NVMET_SN_MAX_SIZE, page, len, ' ');

	return count;
}

static ssize_t nvmet_subsys_attr_serial_store(struct config_item *item,
					      const char *page, size_t count)
{
	struct nvmet_subsys *subsys = to_subsys(item);
	ssize_t ret;

	down_write(&nvmet_config_sem);
	mutex_lock(&subsys->lock);
	ret = nvmet_subsys_attr_serial_store_locked(subsys, page, count);
	mutex_unlock(&subsys->lock);
	up_write(&nvmet_config_sem);

	return ret;
}
CONFIGFS_ATTR(nvmet_subsys_, attr_serial);

static ssize_t nvmet_subsys_attr_cntlid_min_show(struct config_item *item,
						 char *page)
{
	return snprintf(page, PAGE_SIZE, "%u\n", to_subsys(item)->cntlid_min);
}

static ssize_t nvmet_subsys_attr_cntlid_min_store(struct config_item *item,
						  const char *page, size_t cnt)
{
	u16 cntlid_min;

	if (sscanf(page, "%hu\n", &cntlid_min) != 1)
		return -EINVAL;

	if (cntlid_min == 0)
		return -EINVAL;

	down_write(&nvmet_config_sem);
	if (cntlid_min > to_subsys(item)->cntlid_max)
		goto out_unlock;
	to_subsys(item)->cntlid_min = cntlid_min;
	up_write(&nvmet_config_sem);
	return cnt;

out_unlock:
	up_write(&nvmet_config_sem);
	return -EINVAL;
}
CONFIGFS_ATTR(nvmet_subsys_, attr_cntlid_min);

static ssize_t nvmet_subsys_attr_cntlid_max_show(struct config_item *item,
						 char *page)
{
	return snprintf(page, PAGE_SIZE, "%u\n", to_subsys(item)->cntlid_max);
}

static ssize_t nvmet_subsys_attr_cntlid_max_store(struct config_item *item,
						  const char *page, size_t cnt)
{
	u16 cntlid_max;

	if (sscanf(page, "%hu\n", &cntlid_max) != 1)
		return -EINVAL;

	if (cntlid_max == 0)
		return -EINVAL;

	down_write(&nvmet_config_sem);
	if (cntlid_max < to_subsys(item)->cntlid_min)
		goto out_unlock;
	to_subsys(item)->cntlid_max = cntlid_max;
	up_write(&nvmet_config_sem);
	return cnt;

out_unlock:
	up_write(&nvmet_config_sem);
	return -EINVAL;
}
CONFIGFS_ATTR(nvmet_subsys_, attr_cntlid_max);

static ssize_t nvmet_subsys_attr_model_show(struct config_item *item,
					    char *page)
{
	struct nvmet_subsys *subsys = to_subsys(item);

	return snprintf(page, PAGE_SIZE, "%s\n", subsys->model_number);
}

static ssize_t nvmet_subsys_attr_model_store_locked(struct nvmet_subsys *subsys,
		const char *page, size_t count)
{
	int pos = 0, len;
	char *val;

	if (subsys->subsys_discovered) {
		pr_err("Can't set model number. %s is already assigned\n",
		       subsys->model_number);
		return -EINVAL;
	}

	len = strcspn(page, "\n");
	if (!len)
		return -EINVAL;

	if (len > NVMET_MN_MAX_SIZE) {
		pr_err("Model number size can not exceed %d Bytes\n",
		       NVMET_MN_MAX_SIZE);
		return -EINVAL;
	}

	for (pos = 0; pos < len; pos++) {
		if (!nvmet_is_ascii(page[pos]))
			return -EINVAL;
	}

	val = kmemdup_nul(page, len, GFP_KERNEL);
	if (!val)
		return -ENOMEM;
	kfree(subsys->model_number);
	subsys->model_number = val;
	return count;
}

static ssize_t nvmet_subsys_attr_model_store(struct config_item *item,
					     const char *page, size_t count)
{
	struct nvmet_subsys *subsys = to_subsys(item);
	ssize_t ret;

	down_write(&nvmet_config_sem);
	mutex_lock(&subsys->lock);
	ret = nvmet_subsys_attr_model_store_locked(subsys, page, count);
	mutex_unlock(&subsys->lock);
	up_write(&nvmet_config_sem);

	return ret;
}
CONFIGFS_ATTR(nvmet_subsys_, attr_model);

static ssize_t nvmet_subsys_attr_ieee_oui_show(struct config_item *item,
					    char *page)
{
	struct nvmet_subsys *subsys = to_subsys(item);

	return sysfs_emit(page, "0x%06x\n", subsys->ieee_oui);
}

static ssize_t nvmet_subsys_attr_ieee_oui_store_locked(struct nvmet_subsys *subsys,
		const char *page, size_t count)
{
	uint32_t val = 0;
	int ret;

	if (subsys->subsys_discovered) {
		pr_err("Can't set IEEE OUI. 0x%06x is already assigned\n",
		      subsys->ieee_oui);
		return -EINVAL;
	}

	ret = kstrtou32(page, 0, &val);
	if (ret < 0)
		return ret;

	if (val >= 0x1000000)
		return -EINVAL;

	subsys->ieee_oui = val;

	return count;
}

static ssize_t nvmet_subsys_attr_ieee_oui_store(struct config_item *item,
					     const char *page, size_t count)
{
	struct nvmet_subsys *subsys = to_subsys(item);
	ssize_t ret;

	down_write(&nvmet_config_sem);
	mutex_lock(&subsys->lock);
	ret = nvmet_subsys_attr_ieee_oui_store_locked(subsys, page, count);
	mutex_unlock(&subsys->lock);
	up_write(&nvmet_config_sem);

	return ret;
}
CONFIGFS_ATTR(nvmet_subsys_, attr_ieee_oui);

static ssize_t nvmet_subsys_attr_firmware_show(struct config_item *item,
					    char *page)
{
	struct nvmet_subsys *subsys = to_subsys(item);

	return sysfs_emit(page, "%s\n", subsys->firmware_rev);
}

static ssize_t nvmet_subsys_attr_firmware_store_locked(struct nvmet_subsys *subsys,
		const char *page, size_t count)
{
	int pos = 0, len;
	char *val;

	if (subsys->subsys_discovered) {
		pr_err("Can't set firmware revision. %s is already assigned\n",
		       subsys->firmware_rev);
		return -EINVAL;
	}

	len = strcspn(page, "\n");
	if (!len)
		return -EINVAL;

	if (len > NVMET_FR_MAX_SIZE) {
		pr_err("Firmware revision size can not exceed %d Bytes\n",
		       NVMET_FR_MAX_SIZE);
		return -EINVAL;
	}

	for (pos = 0; pos < len; pos++) {
		if (!nvmet_is_ascii(page[pos]))
			return -EINVAL;
	}

	val = kmemdup_nul(page, len, GFP_KERNEL);
	if (!val)
		return -ENOMEM;

	kfree(subsys->firmware_rev);

	subsys->firmware_rev = val;

	return count;
}

static ssize_t nvmet_subsys_attr_firmware_store(struct config_item *item,
					     const char *page, size_t count)
{
	struct nvmet_subsys *subsys = to_subsys(item);
	ssize_t ret;

	down_write(&nvmet_config_sem);
	mutex_lock(&subsys->lock);
	ret = nvmet_subsys_attr_firmware_store_locked(subsys, page, count);
	mutex_unlock(&subsys->lock);
	up_write(&nvmet_config_sem);

	return ret;
}
CONFIGFS_ATTR(nvmet_subsys_, attr_firmware);

#ifdef CONFIG_BLK_DEV_INTEGRITY
static ssize_t nvmet_subsys_attr_pi_enable_show(struct config_item *item,
						char *page)
{
	return snprintf(page, PAGE_SIZE, "%d\n", to_subsys(item)->pi_support);
}

static ssize_t nvmet_subsys_attr_pi_enable_store(struct config_item *item,
						 const char *page, size_t count)
{
	struct nvmet_subsys *subsys = to_subsys(item);
	bool pi_enable;

	if (kstrtobool(page, &pi_enable))
		return -EINVAL;

	subsys->pi_support = pi_enable;
	return count;
}
CONFIGFS_ATTR(nvmet_subsys_, attr_pi_enable);
#endif

static ssize_t nvmet_subsys_attr_qid_max_show(struct config_item *item,
					      char *page)
{
	return snprintf(page, PAGE_SIZE, "%u\n", to_subsys(item)->max_qid);
}

static ssize_t nvmet_subsys_attr_qid_max_store(struct config_item *item,
					       const char *page, size_t cnt)
{
	struct nvmet_subsys *subsys = to_subsys(item);
	struct nvmet_ctrl *ctrl;
	u16 qid_max;

	if (sscanf(page, "%hu\n", &qid_max) != 1)
		return -EINVAL;

	if (qid_max < 1 || qid_max > NVMET_NR_QUEUES)
		return -EINVAL;

	down_write(&nvmet_config_sem);
	subsys->max_qid = qid_max;

	/* Force reconnect */
	list_for_each_entry(ctrl, &subsys->ctrls, subsys_entry)
		ctrl->ops->delete_ctrl(ctrl);
	up_write(&nvmet_config_sem);

	return cnt;
}
CONFIGFS_ATTR(nvmet_subsys_, attr_qid_max);

static struct configfs_attribute *nvmet_subsys_attrs[] = {
	&nvmet_subsys_attr_attr_allow_any_host,
	&nvmet_subsys_attr_attr_version,
	&nvmet_subsys_attr_attr_serial,
	&nvmet_subsys_attr_attr_cntlid_min,
	&nvmet_subsys_attr_attr_cntlid_max,
	&nvmet_subsys_attr_attr_model,
	&nvmet_subsys_attr_attr_qid_max,
	&nvmet_subsys_attr_attr_ieee_oui,
	&nvmet_subsys_attr_attr_firmware,
#ifdef CONFIG_BLK_DEV_INTEGRITY
	&nvmet_subsys_attr_attr_pi_enable,
#endif
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

	if (sysfs_streq(name, nvmet_disc_subsys->subsysnqn)) {
		pr_err("can't create subsystem using unique discovery NQN\n");
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

	nvmet_add_passthru_group(subsys);

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

	if (kstrtobool(page, &enable))
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

static void nvmet_referral_notify(struct config_group *group,
		struct config_item *item)
{
	struct nvmet_port *parent = to_nvmet_port(item->ci_parent->ci_parent);
	struct nvmet_port *port = to_nvmet_port(item);

	nvmet_referral_disable(parent, port);
}

static void nvmet_referral_release(struct config_item *item)
{
	struct nvmet_port *port = to_nvmet_port(item);

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
	.disconnect_notify	= nvmet_referral_notify,
};

static const struct config_item_type nvmet_referrals_type = {
	.ct_owner	= THIS_MODULE,
	.ct_group_ops	= &nvmet_referral_group_ops,
};

static struct nvmet_type_name_map nvmet_ana_state[] = {
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

	for (i = 0; i < ARRAY_SIZE(nvmet_ana_state); i++) {
		if (state == nvmet_ana_state[i].type)
			return sprintf(page, "%s\n", nvmet_ana_state[i].name);
	}

	return sprintf(page, "\n");
}

static ssize_t nvmet_ana_group_ana_state_store(struct config_item *item,
		const char *page, size_t count)
{
	struct nvmet_ana_group *grp = to_ana_group(item);
	enum nvme_ana_state *ana_state = grp->port->ana_state;
	int i;

	for (i = 0; i < ARRAY_SIZE(nvmet_ana_state); i++) {
		if (sysfs_streq(page, nvmet_ana_state[i].name))
			goto found;
	}

	pr_err("Invalid value '%s' for ana_state\n", page);
	return -EINVAL;

found:
	down_write(&nvmet_ana_sem);
	ana_state[grp->grpid] = (enum nvme_ana_state) nvmet_ana_state[i].type;
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
	grpid = array_index_nospec(grpid, NVMET_MAX_ANAGRPS);
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

	/* Let inflight controllers teardown complete */
	flush_workqueue(nvmet_wq);
	list_del(&port->global_entry);

	key_put(port->keyring);
	kfree(port->ana_state);
	kfree(port);
}

static struct configfs_attribute *nvmet_port_attrs[] = {
	&nvmet_attr_addr_adrfam,
	&nvmet_attr_addr_treq,
	&nvmet_attr_addr_traddr,
	&nvmet_attr_addr_trsvcid,
	&nvmet_attr_addr_trtype,
	&nvmet_attr_addr_tsas,
	&nvmet_attr_param_inline_data_size,
	&nvmet_attr_param_max_queue_size,
#ifdef CONFIG_BLK_DEV_INTEGRITY
	&nvmet_attr_param_pi_enable,
#endif
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

	if (IS_ENABLED(CONFIG_NVME_TARGET_TCP_TLS) && nvme_keyring_id()) {
		port->keyring = key_lookup(nvme_keyring_id());
		if (IS_ERR(port->keyring)) {
			pr_warn("NVMe keyring not available, disabling TLS\n");
			port->keyring = NULL;
		}
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
	port->max_queue_size = -1;	/* < 0 == let the transport choose */

	port->disc_addr.portid = cpu_to_le16(portid);
	port->disc_addr.adrfam = NVMF_ADDR_FAMILY_MAX;
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

#ifdef CONFIG_NVME_TARGET_AUTH
static ssize_t nvmet_host_dhchap_key_show(struct config_item *item,
		char *page)
{
	u8 *dhchap_secret = to_host(item)->dhchap_secret;

	if (!dhchap_secret)
		return sprintf(page, "\n");
	return sprintf(page, "%s\n", dhchap_secret);
}

static ssize_t nvmet_host_dhchap_key_store(struct config_item *item,
		const char *page, size_t count)
{
	struct nvmet_host *host = to_host(item);
	int ret;

	ret = nvmet_auth_set_key(host, page, false);
	/*
	 * Re-authentication is a soft state, so keep the
	 * current authentication valid until the host
	 * requests re-authentication.
	 */
	return ret < 0 ? ret : count;
}

CONFIGFS_ATTR(nvmet_host_, dhchap_key);

static ssize_t nvmet_host_dhchap_ctrl_key_show(struct config_item *item,
		char *page)
{
	u8 *dhchap_secret = to_host(item)->dhchap_ctrl_secret;

	if (!dhchap_secret)
		return sprintf(page, "\n");
	return sprintf(page, "%s\n", dhchap_secret);
}

static ssize_t nvmet_host_dhchap_ctrl_key_store(struct config_item *item,
		const char *page, size_t count)
{
	struct nvmet_host *host = to_host(item);
	int ret;

	ret = nvmet_auth_set_key(host, page, true);
	/*
	 * Re-authentication is a soft state, so keep the
	 * current authentication valid until the host
	 * requests re-authentication.
	 */
	return ret < 0 ? ret : count;
}

CONFIGFS_ATTR(nvmet_host_, dhchap_ctrl_key);

static ssize_t nvmet_host_dhchap_hash_show(struct config_item *item,
		char *page)
{
	struct nvmet_host *host = to_host(item);
	const char *hash_name = nvme_auth_hmac_name(host->dhchap_hash_id);

	return sprintf(page, "%s\n", hash_name ? hash_name : "none");
}

static ssize_t nvmet_host_dhchap_hash_store(struct config_item *item,
		const char *page, size_t count)
{
	struct nvmet_host *host = to_host(item);
	u8 hmac_id;

	hmac_id = nvme_auth_hmac_id(page);
	if (hmac_id == NVME_AUTH_HASH_INVALID)
		return -EINVAL;
	if (!crypto_has_shash(nvme_auth_hmac_name(hmac_id), 0, 0))
		return -ENOTSUPP;
	host->dhchap_hash_id = hmac_id;
	return count;
}

CONFIGFS_ATTR(nvmet_host_, dhchap_hash);

static ssize_t nvmet_host_dhchap_dhgroup_show(struct config_item *item,
		char *page)
{
	struct nvmet_host *host = to_host(item);
	const char *dhgroup = nvme_auth_dhgroup_name(host->dhchap_dhgroup_id);

	return sprintf(page, "%s\n", dhgroup ? dhgroup : "none");
}

static ssize_t nvmet_host_dhchap_dhgroup_store(struct config_item *item,
		const char *page, size_t count)
{
	struct nvmet_host *host = to_host(item);
	int dhgroup_id;

	dhgroup_id = nvme_auth_dhgroup_id(page);
	if (dhgroup_id == NVME_AUTH_DHGROUP_INVALID)
		return -EINVAL;
	if (dhgroup_id != NVME_AUTH_DHGROUP_NULL) {
		const char *kpp = nvme_auth_dhgroup_kpp(dhgroup_id);

		if (!crypto_has_kpp(kpp, 0, 0))
			return -EINVAL;
	}
	host->dhchap_dhgroup_id = dhgroup_id;
	return count;
}

CONFIGFS_ATTR(nvmet_host_, dhchap_dhgroup);

static struct configfs_attribute *nvmet_host_attrs[] = {
	&nvmet_host_attr_dhchap_key,
	&nvmet_host_attr_dhchap_ctrl_key,
	&nvmet_host_attr_dhchap_hash,
	&nvmet_host_attr_dhchap_dhgroup,
	NULL,
};
#endif /* CONFIG_NVME_TARGET_AUTH */

static void nvmet_host_release(struct config_item *item)
{
	struct nvmet_host *host = to_host(item);

#ifdef CONFIG_NVME_TARGET_AUTH
	kfree(host->dhchap_secret);
	kfree(host->dhchap_ctrl_secret);
#endif
	kfree(host);
}

static struct configfs_item_operations nvmet_host_item_ops = {
	.release		= nvmet_host_release,
};

static const struct config_item_type nvmet_host_type = {
	.ct_item_ops		= &nvmet_host_item_ops,
#ifdef CONFIG_NVME_TARGET_AUTH
	.ct_attrs		= nvmet_host_attrs,
#endif
	.ct_owner		= THIS_MODULE,
};

static struct config_group *nvmet_hosts_make_group(struct config_group *group,
		const char *name)
{
	struct nvmet_host *host;

	host = kzalloc(sizeof(*host), GFP_KERNEL);
	if (!host)
		return ERR_PTR(-ENOMEM);

#ifdef CONFIG_NVME_TARGET_AUTH
	/* Default to SHA256 */
	host->dhchap_hash_id = NVME_AUTH_HASH_SHA256;
#endif

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

static ssize_t nvmet_root_discovery_nqn_show(struct config_item *item,
					     char *page)
{
	return snprintf(page, PAGE_SIZE, "%s\n", nvmet_disc_subsys->subsysnqn);
}

static ssize_t nvmet_root_discovery_nqn_store(struct config_item *item,
		const char *page, size_t count)
{
	struct list_head *entry;
	size_t len;

	len = strcspn(page, "\n");
	if (!len || len > NVMF_NQN_FIELD_LEN - 1)
		return -EINVAL;

	down_write(&nvmet_config_sem);
	list_for_each(entry, &nvmet_subsystems_group.cg_children) {
		struct config_item *item =
			container_of(entry, struct config_item, ci_entry);

		if (!strncmp(config_item_name(item), page, len)) {
			pr_err("duplicate NQN %s\n", config_item_name(item));
			up_write(&nvmet_config_sem);
			return -EINVAL;
		}
	}
	memset(nvmet_disc_subsys->subsysnqn, 0, NVMF_NQN_FIELD_LEN);
	memcpy(nvmet_disc_subsys->subsysnqn, page, len);
	up_write(&nvmet_config_sem);

	return len;
}

CONFIGFS_ATTR(nvmet_root_, discovery_nqn);

static struct configfs_attribute *nvmet_root_attrs[] = {
	&nvmet_root_attr_discovery_nqn,
	NULL,
};

static const struct config_item_type nvmet_root_type = {
	.ct_attrs		= nvmet_root_attrs,
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
