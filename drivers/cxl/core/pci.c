// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2021 Intel Corporation. All rights reserved. */
#include <linux/io-64-nonatomic-lo-hi.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/pci-doe.h>
#include <cxlpci.h>
#include <cxlmem.h>
#include <cxl.h>
#include "core.h"

/**
 * DOC: cxl core pci
 *
 * Compute Express Link protocols are layered on top of PCIe. CXL core provides
 * a set of helpers for CXL interactions which occur via PCIe.
 */

static unsigned short media_ready_timeout = 60;
module_param(media_ready_timeout, ushort, 0644);
MODULE_PARM_DESC(media_ready_timeout, "seconds to wait for media ready");

struct cxl_walk_context {
	struct pci_bus *bus;
	struct cxl_port *port;
	int type;
	int error;
	int count;
};

static int match_add_dports(struct pci_dev *pdev, void *data)
{
	struct cxl_walk_context *ctx = data;
	struct cxl_port *port = ctx->port;
	int type = pci_pcie_type(pdev);
	struct cxl_register_map map;
	struct cxl_dport *dport;
	u32 lnkcap, port_num;
	int rc;

	if (pdev->bus != ctx->bus)
		return 0;
	if (!pci_is_pcie(pdev))
		return 0;
	if (type != ctx->type)
		return 0;
	if (pci_read_config_dword(pdev, pci_pcie_cap(pdev) + PCI_EXP_LNKCAP,
				  &lnkcap))
		return 0;

	rc = cxl_find_regblock(pdev, CXL_REGLOC_RBI_COMPONENT, &map);
	if (rc)
		dev_dbg(&port->dev, "failed to find component registers\n");

	port_num = FIELD_GET(PCI_EXP_LNKCAP_PN, lnkcap);
	dport = devm_cxl_add_dport(port, &pdev->dev, port_num,
				   cxl_regmap_to_base(pdev, &map));
	if (IS_ERR(dport)) {
		ctx->error = PTR_ERR(dport);
		return PTR_ERR(dport);
	}
	ctx->count++;

	dev_dbg(&port->dev, "add dport%d: %s\n", port_num, dev_name(&pdev->dev));

	return 0;
}

/**
 * devm_cxl_port_enumerate_dports - enumerate downstream ports of the upstream port
 * @port: cxl_port whose ->uport is the upstream of dports to be enumerated
 *
 * Returns a positive number of dports enumerated or a negative error
 * code.
 */
int devm_cxl_port_enumerate_dports(struct cxl_port *port)
{
	struct pci_bus *bus = cxl_port_to_pci_bus(port);
	struct cxl_walk_context ctx;
	int type;

	if (!bus)
		return -ENXIO;

	if (pci_is_root_bus(bus))
		type = PCI_EXP_TYPE_ROOT_PORT;
	else
		type = PCI_EXP_TYPE_DOWNSTREAM;

	ctx = (struct cxl_walk_context) {
		.port = port,
		.bus = bus,
		.type = type,
	};
	pci_walk_bus(bus, match_add_dports, &ctx);

	if (ctx.count == 0)
		return -ENODEV;
	if (ctx.error)
		return ctx.error;
	return ctx.count;
}
EXPORT_SYMBOL_NS_GPL(devm_cxl_port_enumerate_dports, CXL);

/*
 * Wait up to @media_ready_timeout for the device to report memory
 * active.
 */
int cxl_await_media_ready(struct cxl_dev_state *cxlds)
{
	struct pci_dev *pdev = to_pci_dev(cxlds->dev);
	int d = cxlds->cxl_dvsec;
	bool active = false;
	u64 md_status;
	int rc, i;

	for (i = media_ready_timeout; i; i--) {
		u32 temp;

		rc = pci_read_config_dword(
			pdev, d + CXL_DVSEC_RANGE_SIZE_LOW(0), &temp);
		if (rc)
			return rc;

		active = FIELD_GET(CXL_DVSEC_MEM_ACTIVE, temp);
		if (active)
			break;
		msleep(1000);
	}

	if (!active) {
		dev_err(&pdev->dev,
			"timeout awaiting memory active after %d seconds\n",
			media_ready_timeout);
		return -ETIMEDOUT;
	}

	md_status = readq(cxlds->regs.memdev + CXLMDEV_STATUS_OFFSET);
	if (!CXLMDEV_READY(md_status))
		return -EIO;

	return 0;
}
EXPORT_SYMBOL_NS_GPL(cxl_await_media_ready, CXL);

