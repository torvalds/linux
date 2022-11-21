// SPDX-License-Identifier: GPL-2.0
/*
 * UEFI Common Platform Error Record (CPER) support
 *
 * Copyright (C) 2010, Intel Corp.
 *	Author: Huang Ying <ying.huang@intel.com>
 *
 * CPER is the format used to describe platform hardware error by
 * various tables, such as ERST, BERT and HEST etc.
 *
 * For more information about CPER, please refer to Appendix N of UEFI
 * Specification version 2.4.
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

/*
 * CPER record ID need to be unique even after reboot, because record
 * ID is used as index for ERST storage, while CPER records from
 * multiple boot may co-exist in ERST.
 */
u64 cper_next_record_id(void)
{
	static atomic64_t seq;

	if (!atomic64_read(&seq)) {
		time64_t time = ktime_get_real_seconds();

		/*
		 * This code is unlikely to still be needed in year 2106,
		 * but just in case, let's use a few more bits for timestamps
		 * after y2038 to be sure they keep increasing monotonically
		 * for the next few hundred years...
		 */
		if (time < 0x80000000)
			atomic64_set(&seq, (ktime_get_real_seconds()) << 32);
		else
			atomic64_set(&seq, 0x8000000000000000ull |
					   ktime_get_real_seconds() << 24);
	}

	return atomic64_inc_return(&seq);
}
EXPORT_SYMBOL_GPL(cper_next_record_id);

static const char * const severity_strs[] = {
	"recoverable",
	"fatal",
	"corrected",
	"info",
};

const char *cper_severity_str(unsigned int severity)
{
	return severity < ARRAY_SIZE(severity_strs) ?
		severity_strs[severity] : "unknown";
}
EXPORT_SYMBOL_GPL(cper_severity_str);

/*
 * cper_print_bits - print strings for set bits
 * @pfx: prefix for each line, including log level and prefix string
 * @bits: bit mask
 * @strs: string array, indexed by bit position
 * @strs_size: size of the string array: @strs
 *
 * For each set bit in @bits, print the corresponding string in @strs.
 * If the output length is longer than 80, multiple line will be
 * printed, with @pfx is printed at the beginning of each line.
 */
void cper_print_bits(const char *pfx, unsigned int bits,
		     const char * const strs[], unsigned int strs_size)
{
	int i, len = 0;
	const char *str;
	char buf[84];

	for (i = 0; i < strs_size; i++) {
		if (!(bits & (1U << i)))
			continue;
		str = strs[i];
		if (!str)
			continue;
		if (len && len + strlen(str) + 2 > 80) {
			printk("%s\n", buf);
			len = 0;
		}
		if (!len)
			len = snprintf(buf, sizeof(buf), "%s%s", pfx, str);
		else
			len += scnprintf(buf+len, sizeof(buf)-len, ", %s", str);
	}
	if (len)
		printk("%s\n", buf);
}

static const char * const proc_type_strs[] = {
	"IA32/X64",
	"IA64",
	"ARM",
};

static const char * const proc_isa_strs[] = {
	"IA32",
	"IA64",
	"X64",
	"ARM A32/T32",
	"ARM A64",
};

const char * const cper_proc_error_type_strs[] = {
	"cache error",
	"TLB error",
	"bus error",
	"micro-architectural error",
};

static const char * const proc_op_strs[] = {
	"unknown or generic",
	"data read",
	"data write",
	"instruction execution",
};

static const char * const proc_flag_strs[] = {
	"restartable",
	"precise IP",
	"overflow",
	"corrected",
};

