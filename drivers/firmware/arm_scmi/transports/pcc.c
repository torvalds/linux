// SPDX-License-Identifier: GPL-2.0
/*
 * System Control and Management Interface (SCMI) Message ACPI PCC
 * Transport Driver
 *
 * This transport uses ACPI PCC (PCCT Type 3/4) subspaces via the Linux
 * PCC mailbox controller to exchange SCMI messages over the standard
 * SCMI Shared Memory Transport (SMT) layout.
 *
 * PCC subspace selection is conveyed via ACPI SCMI namespace device.
 * described in ACPI for System Control and Management Interface Platform
 * Design Document[1]
 *
 * [1] https://support.arm.com/documentation/111115/latest
 *
 * Copyright (C) 2026
 */

#include <linux/acpi.h>
#include <linux/cleanup.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/hashtable.h>
#include <linux/io.h>
#include <linux/limits.h>
#include <linux/list.h>
#include <linux/mailbox_client.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <acpi/pcc.h>

#include "../common.h"

#define SCMI_TRANSPORT_PACKAGE_MAX_VERSION	(1)
#define SCMI_PROTOCOL_PACKAGE_MAX_VERSION	(1)
#define SCMI_TRANSPORT_SHARED_CHANNEL		BIT_ULL(0)
#define SCMI_TRANSPORT_P2A_CHANNEL		BIT_ULL(1)
#define SCMI_TRANSPORT_FLAGS_MASK	(SCMI_TRANSPORT_SHARED_CHANNEL | \
					 SCMI_TRANSPORT_P2A_CHANNEL)

#define SCMI_DSD_GUID_ENTRY_COUNT	2
#define SCMI_PROTOCOL_MAX_TRANSPORTS	2

enum scmi_dsd_property_index {
	SCMI_DSD_PROPERTY_NAME,
	SCMI_DSD_PROPERTY_VALUE,
	SCMI_DSD_PROPERTY_COUNT,
};

enum scmi_pcc_transport_index {
	SCMI_PCC_TRANSPORT_REVISION,
	SCMI_PCC_TRANSPORT_COUNT,
	SCMI_PCC_TRANSPORT_DESCRIPTORS,
};

enum scmi_pcc_descriptor_index {
	SCMI_PCC_DESCRIPTOR_PCCT_INDEX,
	SCMI_PCC_DESCRIPTOR_TRANSPORT_UID,
	SCMI_PCC_DESCRIPTOR_FLAGS,
	SCMI_PCC_DESCRIPTOR_COUNT,
};

enum scmi_protocol_property_index {
	SCMI_PROTOCOL_PROPERTY_REVISION,
	SCMI_PROTOCOL_PROPERTY_TRANSPORTS,
	SCMI_PROTOCOL_PROPERTY_ASSOCIATIONS,
	SCMI_PROTOCOL_PROPERTY_COUNT,
};

enum scmi_protocol_transport_index {
	SCMI_PROTOCOL_TRANSPORT_UID,
	SCMI_PROTOCOL_TRANSPORT_FLAGS,
	SCMI_PROTOCOL_TRANSPORT_COUNT,
};

/*
 * SCMI specification requires all parameters, message headers, return
 * arguments or any protocol data to be expressed in little endian
 * format only.
 */
struct pcc_shared_mem {
	struct acpi_pcct_ext_pcc_shared_memory header;
	u8 msg_payload[];
};

#define SCMI_PCC_MSG_HEADER_SIZE	sizeof(u32)
#define SCMI_PCC_STATUS_SIZE		sizeof(u32)
#define SCMI_PCC_RESPONSE_OVERHEAD	(SCMI_PCC_MSG_HEADER_SIZE + \
					 SCMI_PCC_STATUS_SIZE)
#define SCMI_PCC_SHMEM_OVERHEAD		(sizeof(struct pcc_shared_mem) + \
					 SCMI_PCC_STATUS_SIZE)

/**
 * struct scmi_pcc - Structure representing a SCMI mailbox transport
 *
 * @cl: Mailbox Client
 * @pchan: Transmit/Receive PCC/mailbox channel
 * @cinfo: SCMI channel info
 */
struct scmi_pcc {
	struct mbox_client cl;
	struct pcc_mbox_chan *pchan;
	struct scmi_chan_info *cinfo;
};

