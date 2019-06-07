// SPDX-License-Identifier: GPL-2.0

#define pr_fmt(fmt)	"papr-scm: " fmt

#include <linux/of.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/ndctl.h>
#include <linux/sched.h>
#include <linux/libnvdimm.h>
#include <linux/platform_device.h>

#include <asm/plpar_wrappers.h>

#define BIND_ANY_ADDR (~0ul)

#define PAPR_SCM_DIMM_CMD_MASK \
	((1ul << ND_CMD_GET_CONFIG_SIZE) | \
	 (1ul << ND_CMD_GET_CONFIG_DATA) | \
	 (1ul << ND_CMD_SET_CONFIG_DATA))

struct papr_scm_priv {
	struct platform_device *pdev;
	struct device_node *dn;
	uint32_t drc_index;
	uint64_t blocks;
	uint64_t block_size;
	int metadata_size;
	bool is_volatile;

	uint64_t bound_addr;

	struct nvdimm_bus_descriptor bus_desc;
	struct nvdimm_bus *bus;
	struct nvdimm *nvdimm;
	struct resource res;
	struct nd_region *region;
	struct nd_interleave_set nd_set;
};

static int drc_pmem_bind(struct papr_scm_priv *p)
{
	unsigned long ret[PLPAR_HCALL_BUFSIZE];
	uint64_t rc, token;
	uint64_t saved = 0;

	/*
	 * When the hypervisor cannot map all the requested memory in a single
	 * hcall it returns H_BUSY and we call again with the token until
	 * we get H_SUCCESS. Aborting the retry loop before getting H_SUCCESS
	 * leave the system in an undefined state, so we wait.
	 */
	token = 0;

	do {
		rc = plpar_hcall(H_SCM_BIND_MEM, ret, p->drc_index, 0,
				p->blocks, BIND_ANY_ADDR, token);
		token = ret[0];
		if (!saved)
			saved = ret[1];
		cond_resched();
	} while (rc == H_BUSY);

	if (rc) {
		dev_err(&p->pdev->dev, "bind err: %lld\n", rc);
		return -ENXIO;
	}

	p->bound_addr = saved;

	dev_dbg(&p->pdev->dev, "bound drc %x to %pR\n", p->drc_index, &p->res);

	return 0;
}

static int drc_pmem_unbind(struct papr_scm_priv *p)
{
	unsigned long ret[PLPAR_HCALL_BUFSIZE];
	uint64_t rc, token;

	token = 0;

	/* NB: unbind has the same retry requirements mentioned above */
	do {
		rc = plpar_hcall(H_SCM_UNBIND_MEM, ret, p->drc_index,
				p->bound_addr, p->blocks, token);
		token = ret[0];
		cond_resched();
	} while (rc == H_BUSY);

	if (rc)
		dev_err(&p->pdev->dev, "unbind error: %lld\n", rc);

	return !!rc;
}

static int papr_scm_meta_get(struct papr_scm_priv *p,
			     struct nd_cmd_get_config_data_hdr *hdr)
{
	unsigned long data[PLPAR_HCALL_BUFSIZE];
	unsigned long offset, data_offset;
	int len, read;
	int64_t ret;

	if ((hdr->in_offset + hdr->in_length) >= p->metadata_size)
		return -EINVAL;

	for (len = hdr->in_length; len; len -= read) {

		data_offset = hdr->in_length - len;
		offset = hdr->in_offset + data_offset;

		if (len >= 8)
			read = 8;
		else if (len >= 4)
			read = 4;
		else if (len >= 2)
			read = 2;
		else
			read = 1;

		ret = plpar_hcall(H_SCM_READ_METADATA, data, p->drc_index,
				  offset, read);

		if (ret == H_PARAMETER) /* bad DRC index */
			return -ENODEV;
		if (ret)
			return -EINVAL; /* other invalid parameter */

		switch (read) {
		case 8:
			*(uint64_t *)(hdr->out_buf + data_offset) = be64_to_cpu(data[0]);
			break;
		case 4:
			*(uint32_t *)(hdr->out_buf + data_offset) = be32_to_cpu(data[0] & 0xffffffff);
			break;

		case 2:
			*(uint16_t *)(hdr->out_buf + data_offset) = be16_to_cpu(data[0] & 0xffff);
			break;

		case 1:
			*(uint8_t *)(hdr->out_buf + data_offset) = (data[0] & 0xff);
			break;
		}
	}
	return 0;
}

static int papr_scm_meta_set(struct papr_scm_priv *p,
			     struct nd_cmd_set_config_hdr *hdr)
{
	unsigned long offset, data_offset;
	int len, wrote;
	unsigned long data;
	__be64 data_be;
	int64_t ret;

