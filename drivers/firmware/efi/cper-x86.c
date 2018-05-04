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

#define INFO_VALID_CHECK_INFO		BIT_ULL(0)
#define INFO_VALID_TARGET_ID		BIT_ULL(1)
#define INFO_VALID_REQUESTOR_ID		BIT_ULL(2)
#define INFO_VALID_RESPONDER_ID		BIT_ULL(3)
#define INFO_VALID_IP			BIT_ULL(4)

void cper_print_proc_ia(const char *pfx, const struct cper_sec_proc_ia *proc)
{
	int i;
	struct cper_ia_err_info *err_info;
	char newpfx[64];

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

		printk("%sError Structure Type: %pUl\n", newpfx,
		       &err_info->err_type);

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
