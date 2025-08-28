// SPDX-License-Identifier: GPL-2.0-only
/*
 * AMD Secure AVIC Support (SEV-SNP Guests)
 *
 * Copyright (C) 2024 Advanced Micro Devices, Inc.
 *
 * Author: Neeraj Upadhyay <Neeraj.Upadhyay@amd.com>
 */

#include <linux/cc_platform.h>
#include <linux/percpu-defs.h>
#include <linux/align.h>

#include <asm/apic.h>
#include <asm/sev.h>

#include "local.h"

struct secure_avic_page {
	u8 regs[PAGE_SIZE];
} __aligned(PAGE_SIZE);

static struct secure_avic_page __percpu *savic_page __ro_after_init;

static int savic_acpi_madt_oem_check(char *oem_id, char *oem_table_id)
{
	return x2apic_enabled() && cc_platform_has(CC_ATTR_SNP_SECURE_AVIC);
}

#define SAVIC_ALLOWED_IRR	0x204

/*
 * When Secure AVIC is enabled, RDMSR/WRMSR of the APIC registers
 * result in #VC exception (for non-accelerated register accesses)
 * with VMEXIT_AVIC_NOACCEL error code. The #VC exception handler
 * can read/write the x2APIC register in the guest APIC backing page.
 *
 * Since doing this would increase the latency of accessing x2APIC
 * registers, instead of doing RDMSR/WRMSR based accesses and
 * handling the APIC register reads/writes in the #VC exception handler,
 * the read() and write() callbacks directly read/write the APIC register
 * from/to the vCPU's APIC backing page.
 */
static u32 savic_read(u32 reg)
{
	void *ap = this_cpu_ptr(savic_page);

	switch (reg) {
	case APIC_LVTT:
	case APIC_TMICT:
	case APIC_TMCCT:
	case APIC_TDCR:
	case APIC_ID:
	case APIC_LVR:
	case APIC_TASKPRI:
	case APIC_ARBPRI:
	case APIC_PROCPRI:
	case APIC_LDR:
	case APIC_SPIV:
	case APIC_ESR:
	case APIC_LVTTHMR:
	case APIC_LVTPC:
	case APIC_LVT0:
	case APIC_LVT1:
	case APIC_LVTERR:
	case APIC_EFEAT:
	case APIC_ECTRL:
	case APIC_SEOI:
	case APIC_IER:
	case APIC_EILVTn(0) ... APIC_EILVTn(3):
		return apic_get_reg(ap, reg);
	case APIC_ICR:
		return (u32)apic_get_reg64(ap, reg);
	case APIC_ISR ... APIC_ISR + 0x70:
	case APIC_TMR ... APIC_TMR + 0x70:
		if (WARN_ONCE(!IS_ALIGNED(reg, 16),
			      "APIC register read offset 0x%x not aligned at 16 bytes", reg))
			return 0;
		return apic_get_reg(ap, reg);
	/* IRR and ALLOWED_IRR offset range */
	case APIC_IRR ... APIC_IRR + 0x74:
		/*
		 * Valid APIC_IRR/SAVIC_ALLOWED_IRR registers are at 16 bytes strides from
		 * their respective base offset. APIC_IRRs are in the range
		 *
		 * (0x200, 0x210,  ..., 0x270)
		 *
		 * while the SAVIC_ALLOWED_IRR range starts 4 bytes later, in the range
		 *
		 * (0x204, 0x214, ..., 0x274).
		 *
		 * Filter out everything else.
		 */
		if (WARN_ONCE(!(IS_ALIGNED(reg, 16) ||
				IS_ALIGNED(reg - 4, 16)),
			      "Misaligned APIC_IRR/ALLOWED_IRR APIC register read offset 0x%x", reg))
			return 0;
		return apic_get_reg(ap, reg);
	default:
		pr_err("Error reading unknown Secure AVIC reg offset 0x%x\n", reg);
		return 0;
	}
}

#define SAVIC_NMI_REQ		0x278

static void savic_write(u32 reg, u32 data)
{
	void *ap = this_cpu_ptr(savic_page);

	switch (reg) {
	case APIC_LVTT:
	case APIC_LVT0:
	case APIC_LVT1:
	case APIC_TMICT:
	case APIC_TDCR:
	case APIC_SELF_IPI:
	case APIC_TASKPRI:
	case APIC_EOI:
	case APIC_SPIV:
	case SAVIC_NMI_REQ:
	case APIC_ESR:
	case APIC_LVTTHMR:
	case APIC_LVTPC:
	case APIC_LVTERR:
	case APIC_ECTRL:
	case APIC_SEOI:
	case APIC_IER:
	case APIC_EILVTn(0) ... APIC_EILVTn(3):
		apic_set_reg(ap, reg, data);
		break;
	case APIC_ICR:
		apic_set_reg64(ap, reg, (u64)data);
		break;
	/* ALLOWED_IRR offsets are writable */
	case SAVIC_ALLOWED_IRR ... SAVIC_ALLOWED_IRR + 0x70:
		if (IS_ALIGNED(reg - 4, 16)) {
			apic_set_reg(ap, reg, data);
			break;
		}
		fallthrough;
	default:
		pr_err("Error writing unknown Secure AVIC reg offset 0x%x\n", reg);
	}
}

static void savic_setup(void)
{
	void *ap = this_cpu_ptr(savic_page);
	enum es_result res;
	unsigned long gpa;

	/*
	 * Before Secure AVIC is enabled, APIC MSR reads are intercepted.
	 * APIC_ID MSR read returns the value from the hypervisor.
	 */
	apic_set_reg(ap, APIC_ID, native_apic_msr_read(APIC_ID));

	gpa = __pa(ap);

	/*
	 * The NPT entry for a vCPU's APIC backing page must always be
	 * present when the vCPU is running in order for Secure AVIC to
	 * function. A VMEXIT_BUSY is returned on VMRUN and the vCPU cannot
	 * be resumed if the NPT entry for the APIC backing page is not
	 * present. Notify GPA of the vCPU's APIC backing page to the
	 * hypervisor by calling savic_register_gpa(). Before executing
	 * VMRUN, the hypervisor makes use of this information to make sure
	 * the APIC backing page is mapped in NPT.
	 */
	res = savic_register_gpa(gpa);
	if (res != ES_OK)
		snp_abort();
}

static int savic_probe(void)
{
	if (!cc_platform_has(CC_ATTR_SNP_SECURE_AVIC))
		return 0;

	if (!x2apic_mode) {
		pr_err("Secure AVIC enabled in non x2APIC mode\n");
		snp_abort();
		/* unreachable */
	}

	savic_page = alloc_percpu(struct secure_avic_page);
	if (!savic_page)
		snp_abort();

	return 1;
}

static struct apic apic_x2apic_savic __ro_after_init = {

	.name				= "secure avic x2apic",
	.probe				= savic_probe,
	.acpi_madt_oem_check		= savic_acpi_madt_oem_check,
	.setup				= savic_setup,

	.dest_mode_logical		= false,

	.disable_esr			= 0,

	.cpu_present_to_apicid		= default_cpu_present_to_apicid,

	.max_apic_id			= UINT_MAX,
	.x2apic_set_max_apicid		= true,
	.get_apic_id			= x2apic_get_apic_id,

	.calc_dest_apicid		= apic_default_calc_apicid,

	.nmi_to_offline_cpu		= true,

	.read				= savic_read,
	.write				= savic_write,
	.eoi				= native_apic_msr_eoi,
	.icr_read			= native_x2apic_icr_read,
	.icr_write			= native_x2apic_icr_write,
};

apic_driver(apic_x2apic_savic);