	if ((hdr->in_offset + hdr->in_length) >= p->metadata_size)
		return -EINVAL;

	for (len = hdr->in_length; len; len -= wrote) {

		data_offset = hdr->in_length - len;
		offset = hdr->in_offset + data_offset;

		if (len >= 8) {
			data = *(uint64_t *)(hdr->in_buf + data_offset);
			data_be = cpu_to_be64(data);
			wrote = 8;
		} else if (len >= 4) {
			data = *(uint32_t *)(hdr->in_buf + data_offset);
			data &= 0xffffffff;
			data_be = cpu_to_be32(data);
			wrote = 4;
		} else if (len >= 2) {
			data = *(uint16_t *)(hdr->in_buf + data_offset);
			data &= 0xffff;
			data_be = cpu_to_be16(data);
			wrote = 2;
		} else {
			data_be = *(uint8_t *)(hdr->in_buf + data_offset);
			data_be &= 0xff;
			wrote = 1;
		}

		ret = plpar_hcall_norets(H_SCM_WRITE_METADATA, p->drc_index,
					 offset, data_be, wrote);
		if (ret == H_PARAMETER) /* bad DRC index */
			return -ENODEV;
		if (ret)
			return -EINVAL; /* other invalid parameter */
	}

	return 0;
}

int papr_scm_ndctl(struct nvdimm_bus_descriptor *nd_desc, struct nvdimm *nvdimm,
		unsigned int cmd, void *buf, unsigned int buf_len, int *cmd_rc)
{
	struct nd_cmd_get_config_size *get_size_hdr;
	struct papr_scm_priv *p;

	/* Only dimm-specific calls are supported atm */
	if (!nvdimm)
		return -EINVAL;

	p = nvdimm_provider_data(nvdimm);

	switch (cmd) {
	case ND_CMD_GET_CONFIG_SIZE:
		get_size_hdr = buf;

		get_size_hdr->status = 0;
		get_size_hdr->max_xfer = 8;
		get_size_hdr->config_size = p->metadata_size;
		*cmd_rc = 0;
		break;

	case ND_CMD_GET_CONFIG_DATA:
		*cmd_rc = papr_scm_meta_get(p, buf);
		break;

	case ND_CMD_SET_CONFIG_DATA:
		*cmd_rc = papr_scm_meta_set(p, buf);
		break;

	default:
		return -EINVAL;
	}

	dev_dbg(&p->pdev->dev, "returned with cmd_rc = %d\n", *cmd_rc);

	return 0;
}

static const struct attribute_group *region_attr_groups[] = {
	&nd_region_attribute_group,
	&nd_device_attribute_group,
	&nd_mapping_attribute_group,
	&nd_numa_attribute_group,
	NULL,
};

static const struct attribute_group *bus_attr_groups[] = {
	&nvdimm_bus_attribute_group,
	NULL,
};

static const struct attribute_group *papr_scm_dimm_groups[] = {
	&nvdimm_attribute_group,
	&nd_device_attribute_group,
	NULL,
};

static int papr_scm_nvdimm_init(struct papr_scm_priv *p)
{
	struct device *dev = &p->pdev->dev;
	struct nd_mapping_desc mapping;
	struct nd_region_desc ndr_desc;
	unsigned long dimm_flags;

	p->bus_desc.ndctl = papr_scm_ndctl;
	p->bus_desc.module = THIS_MODULE;
	p->bus_desc.of_node = p->pdev->dev.of_node;
	p->bus_desc.attr_groups = bus_attr_groups;
	p->bus_desc.provider_name = kstrdup(p->pdev->name, GFP_KERNEL);

	if (!p->bus_desc.provider_name)
		return -ENOMEM;

	p->bus = nvdimm_bus_register(NULL, &p->bus_desc);
	if (!p->bus) {
		dev_err(dev, "Error creating nvdimm bus %pOF\n", p->dn);
		return -ENXIO;
	}

	dimm_flags = 0;
	set_bit(NDD_ALIASING, &dimm_flags);

	p->nvdimm = nvdimm_create(p->bus, p, papr_scm_dimm_groups,
				dimm_flags, PAPR_SCM_DIMM_CMD_MASK, 0, NULL);
	if (!p->nvdimm) {
		dev_err(dev, "Error creating DIMM object for %pOF\n", p->dn);
		goto err;
	}

	if (nvdimm_bus_check_dimm_count(p->bus, 1))
		goto err;

	/* now add the region */

	memset(&mapping, 0, sizeof(mapping));
	mapping.nvdimm = p->nvdimm;
	mapping.start = 0;
	mapping.size = p->blocks * p->block_size; // XXX: potential overflow?

	memset(&ndr_desc, 0, sizeof(ndr_desc));
	ndr_desc.attr_groups = region_attr_groups;
	ndr_desc.numa_node = dev_to_node(&p->pdev->dev);
	ndr_desc.target_node = ndr_desc.numa_node;
	ndr_desc.res = &p->res;
	ndr_desc.of_node = p->dn;
	ndr_desc.provider_data = p;
	ndr_desc.mapping = &mapping;
	ndr_desc.num_mappings = 1;
	ndr_desc.nd_set = &p->nd_set;
	set_bit(ND_REGION_PAGEMAP, &ndr_desc.flags);

	if (p->is_volatile)
		p->region = nvdimm_volatile_region_create(p->bus, &ndr_desc);
	else
		p->region = nvdimm_pmem_region_create(p->bus, &ndr_desc);
	if (!p->region) {
		dev_err(dev, "Error registering region %pR from %pOF\n",
				ndr_desc.res, p->dn);
		goto err;
	}

	return 0;

err:	nvdimm_bus_unregister(p->bus);
	kfree(p->bus_desc.provider_name);
	return -ENXIO;
}

