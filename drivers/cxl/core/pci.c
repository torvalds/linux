// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2021 Intel Corporation. All rights reserved. */
#include <linux/units.h>
#include <linux/io-64-nonatomic-lo-hi.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/pci-doe.h>
#include <linux/aer.h>
#include <cxlpci.h>
#include <cxlmem.h>
#include <cxl.h>
#include "core.h"
#include "trace.h"

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
	dport = devm_cxl_add_dport(port, &pdev->dev, port_num, map.resource);
	if (IS_ERR(dport)) {
		ctx->error = PTR_ERR(dport);
		return PTR_ERR(dport);
	}
	ctx->count++;

	return 0;
}

/**
 * devm_cxl_port_enumerate_dports - enumerate downstream ports of the upstream port
 * @port: cxl_port whose ->uport_dev is the upstream of dports to be enumerated
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

static int cxl_dvsec_mem_range_valid(struct cxl_dev_state *cxlds, int id)
{
	struct pci_dev *pdev = to_pci_dev(cxlds->dev);
	int d = cxlds->cxl_dvsec;
	bool valid = false;
	int rc, i;
	u32 temp;

	if (id > CXL_DVSEC_RANGE_MAX)
		return -EINVAL;

	/* Check MEM INFO VALID bit first, give up after 1s */
	i = 1;
	do {
		rc = pci_read_config_dword(pdev,
					   d + CXL_DVSEC_RANGE_SIZE_LOW(id),
					   &temp);
		if (rc)
			return rc;

		valid = FIELD_GET(CXL_DVSEC_MEM_INFO_VALID, temp);
		if (valid)
			break;
		msleep(1000);
	} while (i--);

	if (!valid) {
		dev_err(&pdev->dev,
			"Timeout awaiting memory range %d valid after 1s.\n",
			id);
		return -ETIMEDOUT;
	}

	return 0;
}