static int wait_for_valid(struct cxl_dev_state *cxlds)
{
	struct pci_dev *pdev = to_pci_dev(cxlds->dev);
	int d = cxlds->cxl_dvsec, rc;
	u32 val;

	/*
	 * Memory_Info_Valid: When set, indicates that the CXL Range 1 Size high
	 * and Size Low registers are valid. Must be set within 1 second of
	 * deassertion of reset to CXL device. Likely it is already set by the
	 * time this runs, but otherwise give a 1.5 second timeout in case of
	 * clock skew.
	 */
	rc = pci_read_config_dword(pdev, d + CXL_DVSEC_RANGE_SIZE_LOW(0), &val);
	if (rc)
		return rc;

	if (val & CXL_DVSEC_MEM_INFO_VALID)
		return 0;

	msleep(1500);

	rc = pci_read_config_dword(pdev, d + CXL_DVSEC_RANGE_SIZE_LOW(0), &val);
	if (rc)
		return rc;

	if (val & CXL_DVSEC_MEM_INFO_VALID)
		return 0;

	return -ETIMEDOUT;
}

static int cxl_set_mem_enable(struct cxl_dev_state *cxlds, u16 val)
{
	struct pci_dev *pdev = to_pci_dev(cxlds->dev);
	int d = cxlds->cxl_dvsec;
	u16 ctrl;
	int rc;

	rc = pci_read_config_word(pdev, d + CXL_DVSEC_CTRL_OFFSET, &ctrl);
	if (rc < 0)
		return rc;

	if ((ctrl & CXL_DVSEC_MEM_ENABLE) == val)
		return 1;
	ctrl &= ~CXL_DVSEC_MEM_ENABLE;
	ctrl |= val;

	rc = pci_write_config_word(pdev, d + CXL_DVSEC_CTRL_OFFSET, ctrl);
	if (rc < 0)
		return rc;

	return 0;
}

static void clear_mem_enable(void *cxlds)
{
	cxl_set_mem_enable(cxlds, 0);
}

static int devm_cxl_enable_mem(struct device *host, struct cxl_dev_state *cxlds)
{
	int rc;

	rc = cxl_set_mem_enable(cxlds, CXL_DVSEC_MEM_ENABLE);
	if (rc < 0)
		return rc;
	if (rc > 0)
		return 0;
	return devm_add_action_or_reset(host, clear_mem_enable, cxlds);
}

static bool range_contains(struct range *r1, struct range *r2)
{
	return r1->start <= r2->start && r1->end >= r2->end;
}

/* require dvsec ranges to be covered by a locked platform window */
static int dvsec_range_allowed(struct device *dev, void *arg)
{
	struct range *dev_range = arg;
	struct cxl_decoder *cxld;

	if (!is_root_decoder(dev))
		return 0;

	cxld = to_cxl_decoder(dev);

	if (!(cxld->flags & CXL_DECODER_F_LOCK))
		return 0;
	if (!(cxld->flags & CXL_DECODER_F_RAM))
		return 0;

	return range_contains(&cxld->hpa_range, dev_range);
}

static void disable_hdm(void *_cxlhdm)
{
	u32 global_ctrl;
	struct cxl_hdm *cxlhdm = _cxlhdm;
	void __iomem *hdm = cxlhdm->regs.hdm_decoder;

	global_ctrl = readl(hdm + CXL_HDM_DECODER_CTRL_OFFSET);
	writel(global_ctrl & ~CXL_HDM_DECODER_ENABLE,
	       hdm + CXL_HDM_DECODER_CTRL_OFFSET);
}

static int devm_cxl_enable_hdm(struct device *host, struct cxl_hdm *cxlhdm)
{
	void __iomem *hdm = cxlhdm->regs.hdm_decoder;
	u32 global_ctrl;

	global_ctrl = readl(hdm + CXL_HDM_DECODER_CTRL_OFFSET);
	writel(global_ctrl | CXL_HDM_DECODER_ENABLE,
	       hdm + CXL_HDM_DECODER_CTRL_OFFSET);

	return devm_add_action_or_reset(host, disable_hdm, cxlhdm);
}

static bool __cxl_hdm_decode_init(struct cxl_dev_state *cxlds,
				  struct cxl_hdm *cxlhdm,
				  struct cxl_endpoint_dvsec_info *info)
{
	void __iomem *hdm = cxlhdm->regs.hdm_decoder;
	struct cxl_port *port = cxlhdm->port;
	struct device *dev = cxlds->dev;
	struct cxl_port *root;
	int i, rc, allowed;
	u32 global_ctrl;