struct pcc_transport {
	u64 uid;
	u64 flags;
	u32 pcc_ss_id;
	u32 protocol_id;
	struct hlist_node hnode;
};

struct pcc_transport_map {
	struct fwnode_handle *fwnode;
	struct device *dev;
	struct list_head node;
	DECLARE_HASHTABLE(table, ilog2(MAX_PCC_SUBSPACES / 8));
};

#define client_to_scmi_pcc(c) container_of(c, struct scmi_pcc, cl)

static struct scmi_transport_core_operations *core;
static LIST_HEAD(pcc_transport_maps);
static DEFINE_MUTEX(pcc_transport_maps_lock);

static void acpi_scmi_clear_transport_map(struct pcc_transport_map *map)
{
	struct pcc_transport *p;
	struct hlist_node *tmp;
	int idx;

	hash_for_each_safe(map->table, idx, tmp, p, hnode) {
		hash_del(&p->hnode);
		kfree(p);
	}
}

static void acpi_scmi_destroy_transport_map(struct pcc_transport_map *map)
{
	acpi_scmi_clear_transport_map(map);
	if (map->fwnode)
		fwnode_handle_put(map->fwnode);
	kfree(map);
}

static void pcc_transport_map_release(void *data)
{
	struct pcc_transport_map *map = data;

	guard(mutex)(&pcc_transport_maps_lock);
	list_del(&map->node);
	acpi_scmi_destroy_transport_map(map);
}

static void acpi_scmi_clear_transport_maps(void)
{
	struct pcc_transport_map *map, *tmp;

	guard(mutex)(&pcc_transport_maps_lock);
	list_for_each_entry_safe(map, tmp, &pcc_transport_maps, node) {
		list_del(&map->node);
		acpi_scmi_destroy_transport_map(map);
	}
}

static const union acpi_object *
acpi_scmi_pkg_elements(const union acpi_object *obj, unsigned int count)
{
	if (obj->type != ACPI_TYPE_PACKAGE || obj->package.count != count)
		return NULL;

	return obj->package.elements;
}

static int
acpi_scmi_pkg_u64(const union acpi_object *pkg, unsigned int idx, u64 *val)
{
	const union acpi_object *elem = &pkg->package.elements[idx];

	if (elem->type != ACPI_TYPE_INTEGER)
		return -EINVAL;

	*val = elem->integer.value;

	return 0;
}

static int acpi_scmi_property(const union acpi_object *properties,
			      unsigned int idx, const char **name,
			      const union acpi_object **value)
{
	const union acpi_object *property;
	const union acpi_object *elems;

	property = &properties->package.elements[idx];
	elems = acpi_scmi_pkg_elements(property, SCMI_DSD_PROPERTY_COUNT);
	if (!elems)
		return -EINVAL;
	if (elems[SCMI_DSD_PROPERTY_NAME].type != ACPI_TYPE_STRING ||
	    !elems[SCMI_DSD_PROPERTY_NAME].string.pointer)
		return -EINVAL;

	*name = elems[SCMI_DSD_PROPERTY_NAME].string.pointer;
	*value = &elems[SCMI_DSD_PROPERTY_VALUE];

	return 0;
}

