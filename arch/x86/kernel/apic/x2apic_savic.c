// SPDX-License-Identifier: GPL-2.0-only
/*
 * AMD Secure AVIC Support (SEV-SNP Guests)
 *
 * Copyright (C) 2024 Advanced Micro Devices, Inc.
 *
 * Author: Neeraj Upadhyay <Neeraj.Upadhyay@amd.com>
 */

#include <linux/cc_platform.h>
#include <linux/cpumask.h>
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

static inline void *get_reg_bitmap(unsigned int cpu, unsigned int offset)
{
	return &per_cpu_ptr(savic_page, cpu)->regs[offset];
}

static inline void update_vector(unsigned int cpu, unsigned int offset,
				 unsigned int vector, bool set)
{
	void *bitmap = get_reg_bitmap(cpu, offset);

	if (set)
		apic_set_vector(vector, bitmap);
	else
		apic_clear_vector(vector, bitmap);
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
	case APIC_LVTTHMR:
	case APIC_LVTPC:
	case APIC_LVT0:
	case APIC_LVT1:
	case APIC_LVTERR:
		return savic_ghcb_msr_read(reg);
	case APIC_ID:
	case APIC_LVR:
	case APIC_TASKPRI:
	case APIC_ARBPRI:
	case APIC_PROCPRI:
	case APIC_LDR:
	case APIC_SPIV:
	case APIC_ESR:
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

/*
 * On WRMSR to APIC_SELF_IPI register by the guest, Secure AVIC hardware
 * updates the APIC_IRR in the APIC backing page of the vCPU. In addition,
 * hardware evaluates the new APIC_IRR update for interrupt injection to
 * the vCPU. So, self IPIs are hardware-accelerated.
 */
static inline void self_ipi_reg_write(unsigned int vector)
{
	native_apic_msr_write(APIC_SELF_IPI, vector);
}

static void send_ipi_dest(unsigned int cpu, unsigned int vector, bool nmi)
{
	if (nmi)
		apic_set_reg(per_cpu_ptr(savic_page, cpu), SAVIC_NMI_REQ, 1);
	else
		update_vector(cpu, APIC_IRR, vector, true);
}

static void send_ipi_allbut(unsigned int vector, bool nmi)
{
	unsigned int cpu, src_cpu;

	guard(irqsave)();

	src_cpu = raw_smp_processor_id();

	for_each_cpu(cpu, cpu_online_mask) {
		if (cpu == src_cpu)
			continue;
		send_ipi_dest(cpu, vector, nmi);
	}
}

static inline void self_ipi(unsigned int vector, bool nmi)
{
	u32 icr_low = APIC_SELF_IPI | vector;

	if (nmi)
		icr_low |= APIC_DM_NMI;

	native_x2apic_icr_write(icr_low, 0);
}

static void savic_icr_write(u32 icr_low, u32 icr_high)
{
	unsigned int dsh, vector;
	u64 icr_data;
	bool nmi;

	dsh = icr_low & APIC_DEST_ALLBUT;
	vector = icr_low & APIC_VECTOR_MASK;
	nmi = ((icr_low & APIC_DM_FIXED_MASK) == APIC_DM_NMI);

	switch (dsh) {
	case APIC_DEST_SELF:
		self_ipi(vector, nmi);
		break;
	case APIC_DEST_ALLINC:
		self_ipi(vector, nmi);
		fallthrough;
	case APIC_DEST_ALLBUT:
		send_ipi_allbut(vector, nmi);
		break;
	default:
		send_ipi_dest(icr_high, vector, nmi);
		break;
	}

	icr_data = ((u64)icr_high) << 32 | icr_low;
	if (dsh != APIC_DEST_SELF)
		savic_ghcb_msr_write(APIC_ICR, icr_data);
	apic_set_reg64(this_cpu_ptr(savic_page), APIC_ICR, icr_data);
}

static void savic_write(u32 reg, u32 data)
{
	void *ap = this_cpu_ptr(savic_page);

	switch (reg) {
	case APIC_LVTT:
	case APIC_TMICT:
	case APIC_TDCR:
	case APIC_LVT0:
	case APIC_LVT1:
	case APIC_LVTTHMR:
	case APIC_LVTPC:
	case APIC_LVTERR:
		savic_ghcb_msr_write(reg, data);
		break;
	case APIC_TASKPRI:
	case APIC_EOI:
	case APIC_SPIV:
	case SAVIC_NMI_REQ:
	case APIC_ESR:
	case APIC_ECTRL:
	case APIC_SEOI:
	case APIC_IER:
	case APIC_EILVTn(0) ... APIC_EILVTn(3):
		apic_set_reg(ap, reg, data);
		break;
	case APIC_ICR:
		savic_icr_write(data, 0);
		break;
	case APIC_SELF_IPI:
		self_ipi_reg_write(data);
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

static void send_ipi(u32 dest, unsigned int vector, unsigned int dsh)
{
	unsigned int icr_low;

	icr_low = __prepare_ICR(dsh, vector, APIC_DEST_PHYSICAL);
	savic_icr_write(icr_low, dest);
}

static void savic_send_ipi(int cpu, int vector)
{
	u32 dest = per_cpu(x86_cpu_to_apicid, cpu);

	send_ipi(dest, vector, 0);
}

static void send_ipi_mask(const struct cpumask *mask, unsigned int vector, bool excl_self)
{
	unsigned int cpu, this_cpu;

	guard(irqsave)();

	this_cpu = raw_smp_processor_id();

	for_each_cpu(cpu, mask) {
		if (excl_self && cpu == this_cpu)
			continue;
		send_ipi(per_cpu(x86_cpu_to_apicid, cpu), vector, 0);
	}
}

static void savic_send_ipi_mask(const struct cpumask *mask, int vector)
{
	send_ipi_mask(mask, vector, false);
}

static void savic_send_ipi_mask_allbutself(const struct cpumask *mask, int vector)
{
	send_ipi_mask(mask, vector, true);
}

static void savic_send_ipi_allbutself(int vector)
{
	send_ipi(0, vector, APIC_DEST_ALLBUT);
}

static void savic_send_ipi_all(int vector)
{
	send_ipi(0, vector, APIC_DEST_ALLINC);
}

static void savic_send_ipi_self(int vector)
{
	self_ipi_reg_write(vector);
}

static void savic_update_vector(unsigned int cpu, unsigned int vector, bool set)
{
	update_vector(cpu, SAVIC_ALLOWED_IRR, vector, set);
}

static void savic_eoi(void)
{
	unsigned int cpu;
	int vec;

	cpu = raw_smp_processor_id();
	vec = apic_find_highest_vector(get_reg_bitmap(cpu, APIC_ISR));
	if (WARN_ONCE(vec == -1, "EOI write while no active interrupt in APIC_ISR"))
		return;

	/* Is level-triggered interrupt? */
	if (apic_test_vector(vec, get_reg_bitmap(cpu, APIC_TMR))) {
		update_vector(cpu, APIC_ISR, vec, false);
		/*
		 * Propagate the EOI write to the hypervisor for level-triggered
		 * interrupts. Return to the guest from GHCB protocol event takes
		 * care of re-evaluating interrupt state.
		 */
		savic_ghcb_msr_write(APIC_EOI, 0);
	} else {
		/*
		 * Hardware clears APIC_ISR and re-evaluates the interrupt state
		 * to determine if there is any pending interrupt which can be
		 * delivered to CPU.
		 */
		native_apic_msr_eoi();
	}
}

static void savic_teardown(void)
{
	/* Disable Secure AVIC */
	native_wrmsrq(MSR_AMD64_SAVIC_CONTROL, 0);
	savic_unregister_gpa(NULL);
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
		sev_es_terminate(SEV_TERM_SET_LINUX, GHCB_TERM_SAVIC_FAIL);

	native_wrmsrq(MSR_AMD64_SAVIC_CONTROL,
		      gpa | MSR_AMD64_SAVIC_EN | MSR_AMD64_SAVIC_ALLOWEDNMI);
}

static int savic_probe(void)
{
	if (!cc_platform_has(CC_ATTR_SNP_SECURE_AVIC))
		return 0;

	if (!x2apic_mode) {
		pr_err("Secure AVIC enabled in non x2APIC mode\n");
		sev_es_terminate(SEV_TERM_SET_LINUX, GHCB_TERM_SAVIC_FAIL);
		/* unreachable */
	}

	savic_page = alloc_percpu(struct secure_avic_page);
	if (!savic_page)
		sev_es_terminate(SEV_TERM_SET_LINUX, GHCB_TERM_SAVIC_FAIL);

	return 1;
}

static struct apic apic_x2apic_savic __ro_after_init = {

	.name				= "secure avic x2apic",
	.probe				= savic_probe,
	.acpi_madt_oem_check		= savic_acpi_madt_oem_check,
	.setup				= savic_setup,
	.teardown			= savic_teardown,

	.dest_mode_logical		= false,

	.disable_esr			= 0,

	.cpu_present_to_apicid		= default_cpu_present_to_apicid,

	.max_apic_id			= UINT_MAX,
	.x2apic_set_max_apicid		= true,
	.get_apic_id			= x2apic_get_apic_id,

	.calc_dest_apicid		= apic_default_calc_apicid,

	.send_IPI			= savic_send_ipi,
	.send_IPI_mask			= savic_send_ipi_mask,
	.send_IPI_mask_allbutself	= savic_send_ipi_mask_allbutself,
	.send_IPI_allbutself		= savic_send_ipi_allbutself,
	.send_IPI_all			= savic_send_ipi_all,
	.send_IPI_self			= savic_send_ipi_self,

	.nmi_to_offline_cpu		= true,

	.read				= savic_read,
	.write				= savic_write,
	.eoi				= savic_eoi,
	.icr_read			= native_x2apic_icr_read,
	.icr_write			= savic_icr_write,

	.update_vector			= savic_update_vector,
};

apic_driver(apic_x2apic_savic);
