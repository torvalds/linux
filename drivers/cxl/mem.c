// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2022 Intel Corporation. All rights reserved. */
#include <linux/device.h>
#include <linux/module.h>
#include <linux/pci.h>

#include "cxlmem.h"
#include "cxlpci.h"

/**
 * DOC: cxl mem
 *
 * CXL memory endpoint devices and switches are CXL capable devices that are
 * participating in CXL.mem protocol. Their functionality builds on top of the
 * CXL.io protocol that allows enumerating and configuring components via
 * standard PCI mechanisms.
 *
 * The cxl_mem driver owns kicking off the enumeration of this CXL.mem
 * capability. With the detection of a CXL capable endpoint, the driver will
 * walk up to find the platform specific port it is connected to, and determine
 * if there are intervening switches in the path. If there are switches, a
 * secondary action is to enumerate those (implemented in cxl_core). Finally the
 * cxl_mem driver adds the device it is bound to as a CXL endpoint-port for use
 * in higher level operations.
 */

static int wait_for_media(struct cxl_memdev *cxlmd)
{
	struct cxl_dev_state *cxlds = cxlmd->cxlds;
	struct cxl_endpoint_dvsec_info *info = &cxlds->info;
	int rc;

	if (!info->mem_enabled)
		return -EBUSY;

	rc = cxlds->wait_media_ready(cxlds);
	if (rc)
		return rc;

	/*
	 * We know the device is active, and enabled, if any ranges are non-zero
	 * we'll need to check later before adding the port since that owns the
	 * HDM decoder registers.
	 */
	return 0;
}

static int create_endpoint(struct cxl_memdev *cxlmd,
			   struct cxl_port *parent_port)
{
	struct cxl_dev_state *cxlds = cxlmd->cxlds;
	struct cxl_port *endpoint;

	endpoint = devm_cxl_add_port(&parent_port->dev, &cxlmd->dev,
				     cxlds->component_reg_phys, parent_port);
	if (IS_ERR(endpoint))
		return PTR_ERR(endpoint);

	dev_dbg(&cxlmd->dev, "add: %s\n", dev_name(&endpoint->dev));

	if (!endpoint->dev.driver) {
		dev_err(&cxlmd->dev, "%s failed probe\n",
			dev_name(&endpoint->dev));
		return -ENXIO;
	}

	return cxl_endpoint_autoremove(cxlmd, endpoint);
}

/**
 * cxl_dvsec_decode_init() - Setup HDM decoding for the endpoint
 * @cxlds: Device state
 *
 * Additionally, enables global HDM decoding. Warning: don't call this outside
 * of probe. Once probe is complete, the port driver owns all access to the HDM
 * decoder registers.
 *
 * Returns: false if DVSEC Ranges are being used instead of HDM
 * decoders, or if it can not be determined if DVSEC Ranges are in use.
 * Otherwise, returns true.
 */
__mock bool cxl_dvsec_decode_init(struct cxl_dev_state *cxlds)
{
	struct cxl_endpoint_dvsec_info *info = &cxlds->info;
	struct cxl_register_map map;
	struct cxl_component_reg_map *cmap = &map.component_map;
	bool global_enable, do_hdm_init = false;
	void __iomem *crb;
	u32 global_ctrl;

	/* map hdm decoder */
	crb = ioremap(cxlds->component_reg_phys, CXL_COMPONENT_REG_BLOCK_SIZE);
	if (!crb) {
		dev_dbg(cxlds->dev, "Failed to map component registers\n");
		return false;
	}

	cxl_probe_component_regs(cxlds->dev, crb, cmap);
	if (!cmap->hdm_decoder.valid) {
		dev_dbg(cxlds->dev, "Invalid HDM decoder registers\n");
		goto out;
	}

	global_ctrl = readl(crb + cmap->hdm_decoder.offset +
			    CXL_HDM_DECODER_CTRL_OFFSET);
	global_enable = global_ctrl & CXL_HDM_DECODER_ENABLE;
	if (!global_enable && info->ranges) {
		dev_dbg(cxlds->dev,
			"DVSEC ranges already programmed and HDM decoders not enabled.\n");
		goto out;
	}

	do_hdm_init = true;

	/*
	 * Permanently (for this boot at least) opt the device into HDM
	 * operation. Individual HDM decoders still need to be enabled after
	 * this point.
	 */
	if (!global_enable) {
		dev_dbg(cxlds->dev, "Enabling HDM decode\n");
		writel(global_ctrl | CXL_HDM_DECODER_ENABLE,
		       crb + cmap->hdm_decoder.offset +
			       CXL_HDM_DECODER_CTRL_OFFSET);
	}

out:
	iounmap(crb);
	return do_hdm_init;
}