static int
acpi_scmi_dsd_parse_transport_package(struct pcc_transport_map *map,
				      const union acpi_object *obj)
{
	u64 revision, pkg_cnt;
	bool common_a2p = false, common_p2a = false;

	if (obj->type != ACPI_TYPE_PACKAGE ||
	    obj->package.count < SCMI_PCC_TRANSPORT_DESCRIPTORS ||
	    acpi_scmi_pkg_u64(obj, SCMI_PCC_TRANSPORT_REVISION, &revision) ||
	    acpi_scmi_pkg_u64(obj, SCMI_PCC_TRANSPORT_COUNT, &pkg_cnt))
		return -EINVAL;
	if (revision != SCMI_TRANSPORT_PACKAGE_MAX_VERSION)
		return -EINVAL;
	if (pkg_cnt != obj->package.count - SCMI_PCC_TRANSPORT_DESCRIPTORS)
		return -EINVAL;

	for (u32 idx = 0; idx < pkg_cnt; idx++) {
		struct pcc_transport *p, *tmp;
		union acpi_object *pack;
		u64 pcc_ss_id, uid, flags;

		pack = &obj->package.elements[idx +
					      SCMI_PCC_TRANSPORT_DESCRIPTORS];
		if (!acpi_scmi_pkg_elements(pack, SCMI_PCC_DESCRIPTOR_COUNT)) {
			pr_info("Invalid transport properties pkg %u\n", idx);
			return -EINVAL;
		}
		if (acpi_scmi_pkg_u64(pack, SCMI_PCC_DESCRIPTOR_PCCT_INDEX,
				      &pcc_ss_id) ||
		    acpi_scmi_pkg_u64(pack, SCMI_PCC_DESCRIPTOR_TRANSPORT_UID,
				      &uid) ||
		    acpi_scmi_pkg_u64(pack, SCMI_PCC_DESCRIPTOR_FLAGS, &flags))
			return -EINVAL;
		if (pcc_ss_id > INT_MAX ||
		    flags & ~SCMI_TRANSPORT_FLAGS_MASK)
			return -EINVAL;

		hash_for_each_possible(map->table, tmp, hnode, uid) {
			if (tmp->uid == uid) {
				pr_info("Duplicate UID %llu\n", uid);
				return -EEXIST;
			}
		}

		if (flags & SCMI_TRANSPORT_SHARED_CHANNEL) {
			if (flags & SCMI_TRANSPORT_P2A_CHANNEL) {
				if (common_p2a)
					return -EINVAL;
				common_p2a = true;
			} else {
				if (common_a2p)
					return -EINVAL;
				common_a2p = true;
			}
		}

		p = kzalloc(sizeof(*p), GFP_KERNEL);
		if (!p)
			return -ENOMEM;

		p->uid = uid;
		p->pcc_ss_id = pcc_ss_id;
		p->flags = flags;
		if (p->flags & SCMI_TRANSPORT_SHARED_CHANNEL)
			p->protocol_id = SCMI_PROTOCOL_BASE;

		hash_add(map->table, &p->hnode, uid);
	}

	if (!common_a2p)
		return -EINVAL;

	return 0;
}

static int
acpi_scmi_dsd_parse_protocol_subpackage(struct pcc_transport_map *map,
					const union acpi_object *obj,
					int prot_id)
{
	struct pcc_transport *p;
	unsigned int pkg_cnt = obj->package.count;
	bool found, tx_found = false, rx_found = false;
	u64 uid;

	if (pkg_cnt > SCMI_PROTOCOL_MAX_TRANSPORTS) {
		pr_warn("Found %u protocol channels, at most %u allowed\n",
			pkg_cnt, SCMI_PROTOCOL_MAX_TRANSPORTS);
		return -EINVAL;
	}

	for (u32 idx = 0; idx < pkg_cnt; idx++) {
		union acpi_object *pack = &obj->package.elements[idx];
		u64 flags;

		if (!acpi_scmi_pkg_elements(pack, SCMI_PROTOCOL_TRANSPORT_COUNT) ||
		    acpi_scmi_pkg_u64(pack, SCMI_PROTOCOL_TRANSPORT_UID, &uid) ||
		    acpi_scmi_pkg_u64(pack, SCMI_PROTOCOL_TRANSPORT_FLAGS,
				      &flags))
			return -EINVAL;
		if (flags)
			return -EINVAL;

		found = false;
		hash_for_each_possible(map->table, p, hnode, uid) {
			if (p->uid != uid)
				continue;

			found = true;
			if (p->flags & SCMI_TRANSPORT_SHARED_CHANNEL) {
				pr_info("Protocol package references common PCC subspace %u\n",
					p->pcc_ss_id);
				return -EINVAL;
			}
			if (p->protocol_id && p->protocol_id != prot_id)
				return -EINVAL;

			if (p->flags & SCMI_TRANSPORT_P2A_CHANNEL) {
				if (rx_found)
					return -EINVAL;
				rx_found = true;
			} else {
				if (tx_found)
					return -EINVAL;
				tx_found = true;
			}
			p->protocol_id = prot_id;
			break;
		}

		if (!found)
			return -ENOENT;
	}

	return 0;
}