	global_ctrl = readl(hdm + CXL_HDM_DECODER_CTRL_OFFSET);

	/*
	 * If the HDM Decoder Capability is already enabled then assume
	 * that some other agent like platform firmware set it up.
	 */
	if (global_ctrl & CXL_HDM_DECODER_ENABLE) {
		rc = devm_cxl_enable_mem(&port->dev, cxlds);
		if (rc)
			return false;
		return true;
	}

	root = to_cxl_port(port->dev.parent);
	while (!is_cxl_root(root) && is_cxl_port(root->dev.parent))
		root = to_cxl_port(root->dev.parent);
	if (!is_cxl_root(root)) {
		dev_err(dev, "Failed to acquire root port for HDM enable\n");
		return false;
	}

	for (i = 0, allowed = 0; info->mem_enabled && i < info->ranges; i++) {
		struct device *cxld_dev;

		cxld_dev = device_find_child(&root->dev, &info->dvsec_range[i],
					     dvsec_range_allowed);
		if (!cxld_dev) {
			dev_dbg(dev, "DVSEC Range%d denied by platform\n", i);
			continue;
		}
		dev_dbg(dev, "DVSEC Range%d allowed by platform\n", i);
		put_device(cxld_dev);
		allowed++;
	}

	if (!allowed) {
		cxl_set_mem_enable(cxlds, 0);
		info->mem_enabled = 0;
	}

	/*
	 * Per CXL 2.0 Section 8.1.3.8.3 and 8.1.3.8.4 DVSEC CXL Range 1 Base
	 * [High,Low] when HDM operation is enabled the range register values
	 * are ignored by the device, but the spec also recommends matching the
	 * DVSEC Range 1,2 to HDM Decoder Range 0,1. So, non-zero info->ranges
	 * are expected even though Linux does not require or maintain that
	 * match. If at least one DVSEC range is enabled and allowed, skip HDM
	 * Decoder Capability Enable.
	 */
	if (info->mem_enabled)
		return false;

	rc = devm_cxl_enable_hdm(&port->dev, cxlhdm);
	if (rc)
		return false;

	rc = devm_cxl_enable_mem(&port->dev, cxlds);
	if (rc)
		return false;

	return true;
}

/**
 * cxl_hdm_decode_init() - Setup HDM decoding for the endpoint
 * @cxlds: Device state
 * @cxlhdm: Mapped HDM decoder Capability
 *
 * Try to enable the endpoint's HDM Decoder Capability
 */
