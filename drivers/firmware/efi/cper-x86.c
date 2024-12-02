// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2018, Advanced Micro Devices, Inc.

#include <linux/cper.h>
#include <linux/acpi.h>

/*
 * We don't need a "CPER_IA" prefix since these are all locally defined.
 * This will save us a lot of line space.
 */
#define VALID_LAPIC_ID			BIT_ULL(0)
#define VALID_CPUID_INFO		BIT_ULL(1)
#define VALID_PROC_ERR_INFO_NUM(bits)	(((bits) & GENMASK_ULL(7, 2)) >> 2)
#define VALID_PROC_CXT_INFO_NUM(bits)	(((bits) & GENMASK_ULL(13, 8)) >> 8)

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

#define CHECK_VALID_TRANS_TYPE		BIT_ULL(0)
#define CHECK_VALID_OPERATION		BIT_ULL(1)
#define CHECK_VALID_LEVEL		BIT_ULL(2)
#define CHECK_VALID_PCC			BIT_ULL(3)
#define CHECK_VALID_UNCORRECTED		BIT_ULL(4)
#define CHECK_VALID_PRECISE_IP		BIT_ULL(5)
#define CHECK_VALID_RESTARTABLE_IP	BIT_ULL(6)
#define CHECK_VALID_OVERFLOW		BIT_ULL(7)

#define CHECK_VALID_BUS_PART_TYPE	BIT_ULL(8)
#define CHECK_VALID_BUS_TIME_OUT	BIT_ULL(9)
#define CHECK_VALID_BUS_ADDR_SPACE	BIT_ULL(10)

#define CHECK_VALID_BITS(check)		(((check) & GENMASK_ULL(15, 0)))
#define CHECK_TRANS_TYPE(check)		(((check) & GENMASK_ULL(17, 16)) >> 16)
#define CHECK_OPERATION(check)		(((check) & GENMASK_ULL(21, 18)) >> 18)
#define CHECK_LEVEL(check)		(((check) & GENMASK_ULL(24, 22)) >> 22)
#define CHECK_PCC			BIT_ULL(25)
#define CHECK_UNCORRECTED		BIT_ULL(26)
#define CHECK_PRECISE_IP		BIT_ULL(27)
#define CHECK_RESTARTABLE_IP		BIT_ULL(28)
#define CHECK_OVERFLOW			BIT_ULL(29)

#define CHECK_BUS_PART_TYPE(check)	(((check) & GENMASK_ULL(31, 30)) >> 30)
#define CHECK_BUS_TIME_OUT		BIT_ULL(32)
#define CHECK_BUS_ADDR_SPACE(check)	(((check) & GENMASK_ULL(34, 33)) >> 33)

#define CHECK_VALID_MS_ERR_TYPE		BIT_ULL(0)
#define CHECK_VALID_MS_PCC		BIT_ULL(1)
#define CHECK_VALID_MS_UNCORRECTED	BIT_ULL(2)
#define CHECK_VALID_MS_PRECISE_IP	BIT_ULL(3)
#define CHECK_VALID_MS_RESTARTABLE_IP	BIT_ULL(4)
#define CHECK_VALID_MS_OVERFLOW		BIT_ULL(5)

#define CHECK_MS_ERR_TYPE(check)	(((check) & GENMASK_ULL(18, 16)) >> 16)
#define CHECK_MS_PCC			BIT_ULL(19)
#define CHECK_MS_UNCORRECTED		BIT_ULL(20)
#define CHECK_MS_PRECISE_IP		BIT_ULL(21)
#define CHECK_MS_RESTARTABLE_IP		BIT_ULL(22)
#define CHECK_MS_OVERFLOW		BIT_ULL(23)

#define CTX_TYPE_MSR			1
#define CTX_TYPE_MMREG			7

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

static const char * const ia_check_trans_type_strs[] = {
	"Instruction",
	"Data Access",
	"Generic",
};