static void cper_print_proc_generic(const char *pfx,
				    const struct cper_sec_proc_generic *proc)
{
	if (proc->validation_bits & CPER_PROC_VALID_TYPE)
		printk("%s""processor_type: %d, %s\n", pfx, proc->proc_type,
		       proc->proc_type < ARRAY_SIZE(proc_type_strs) ?
		       proc_type_strs[proc->proc_type] : "unknown");
	if (proc->validation_bits & CPER_PROC_VALID_ISA)
		printk("%s""processor_isa: %d, %s\n", pfx, proc->proc_isa,
		       proc->proc_isa < ARRAY_SIZE(proc_isa_strs) ?
		       proc_isa_strs[proc->proc_isa] : "unknown");
	if (proc->validation_bits & CPER_PROC_VALID_ERROR_TYPE) {
		printk("%s""error_type: 0x%02x\n", pfx, proc->proc_error_type);
		cper_print_bits(pfx, proc->proc_error_type,
				cper_proc_error_type_strs,
				ARRAY_SIZE(cper_proc_error_type_strs));
	}
	if (proc->validation_bits & CPER_PROC_VALID_OPERATION)
		printk("%s""operation: %d, %s\n", pfx, proc->operation,
		       proc->operation < ARRAY_SIZE(proc_op_strs) ?
		       proc_op_strs[proc->operation] : "unknown");
	if (proc->validation_bits & CPER_PROC_VALID_FLAGS) {
		printk("%s""flags: 0x%02x\n", pfx, proc->flags);
		cper_print_bits(pfx, proc->flags, proc_flag_strs,
				ARRAY_SIZE(proc_flag_strs));
	}
	if (proc->validation_bits & CPER_PROC_VALID_LEVEL)
		printk("%s""level: %d\n", pfx, proc->level);
	if (proc->validation_bits & CPER_PROC_VALID_VERSION)
		printk("%s""version_info: 0x%016llx\n", pfx, proc->cpu_version);
	if (proc->validation_bits & CPER_PROC_VALID_ID)
		printk("%s""processor_id: 0x%016llx\n", pfx, proc->proc_id);
	if (proc->validation_bits & CPER_PROC_VALID_TARGET_ADDRESS)
		printk("%s""target_address: 0x%016llx\n",
		       pfx, proc->target_addr);
	if (proc->validation_bits & CPER_PROC_VALID_REQUESTOR_ID)
		printk("%s""requestor_id: 0x%016llx\n",
		       pfx, proc->requestor_id);
	if (proc->validation_bits & CPER_PROC_VALID_RESPONDER_ID)
		printk("%s""responder_id: 0x%016llx\n",
		       pfx, proc->responder_id);
	if (proc->validation_bits & CPER_PROC_VALID_IP)
		printk("%s""IP: 0x%016llx\n", pfx, proc->ip);
}

static const char * const mem_err_type_strs[] = {
	"unknown",
	"no error",
	"single-bit ECC",
	"multi-bit ECC",
	"single-symbol chipkill ECC",
	"multi-symbol chipkill ECC",
	"master abort",
	"target abort",
	"parity error",
	"watchdog timeout",
	"invalid address",
	"mirror Broken",
	"memory sparing",
	"scrub corrected error",
	"scrub uncorrected error",
	"physical memory map-out event",
};

const char *cper_mem_err_type_str(unsigned int etype)
{
	return etype < ARRAY_SIZE(mem_err_type_strs) ?
		mem_err_type_strs[etype] : "unknown";
}
EXPORT_SYMBOL_GPL(cper_mem_err_type_str);

static int cper_mem_err_location(struct cper_mem_err_compact *mem, char *msg)
{
	u32 len, n;

	if (!msg)
		return 0;

	n = 0;
	len = CPER_REC_LEN;
	if (mem->validation_bits & CPER_MEM_VALID_NODE)
		n += scnprintf(msg + n, len - n, "node: %d ", mem->node);
	if (mem->validation_bits & CPER_MEM_VALID_CARD)
		n += scnprintf(msg + n, len - n, "card: %d ", mem->card);
	if (mem->validation_bits & CPER_MEM_VALID_MODULE)
		n += scnprintf(msg + n, len - n, "module: %d ", mem->module);
	if (mem->validation_bits & CPER_MEM_VALID_RANK_NUMBER)
		n += scnprintf(msg + n, len - n, "rank: %d ", mem->rank);
	if (mem->validation_bits & CPER_MEM_VALID_BANK)
		n += scnprintf(msg + n, len - n, "bank: %d ", mem->bank);
	if (mem->validation_bits & CPER_MEM_VALID_BANK_GROUP)
		n += scnprintf(msg + n, len - n, "bank_group: %d ",
			       mem->bank >> CPER_MEM_BANK_GROUP_SHIFT);
	if (mem->validation_bits & CPER_MEM_VALID_BANK_ADDRESS)
		n += scnprintf(msg + n, len - n, "bank_address: %d ",
			       mem->bank & CPER_MEM_BANK_ADDRESS_MASK);
	if (mem->validation_bits & CPER_MEM_VALID_DEVICE)
		n += scnprintf(msg + n, len - n, "device: %d ", mem->device);
	if (mem->validation_bits & (CPER_MEM_VALID_ROW | CPER_MEM_VALID_ROW_EXT)) {
		u32 row = mem->row;

		row |= cper_get_mem_extension(mem->validation_bits, mem->extended);
		n += scnprintf(msg + n, len - n, "row: %d ", row);
	}
	if (mem->validation_bits & CPER_MEM_VALID_COLUMN)
		n += scnprintf(msg + n, len - n, "column: %d ", mem->column);
	if (mem->validation_bits & CPER_MEM_VALID_BIT_POSITION)
		n += scnprintf(msg + n, len - n, "bit_position: %d ",
			       mem->bit_pos);
	if (mem->validation_bits & CPER_MEM_VALID_REQUESTOR_ID)
		n += scnprintf(msg + n, len - n, "requestor_id: 0x%016llx ",
			       mem->requestor_id);
	if (mem->validation_bits & CPER_MEM_VALID_RESPONDER_ID)
		n += scnprintf(msg + n, len - n, "responder_id: 0x%016llx ",
			       mem->responder_id);
	if (mem->validation_bits & CPER_MEM_VALID_TARGET_ID)
		n += scnprintf(msg + n, len - n, "target_id: 0x%016llx ",
			       mem->target_id);
	if (mem->validation_bits & CPER_MEM_VALID_CHIP_ID)
		n += scnprintf(msg + n, len - n, "chip_id: %d ",
			       mem->extended >> CPER_MEM_CHIP_ID_SHIFT);

	return n;
}