int cxl_hdm_decode_init(struct cxl_dev_state *cxlds, struct cxl_hdm *cxlhdm)
{
	struct pci_dev *pdev = to_pci_dev(cxlds->dev);
	struct cxl_endpoint_dvsec_info info = { 0 };
	int hdm_count, rc, i, ranges = 0;
	struct device *dev = &pdev->dev;
	int d = cxlds->cxl_dvsec;
	u16 cap, ctrl;

	if (!d) {
		dev_dbg(dev, "No DVSEC Capability\n");
		return -ENXIO;
	}

	rc = pci_read_config_word(pdev, d + CXL_DVSEC_CAP_OFFSET, &cap);
	if (rc)
		return rc;

	rc = pci_read_config_word(pdev, d + CXL_DVSEC_CTRL_OFFSET, &ctrl);
	if (rc)
		return rc;

	if (!(cap & CXL_DVSEC_MEM_CAPABLE)) {
		dev_dbg(dev, "Not MEM Capable\n");
		return -ENXIO;
	}

	/*
	 * It is not allowed by spec for MEM.capable to be set and have 0 legacy
	 * HDM decoders (values > 2 are also undefined as of CXL 2.0). As this
	 * driver is for a spec defined class code which must be CXL.mem
	 * capable, there is no point in continuing to enable CXL.mem.
	 */
	hdm_count = FIELD_GET(CXL_DVSEC_HDM_COUNT_MASK, cap);
	if (!hdm_count || hdm_count > 2)
		return -EINVAL;

	rc = wait_for_valid(cxlds);
	if (rc) {
		dev_dbg(dev, "Failure awaiting MEM_INFO_VALID (%d)\n", rc);
		return rc;
	}

	/*
	 * The current DVSEC values are moot if the memory capability is
	 * disabled, and they will remain moot after the HDM Decoder
	 * capability is enabled.
	 */
	info.mem_enabled = FIELD_GET(CXL_DVSEC_MEM_ENABLE, ctrl);
	if (!info.mem_enabled)
		goto hdm_init;

	for (i = 0; i < hdm_count; i++) {
		u64 base, size;
		u32 temp;

		rc = pci_read_config_dword(
			pdev, d + CXL_DVSEC_RANGE_SIZE_HIGH(i), &temp);
		if (rc)
			return rc;

		size = (u64)temp << 32;

		rc = pci_read_config_dword(
			pdev, d + CXL_DVSEC_RANGE_SIZE_LOW(i), &temp);
		if (rc)
			return rc;

		size |= temp & CXL_DVSEC_MEM_SIZE_LOW_MASK;

		rc = pci_read_config_dword(
			pdev, d + CXL_DVSEC_RANGE_BASE_HIGH(i), &temp);
		if (rc)
			return rc;

		base = (u64)temp << 32;

		rc = pci_read_config_dword(
			pdev, d + CXL_DVSEC_RANGE_BASE_LOW(i), &temp);
		if (rc)
			return rc;

		base |= temp & CXL_DVSEC_MEM_BASE_LOW_MASK;

		info.dvsec_range[i] = (struct range) {
			.start = base,
			.end = base + size - 1
		};

		if (size)
			ranges++;
	}

	info.ranges = ranges;

	/*
	 * If DVSEC ranges are being used instead of HDM decoder registers there
	 * is no use in trying to manage those.
	 */
hdm_init:
	if (!__cxl_hdm_decode_init(cxlds, cxlhdm, &info)) {
		dev_err(dev,
			"Legacy range registers configuration prevents HDM operation.\n");
		return -EBUSY;
	}

	return 0;
}
EXPORT_SYMBOL_NS_GPL(cxl_hdm_decode_init, CXL);

#define CXL_DOE_TABLE_ACCESS_REQ_CODE		0x000000ff
#define   CXL_DOE_TABLE_ACCESS_REQ_CODE_READ	0
#define CXL_DOE_TABLE_ACCESS_TABLE_TYPE		0x0000ff00
#define   CXL_DOE_TABLE_ACCESS_TABLE_TYPE_CDATA	0
#define CXL_DOE_TABLE_ACCESS_ENTRY_HANDLE	0xffff0000
#define CXL_DOE_TABLE_ACCESS_LAST_ENTRY		0xffff
#define CXL_DOE_PROTOCOL_TABLE_ACCESS 2

static struct pci_doe_mb *find_cdat_doe(struct device *uport)
{
	struct cxl_memdev *cxlmd;
	struct cxl_dev_state *cxlds;
	unsigned long index;
	void *entry;

	cxlmd = to_cxl_memdev(uport);
	cxlds = cxlmd->cxlds;

	xa_for_each(&cxlds->doe_mbs, index, entry) {
		struct pci_doe_mb *cur = entry;

		if (pci_doe_supports_prot(cur, PCI_DVSEC_VENDOR_ID_CXL,
					  CXL_DOE_PROTOCOL_TABLE_ACCESS))
			return cur;
	}

	return NULL;
}

#define CDAT_DOE_REQ(entry_handle) cpu_to_le32				\
	(FIELD_PREP(CXL_DOE_TABLE_ACCESS_REQ_CODE,			\
		    CXL_DOE_TABLE_ACCESS_REQ_CODE_READ) |		\
	 FIELD_PREP(CXL_DOE_TABLE_ACCESS_TABLE_TYPE,			\
		    CXL_DOE_TABLE_ACCESS_TABLE_TYPE_CDATA) |		\
	 FIELD_PREP(CXL_DOE_TABLE_ACCESS_ENTRY_HANDLE, (entry_handle)))

static void cxl_doe_task_complete(struct pci_doe_task *task)
{
	complete(task->private);
}

struct cdat_doe_task {
	__le32 request_pl;
	__le32 response_pl[32];
	struct completion c;
	struct pci_doe_task task;
};

#define DECLARE_CDAT_DOE_TASK(req, cdt)                       \
struct cdat_doe_task cdt = {                                  \
	.c = COMPLETION_INITIALIZER_ONSTACK(cdt.c),           \
	.request_pl = req,				      \
	.task = {                                             \
		.prot.vid = PCI_DVSEC_VENDOR_ID_CXL,        \
		.prot.type = CXL_DOE_PROTOCOL_TABLE_ACCESS, \
		.request_pl = &cdt.request_pl,                \
		.request_pl_sz = sizeof(cdt.request_pl),      \
		.response_pl = cdt.response_pl,               \
		.response_pl_sz = sizeof(cdt.response_pl),    \
		.complete = cxl_doe_task_complete,            \
		.private = &cdt.c,                            \
	}                                                     \
}