static const char * const ia_check_op_strs[] = {
	"generic error",
	"generic read",
	"generic write",
	"data read",
	"data write",
	"instruction fetch",
	"prefetch",
	"eviction",
	"snoop",
};

static const char * const ia_check_bus_part_type_strs[] = {
	"Local Processor originated request",
	"Local Processor responded to request",
	"Local Processor observed",
	"Generic",
};

static const char * const ia_check_bus_addr_space_strs[] = {
	"Memory Access",
	"Reserved",
	"I/O",
	"Other Transaction",
};

static const char * const ia_check_ms_error_type_strs[] = {
	"No Error",
	"Unclassified",
	"Microcode ROM Parity Error",
	"External Error",
	"FRC Error",
	"Internal Unclassified",
};

static const char * const ia_reg_ctx_strs[] = {
	"Unclassified Data",
	"MSR Registers (Machine Check and other MSRs)",
	"32-bit Mode Execution Context",
	"64-bit Mode Execution Context",
	"FXSAVE Context",
	"32-bit Mode Debug Registers (DR0-DR7)",
	"64-bit Mode Debug Registers (DR0-DR7)",
	"Memory Mapped Registers",
};

static inline void print_bool(char *str, const char *pfx, u64 check, u64 bit)
{
	printk("%s%s: %s\n", pfx, str, (check & bit) ? "true" : "false");
}

static void print_err_info_ms(const char *pfx, u16 validation_bits, u64 check)
{
	if (validation_bits & CHECK_VALID_MS_ERR_TYPE) {
		u8 err_type = CHECK_MS_ERR_TYPE(check);

		printk("%sError Type: %u, %s\n", pfx, err_type,
		       err_type < ARRAY_SIZE(ia_check_ms_error_type_strs) ?
		       ia_check_ms_error_type_strs[err_type] : "unknown");
	}

	if (validation_bits & CHECK_VALID_MS_PCC)
		print_bool("Processor Context Corrupt", pfx, check, CHECK_MS_PCC);

	if (validation_bits & CHECK_VALID_MS_UNCORRECTED)
		print_bool("Uncorrected", pfx, check, CHECK_MS_UNCORRECTED);

	if (validation_bits & CHECK_VALID_MS_PRECISE_IP)
		print_bool("Precise IP", pfx, check, CHECK_MS_PRECISE_IP);

	if (validation_bits & CHECK_VALID_MS_RESTARTABLE_IP)
		print_bool("Restartable IP", pfx, check, CHECK_MS_RESTARTABLE_IP);

	if (validation_bits & CHECK_VALID_MS_OVERFLOW)
		print_bool("Overflow", pfx, check, CHECK_MS_OVERFLOW);
}