static int cper_dimm_err_location(struct cper_mem_err_compact *mem, char *msg)
{
	u32 len, n;
	const char *bank = NULL, *device = NULL;

	if (!msg || !(mem->validation_bits & CPER_MEM_VALID_MODULE_HANDLE))
		return 0;

	len = CPER_REC_LEN;
	dmi_memdev_name(mem->mem_dev_handle, &bank, &device);
	if (bank && device)
		n = snprintf(msg, len, "DIMM location: %s %s ", bank, device);
	else
		n = snprintf(msg, len,
			     "DIMM location: not present. DMI handle: 0x%.4x ",
			     mem->mem_dev_handle);

	return n;
}

void cper_mem_err_pack(const struct cper_sec_mem_err *mem,
		       struct cper_mem_err_compact *cmem)
{
	cmem->validation_bits = mem->validation_bits;
	cmem->node = mem->node;
	cmem->card = mem->card;
	cmem->module = mem->module;
	cmem->bank = mem->bank;
	cmem->device = mem->device;
	cmem->row = mem->row;
	cmem->column = mem->column;
	cmem->bit_pos = mem->bit_pos;
	cmem->requestor_id = mem->requestor_id;
	cmem->responder_id = mem->responder_id;
	cmem->target_id = mem->target_id;
	cmem->extended = mem->extended;
	cmem->rank = mem->rank;
	cmem->mem_array_handle = mem->mem_array_handle;
	cmem->mem_dev_handle = mem->mem_dev_handle;
}

const char *cper_mem_err_unpack(struct trace_seq *p,
				struct cper_mem_err_compact *cmem)
{
	const char *ret = trace_seq_buffer_ptr(p);
	char rcd_decode_str[CPER_REC_LEN];

	if (cper_mem_err_location(cmem, rcd_decode_str))
		trace_seq_printf(p, "%s", rcd_decode_str);
	if (cper_dimm_err_location(cmem, rcd_decode_str))
		trace_seq_printf(p, "%s", rcd_decode_str);
	trace_seq_putc(p, '\0');

	return ret;
}

static void cper_print_mem(const char *pfx, const struct cper_sec_mem_err *mem,
	int len)
{
	struct cper_mem_err_compact cmem;
	char rcd_decode_str[CPER_REC_LEN];

	/* Don't trust UEFI 2.1/2.2 structure with bad validation bits */
	if (len == sizeof(struct cper_sec_mem_err_old) &&
	    (mem->validation_bits & ~(CPER_MEM_VALID_RANK_NUMBER - 1))) {
		pr_err(FW_WARN "valid bits set for fields beyond structure\n");
		return;
	}
	if (mem->validation_bits & CPER_MEM_VALID_ERROR_STATUS)
		printk("%s""error_status: 0x%016llx\n", pfx, mem->error_status);
	if (mem->validation_bits & CPER_MEM_VALID_PA)
		printk("%s""physical_address: 0x%016llx\n",
		       pfx, mem->physical_addr);
	if (mem->validation_bits & CPER_MEM_VALID_PA_MASK)
		printk("%s""physical_address_mask: 0x%016llx\n",
		       pfx, mem->physical_addr_mask);
	cper_mem_err_pack(mem, &cmem);
	if (cper_mem_err_location(&cmem, rcd_decode_str))
		printk("%s%s\n", pfx, rcd_decode_str);
	if (mem->validation_bits & CPER_MEM_VALID_ERROR_TYPE) {
		u8 etype = mem->error_type;
		printk("%s""error_type: %d, %s\n", pfx, etype,
		       cper_mem_err_type_str(etype));
	}
	if (cper_dimm_err_location(&cmem, rcd_decode_str))
		printk("%s%s\n", pfx, rcd_decode_str);
}