static int cxl_mem_probe(struct device *dev)
{
	struct cxl_memdev *cxlmd = to_cxl_memdev(dev);
	struct cxl_dev_state *cxlds = cxlmd->cxlds;
	struct cxl_port *parent_port;
	int rc;

	/*
	 * Someone is trying to reattach this device after it lost its port
	 * connection (an endpoint port previously registered by this memdev was
	 * disabled). This racy check is ok because if the port is still gone,
	 * no harm done, and if the port hierarchy comes back it will re-trigger
	 * this probe. Port rescan and memdev detach work share the same
	 * single-threaded workqueue.
	 */
	if (work_pending(&cxlmd->detach_work))
		return -EBUSY;

	rc = wait_for_media(cxlmd);
	if (rc) {
		dev_err(dev, "Media not active (%d)\n", rc);
		return rc;
	}

	/*
	 * If DVSEC ranges are being used instead of HDM decoder registers there
	 * is no use in trying to manage those.
	 */
	if (!cxl_dvsec_decode_init(cxlds)) {
		struct cxl_endpoint_dvsec_info *info = &cxlds->info;
		int i;

		/* */
		for (i = 0; i < 2; i++) {
			u64 base, size;

			/*
			 * Give a nice warning to the user that BIOS has really
			 * botched things for them if it didn't place DVSEC
			 * ranges in the memory map.
			 */
			base = info->dvsec_range[i].start;
			size = range_len(&info->dvsec_range[i]);
			if (size && !region_intersects(base, size,
						       IORESOURCE_SYSTEM_RAM,
						       IORES_DESC_NONE)) {
				dev_err(dev,
					"DVSEC range %#llx-%#llx must be reserved by BIOS, but isn't\n",
					base, base + size - 1);
			}
		}
		dev_err(dev,
			"Active DVSEC range registers in use. Will not bind.\n");
		return -EBUSY;
	}

	rc = devm_cxl_enumerate_ports(cxlmd);
	if (rc)
		return rc;

	parent_port = cxl_mem_find_port(cxlmd);
	if (!parent_port) {
		dev_err(dev, "CXL port topology not found\n");
		return -ENXIO;
	}

	cxl_device_lock(&parent_port->dev);
	if (!parent_port->dev.driver) {
		dev_err(dev, "CXL port topology %s not enabled\n",
			dev_name(&parent_port->dev));
		rc = -ENXIO;
		goto out;
	}

	rc = create_endpoint(cxlmd, parent_port);
out:
	cxl_device_unlock(&parent_port->dev);
	put_device(&parent_port->dev);
	return rc;
}

static struct cxl_driver cxl_mem_driver = {
	.name = "cxl_mem",
	.probe = cxl_mem_probe,
	.id = CXL_DEVICE_MEMORY_EXPANDER,
};

module_cxl_driver(cxl_mem_driver);

MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS(CXL);
MODULE_ALIAS_CXL(CXL_DEVICE_MEMORY_EXPANDER);
/*
 * create_endpoint() wants to validate port driver attach immediately after
 * endpoint registration.
 */
MODULE_SOFTDEP("pre: cxl_port");