static int cxl_dvsec_mem_range_active(struct cxl_dev_state *cxlds, int id)
{
	struct pci_dev *pdev = to_pci_dev(cxlds->dev);
	int d = cxlds->cxl_dvsec;
	bool active = false;
	int rc, i;
	u32 temp;

	if (id > CXL_DVSEC_RANGE_MAX)
		return -EINVAL;

	/* Check MEM ACTIVE bit, up to 60s timeout by default */
	for (i = media_ready_timeout; i; i--) {
		rc = pci_read_config_dword(
			pdev, d + CXL_DVSEC_RANGE_SIZE_LOW(id), &temp);
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

	return 0;
}

/*
 * Wait up to @media_ready_timeout for the device to report memory
 * active.
 */
int cxl_await_media_ready(struct cxl_dev_state *cxlds)
{
	struct pci_dev *pdev = to_pci_dev(cxlds->dev);
	int d = cxlds->cxl_dvsec;
	int rc, i, hdm_count;
	u64 md_status;
	u16 cap;

	rc = pci_read_config_word(pdev,
				  d + CXL_DVSEC_CAP_OFFSET, &cap);
	if (rc)
		return rc;

	hdm_count = FIELD_GET(CXL_DVSEC_HDM_COUNT_MASK, cap);
	for (i = 0; i < hdm_count; i++) {
		rc = cxl_dvsec_mem_range_valid(cxlds, i);
		if (rc)
			return rc;
	}

	for (i = 0; i < hdm_count; i++) {
		rc = cxl_dvsec_mem_range_active(cxlds, i);
		if (rc)
			return rc;
	}

	md_status = readq(cxlds->regs.memdev + CXLMDEV_STATUS_OFFSET);
	if (!CXLMDEV_READY(md_status))
		return -EIO;

	return 0;
}
EXPORT_SYMBOL_NS_GPL(cxl_await_media_ready, CXL);

static int wait_for_valid(struct pci_dev *pdev, int d)
{
	u32 val;
	int rc;

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

/* require dvsec ranges to be covered by a locked platform window */
static int dvsec_range_allowed(struct device *dev, void *arg)
{
	struct range *dev_range = arg;
	struct cxl_decoder *cxld;

	if (!is_root_decoder(dev))
		return 0;

	cxld = to_cxl_decoder(dev);

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

int cxl_dvsec_rr_decode(struct device *dev, int d,
			struct cxl_endpoint_dvsec_info *info)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	int hdm_count, rc, i, ranges = 0;
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

	rc = wait_for_valid(pdev, d);
	if (rc) {
		dev_dbg(dev, "Failure awaiting MEM_INFO_VALID (%d)\n", rc);
		return rc;
	}

	/*
	 * The current DVSEC values are moot if the memory capability is
	 * disabled, and they will remain moot after the HDM Decoder
	 * capability is enabled.
	 */
	info->mem_enabled = FIELD_GET(CXL_DVSEC_MEM_ENABLE, ctrl);
	if (!info->mem_enabled)
		return 0;

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
		if (!size) {
			info->dvsec_range[i] = (struct range) {
				.start = 0,
				.end = CXL_RESOURCE_NONE,
			};
			continue;
		}

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

		info->dvsec_range[i] = (struct range) {
			.start = base,
			.end = base + size - 1
		};

		ranges++;
	}

	info->ranges = ranges;

	return 0;
}
EXPORT_SYMBOL_NS_GPL(cxl_dvsec_rr_decode, CXL);

/**
 * cxl_hdm_decode_init() - Setup HDM decoding for the endpoint
 * @cxlds: Device state
 * @cxlhdm: Mapped HDM decoder Capability
 * @info: Cached DVSEC range registers info
 *
 * Try to enable the endpoint's HDM Decoder Capability
 */
int cxl_hdm_decode_init(struct cxl_dev_state *cxlds, struct cxl_hdm *cxlhdm,
			struct cxl_endpoint_dvsec_info *info)
{
	void __iomem *hdm = cxlhdm->regs.hdm_decoder;
	struct cxl_port *port = cxlhdm->port;
	struct device *dev = cxlds->dev;
	struct cxl_port *root;
	int i, rc, allowed;
	u32 global_ctrl = 0;

	if (hdm)
		global_ctrl = readl(hdm + CXL_HDM_DECODER_CTRL_OFFSET);

	/*
	 * If the HDM Decoder Capability is already enabled then assume
	 * that some other agent like platform firmware set it up.
	 */
	if (global_ctrl & CXL_HDM_DECODER_ENABLE || (!hdm && info->mem_enabled))
		return devm_cxl_enable_mem(&port->dev, cxlds);
	else if (!hdm)
		return -ENODEV;

	root = to_cxl_port(port->dev.parent);
	while (!is_cxl_root(root) && is_cxl_port(root->dev.parent))
		root = to_cxl_port(root->dev.parent);
	if (!is_cxl_root(root)) {
		dev_err(dev, "Failed to acquire root port for HDM enable\n");
		return -ENODEV;
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

	if (!allowed && info->mem_enabled) {
		dev_err(dev, "Range register decodes outside platform defined CXL ranges.\n");
		return -ENXIO;
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
		return 0;

	rc = devm_cxl_enable_hdm(&port->dev, cxlhdm);
	if (rc)
		return rc;

	return devm_cxl_enable_mem(&port->dev, cxlds);
}
EXPORT_SYMBOL_NS_GPL(cxl_hdm_decode_init, CXL);

#define CXL_DOE_TABLE_ACCESS_REQ_CODE		0x000000ff
#define   CXL_DOE_TABLE_ACCESS_REQ_CODE_READ	0
#define CXL_DOE_TABLE_ACCESS_TABLE_TYPE		0x0000ff00
#define   CXL_DOE_TABLE_ACCESS_TABLE_TYPE_CDATA	0
#define CXL_DOE_TABLE_ACCESS_ENTRY_HANDLE	0xffff0000
#define CXL_DOE_TABLE_ACCESS_LAST_ENTRY		0xffff
#define CXL_DOE_PROTOCOL_TABLE_ACCESS 2

#define CDAT_DOE_REQ(entry_handle) cpu_to_le32				\
	(FIELD_PREP(CXL_DOE_TABLE_ACCESS_REQ_CODE,			\
		    CXL_DOE_TABLE_ACCESS_REQ_CODE_READ) |		\
	 FIELD_PREP(CXL_DOE_TABLE_ACCESS_TABLE_TYPE,			\
		    CXL_DOE_TABLE_ACCESS_TABLE_TYPE_CDATA) |		\
	 FIELD_PREP(CXL_DOE_TABLE_ACCESS_ENTRY_HANDLE, (entry_handle)))

static int cxl_cdat_get_length(struct device *dev,
			       struct pci_doe_mb *cdat_doe,
			       size_t *length)
{
	__le32 request = CDAT_DOE_REQ(0);
	__le32 response[2];
	int rc;

	rc = pci_doe(cdat_doe, PCI_DVSEC_VENDOR_ID_CXL,
		     CXL_DOE_PROTOCOL_TABLE_ACCESS,
		     &request, sizeof(request),
		     &response, sizeof(response));
	if (rc < 0) {
		dev_err(dev, "DOE failed: %d", rc);
		return rc;
	}
	if (rc < sizeof(response))
		return -EIO;

	*length = le32_to_cpu(response[1]);
	dev_dbg(dev, "CDAT length %zu\n", *length);

	return 0;
}

static int cxl_cdat_read_table(struct device *dev,
			       struct pci_doe_mb *cdat_doe,
			       void *cdat_table, size_t *cdat_length)
{
	size_t length = *cdat_length + sizeof(__le32);
	__le32 *data = cdat_table;
	int entry_handle = 0;
	__le32 saved_dw = 0;

	do {
		__le32 request = CDAT_DOE_REQ(entry_handle);
		struct cdat_entry_header *entry;
		size_t entry_dw;
		int rc;

		rc = pci_doe(cdat_doe, PCI_DVSEC_VENDOR_ID_CXL,
			     CXL_DOE_PROTOCOL_TABLE_ACCESS,
			     &request, sizeof(request),
			     data, length);
		if (rc < 0) {
			dev_err(dev, "DOE failed: %d", rc);
			return rc;
		}

		/* 1 DW Table Access Response Header + CDAT entry */
		entry = (struct cdat_entry_header *)(data + 1);
		if ((entry_handle == 0 &&
		     rc != sizeof(__le32) + sizeof(struct cdat_header)) ||
		    (entry_handle > 0 &&
		     (rc < sizeof(__le32) + sizeof(*entry) ||
		      rc != sizeof(__le32) + le16_to_cpu(entry->length))))
			return -EIO;

		/* Get the CXL table access header entry handle */
		entry_handle = FIELD_GET(CXL_DOE_TABLE_ACCESS_ENTRY_HANDLE,
					 le32_to_cpu(data[0]));
		entry_dw = rc / sizeof(__le32);
		/* Skip Header */
		entry_dw -= 1;
		/*
		 * Table Access Response Header overwrote the last DW of
		 * previous entry, so restore that DW
		 */
		*data = saved_dw;
		length -= entry_dw * sizeof(__le32);
		data += entry_dw;
		saved_dw = *data;
	} while (entry_handle != CXL_DOE_TABLE_ACCESS_LAST_ENTRY);

	/* Length in CDAT header may exceed concatenation of CDAT entries */
	*cdat_length -= length - sizeof(__le32);

	return 0;
}

static unsigned char cdat_checksum(void *buf, size_t size)
{
	unsigned char sum, *data = buf;
	size_t i;

	for (sum = 0, i = 0; i < size; i++)
		sum += data[i];
	return sum;
}

/**
 * read_cdat_data - Read the CDAT data on this port
 * @port: Port to read data from
 *
 * This call will sleep waiting for responses from the DOE mailbox.
 */
void read_cdat_data(struct cxl_port *port)
{
	struct device *uport = port->uport_dev;
	struct device *dev = &port->dev;
	struct pci_doe_mb *cdat_doe;
	struct pci_dev *pdev = NULL;
	struct cxl_memdev *cxlmd;
	size_t cdat_length;
	void *cdat_table, *cdat_buf;
	int rc;

	if (is_cxl_memdev(uport)) {
		struct device *host;

		cxlmd = to_cxl_memdev(uport);
		host = cxlmd->dev.parent;
		if (dev_is_pci(host))
			pdev = to_pci_dev(host);
	} else if (dev_is_pci(uport)) {
		pdev = to_pci_dev(uport);
	}

	if (!pdev)
		return;

	cdat_doe = pci_find_doe_mailbox(pdev, PCI_DVSEC_VENDOR_ID_CXL,
					CXL_DOE_PROTOCOL_TABLE_ACCESS);
	if (!cdat_doe) {
		dev_dbg(dev, "No CDAT mailbox\n");
		return;
	}

	port->cdat_available = true;

	if (cxl_cdat_get_length(dev, cdat_doe, &cdat_length)) {
		dev_dbg(dev, "No CDAT length\n");
		return;
	}

	cdat_buf = devm_kzalloc(dev, cdat_length + sizeof(__le32), GFP_KERNEL);
	if (!cdat_buf)
		return;

	rc = cxl_cdat_read_table(dev, cdat_doe, cdat_buf, &cdat_length);
	if (rc)
		goto err;

	cdat_table = cdat_buf + sizeof(__le32);
	if (cdat_checksum(cdat_table, cdat_length))
		goto err;

	port->cdat.table = cdat_table;
	port->cdat.length = cdat_length;
	return;

err:
	/* Don't leave table data allocated on error */
	devm_kfree(dev, cdat_buf);
	dev_err(dev, "Failed to read/validate CDAT.\n");
}
EXPORT_SYMBOL_NS_GPL(read_cdat_data, CXL);

static void __cxl_handle_cor_ras(struct cxl_dev_state *cxlds,
				 void __iomem *ras_base)
{
	void __iomem *addr;
	u32 status;

	if (!ras_base)
		return;

	addr = ras_base + CXL_RAS_CORRECTABLE_STATUS_OFFSET;
	status = readl(addr);
	if (status & CXL_RAS_CORRECTABLE_STATUS_MASK) {
		writel(status & CXL_RAS_CORRECTABLE_STATUS_MASK, addr);
		trace_cxl_aer_correctable_error(cxlds->cxlmd, status);
	}
}

static void cxl_handle_endpoint_cor_ras(struct cxl_dev_state *cxlds)
{
	return __cxl_handle_cor_ras(cxlds, cxlds->regs.ras);
}

/* CXL spec rev3.0 8.2.4.16.1 */
static void header_log_copy(void __iomem *ras_base, u32 *log)
{
	void __iomem *addr;
	u32 *log_addr;
	int i, log_u32_size = CXL_HEADERLOG_SIZE / sizeof(u32);

	addr = ras_base + CXL_RAS_HEADER_LOG_OFFSET;
	log_addr = log;

	for (i = 0; i < log_u32_size; i++) {
		*log_addr = readl(addr);
		log_addr++;
		addr += sizeof(u32);
	}
}

/*
 * Log the state of the RAS status registers and prepare them to log the
 * next error status. Return 1 if reset needed.
 */
static bool __cxl_handle_ras(struct cxl_dev_state *cxlds,
				  void __iomem *ras_base)
{
	u32 hl[CXL_HEADERLOG_SIZE_U32];
	void __iomem *addr;
	u32 status;
	u32 fe;

	if (!ras_base)
		return false;

	addr = ras_base + CXL_RAS_UNCORRECTABLE_STATUS_OFFSET;
	status = readl(addr);
	if (!(status & CXL_RAS_UNCORRECTABLE_STATUS_MASK))
		return false;

	/* If multiple errors, log header points to first error from ctrl reg */
	if (hweight32(status) > 1) {
		void __iomem *rcc_addr =
			ras_base + CXL_RAS_CAP_CONTROL_OFFSET;

		fe = BIT(FIELD_GET(CXL_RAS_CAP_CONTROL_FE_MASK,
				   readl(rcc_addr)));
	} else {
		fe = status;
	}

	header_log_copy(ras_base, hl);
	trace_cxl_aer_uncorrectable_error(cxlds->cxlmd, status, fe, hl);
	writel(status & CXL_RAS_UNCORRECTABLE_STATUS_MASK, addr);

	return true;
}

static bool cxl_handle_endpoint_ras(struct cxl_dev_state *cxlds)
{
	return __cxl_handle_ras(cxlds, cxlds->regs.ras);
}

#ifdef CONFIG_PCIEAER_CXL

static void cxl_dport_map_rch_aer(struct cxl_dport *dport)
{
	struct cxl_rcrb_info *ri = &dport->rcrb;
	void __iomem *dport_aer = NULL;
	resource_size_t aer_phys;
	struct device *host;

	if (dport->rch && ri->aer_cap) {
		host = dport->reg_map.host;
		aer_phys = ri->aer_cap + ri->base;
		dport_aer = devm_cxl_iomap_block(host, aer_phys,
				sizeof(struct aer_capability_regs));
	}

	dport->regs.dport_aer = dport_aer;
}

static void cxl_dport_map_regs(struct cxl_dport *dport)
{
	struct cxl_register_map *map = &dport->reg_map;
	struct device *dev = dport->dport_dev;

	if (!map->component_map.ras.valid)
		dev_dbg(dev, "RAS registers not found\n");
	else if (cxl_map_component_regs(map, &dport->regs.component,
					BIT(CXL_CM_CAP_CAP_ID_RAS)))
		dev_dbg(dev, "Failed to map RAS capability.\n");

	if (dport->rch)
		cxl_dport_map_rch_aer(dport);
}

static void cxl_disable_rch_root_ints(struct cxl_dport *dport)
{
	void __iomem *aer_base = dport->regs.dport_aer;
	struct pci_host_bridge *bridge;
	u32 aer_cmd_mask, aer_cmd;

	if (!aer_base)
		return;

	bridge = to_pci_host_bridge(dport->dport_dev);

	/*
	 * Disable RCH root port command interrupts.
	 * CXL 3.0 12.2.1.1 - RCH Downstream Port-detected Errors
	 *
	 * This sequence may not be necessary. CXL spec states disabling
	 * the root cmd register's interrupts is required. But, PCI spec
	 * shows these are disabled by default on reset.
	 */
	if (bridge->native_aer) {
		aer_cmd_mask = (PCI_ERR_ROOT_CMD_COR_EN |
				PCI_ERR_ROOT_CMD_NONFATAL_EN |
				PCI_ERR_ROOT_CMD_FATAL_EN);
		aer_cmd = readl(aer_base + PCI_ERR_ROOT_COMMAND);
		aer_cmd &= ~aer_cmd_mask;
		writel(aer_cmd, aer_base + PCI_ERR_ROOT_COMMAND);
	}
}

void cxl_setup_parent_dport(struct device *host, struct cxl_dport *dport)
{
	struct device *dport_dev = dport->dport_dev;
	struct pci_host_bridge *host_bridge;

	host_bridge = to_pci_host_bridge(dport_dev);
	if (host_bridge->native_aer)
		dport->rcrb.aer_cap = cxl_rcrb_to_aer(dport_dev, dport->rcrb.base);

	dport->reg_map.host = host;
	cxl_dport_map_regs(dport);

	if (dport->rch)
		cxl_disable_rch_root_ints(dport);
}
EXPORT_SYMBOL_NS_GPL(cxl_setup_parent_dport, CXL);

static void cxl_handle_rdport_cor_ras(struct cxl_dev_state *cxlds,
					  struct cxl_dport *dport)
{
	return __cxl_handle_cor_ras(cxlds, dport->regs.ras);
}

static bool cxl_handle_rdport_ras(struct cxl_dev_state *cxlds,
				       struct cxl_dport *dport)
{
	return __cxl_handle_ras(cxlds, dport->regs.ras);
}

/*
 * Copy the AER capability registers using 32 bit read accesses.
 * This is necessary because RCRB AER capability is MMIO mapped. Clear the
 * status after copying.
 *
 * @aer_base: base address of AER capability block in RCRB
 * @aer_regs: destination for copying AER capability
 */
static bool cxl_rch_get_aer_info(void __iomem *aer_base,
				 struct aer_capability_regs *aer_regs)
{
	int read_cnt = sizeof(struct aer_capability_regs) / sizeof(u32);
	u32 *aer_regs_buf = (u32 *)aer_regs;
	int n;

	if (!aer_base)
		return false;

	/* Use readl() to guarantee 32-bit accesses */
	for (n = 0; n < read_cnt; n++)
		aer_regs_buf[n] = readl(aer_base + n * sizeof(u32));

	writel(aer_regs->uncor_status, aer_base + PCI_ERR_UNCOR_STATUS);
	writel(aer_regs->cor_status, aer_base + PCI_ERR_COR_STATUS);

	return true;
}

/* Get AER severity. Return false if there is no error. */
static bool cxl_rch_get_aer_severity(struct aer_capability_regs *aer_regs,
				     int *severity)
{
	if (aer_regs->uncor_status & ~aer_regs->uncor_mask) {
		if (aer_regs->uncor_status & PCI_ERR_ROOT_FATAL_RCV)
			*severity = AER_FATAL;
		else
			*severity = AER_NONFATAL;
		return true;
	}

	if (aer_regs->cor_status & ~aer_regs->cor_mask) {
		*severity = AER_CORRECTABLE;
		return true;
	}

	return false;
}

static void cxl_handle_rdport_errors(struct cxl_dev_state *cxlds)
{
	struct pci_dev *pdev = to_pci_dev(cxlds->dev);
	struct aer_capability_regs aer_regs;
	struct cxl_dport *dport;
	struct cxl_port *port;
	int severity;

	port = cxl_pci_find_port(pdev, &dport);
	if (!port)
		return;

	put_device(&port->dev);

	if (!cxl_rch_get_aer_info(dport->regs.dport_aer, &aer_regs))
		return;

	if (!cxl_rch_get_aer_severity(&aer_regs, &severity))
		return;

	pci_print_aer(pdev, severity, &aer_regs);

	if (severity == AER_CORRECTABLE)
		cxl_handle_rdport_cor_ras(cxlds, dport);
	else
		cxl_handle_rdport_ras(cxlds, dport);
}

#else
static void cxl_handle_rdport_errors(struct cxl_dev_state *cxlds) { }
#endif

void cxl_cor_error_detected(struct pci_dev *pdev)
{
	struct cxl_dev_state *cxlds = pci_get_drvdata(pdev);
	struct device *dev = &cxlds->cxlmd->dev;

	scoped_guard(device, dev) {
		if (!dev->driver) {
			dev_warn(&pdev->dev,
				 "%s: memdev disabled, abort error handling\n",
				 dev_name(dev));
			return;
		}

		if (cxlds->rcd)
			cxl_handle_rdport_errors(cxlds);

		cxl_handle_endpoint_cor_ras(cxlds);
	}
}
EXPORT_SYMBOL_NS_GPL(cxl_cor_error_detected, CXL);

pci_ers_result_t cxl_error_detected(struct pci_dev *pdev,
				    pci_channel_state_t state)
{
	struct cxl_dev_state *cxlds = pci_get_drvdata(pdev);
	struct cxl_memdev *cxlmd = cxlds->cxlmd;
	struct device *dev = &cxlmd->dev;
	bool ue;

	scoped_guard(device, dev) {
		if (!dev->driver) {
			dev_warn(&pdev->dev,
				 "%s: memdev disabled, abort error handling\n",
				 dev_name(dev));
			return PCI_ERS_RESULT_DISCONNECT;
		}

		if (cxlds->rcd)
			cxl_handle_rdport_errors(cxlds);
		/*
		 * A frozen channel indicates an impending reset which is fatal to
		 * CXL.mem operation, and will likely crash the system. On the off
		 * chance the situation is recoverable dump the status of the RAS
		 * capability registers and bounce the active state of the memdev.
		 */
		ue = cxl_handle_endpoint_ras(cxlds);
	}


	switch (state) {
	case pci_channel_io_normal:
		if (ue) {
			device_release_driver(dev);
			return PCI_ERS_RESULT_NEED_RESET;
		}
		return PCI_ERS_RESULT_CAN_RECOVER;
	case pci_channel_io_frozen:
		dev_warn(&pdev->dev,
			 "%s: frozen state error detected, disable CXL.mem\n",
			 dev_name(dev));
		device_release_driver(dev);
		return PCI_ERS_RESULT_NEED_RESET;
	case pci_channel_io_perm_failure:
		dev_warn(&pdev->dev,
			 "failure state error detected, request disconnect\n");
		return PCI_ERS_RESULT_DISCONNECT;
	}
	return PCI_ERS_RESULT_NEED_RESET;
}
EXPORT_SYMBOL_NS_GPL(cxl_error_detected, CXL);

static int cxl_flit_size(struct pci_dev *pdev)
{
	if (cxl_pci_flit_256(pdev))
		return 256;

	return 68;
}

/**
 * cxl_pci_get_latency - calculate the link latency for the PCIe link
 * @pdev: PCI device
 *
 * return: calculated latency or 0 for no latency
 *
 * CXL Memory Device SW Guide v1.0 2.11.4 Link latency calculation
 * Link latency = LinkPropagationLatency + FlitLatency + RetimerLatency
 * LinkProgationLatency is negligible, so 0 will be used
 * RetimerLatency is assumed to be negligible and 0 will be used
 * FlitLatency = FlitSize / LinkBandwidth
 * FlitSize is defined by spec. CXL rev3.0 4.2.1.
 * 68B flit is used up to 32GT/s. >32GT/s, 256B flit size is used.
 * The FlitLatency is converted to picoseconds.
 */
long cxl_pci_get_latency(struct pci_dev *pdev)
{
	long bw;

	bw = pcie_link_speed_mbps(pdev);
	if (bw < 0)
		return 0;
	bw /= BITS_PER_BYTE;

	return cxl_flit_size(pdev) * MEGA / bw;
}