static int
acpi_scmi_dsd_parse_protocol_package(struct pcc_transport_map *map,
				     const union acpi_object *obj, int prot_id)
{
	const union acpi_object *elems;
	const union acpi_object *pack;
	u64 revision;
	int ret;

	elems = acpi_scmi_pkg_elements(obj, SCMI_PROTOCOL_PROPERTY_COUNT);
	if (!elems ||
	    acpi_scmi_pkg_u64(obj, SCMI_PROTOCOL_PROPERTY_REVISION, &revision))
		return -EINVAL;

	pack = &elems[SCMI_PROTOCOL_PROPERTY_TRANSPORTS];

	if (revision != SCMI_PROTOCOL_PACKAGE_MAX_VERSION)
		return -EINVAL;

	if (pack->type != ACPI_TYPE_PACKAGE) {
		pr_info("Invalid protocol transport package\n");
		return -EINVAL;
	}

	/*
	 * Document 111115A v1.0, Sections 2.2.2.1 and 2.2.3 permit an empty
	 * protocol transport package when common transport channels exist.
	 */
	if (pack->package.count != 0) {
		ret = acpi_scmi_dsd_parse_protocol_subpackage(map, pack, prot_id);
		if (ret)
			return ret;
	}

	pack = &elems[SCMI_PROTOCOL_PROPERTY_ASSOCIATIONS];
	if (pack->type != ACPI_TYPE_PACKAGE) {
		pr_info("Invalid protocol transport association package\n");
		return -EINVAL;
	}

	if (pack->package.count != 0) {
		pr_info("Non-empty association package not supported\n");
		return -EINVAL;
	}

	return 0;
}

/* ACPI SCMI _DSD UUID: "84a2d1c6-86b6-4199-8dac-9c17449d5e03" */
static const guid_t acpi_scmi_uuid = GUID_INIT(0x84a2d1c6, 0x86b6, 0x4199,
					       0x8d, 0xac, 0x9c, 0x17,
					       0x44, 0x9d, 0x5e, 0x03);

static int acpi_scmi_lookup_protocol_id(const char *name)
{
	for (size_t i = 0; i < ARRAY_SIZE(scmi_dsd_info_list); i++) {
		if (!strcmp(name, scmi_dsd_info_list[i].property_name))
			return scmi_dsd_info_list[i].protocol_id;
	}
	return -ENOENT;
}

static int acpi_scmi_parse_properties(struct pcc_transport_map *map,
				      const union acpi_object *properties)
{
	bool transport_found = false;

	if (properties->type != ACPI_TYPE_PACKAGE)
		return -EINVAL;

	/*
	 * Protocol properties reference TransportUIDs declared by the transport
	 * property. Parse the transport first because _DSD property ordering is
	 * not defined.
	 */
	for (u32 i = 0; i < properties->package.count; i++) {
		const union acpi_object *v;
		const char *name;
		int prot_id, ret;

		ret = acpi_scmi_property(properties, i, &name, &v);
		if (ret)
			return ret;

		prot_id = acpi_scmi_lookup_protocol_id(name);
		if (prot_id < 0)
			continue;
		if (prot_id != SCMI_PROTOCOL_BASE)
			continue;
		if (v->type != ACPI_TYPE_PACKAGE)
			return -EINVAL;
		if (transport_found)
			return -EEXIST;

		ret = acpi_scmi_dsd_parse_transport_package(map, v);
		if (ret)
			return ret;
		transport_found = true;
	}

	if (!transport_found)
		return -ENOENT;

	for (u32 i = 0; i < properties->package.count; i++) {
		const union acpi_object *v;
		const char *name;
		int prot_id, ret;

		ret = acpi_scmi_property(properties, i, &name, &v);
		if (ret)
			return ret;

		prot_id = acpi_scmi_lookup_protocol_id(name);
		if (prot_id < 0 || prot_id == SCMI_PROTOCOL_BASE)
			continue;
		if (v->type != ACPI_TYPE_PACKAGE)
			return -EINVAL;

		ret = acpi_scmi_dsd_parse_protocol_package(map, v, prot_id);
		if (ret)
			return ret;
	}

	return 0;
}

static int acpi_scmi_namespace_fwnode_parse(struct fwnode_handle *fwnode,
					    struct pcc_transport_map *map)
{
	struct acpi_buffer buf = { ACPI_ALLOCATE_BUFFER, NULL };
	struct acpi_device *adev = to_acpi_device_node(fwnode);
	union acpi_object *desc;
	acpi_status status;
	int ret = -ENOENT;

	if (!adev || !adev->handle)
		return -EINVAL;