static void print_err_info(const char *pfx, u8 err_type, u64 check)
{
	u16 validation_bits = CHECK_VALID_BITS(check);

	/*
	 * The MS Check structure varies a lot from the others, so use a
	 * separate function for decoding.
	 */
	if (err_type == ERR_TYPE_MS)
		return print_err_info_ms(pfx, validation_bits, check);

	if (validation_bits & CHECK_VALID_TRANS_TYPE) {
		u8 trans_type = CHECK_TRANS_TYPE(check);

		printk("%sTransaction Type: %u, %s\n", pfx, trans_type,
		       trans_type < ARRAY_SIZE(ia_check_trans_type_strs) ?
		       ia_check_trans_type_strs[trans_type] : "unknown");
	}

	if (validation_bits & CHECK_VALID_OPERATION) {
		u8 op = CHECK_OPERATION(check);

		/*
		 * CACHE has more operation types than TLB or BUS, though the
		 * name and the order are the same.
		 */
		u8 max_ops = (err_type == ERR_TYPE_CACHE) ? 9 : 7;

		printk("%sOperation: %u, %s\n", pfx, op,
		       op < max_ops ? ia_check_op_strs[op] : "unknown");
	}

	if (validation_bits & CHECK_VALID_LEVEL)
		printk("%sLevel: %llu\n", pfx, CHECK_LEVEL(check));

	if (validation_bits & CHECK_VALID_PCC)
		print_bool("Processor Context Corrupt", pfx, check, CHECK_PCC);

	if (validation_bits & CHECK_VALID_UNCORRECTED)
		print_bool("Uncorrected", pfx, check, CHECK_UNCORRECTED);

	if (validation_bits & CHECK_VALID_PRECISE_IP)
		print_bool("Precise IP", pfx, check, CHECK_PRECISE_IP);

	if (validation_bits & CHECK_VALID_RESTARTABLE_IP)
		print_bool("Restartable IP", pfx, check, CHECK_RESTARTABLE_IP);

	if (validation_bits & CHECK_VALID_OVERFLOW)
		print_bool("Overflow", pfx, check, CHECK_OVERFLOW);

	if (err_type != ERR_TYPE_BUS)
		return;

	if (validation_bits & CHECK_VALID_BUS_PART_TYPE) {
		u8 part_type = CHECK_BUS_PART_TYPE(check);

		printk("%sParticipation Type: %u, %s\n", pfx, part_type,
		       part_type < ARRAY_SIZE(ia_check_bus_part_type_strs) ?
		       ia_check_bus_part_type_strs[part_type] : "unknown");
	}

	if (validation_bits & CHECK_VALID_BUS_TIME_OUT)
		print_bool("Time Out", pfx, check, CHECK_BUS_TIME_OUT);

	if (validation_bits & CHECK_VALID_BUS_ADDR_SPACE) {
		u8 addr_space = CHECK_BUS_ADDR_SPACE(check);

		printk("%sAddress Space: %u, %s\n", pfx, addr_space,
		       addr_space < ARRAY_SIZE(ia_check_bus_addr_space_strs) ?
		       ia_check_bus_addr_space_strs[addr_space] : "unknown");
	}
}

void cper_print_proc_ia(const char *pfx, const struct cper_sec_proc_ia *proc)
{
	int i;
	struct cper_ia_err_info *err_info;
	struct cper_ia_proc_ctx *ctx_info;
	char newpfx[64], infopfx[64];
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

			if (err_type < N_ERR_TYPES) {
				snprintf(infopfx, sizeof(infopfx), "%s ",
					 newpfx);

				print_err_info(infopfx, err_type,
					       err_info->check_info);
			}
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

	ctx_info = (struct cper_ia_proc_ctx *)err_info;
	for (i = 0; i < VALID_PROC_CXT_INFO_NUM(proc->validation_bits); i++) {
		int size = sizeof(*ctx_info) + ctx_info->reg_arr_size;
		int groupsize = 4;

		printk("%sContext Information Structure %d:\n", pfx, i);

		printk("%sRegister Context Type: %s\n", newpfx,
		       ctx_info->reg_ctx_type < ARRAY_SIZE(ia_reg_ctx_strs) ?
		       ia_reg_ctx_strs[ctx_info->reg_ctx_type] : "unknown");

		printk("%sRegister Array Size: 0x%04x\n", newpfx,
		       ctx_info->reg_arr_size);

		if (ctx_info->reg_ctx_type == CTX_TYPE_MSR) {
			groupsize = 8; /* MSRs are 8 bytes wide. */
			printk("%sMSR Address: 0x%08x\n", newpfx,
			       ctx_info->msr_addr);
		}

		if (ctx_info->reg_ctx_type == CTX_TYPE_MMREG) {
			printk("%sMM Register Address: 0x%016llx\n", newpfx,
			       ctx_info->mm_reg_addr);
		}

		if (ctx_info->reg_ctx_type != CTX_TYPE_MSR ||
		    arch_apei_report_x86_error(ctx_info, proc->lapic_id)) {
			printk("%sRegister Array:\n", newpfx);
			print_hex_dump(newpfx, "", DUMP_PREFIX_OFFSET, 16,
				       groupsize, (ctx_info + 1),
				       ctx_info->reg_arr_size, 0);
		}

		ctx_info = (struct cper_ia_proc_ctx *)((long)ctx_info + size);
	}
}
