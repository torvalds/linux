// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Advanced Micro Devices, Inc.
 */

#include <linux/prmt.h>
#include <linux/pci.h>
#include <linux/acpi.h>

#include <cxlmem.h>
#include "core.h"

/*
 * PRM Address Translation - CXL DPA to System Physical Address
 *
 * Reference:
 *
 * AMD Family 1Ah Models 00h–0Fh and Models 10h–1Fh
 * ACPI v6.5 Porting Guide, Publication # 58088
 */

static const guid_t prm_cxl_dpa_spa_guid =
	GUID_INIT(0xee41b397, 0x25d4, 0x452c, 0xad, 0x54, 0x48, 0xc6, 0xe3,
		  0x48, 0x0b, 0x94);

struct prm_cxl_dpa_spa_data {
	u64 dpa;
	u8 reserved;
	u8 devfn;
	u8 bus;
	u8 segment;
	u64 *spa;
} __packed;

static u64 prm_cxl_dpa_spa(struct pci_dev *pci_dev, u64 dpa)
{
	struct prm_cxl_dpa_spa_data data;
	u64 spa;
	int rc;

	data = (struct prm_cxl_dpa_spa_data) {
		.dpa     = dpa,
		.devfn   = pci_dev->devfn,
		.bus     = pci_dev->bus->number,
		.segment = pci_domain_nr(pci_dev->bus),
		.spa     = &spa,
	};

	rc = acpi_call_prm_handler(prm_cxl_dpa_spa_guid, &data);
	if (rc) {
		pci_dbg(pci_dev, "failed to get SPA for %#llx: %d\n", dpa, rc);
		return ULLONG_MAX;
	}

	pci_dbg(pci_dev, "PRM address translation: DPA -> SPA: %#llx -> %#llx\n", dpa, spa);

	return spa;
}

