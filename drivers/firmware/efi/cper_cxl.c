// SPDX-License-Identifier: GPL-2.0-only
/*
 * UEFI Common Platform Error Record (CPER) support for CXL Section.
 *
 * Copyright (C) 2022 Advanced Micro Devices, Inc.
 *
 * Author: Smita Koralahalli <Smita.KoralahalliChannabasappa@amd.com>
 */

#include <linux/cper.h>
#include "cper_cxl.h"

#define PROT_ERR_VALID_AGENT_TYPE		BIT_ULL(0)
#define PROT_ERR_VALID_AGENT_ADDRESS		BIT_ULL(1)
#define PROT_ERR_VALID_DEVICE_ID		BIT_ULL(2)
#define PROT_ERR_VALID_SERIAL_NUMBER		BIT_ULL(3)
#define PROT_ERR_VALID_CAPABILITY		BIT_ULL(4)
#define PROT_ERR_VALID_DVSEC			BIT_ULL(5)
#define PROT_ERR_VALID_ERROR_LOG		BIT_ULL(6)

/* CXL RAS Capability Structure, CXL v3.0 sec 8.2.4.16 */
struct cxl_ras_capability_regs {
	u32 uncor_status;
	u32 uncor_mask;
	u32 uncor_severity;
	u32 cor_status;
	u32 cor_mask;
	u32 cap_control;
	u32 header_log[16];
};

static const char * const prot_err_agent_type_strs[] = {
	"Restricted CXL Device",
	"Restricted CXL Host Downstream Port",
	"CXL Device",
	"CXL Logical Device",
	"CXL Fabric Manager managed Logical Device",
	"CXL Root Port",
	"CXL Downstream Switch Port",
	"CXL Upstream Switch Port",
};

/*
 * The layout of the enumeration and the values matches CXL Agent Type
 * field in the UEFI 2.10 Section N.2.13,
 */
enum {
	RCD,	/* Restricted CXL Device */
	RCH_DP,	/* Restricted CXL Host Downstream Port */
	DEVICE,	/* CXL Device */
	LD,	/* CXL Logical Device */
	FMLD,	/* CXL Fabric Manager managed Logical Device */
	RP,	/* CXL Root Port */
	DSP,	/* CXL Downstream Switch Port */
	USP,	/* CXL Upstream Switch Port */
};

void cper_print_prot_err(const char *pfx, const struct cper_sec_prot_err *prot_err)
{
	if (prot_err->valid_bits & PROT_ERR_VALID_AGENT_TYPE)
		pr_info("%s agent_type: %d, %s\n", pfx, prot_err->agent_type,
			prot_err->agent_type < ARRAY_SIZE(prot_err_agent_type_strs)
			? prot_err_agent_type_strs[prot_err->agent_type]
			: "unknown");

	if (prot_err->valid_bits & PROT_ERR_VALID_AGENT_ADDRESS) {
		switch (prot_err->agent_type) {
		/*
		 * According to UEFI 2.10 Section N.2.13, the term CXL Device
		 * is used to refer to Restricted CXL Device, CXL Device, CXL
		 * Logical Device or a CXL Fabric Manager Managed Logical
		 * Device.
		 */
		case RCD:
		case DEVICE:
		case LD:
		case FMLD:
		case RP:
		case DSP:
		case USP:
			pr_info("%s agent_address: %04x:%02x:%02x.%x\n",
				pfx, prot_err->agent_addr.segment,
				prot_err->agent_addr.bus,
				prot_err->agent_addr.device,
				prot_err->agent_addr.function);
			break;
		case RCH_DP:
			pr_info("%s rcrb_base_address: 0x%016llx\n", pfx,
				prot_err->agent_addr.rcrb_base_addr);
			break;
		default:
			break;
		}
	}

	if (prot_err->valid_bits & PROT_ERR_VALID_DEVICE_ID) {
		const __u8 *class_code;

		switch (prot_err->agent_type) {
		case RCD:
		case DEVICE:
		case LD:
		case FMLD:
		case RP:
		case DSP:
		case USP:
			pr_info("%s slot: %d\n", pfx,
				prot_err->device_id.slot >> CPER_PCIE_SLOT_SHIFT);
			pr_info("%s vendor_id: 0x%04x, device_id: 0x%04x\n",
				pfx, prot_err->device_id.vendor_id,
				prot_err->device_id.device_id);
			pr_info("%s sub_vendor_id: 0x%04x, sub_device_id: 0x%04x\n",
				pfx, prot_err->device_id.subsystem_vendor_id,
				prot_err->device_id.subsystem_id);
			class_code = prot_err->device_id.class_code;
			pr_info("%s class_code: %02x%02x\n", pfx,
				class_code[1], class_code[0]);
			break;
		default:
			break;
		}
	}

	if (prot_err->valid_bits & PROT_ERR_VALID_SERIAL_NUMBER) {
		switch (prot_err->agent_type) {
		case RCD:
		case DEVICE:
		case LD:
		case FMLD:
			pr_info("%s lower_dw: 0x%08x, upper_dw: 0x%08x\n", pfx,
				prot_err->dev_serial_num.lower_dw,
				prot_err->dev_serial_num.upper_dw);
			break;
		default:
			break;
		}
	}

	if (prot_err->valid_bits & PROT_ERR_VALID_CAPABILITY) {
		switch (prot_err->agent_type) {
		case RCD:
		case DEVICE:
		case LD:
		case FMLD:
		case RP:
		case DSP:
		case USP:
			print_hex_dump(pfx, "", DUMP_PREFIX_OFFSET, 16, 4,
				       prot_err->capability,
				       sizeof(prot_err->capability), 0);
			break;
		default:
			break;
		}
	}

	if (prot_err->valid_bits & PROT_ERR_VALID_DVSEC) {
		pr_info("%s DVSEC length: 0x%04x\n", pfx, prot_err->dvsec_len);

		pr_info("%s CXL DVSEC:\n", pfx);
		print_hex_dump(pfx, "", DUMP_PREFIX_OFFSET, 16, 4, (prot_err + 1),
			       prot_err->dvsec_len, 0);
	}

	if (prot_err->valid_bits & PROT_ERR_VALID_ERROR_LOG) {
		size_t size = sizeof(*prot_err) + prot_err->dvsec_len;
		struct cxl_ras_capability_regs *cxl_ras;

		pr_info("%s Error log length: 0x%04x\n", pfx, prot_err->err_len);

		pr_info("%s CXL Error Log:\n", pfx);
		cxl_ras = (struct cxl_ras_capability_regs *)((long)prot_err + size);
		pr_info("%s cxl_ras_uncor_status: 0x%08x", pfx,
			cxl_ras->uncor_status);
		pr_info("%s cxl_ras_uncor_mask: 0x%08x\n", pfx,
			cxl_ras->uncor_mask);
		pr_info("%s cxl_ras_uncor_severity: 0x%08x\n", pfx,
			cxl_ras->uncor_severity);
		pr_info("%s cxl_ras_cor_status: 0x%08x", pfx,
			cxl_ras->cor_status);
		pr_info("%s cxl_ras_cor_mask: 0x%08x\n", pfx,
			cxl_ras->cor_mask);
		pr_info("%s cap_control: 0x%08x\n", pfx,
			cxl_ras->cap_control);
		pr_info("%s Header Log Registers:\n", pfx);
		print_hex_dump(pfx, "", DUMP_PREFIX_OFFSET, 16, 4, cxl_ras->header_log,
			       sizeof(cxl_ras->header_log), 0);
	}
}