static int papr_scm_probe(struct platform_device *pdev)
{
	struct device_node *dn = pdev->dev.of_node;
	u32 drc_index, metadata_size;
	u64 blocks, block_size;
	struct papr_scm_priv *p;
	const char *uuid_str;
	u64 uuid[2];
	int rc;

	/* check we have all the required DT properties */
	if (of_property_read_u32(dn, "ibm,my-drc-index", &drc_index)) {
		dev_err(&pdev->dev, "%pOF: missing drc-index!\n", dn);
		return -ENODEV;
	}

	if (of_property_read_u64(dn, "ibm,block-size", &block_size)) {
		dev_err(&pdev->dev, "%pOF: missing block-size!\n", dn);
		return -ENODEV;
	}

	if (of_property_read_u64(dn, "ibm,number-of-blocks", &blocks)) {
		dev_err(&pdev->dev, "%pOF: missing number-of-blocks!\n", dn);
		return -ENODEV;
	}

	if (of_property_read_string(dn, "ibm,unit-guid", &uuid_str)) {
		dev_err(&pdev->dev, "%pOF: missing unit-guid!\n", dn);
		return -ENODEV;
	}


	p = kzalloc(sizeof(*p), GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	/* optional DT properties */
	of_property_read_u32(dn, "ibm,metadata-size", &metadata_size);

	p->dn = dn;
	p->drc_index = drc_index;
	p->block_size = block_size;
	p->blocks = blocks;
	p->is_volatile = !of_property_read_bool(dn, "ibm,cache-flush-required");

	/* We just need to ensure that set cookies are unique across */
	uuid_parse(uuid_str, (uuid_t *) uuid);
	p->nd_set.cookie1 = uuid[0];
	p->nd_set.cookie2 = uuid[1];

	/* might be zero */
	p->metadata_size = metadata_size;
	p->pdev = pdev;

	/* request the hypervisor to bind this region to somewhere in memory */
	rc = drc_pmem_bind(p);
	if (rc)
		goto err;

	/* setup the resource for the newly bound range */
	p->res.start = p->bound_addr;
	p->res.end   = p->bound_addr + p->blocks * p->block_size - 1;
	p->res.name  = pdev->name;
	p->res.flags = IORESOURCE_MEM;

	rc = papr_scm_nvdimm_init(p);
	if (rc)
		goto err2;

	platform_set_drvdata(pdev, p);

	return 0;

err2:	drc_pmem_unbind(p);
err:	kfree(p);
	return rc;
}

static int papr_scm_remove(struct platform_device *pdev)
{
	struct papr_scm_priv *p = platform_get_drvdata(pdev);

	nvdimm_bus_unregister(p->bus);
	drc_pmem_unbind(p);
	kfree(p);

	return 0;
}

static const struct of_device_id papr_scm_match[] = {
	{ .compatible = "ibm,pmemory" },
	{ },
};

static struct platform_driver papr_scm_driver = {
	.probe = papr_scm_probe,
	.remove = papr_scm_remove,
	.driver = {
		.name = "papr_scm",
		.owner = THIS_MODULE,
		.of_match_table = papr_scm_match,
	},
};

module_platform_driver(papr_scm_driver);
MODULE_DEVICE_TABLE(of, papr_scm_match);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("IBM Corporation");