static const char * const pcie_port_type_strs[] = {
	"PCIe end point",
	"legacy PCI end point",
	"unknown",
	"unknown",
	"root port",
	"upstream switch port",
	"downstream switch port",
	"PCIe to PCI/PCI-X bridge",
	"PCI/PCI-X to PCIe bridge",
	"root complex integrated endpoint device",
	"root complex event collector",
};

static void cper_print_pcie(const char *pfx, const struct cper_sec_pcie *pcie,
			    const struct acpi_hest_generic_data *gdata)
{
	if (pcie->validation_bits & CPER_PCIE_VALID_PORT_TYPE)
		printk("%s""port_type: %d, %s\n", pfx, pcie->port_type,
		       pcie->port_type < ARRAY_SIZE(pcie_port_type_strs) ?
		       pcie_port_type_strs[pcie->port_type] : "unknown");
	if (pcie->validation_bits & CPER_PCIE_VALID_VERSION)
		printk("%s""version: %d.%d\n", pfx,
		       pcie->version.major, pcie->version.minor);
	if (pcie->validation_bits & CPER_PCIE_VALID_COMMAND_STATUS)
		printk("%s""command: 0x%04x, status: 0x%04x\n", pfx,
		       pcie->command, pcie->status);
	if (pcie->validation_bits & CPER_PCIE_VALID_DEVICE_ID) {
		const __u8 *p;
		printk("%s""device_id: %04x:%02x:%02x.%x\n", pfx,
		       pcie->device_id.segment, pcie->device_id.bus,
		       pcie->device_id.device, pcie->device_id.function);
		printk("%s""slot: %d\n", pfx,
		       pcie->device_id.slot >> CPER_PCIE_SLOT_SHIFT);
		printk("%s""secondary_bus: 0x%02x\n", pfx,
		       pcie->device_id.secondary_bus);
		printk("%s""vendor_id: 0x%04x, device_id: 0x%04x\n", pfx,
		       pcie->device_id.vendor_id, pcie->device_id.device_id);
		p = pcie->device_id.class_code;
		printk("%s""class_code: %02x%02x%02x\n", pfx, p[2], p[1], p[0]);
	}
	if (pcie->validation_bits & CPER_PCIE_VALID_SERIAL_NUMBER)
		printk("%s""serial number: 0x%04x, 0x%04x\n", pfx,
		       pcie->serial_number.lower, pcie->serial_number.upper);
	if (pcie->validation_bits & CPER_PCIE_VALID_BRIDGE_CONTROL_STATUS)
		printk(
	"%s""bridge: secondary_status: 0x%04x, control: 0x%04x\n",
	pfx, pcie->bridge.secondary_status, pcie->bridge.control);

	/* Fatal errors call __ghes_panic() before AER handler prints this */
	if ((pcie->validation_bits & CPER_PCIE_VALID_AER_INFO) &&
	    (gdata->error_severity & CPER_SEV_FATAL)) {
		struct aer_capability_regs *aer;

		aer = (struct aer_capability_regs *)pcie->aer_info;
		printk("%saer_uncor_status: 0x%08x, aer_uncor_mask: 0x%08x\n",
		       pfx, aer->uncor_status, aer->uncor_mask);
		printk("%saer_uncor_severity: 0x%08x\n",
		       pfx, aer->uncor_severity);
		printk("%sTLP Header: %08x %08x %08x %08x\n", pfx,
		       aer->header_log.dw0, aer->header_log.dw1,
		       aer->header_log.dw2, aer->header_log.dw3);
	}
}

static const char * const fw_err_rec_type_strs[] = {
	"IPF SAL Error Record",
	"SOC Firmware Error Record Type1 (Legacy CrashLog Support)",
	"SOC Firmware Error Record Type2",
};

