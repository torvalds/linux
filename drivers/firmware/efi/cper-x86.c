// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2018, Advanced Micro Devices, Inc.

#include <linux/cper.h>

/*
 * We don't need a "CPER_IA" prefix since these are all locally defined.
 * This will save us a lot of line space.
 */
#define VALID_LAPIC_ID			BIT_ULL(0)
#define VALID_CPUID_INFO		BIT_ULL(1)
#define VALID_PROC_ERR_INFO_NUM(bits)	(((bits) & GENMASK_ULL(7, 2)) >> 2)

#define INFO_ERR_STRUCT_TYPE_CACHE					\
	GUID_INIT(0xA55701F5, 0xE3EF, 0x43DE, 0xAC, 0x72, 0x24, 0x9B,	\
		  0x57, 0x3F, 0xAD, 0x2C)
#define INFO_ERR_STRUCT_TYPE_TLB					\
	GUID_INIT(0xFC06B535, 0x5E1F, 0x4562, 0x9F, 0x25, 0x0A, 0x3B,	\
		  0x9A, 0xDB, 0x63, 0xC3)
#define INFO_ERR_STRUCT_TYPE_BUS					\
	GUID_INIT(0x1CF3F8B3, 0xC5B1, 0x49a2, 0xAA, 0x59, 0x5E, 0xEF,	\
		  0x92, 0xFF, 0xA6, 0x3C)
#define INFO_ERR_STRUCT_TYPE_MS						\
	GUID_INIT(0x48AB7F57, 0xDC34, 0x4f6c, 0xA7, 0xD3, 0xB0, 0xB5,	\
		  0xB0, 0xA7, 0x43, 0x14)

#define INFO_VALID_CHECK_INFO		BIT_ULL(0)
#define INFO_VALID_TARGET_ID		BIT_ULL(1)
#define INFO_VALID_REQUESTOR_ID		BIT_ULL(2)
#define INFO_VALID_RESPONDER_ID		BIT_ULL(3)
#define INFO_VALID_IP			BIT_ULL(4)

enum err_types {
	ERR_TYPE_CACHE = 0,
	ERR_TYPE_TLB,
	ERR_TYPE_BUS,
	ERR_TYPE_MS,
	N_ERR_TYPES
};

static enum err_types cper_get_err_type(const guid_t *err_type)
{
	if (guid_equal(err_type, &INFO_ERR_STRUCT_TYPE_CACHE))
		return ERR_TYPE_CACHE;
	else if (guid_equal(err_type, &INFO_ERR_STRUCT_TYPE_TLB))
		return ERR_TYPE_TLB;
	else if (guid_equal(err_type, &INFO_ERR_STRUCT_TYPE_BUS))
		return ERR_TYPE_BUS;
	else if (guid_equal(err_type, &INFO_ERR_STRUCT_TYPE_MS))
		return ERR_TYPE_MS;
	else
		return N_ERR_TYPES;
}

void cper_print_proc_ia(const char *pfx, const struct cper_sec_proc_ia *proc)
{
	int i;
	struct cper_ia_err_info *err_info;
	char newpfx[64];
	u8 err_type;

	if (proc->validation_bits & VALID_LAPIC_ID)
		printk("%sLocal APIC_ID: 0x%llx\n", pfx, proc->lapic_id);

	if (proc->validation_bits & VALID_CPUID_INFO) {
		printk("%sCPUID Info:\n", pfx);
		print_hex_dump(pfx, "", DUMP_PREFIX_OFFSET, 16, 4, proc->cpuid,
			       sizeof(proc->cpuid), 0);
	}

	snprintf(newpfx, sizeof(newpfx), "%s ", pfx);

	err_info = (struct cper_ia_err_info *)(proc + 1);
	for (i = 0; i < VALID_PROC_ERR_INFO_NUM(proc->validation_bits); i++) {
		printk("%sError Information Structure %d:\n", pfx, i);

		err_type = cper_get_err_type(&err_info->err_type);
		printk("%sError Structure Type: %s\n", newpfx,
		       err_type < ARRAY_SIZE(cper_proc_error_type_strs) ?
		       cper_proc_error_type_strs[err_type] : "unknown");

		if (err_type >= N_ERR_TYPES) {
			printk("%sError Structure Type: %pUl\n", newpfx,
			       &err_info->err_type);
		}

		if (err_info->validation_bits & INFO_VALID_CHECK_INFO) {
			printk("%sCheck Information: 0x%016llx\n", newpfx,
			       err_info->check_info);
		}

		if (err_info->validation_bits & INFO_VALID_TARGET_ID) {
			printk("%sTarget Identifier: 0x%016llx\n",
			       newpfx, err_info->target_id);
		}

		if (err_info->validation_bits & INFO_VALID_REQUESTOR_ID) {
			printk("%sRequestor Identifier: 0x%016llx\n",
			       newpfx, err_info->requestor_id);
		}

		if (err_info->validation_bits & INFO_VALID_RESPONDER_ID) {
			printk("%sResponder Identifier: 0x%016llx\n",
			       newpfx, err_info->responder_id);
		}

		if (err_info->validation_bits & INFO_VALID_IP) {
			printk("%sInstruction Pointer: 0x%016llx\n",
			       newpfx, err_info->ip);
		}

		err_info++;
	}
}
