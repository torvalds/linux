/*
 * UEFI Common Platform Error Record (CPER) support
 *
 * Copyright (C) 2017, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/time.h>
#include <linux/cper.h>
#include <linux/dmi.h>
#include <linux/acpi.h>
#include <linux/pci.h>
#include <linux/aer.h>
#include <linux/printk.h>
#include <linux/bcd.h>
#include <acpi/ghes.h>
#include <ras/ras_event.h>

#define INDENT_SP	" "

static const char * const arm_reg_ctx_strs[] = {
	"AArch32 general purpose registers",
	"AArch32 EL1 context registers",
	"AArch32 EL2 context registers",
	"AArch32 secure context registers",
	"AArch64 general purpose registers",
	"AArch64 EL1 context registers",
	"AArch64 EL2 context registers",
	"AArch64 EL3 context registers",
	"Misc. system register structure",
};

void cper_print_proc_arm(const char *pfx,
			 const struct cper_sec_proc_arm *proc)
{
	int i, len, max_ctx_type;
	struct cper_arm_err_info *err_info;
	struct cper_arm_ctx_info *ctx_info;
	char newpfx[64];

	printk("%sMIDR: 0x%016llx\n", pfx, proc->midr);

	len = proc->section_length - (sizeof(*proc) +
		proc->err_info_num * (sizeof(*err_info)));
	if (len < 0) {
		printk("%ssection length: %d\n", pfx, proc->section_length);
		printk("%ssection length is too small\n", pfx);
		printk("%sfirmware-generated error record is incorrect\n", pfx);
		printk("%sERR_INFO_NUM is %d\n", pfx, proc->err_info_num);
		return;
	}

	if (proc->validation_bits & CPER_ARM_VALID_MPIDR)
		printk("%sMultiprocessor Affinity Register (MPIDR): 0x%016llx\n",
			pfx, proc->mpidr);

	if (proc->validation_bits & CPER_ARM_VALID_AFFINITY_LEVEL)
		printk("%serror affinity level: %d\n", pfx,
			proc->affinity_level);

	if (proc->validation_bits & CPER_ARM_VALID_RUNNING_STATE) {
		printk("%srunning state: 0x%x\n", pfx, proc->running_state);
		printk("%sPower State Coordination Interface state: %d\n",
			pfx, proc->psci_state);
	}

	snprintf(newpfx, sizeof(newpfx), "%s%s", pfx, INDENT_SP);

	err_info = (struct cper_arm_err_info *)(proc + 1);
	for (i = 0; i < proc->err_info_num; i++) {
		printk("%sError info structure %d:\n", pfx, i);

		printk("%snum errors: %d\n", pfx, err_info->multiple_error + 1);

		if (err_info->validation_bits & CPER_ARM_INFO_VALID_FLAGS) {
			if (err_info->flags & CPER_ARM_INFO_FLAGS_FIRST)
				printk("%sfirst error captured\n", newpfx);
			if (err_info->flags & CPER_ARM_INFO_FLAGS_LAST)
				printk("%slast error captured\n", newpfx);
			if (err_info->flags & CPER_ARM_INFO_FLAGS_PROPAGATED)
				printk("%spropagated error captured\n",
				       newpfx);
			if (err_info->flags & CPER_ARM_INFO_FLAGS_OVERFLOW)
				printk("%soverflow occurred, error info is incomplete\n",
				       newpfx);
		}

		printk("%serror_type: %d, %s\n", newpfx, err_info->type,
			err_info->type < ARRAY_SIZE(cper_proc_error_type_strs) ?
			cper_proc_error_type_strs[err_info->type] : "unknown");
		if (err_info->validation_bits & CPER_ARM_INFO_VALID_ERR_INFO)
			printk("%serror_info: 0x%016llx\n", newpfx,
			       err_info->error_info);
		if (err_info->validation_bits & CPER_ARM_INFO_VALID_VIRT_ADDR)
			printk("%svirtual fault address: 0x%016llx\n",
				newpfx, err_info->virt_fault_addr);
		if (err_info->validation_bits & CPER_ARM_INFO_VALID_PHYSICAL_ADDR)
			printk("%sphysical fault address: 0x%016llx\n",
				newpfx, err_info->physical_fault_addr);
		err_info += 1;
	}

	ctx_info = (struct cper_arm_ctx_info *)err_info;
	max_ctx_type = ARRAY_SIZE(arm_reg_ctx_strs) - 1;
	for (i = 0; i < proc->context_info_num; i++) {
		int size = sizeof(*ctx_info) + ctx_info->size;

		printk("%sContext info structure %d:\n", pfx, i);
		if (len < size) {
			printk("%ssection length is too small\n", newpfx);
			printk("%sfirmware-generated error record is incorrect\n", pfx);
			return;
		}
		if (ctx_info->type > max_ctx_type) {
			printk("%sInvalid context type: %d (max: %d)\n",
				newpfx, ctx_info->type, max_ctx_type);
			return;
		}
		printk("%sregister context type: %s\n", newpfx,
			arm_reg_ctx_strs[ctx_info->type]);
		print_hex_dump(newpfx, "", DUMP_PREFIX_OFFSET, 16, 4,
				(ctx_info + 1), ctx_info->size, 0);
		len -= size;
		ctx_info = (struct cper_arm_ctx_info *)((long)ctx_info + size);
	}

	if (len > 0) {
		printk("%sVendor specific error info has %u bytes:\n", pfx,
		       len);
		print_hex_dump(newpfx, "", DUMP_PREFIX_OFFSET, 16, 4, ctx_info,
				len, true);
	}
}