static void cper_print_fw_err(const char *pfx,
			      struct acpi_hest_generic_data *gdata,
			      const struct cper_sec_fw_err_rec_ref *fw_err)
{
	void *buf = acpi_hest_get_payload(gdata);
	u32 offset, length = gdata->error_data_length;

	printk("%s""Firmware Error Record Type: %s\n", pfx,
	       fw_err->record_type < ARRAY_SIZE(fw_err_rec_type_strs) ?
	       fw_err_rec_type_strs[fw_err->record_type] : "unknown");
	printk("%s""Revision: %d\n", pfx, fw_err->revision);

	/* Record Type based on UEFI 2.7 */
	if (fw_err->revision == 0) {
		printk("%s""Record Identifier: %08llx\n", pfx,
		       fw_err->record_identifier);
	} else if (fw_err->revision == 2) {
		printk("%s""Record Identifier: %pUl\n", pfx,
		       &fw_err->record_identifier_guid);
	}

	/*
	 * The FW error record may contain trailing data beyond the
	 * structure defined by the specification. As the fields
	 * defined (and hence the offset of any trailing data) vary
	 * with the revision, set the offset to account for this
	 * variation.
	 */
	if (fw_err->revision == 0) {
		/* record_identifier_guid not defined */
		offset = offsetof(struct cper_sec_fw_err_rec_ref,
				  record_identifier_guid);
	} else if (fw_err->revision == 1) {
		/* record_identifier not defined */
		offset = offsetof(struct cper_sec_fw_err_rec_ref,
				  record_identifier);
	} else {
		offset = sizeof(*fw_err);
	}

	buf += offset;
	length -= offset;

	print_hex_dump(pfx, "", DUMP_PREFIX_OFFSET, 16, 4, buf, length, true);
}

static void cper_print_tstamp(const char *pfx,
				   struct acpi_hest_generic_data_v300 *gdata)
{
	__u8 hour, min, sec, day, mon, year, century, *timestamp;

	if (gdata->validation_bits & ACPI_HEST_GEN_VALID_TIMESTAMP) {
		timestamp = (__u8 *)&(gdata->time_stamp);
		sec       = bcd2bin(timestamp[0]);
		min       = bcd2bin(timestamp[1]);
		hour      = bcd2bin(timestamp[2]);
		day       = bcd2bin(timestamp[4]);
		mon       = bcd2bin(timestamp[5]);
		year      = bcd2bin(timestamp[6]);
		century   = bcd2bin(timestamp[7]);

		printk("%s%ststamp: %02d%02d-%02d-%02d %02d:%02d:%02d\n", pfx,
		       (timestamp[3] & 0x1 ? "precise " : "imprecise "),
		       century, year, mon, day, hour, min, sec);
	}
}