static int cxl_cdat_get_length(struct device *dev,
			       struct pci_doe_mb *cdat_doe,
			       size_t *length)
{
	DECLARE_CDAT_DOE_TASK(CDAT_DOE_REQ(0), t);
	int rc;

	rc = pci_doe_submit_task(cdat_doe, &t.task);
	if (rc < 0) {
		dev_err(dev, "DOE submit failed: %d", rc);
		return rc;
	}
	wait_for_completion(&t.c);
	if (t.task.rv < 2 * sizeof(__le32))
		return -EIO;

	*length = le32_to_cpu(t.response_pl[1]);
	dev_dbg(dev, "CDAT length %zu\n", *length);

	return 0;
}

static int cxl_cdat_read_table(struct device *dev,
			       struct pci_doe_mb *cdat_doe,
			       struct cxl_cdat *cdat)
{
	size_t length = cdat->length;
	__le32 *data = cdat->table;
	int entry_handle = 0;

	do {
		DECLARE_CDAT_DOE_TASK(CDAT_DOE_REQ(entry_handle), t);
		struct cdat_entry_header *entry;
		size_t entry_dw;
		int rc;

		rc = pci_doe_submit_task(cdat_doe, &t.task);
		if (rc < 0) {
			dev_err(dev, "DOE submit failed: %d", rc);
			return rc;
		}
		wait_for_completion(&t.c);

		/* 1 DW Table Access Response Header + CDAT entry */
		entry = (struct cdat_entry_header *)(t.response_pl + 1);
		if ((entry_handle == 0 &&
		     t.task.rv != sizeof(__le32) + sizeof(struct cdat_header)) ||
		    (entry_handle > 0 &&
		     (t.task.rv < sizeof(__le32) + sizeof(*entry) ||
		      t.task.rv != sizeof(__le32) + le16_to_cpu(entry->length))))
			return -EIO;

		/* Get the CXL table access header entry handle */
		entry_handle = FIELD_GET(CXL_DOE_TABLE_ACCESS_ENTRY_HANDLE,
					 le32_to_cpu(t.response_pl[0]));
		entry_dw = t.task.rv / sizeof(__le32);
		/* Skip Header */
		entry_dw -= 1;
		entry_dw = min(length / sizeof(__le32), entry_dw);
		/* Prevent length < 1 DW from causing a buffer overflow */
		if (entry_dw) {
			memcpy(data, entry, entry_dw * sizeof(__le32));
			length -= entry_dw * sizeof(__le32);
			data += entry_dw;
		}
	} while (entry_handle != CXL_DOE_TABLE_ACCESS_LAST_ENTRY);

	/* Length in CDAT header may exceed concatenation of CDAT entries */
	cdat->length -= length;

	return 0;
}

/**
 * read_cdat_data - Read the CDAT data on this port
 * @port: Port to read data from
 *
 * This call will sleep waiting for responses from the DOE mailbox.
 */
void read_cdat_data(struct cxl_port *port)
{
	struct pci_doe_mb *cdat_doe;
	struct device *dev = &port->dev;
	struct device *uport = port->uport;
	size_t cdat_length;
	int rc;

	cdat_doe = find_cdat_doe(uport);
	if (!cdat_doe) {
		dev_dbg(dev, "No CDAT mailbox\n");
		return;
	}

	port->cdat_available = true;

	if (cxl_cdat_get_length(dev, cdat_doe, &cdat_length)) {
		dev_dbg(dev, "No CDAT length\n");
		return;
	}

	port->cdat.table = devm_kzalloc(dev, cdat_length, GFP_KERNEL);
	if (!port->cdat.table)
		return;

	port->cdat.length = cdat_length;
	rc = cxl_cdat_read_table(dev, cdat_doe, &port->cdat);
	if (rc) {
		/* Don't leave table data allocated on error */
		devm_kfree(dev, port->cdat.table);
		port->cdat.table = NULL;
		port->cdat.length = 0;
		dev_err(dev, "CDAT data read error\n");
	}
}
EXPORT_SYMBOL_NS_GPL(read_cdat_data, CXL);