	status = acpi_evaluate_object_typed(adev->handle, "_DSD", NULL, &buf,
					    ACPI_TYPE_PACKAGE);
	if (ACPI_FAILURE(status))
		return -EINVAL;

	desc = buf.pointer;
	if (desc->package.count % SCMI_DSD_GUID_ENTRY_COUNT) {
		ret = -EINVAL;
		goto out_free;
	}

	/* Look for the device properties GUID. */
	for (u32 i = 0; i < desc->package.count;
	     i += SCMI_DSD_GUID_ENTRY_COUNT) {
		const union acpi_object *guid;
		const union acpi_object *properties;

		guid = &desc->package.elements[i];
		properties = &desc->package.elements[i + 1];

		/*
		 * The first element must be a GUID and the second one must be
		 * a package.
		 */
		if (guid->type != ACPI_TYPE_BUFFER ||
		    guid->buffer.length != UUID_SIZE ||
		    properties->type != ACPI_TYPE_PACKAGE)
			continue;

		if (!guid_equal((guid_t *)guid->buffer.pointer,
				&acpi_scmi_uuid))
			continue;

		ret = acpi_scmi_parse_properties(map, properties);
		break;
	}

out_free:
	ACPI_FREE(buf.pointer);
	return ret;
}

static bool
pcc_transport_map_has_ss_id(struct pcc_transport_map *map, u32 pcc_ss_id)
{
	struct pcc_transport *p;
	int idx;

	hash_for_each(map->table, idx, p, hnode) {
		if (p->pcc_ss_id == pcc_ss_id)
			return true;
	}

	return false;
}

static int pcc_transport_map_validate(struct pcc_transport_map *map)
{
	struct pcc_transport_map *iter;
	struct pcc_transport *p;
	int idx;

	list_for_each_entry(iter, &pcc_transport_maps, node) {
		hash_for_each(map->table, idx, p, hnode) {
			if (pcc_transport_map_has_ss_id(iter, p->pcc_ss_id))
				return -EEXIST;
		}
	}

	return 0;
}

static
struct pcc_transport_map *pcc_transport_map_find(struct fwnode_handle *fwnode)
{
	struct pcc_transport_map *map;

	list_for_each_entry(map, &pcc_transport_maps, node) {
		if (map->fwnode == fwnode)
			return map;
	}

	return NULL;
}

static
struct pcc_transport_map *pcc_transport_map_get(struct fwnode_handle *fwnode)
{
	struct pcc_transport_map *map;
	int ret;

	map = pcc_transport_map_find(fwnode);
	if (map)
		return map;

	map = kzalloc_obj(*map, GFP_KERNEL);
	if (!map)
		return ERR_PTR(-ENOMEM);

	hash_init(map->table);
	ret = acpi_scmi_namespace_fwnode_parse(fwnode, map);
	if (ret)
		goto err_free_map;

	ret = pcc_transport_map_validate(map);
	if (ret)
		goto err_free_map;

	map->fwnode = fwnode_handle_get(fwnode);
	list_add_tail(&map->node, &pcc_transport_maps);

	return map;

err_free_map:
	acpi_scmi_clear_transport_map(map);
	kfree(map);
	return ERR_PTR(ret);
}

static int pcc_transport_map_manage(struct pcc_transport_map *map,
				    struct device *dev)
{
	int ret;

	if (!dev || map->dev == dev)
		return 0;
	if (map->dev)
		return -EBUSY;

	/*
	 * Release the cached map after the SCMI remove path tears down channels.
	 */
	ret = devm_add_action(dev, pcc_transport_map_release, map);
	if (ret)
		return ret;

	map->dev = dev;

	return 0;
}

static int pcc_lookup_ss_id(struct pcc_transport_map *map, u32 prot_id, bool tx)
{
	struct pcc_transport *p;
	int idx;

	hash_for_each(map->table, idx, p, hnode) {
		if (p->protocol_id != prot_id)
			continue;

		if ((!tx && (p->flags & SCMI_TRANSPORT_P2A_CHANNEL)) ||
		    (tx && !(p->flags & SCMI_TRANSPORT_P2A_CHANNEL)))
			return p->pcc_ss_id;
	}

	return -ENOENT;
}

