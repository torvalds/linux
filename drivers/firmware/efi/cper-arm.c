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

static const char * const arm_err_trans_type_strs[] = {
	"Instruction",
	"Data Access",
	"Generic",
};

static const char * const arm_bus_err_op_strs[] = {
	"Generic error (type cannot be determined)",
	"Generic read (type of instruction or data request cannot be determined)",
	"Generic write (type of instruction of data request cannot be determined)",
	"Data read",
	"Data write",
	"Instruction fetch",
	"Prefetch",
};

static const char * const arm_cache_err_op_strs[] = {
	"Generic error (type cannot be determined)",
	"Generic read (type of instruction or data request cannot be determined)",
	"Generic write (type of instruction of data request cannot be determined)",
	"Data read",
	"Data write",
	"Instruction fetch",
	"Prefetch",
	"Eviction",
	"Snooping (processor initiated a cache snoop that resulted in an error)",
	"Snooped (processor raised a cache error caused by another processor or device snooping its cache)",
	"Management",
};

static const char * const arm_tlb_err_op_strs[] = {
	"Generic error (type cannot be determined)",
	"Generic read (type of instruction or data request cannot be determined)",
	"Generic write (type of instruction of data request cannot be determined)",
	"Data read",
	"Data write",
	"Instruction fetch",
	"Prefetch",
	"Local management operation (processor initiated a TLB management operation that resulted in an error)",
	"External management operation (processor raised a TLB error caused by another processor or device broadcasting TLB operations)",
};

static const char * const arm_bus_err_part_type_strs[] = {
	"Local processor originated request",
	"Local processor responded to request",
	"Local processor observed",
	"Generic",
};

static const char * const arm_bus_err_addr_space_strs[] = {
	"External Memory Access",
	"Internal Memory Access",
	"Unknown",
	"Device Memory Access",
};