static int cxl_prm_setup_root(struct cxl_root *cxl_root, void *data)
{
	struct cxl_region_context *ctx = data;
	struct cxl_endpoint_decoder *cxled = ctx->cxled;
	struct cxl_decoder *cxld = &cxled->cxld;
	struct cxl_memdev *cxlmd = cxled_to_memdev(cxled);
	struct range hpa_range = ctx->hpa_range;
	struct pci_dev *pci_dev;
	u64 spa_len, len;
	u64 addr, base_spa, base;
	int ways, gran;

	/*
	 * When Normalized Addressing is enabled, the endpoint maintains a 1:1
	 * mapping between HPA and DPA. If disabled, skip address translation
	 * and perform only a range check.
	 */
	if (hpa_range.start != cxled->dpa_res->start)
		return 0;

	/*
	 * Endpoints are programmed passthrough in Normalized Addressing mode.
	 */
	if (ctx->interleave_ways != 1) {
		dev_dbg(&cxld->dev, "unexpected interleaving config: ways: %d granularity: %d\n",
			ctx->interleave_ways, ctx->interleave_granularity);
		return -ENXIO;
	}

	if (!cxlmd || !dev_is_pci(cxlmd->dev.parent)) {
		dev_dbg(&cxld->dev, "No endpoint found: %s, range %#llx-%#llx\n",
			dev_name(cxld->dev.parent), hpa_range.start,
			hpa_range.end);
		return -ENXIO;
	}

	pci_dev = to_pci_dev(cxlmd->dev.parent);

	/* Translate HPA range to SPA. */
	base = hpa_range.start;
	hpa_range.start = prm_cxl_dpa_spa(pci_dev, hpa_range.start);
	hpa_range.end = prm_cxl_dpa_spa(pci_dev, hpa_range.end);
	base_spa = hpa_range.start;

	if (hpa_range.start == ULLONG_MAX || hpa_range.end == ULLONG_MAX) {
		dev_dbg(cxld->dev.parent,
			"CXL address translation: Failed to translate HPA range: %#llx-%#llx:%#llx-%#llx(%s)\n",
			hpa_range.start, hpa_range.end, ctx->hpa_range.start,
			ctx->hpa_range.end, dev_name(&cxld->dev));
		return -ENXIO;
	}

	/*
	 * Since translated addresses include the interleaving offsets, align
	 * the range to 256 MB.
	 */
	hpa_range.start = ALIGN_DOWN(hpa_range.start, SZ_256M);
	hpa_range.end = ALIGN(hpa_range.end, SZ_256M) - 1;

	len = range_len(&ctx->hpa_range);
	spa_len = range_len(&hpa_range);
	if (!len || !spa_len || spa_len % len) {
		dev_dbg(cxld->dev.parent,
			"CXL address translation: HPA range not contiguous: %#llx-%#llx:%#llx-%#llx(%s)\n",
			hpa_range.start, hpa_range.end, ctx->hpa_range.start,
			ctx->hpa_range.end, dev_name(&cxld->dev));
		return -ENXIO;
	}

	ways = spa_len / len;
	gran = SZ_256;

	/*
	 * Determine interleave granularity
	 *
	 * Note: The position of the chunk from one interleaving block to the
	 * next may vary and thus cannot be considered constant. Address offsets
	 * larger than the interleaving block size cannot be used to calculate
	 * the granularity.
	 */
	if (ways > 1) {
		while (gran <= SZ_16M) {
			addr = prm_cxl_dpa_spa(pci_dev, base + gran);
			if (addr != base_spa + gran)
				break;
			gran <<= 1;
		}
	}

	if (gran > SZ_16M) {
		dev_dbg(cxld->dev.parent,
			"CXL address translation: Cannot determine granularity: %#llx-%#llx:%#llx-%#llx(%s)\n",
			hpa_range.start, hpa_range.end, ctx->hpa_range.start,
			ctx->hpa_range.end, dev_name(&cxld->dev));
		return -ENXIO;
	}

	/*
	 * The current kernel implementation does not support endpoint
	 * setup with Normalized Addressing. It only translates an
	 * endpoint's DPA to the SPA range of the host bridge.
	 * Therefore, the endpoint address range cannot be determined,
	 * making a non-auto setup impossible. If a decoder requires
	 * address translation, reprogramming should be disabled and
	 * the decoder locked.
	 *
	 * The BIOS, however, provides all the necessary address
	 * translation data, which the kernel can use to reconfigure
	 * endpoint decoders with normalized addresses. Locking the
	 * decoders in the BIOS would prevent a capable kernel (or
	 * other operating systems) from shutting down auto-generated
	 * regions and managing resources dynamically.
	 *
	 * Indicate that Normalized Addressing is enabled.
	 */
	cxld->flags |= CXL_DECODER_F_LOCK;
	cxld->flags |= CXL_DECODER_F_NORMALIZED_ADDRESSING;

	ctx->hpa_range = hpa_range;
	ctx->interleave_ways = ways;
	ctx->interleave_granularity = gran;

	dev_dbg(&cxld->dev,
		"address mapping found for %s (hpa -> spa): %#llx+%#llx -> %#llx+%#llx ways:%d granularity:%d\n",
		dev_name(cxlmd->dev.parent), base, len, hpa_range.start,
		spa_len, ways, gran);

	return 0;
}

void cxl_setup_prm_address_translation(struct cxl_root *cxl_root)
{
	struct device *host = cxl_root->port.uport_dev;
	u64 spa;
	struct prm_cxl_dpa_spa_data data = { .spa = &spa };
	int rc;

	/*
	 * Applies only to PCIe Host Bridges which are children of the CXL Root
	 * Device (HID=“ACPI0017”). Check this and drop cxl_test instances.
	 */
	if (!acpi_match_device(host->driver->acpi_match_table, host))
		return;

	/* Check kernel (-EOPNOTSUPP) and firmware support (-ENODEV) */
	rc = acpi_call_prm_handler(prm_cxl_dpa_spa_guid, &data);
	if (rc == -EOPNOTSUPP || rc == -ENODEV)
		return;

	cxl_root->ops.translation_setup_root = cxl_prm_setup_root;
}
EXPORT_SYMBOL_NS_GPL(cxl_setup_prm_address_translation, "CXL");