static void
cper_estatus_print_section(const char *pfx, struct acpi_hest_generic_data *gdata,
			   int sec_no)
{
	guid_t *sec_type = (guid_t *)gdata->section_type;
	__u16 severity;
	char newpfx[64];

	if (acpi_hest_get_version(gdata) >= 3)
		cper_print_tstamp(pfx, (struct acpi_hest_generic_data_v300 *)gdata);

	severity = gdata->error_severity;
	printk("%s""Error %d, type: %s\n", pfx, sec_no,
	       cper_severity_str(severity));
	if (gdata->validation_bits & CPER_SEC_VALID_FRU_ID)
		printk("%s""fru_id: %pUl\n", pfx, gdata->fru_id);
	if (gdata->validation_bits & CPER_SEC_VALID_FRU_TEXT)
		printk("%s""fru_text: %.20s\n", pfx, gdata->fru_text);

	snprintf(newpfx, sizeof(newpfx), "%s ", pfx);
	if (guid_equal(sec_type, &CPER_SEC_PROC_GENERIC)) {
		struct cper_sec_proc_generic *proc_err = acpi_hest_get_payload(gdata);

		printk("%s""section_type: general processor error\n", newpfx);
		if (gdata->error_data_length >= sizeof(*proc_err))
			cper_print_proc_generic(newpfx, proc_err);
		else
			goto err_section_too_small;
	} else if (guid_equal(sec_type, &CPER_SEC_PLATFORM_MEM)) {
		struct cper_sec_mem_err *mem_err = acpi_hest_get_payload(gdata);

		printk("%s""section_type: memory error\n", newpfx);
		if (gdata->error_data_length >=
		    sizeof(struct cper_sec_mem_err_old))
			cper_print_mem(newpfx, mem_err,
				       gdata->error_data_length);
		else
			goto err_section_too_small;
	} else if (guid_equal(sec_type, &CPER_SEC_PCIE)) {
		struct cper_sec_pcie *pcie = acpi_hest_get_payload(gdata);

		printk("%s""section_type: PCIe error\n", newpfx);
		if (gdata->error_data_length >= sizeof(*pcie))
			cper_print_pcie(newpfx, pcie, gdata);
		else
			goto err_section_too_small;
#if defined(CONFIG_ARM64) || defined(CONFIG_ARM)
	} else if (guid_equal(sec_type, &CPER_SEC_PROC_ARM)) {
		struct cper_sec_proc_arm *arm_err = acpi_hest_get_payload(gdata);

		printk("%ssection_type: ARM processor error\n", newpfx);
		if (gdata->error_data_length >= sizeof(*arm_err))
			cper_print_proc_arm(newpfx, arm_err);
		else
			goto err_section_too_small;
#endif
#if defined(CONFIG_UEFI_CPER_X86)
	} else if (guid_equal(sec_type, &CPER_SEC_PROC_IA)) {
		struct cper_sec_proc_ia *ia_err = acpi_hest_get_payload(gdata);

		printk("%ssection_type: IA32/X64 processor error\n", newpfx);
		if (gdata->error_data_length >= sizeof(*ia_err))
			cper_print_proc_ia(newpfx, ia_err);
		else
			goto err_section_too_small;
#endif
	} else if (guid_equal(sec_type, &CPER_SEC_FW_ERR_REC_REF)) {
		struct cper_sec_fw_err_rec_ref *fw_err = acpi_hest_get_payload(gdata);

		printk("%ssection_type: Firmware Error Record Reference\n",
		       newpfx);
		/* The minimal FW Error Record contains 16 bytes */
		if (gdata->error_data_length >= SZ_16)
			cper_print_fw_err(newpfx, gdata, fw_err);
		else
			goto err_section_too_small;
	} else {
		const void *err = acpi_hest_get_payload(gdata);

		printk("%ssection type: unknown, %pUl\n", newpfx, sec_type);
		printk("%ssection length: %#x\n", newpfx,
		       gdata->error_data_length);
		print_hex_dump(newpfx, "", DUMP_PREFIX_OFFSET, 16, 4, err,
			       gdata->error_data_length, true);
	}

	return;

err_section_too_small:
	pr_err(FW_WARN "error section length is too small\n");
}

void cper_estatus_print(const char *pfx,
			const struct acpi_hest_generic_status *estatus)
{
	struct acpi_hest_generic_data *gdata;
	int sec_no = 0;
	char newpfx[64];
	__u16 severity;

	severity = estatus->error_severity;
	if (severity == CPER_SEV_CORRECTED)
		printk("%s%s\n", pfx,
		       "It has been corrected by h/w "
		       "and requires no further action");
	printk("%s""event severity: %s\n", pfx, cper_severity_str(severity));
	snprintf(newpfx, sizeof(newpfx), "%s ", pfx);

	apei_estatus_for_each_section(estatus, gdata) {
		cper_estatus_print_section(newpfx, gdata, sec_no);
		sec_no++;
	}
}
EXPORT_SYMBOL_GPL(cper_estatus_print);

int cper_estatus_check_header(const struct acpi_hest_generic_status *estatus)
{
	if (estatus->data_length &&
	    estatus->data_length < sizeof(struct acpi_hest_generic_data))
		return -EINVAL;
	if (estatus->raw_data_length &&
	    estatus->raw_data_offset < sizeof(*estatus) + estatus->data_length)
		return -EINVAL;

	return 0;
}
EXPORT_SYMBOL_GPL(cper_estatus_check_header);

int cper_estatus_check(const struct acpi_hest_generic_status *estatus)
{
	struct acpi_hest_generic_data *gdata;
	unsigned int data_len, record_size;
	int rc;

	rc = cper_estatus_check_header(estatus);
	if (rc)
		return rc;

	data_len = estatus->data_length;

	apei_estatus_for_each_section(estatus, gdata) {
		if (acpi_hest_get_size(gdata) > data_len)
			return -EINVAL;

		record_size = acpi_hest_get_record_size(gdata);
		if (record_size > data_len)
			return -EINVAL;

		data_len -= record_size;
	}
	if (data_len)
		return -EINVAL;

	return 0;
}
EXPORT_SYMBOL_GPL(cper_estatus_check);