static void cper_print_arm_err_info(const char *pfx, u32 type,
				    u64 error_info)
{
	u8 trans_type, op_type, level, participation_type, address_space;
	u16 mem_attributes;
	bool proc_context_corrupt, corrected, precise_pc, restartable_pc;
	bool time_out, access_mode;

	/* If the type is unknown, bail. */
	if (type > CPER_ARM_MAX_TYPE)
		return;

	/*
	 * Vendor type errors have error information values that are vendor
	 * specific.
	 */
	if (type == CPER_ARM_VENDOR_ERROR)
		return;

	if (error_info & CPER_ARM_ERR_VALID_TRANSACTION_TYPE) {
		trans_type = ((error_info >> CPER_ARM_ERR_TRANSACTION_SHIFT)
			      & CPER_ARM_ERR_TRANSACTION_MASK);
		if (trans_type < ARRAY_SIZE(arm_err_trans_type_strs)) {
			printk("%stransaction type: %s\n", pfx,
			       arm_err_trans_type_strs[trans_type]);
		}
	}

	if (error_info & CPER_ARM_ERR_VALID_OPERATION_TYPE) {
		op_type = ((error_info >> CPER_ARM_ERR_OPERATION_SHIFT)
			   & CPER_ARM_ERR_OPERATION_MASK);
		switch (type) {
		case CPER_ARM_CACHE_ERROR:
			if (op_type < ARRAY_SIZE(arm_cache_err_op_strs)) {
				printk("%soperation type: %s\n", pfx,
				       arm_cache_err_op_strs[op_type]);
			}
			break;
		case CPER_ARM_TLB_ERROR:
			if (op_type < ARRAY_SIZE(arm_tlb_err_op_strs)) {
				printk("%soperation type: %s\n", pfx,
				       arm_tlb_err_op_strs[op_type]);
			}
			break;
		case CPER_ARM_BUS_ERROR:
			if (op_type < ARRAY_SIZE(arm_bus_err_op_strs)) {
				printk("%soperation type: %s\n", pfx,
				       arm_bus_err_op_strs[op_type]);
			}
			break;
		}
	}

	if (error_info & CPER_ARM_ERR_VALID_LEVEL) {
		level = ((error_info >> CPER_ARM_ERR_LEVEL_SHIFT)
			 & CPER_ARM_ERR_LEVEL_MASK);
		switch (type) {
		case CPER_ARM_CACHE_ERROR:
			printk("%scache level: %d\n", pfx, level);
			break;
		case CPER_ARM_TLB_ERROR:
			printk("%sTLB level: %d\n", pfx, level);
			break;
		case CPER_ARM_BUS_ERROR:
			printk("%saffinity level at which the bus error occurred: %d\n",
			       pfx, level);
			break;
		}
	}

	if (error_info & CPER_ARM_ERR_VALID_PROC_CONTEXT_CORRUPT) {
		proc_context_corrupt = ((error_info >> CPER_ARM_ERR_PC_CORRUPT_SHIFT)
					& CPER_ARM_ERR_PC_CORRUPT_MASK);
		if (proc_context_corrupt)
			printk("%sprocessor context corrupted\n", pfx);
		else
			printk("%sprocessor context not corrupted\n", pfx);
	}

	if (error_info & CPER_ARM_ERR_VALID_CORRECTED) {
		corrected = ((error_info >> CPER_ARM_ERR_CORRECTED_SHIFT)
			     & CPER_ARM_ERR_CORRECTED_MASK);
		if (corrected)
			printk("%sthe error has been corrected\n", pfx);
		else
			printk("%sthe error has not been corrected\n", pfx);
	}

	if (error_info & CPER_ARM_ERR_VALID_PRECISE_PC) {
		precise_pc = ((error_info >> CPER_ARM_ERR_PRECISE_PC_SHIFT)
			      & CPER_ARM_ERR_PRECISE_PC_MASK);
		if (precise_pc)
			printk("%sPC is precise\n", pfx);
		else
			printk("%sPC is imprecise\n", pfx);
	}

	if (error_info & CPER_ARM_ERR_VALID_RESTARTABLE_PC) {
		restartable_pc = ((error_info >> CPER_ARM_ERR_RESTARTABLE_PC_SHIFT)
				  & CPER_ARM_ERR_RESTARTABLE_PC_MASK);
		if (restartable_pc)
			printk("%sProgram execution can be restarted reliably at the PC associated with the error.\n", pfx);
	}

	/* The rest of the fields are specific to bus errors */
	if (type != CPER_ARM_BUS_ERROR)
		return;

	if (error_info & CPER_ARM_ERR_VALID_PARTICIPATION_TYPE) {
		participation_type = ((error_info >> CPER_ARM_ERR_PARTICIPATION_TYPE_SHIFT)
				      & CPER_ARM_ERR_PARTICIPATION_TYPE_MASK);
		if (participation_type < ARRAY_SIZE(arm_bus_err_part_type_strs)) {
			printk("%sparticipation type: %s\n", pfx,
			       arm_bus_err_part_type_strs[participation_type]);
		}
	}

	if (error_info & CPER_ARM_ERR_VALID_TIME_OUT) {
		time_out = ((error_info >> CPER_ARM_ERR_TIME_OUT_SHIFT)
			    & CPER_ARM_ERR_TIME_OUT_MASK);
		if (time_out)
			printk("%srequest timed out\n", pfx);
	}

	if (error_info & CPER_ARM_ERR_VALID_ADDRESS_SPACE) {
		address_space = ((error_info >> CPER_ARM_ERR_ADDRESS_SPACE_SHIFT)
				 & CPER_ARM_ERR_ADDRESS_SPACE_MASK);
		if (address_space < ARRAY_SIZE(arm_bus_err_addr_space_strs)) {
			printk("%saddress space: %s\n", pfx,
			       arm_bus_err_addr_space_strs[address_space]);
		}
	}

	if (error_info & CPER_ARM_ERR_VALID_MEM_ATTRIBUTES) {
		mem_attributes = ((error_info >> CPER_ARM_ERR_MEM_ATTRIBUTES_SHIFT)
				  & CPER_ARM_ERR_MEM_ATTRIBUTES_MASK);
		printk("%smemory access attributes:0x%x\n", pfx, mem_attributes);
	}

	if (error_info & CPER_ARM_ERR_VALID_ACCESS_MODE) {
		access_mode = ((error_info >> CPER_ARM_ERR_ACCESS_MODE_SHIFT)
			       & CPER_ARM_ERR_ACCESS_MODE_MASK);
		if (access_mode)
			printk("%saccess mode: normal\n", pfx);
		else
			printk("%saccess mode: secure\n", pfx);
	}
}

void cper_print_proc_arm(const char *pfx,
			 const struct cper_sec_proc_arm *proc)
{
	int i, len, max_ctx_type;
	struct cper_arm_err_info *err_info;
	struct cper_arm_ctx_info *ctx_info;
	char newpfx[64], infopfx[64];

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

	snprintf(newpfx, sizeof(newpfx), "%s ", pfx);

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
		if (err_info->validation_bits & CPER_ARM_INFO_VALID_ERR_INFO) {
			printk("%serror_info: 0x%016llx\n", newpfx,
			       err_info->error_info);
			snprintf(infopfx, sizeof(infopfx), "%s ", newpfx);
			cper_print_arm_err_info(infopfx, err_info->type,
						err_info->error_info);
		}
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