static int pcc_get_ss_id(struct fwnode_handle *fwnode, u32 prot_id, bool tx,
			 struct device *dev)
{
	struct pcc_transport_map *map;
	int ret;

	if (!fwnode)
		return -EINVAL;

	guard(mutex)(&pcc_transport_maps_lock);
	map = pcc_transport_map_get(fwnode);
	if (IS_ERR(map))
		return PTR_ERR(map);

	ret = pcc_transport_map_manage(map, dev);
	if (ret)
		return ret;

	return pcc_lookup_ss_id(map, prot_id, tx);
}

static bool
pcc_chan_available(struct fwnode_handle *fwnode, int prot_id, int idx)
{
	return pcc_get_ss_id(fwnode, prot_id, !idx, NULL) >= 0;
}

static void tx_prepare(struct mbox_client *cl, void *m)
{
	struct scmi_pcc *smbox = client_to_scmi_pcc(cl);
	struct pcc_shared_mem __iomem *shmem = smbox->pchan->shmem;
	struct scmi_xfer *xfer = m;

	/*
	 * PCC take cares not to call tx_prepare until last transmit is done.
	 * Request platform interrupt notification if available.
	 */
	if (xfer->tx.buf)
		memcpy_toio(shmem->msg_payload, xfer->tx.buf, xfer->tx.len);
	iowrite32(SCMI_PCC_MSG_HEADER_SIZE + xfer->tx.len,
		  &shmem->header.length);
	iowrite32(pack_scmi_header(&xfer->hdr), &shmem->header.command);
	iowrite32(smbox->pchan->mchan->mbox->txdone_irq ?
		  PCC_CMD_COMPLETION_NOTIFY : 0, &shmem->header.flags);
}

static void rx_callback(struct mbox_client *cl, void *m)
{
	struct scmi_pcc *smbox = client_to_scmi_pcc(cl);
	struct pcc_shared_mem __iomem *shmem = smbox->pchan->shmem;

	core->rx_callback(smbox->cinfo, ioread32(&shmem->header.command), NULL);
}

static int pcc_chan_validate_shmem(struct scmi_chan_info *cinfo,
				   struct scmi_pcc *smbox, int ss_id)
{
	struct pcc_shared_mem __iomem *shmem = smbox->pchan->shmem;
	u32 valid_signature = ss_id + PCC_SIGNATURE;

	if (smbox->pchan->shmem_size < SCMI_PCC_SHMEM_OVERHEAD ||
	    smbox->pchan->shmem_size - SCMI_PCC_SHMEM_OVERHEAD <
	    cinfo->max_msg_size) {
		dev_err(cinfo->dev, "misconfigured SCMI PCC shared memory\n");
		return -ENOSPC;
	}

	if (ioread32(&shmem->header.signature) != valid_signature) {
		dev_err(cinfo->dev, "invalid PCC shared memory signature\n");
		return -EINVAL;
	}

	return 0;
}

static int pcc_chan_setup(struct scmi_chan_info *cinfo, struct device *dev,
			  bool tx)
{
	const char *desc = tx ? "Tx" : "Rx";
	struct device *cdev = cinfo->dev;
	struct scmi_pcc *smbox;
	struct mbox_client *cl;
	int ret, ss_id;

	smbox = devm_kzalloc(dev, sizeof(*smbox), GFP_KERNEL);
	if (!smbox)
		return -ENOMEM;

	cl = &smbox->cl;
	cl->dev = cdev;
	cl->tx_prepare = tx ? tx_prepare : NULL;
	cl->rx_callback = rx_callback;
	cl->tx_block = false;

	ss_id = pcc_get_ss_id(dev_fwnode(cinfo->dev), cinfo->id, tx, dev);
	if (ss_id < 0)
		return ss_id;

	smbox->pchan = pcc_mbox_request_channel(cl, ss_id);
	if (IS_ERR(smbox->pchan)) {
		ret = PTR_ERR(smbox->pchan);
		if (ret != -EPROBE_DEFER)
			dev_err(cdev,
				"failed to request SCMI %s mailbox\n", desc);
		return ret;
	}

	ret = pcc_chan_validate_shmem(cinfo, smbox, ss_id);
	if (ret) {
		pcc_mbox_free_channel(smbox->pchan);
		return ret;
	}

	cinfo->transport_info = smbox;
	smbox->cinfo = cinfo;

	return 0;
}

static int pcc_chan_free(int id, void *p, void *data)
{
	struct scmi_chan_info *cinfo = p;
	struct scmi_pcc *smbox = cinfo->transport_info;

	if (!smbox || IS_ERR_OR_NULL(smbox->pchan))
		return 0;

	pcc_mbox_free_channel(smbox->pchan);
	cinfo->transport_info = NULL;
	smbox->pchan = NULL;
	smbox->cinfo = NULL;

	return 0;
}

static int
pcc_send_message(struct scmi_chan_info *cinfo, struct scmi_xfer *xfer)
{
	struct scmi_pcc *smbox = cinfo->transport_info;
	int ret;

	/*
	 * The mailbox layer has its own queue. However the mailbox queue
	 * confuses the per message SCMI timeouts since the clock starts when
	 * the message is submitted into the mailbox queue. So when multiple
	 * messages are queued up the clock starts on all messages instead of
	 * only the one inflight.
	 */
	ret = mbox_send_message(smbox->pchan->mchan, xfer);
	/* mbox_send_message returns non-negative value on success */
	if (ret < 0)
		return ret;

	return 0;
}

static void pcc_fetch_response(struct scmi_chan_info *cinfo,
			       struct scmi_xfer *xfer)
{
	struct scmi_pcc *smbox = cinfo->transport_info;
	struct pcc_shared_mem __iomem *shmem = smbox->pchan->shmem;
	size_t len = ioread32(&shmem->header.length);
	size_t payload_len;

	xfer->hdr.status = ioread32(shmem->msg_payload);
	payload_len = len > SCMI_PCC_RESPONSE_OVERHEAD ?
		      len - SCMI_PCC_RESPONSE_OVERHEAD : 0;
	xfer->rx.len = min(xfer->rx.len, payload_len);

	/* msg_payload starts after the header, so skip only the status here. */
	memcpy_fromio(xfer->rx.buf, shmem->msg_payload + SCMI_PCC_STATUS_SIZE,
		      xfer->rx.len);
}

static void pcc_fetch_notification(struct scmi_chan_info *cinfo, size_t max_len,
				   struct scmi_xfer *xfer)
{
	struct scmi_pcc *smbox = cinfo->transport_info;
	struct pcc_shared_mem __iomem *shmem = smbox->pchan->shmem;
	size_t len = ioread32(&shmem->header.length);
	size_t payload_len;

	payload_len = len > SCMI_PCC_MSG_HEADER_SIZE ?
		      len - SCMI_PCC_MSG_HEADER_SIZE : 0;
	xfer->rx.len = min(max_len, payload_len);
	memcpy_fromio(xfer->rx.buf, shmem->msg_payload, xfer->rx.len);
}

static const struct scmi_transport_ops scmi_pcc_ops = {
	.chan_available = pcc_chan_available,
	.chan_setup = pcc_chan_setup,
	.chan_free = pcc_chan_free,
	.send_message = pcc_send_message,
	.fetch_response = pcc_fetch_response,
	.fetch_notification = pcc_fetch_notification,
};

static struct scmi_desc scmi_pcc_desc = {
	.ops = &scmi_pcc_ops,
	.max_rx_timeout_ms = 30,
	.max_msg = MBOX_TX_QUEUE_LEN,
	.max_msg_size = SCMI_SHMEM_MAX_PAYLOAD_SIZE,
};

static const struct acpi_device_id scmi_acpi_ids[] = {
	{ .id = "ARML0001" },
	{ }
};

MODULE_DEVICE_TABLE(acpi, scmi_acpi_ids);

DEFINE_SCMI_ACPI_TRANSPORT_DRIVER(scmi_pcc, scmi_pcc_driver,
				  scmi_pcc_desc, scmi_acpi_ids, core);

static int __init scmi_pcc_init(void)
{
	return platform_driver_register(&scmi_pcc_driver);
}

static void __exit scmi_pcc_exit(void)
{
	platform_driver_unregister(&scmi_pcc_driver);
	acpi_scmi_clear_transport_maps();
}
module_init(scmi_pcc_init);
module_exit(scmi_pcc_exit);

MODULE_AUTHOR("Sudeep Holla <sudeep.holla@kernel.org>");
MODULE_DESCRIPTION("SCMI ACPI PCC Transport driver");
MODULE_LICENSE("GPL");
