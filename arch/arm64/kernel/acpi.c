// SPDX-License-Identifier: GPL-2.0-only
/*
 *  ARM64 Specific Low-Level ACPI Boot Support
 *
 *  Copyright (C) 2013-2014, Linaro Ltd.
 *	Author: Al Stone <al.stone@linaro.org>
 *	Author: Graeme Gregory <graeme.gregory@linaro.org>
 *	Author: Hanjun Guo <hanjun.guo@linaro.org>
 *	Author: Tomasz Nowicki <tomasz.nowicki@linaro.org>
 *	Author: Naresh Bhat <naresh.bhat@linaro.org>
 */

#define pr_fmt(fmt) "ACPI: " fmt

#include <linux/acpi.h>
#include <linux/arm-smccc.h>
#include <linux/cpumask.h>
#include <linux/efi.h>
#include <linux/efi-bgrt.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/irq_work.h>
#include <linux/memblock.h>
#include <linux/of_fdt.h>
#include <linux/libfdt.h>
#include <linux/smp.h>
#include <linux/serial_core.h>
#include <linux/pgtable.h>

#include <acpi/ghes.h>
#include <asm/cputype.h>
#include <asm/cpu_ops.h>
#include <asm/daifflags.h>
#include <asm/smp_plat.h>

int acpi_noirq = 1;		/* skip ACPI IRQ initialization */
int acpi_disabled = 1;
EXPORT_SYMBOL(acpi_disabled);

int acpi_pci_disabled = 1;	/* skip ACPI PCI scan and IRQ initialization */
EXPORT_SYMBOL(acpi_pci_disabled);

static bool param_acpi_off __initdata;
static bool param_acpi_on __initdata;
static bool param_acpi_force __initdata;

static int __init parse_acpi(char *arg)
{
	if (!arg)
		return -EINVAL;

	/* "acpi=off" disables both ACPI table parsing and interpreter */
	if (strcmp(arg, "off") == 0)
		param_acpi_off = true;
	else if (strcmp(arg, "on") == 0) /* prefer ACPI over DT */
		param_acpi_on = true;
	else if (strcmp(arg, "force") == 0) /* force ACPI to be enabled */
		param_acpi_force = true;
	else
		return -EINVAL;	/* Core will print when we return error */

	return 0;
}
early_param("acpi", parse_acpi);

static bool __init dt_is_stub(void)
{
	int node;

	fdt_for_each_subnode(node, initial_boot_params, 0) {
		const char *name = fdt_get_name(initial_boot_params, node, NULL);
		if (strcmp(name, "chosen") == 0)
			continue;
		if (strcmp(name, "hypervisor") == 0 &&
		    of_flat_dt_is_compatible(node, "xen,xen"))
			continue;

		return false;
	}

	return true;
}

/*
 * __acpi_map_table() will be called before page_init(), so early_ioremap()
 * or early_memremap() should be called here to for ACPI table mapping.
 */
void __init __iomem *__acpi_map_table(unsigned long phys, unsigned long size)
{
	if (!size)
		return NULL;

	return early_memremap(phys, size);
}

void __init __acpi_unmap_table(void __iomem *map, unsigned long size)
{
	if (!map || !size)
		return;

	early_memunmap(map, size);
}

bool __init acpi_psci_present(void)
{
	return acpi_gbl_FADT.arm_boot_flags & ACPI_FADT_PSCI_COMPLIANT;
}

/* Whether HVC must be used instead of SMC as the PSCI conduit */
bool acpi_psci_use_hvc(void)
{
	return acpi_gbl_FADT.arm_boot_flags & ACPI_FADT_PSCI_USE_HVC;
}

/*
 * acpi_fadt_sanity_check() - Check FADT presence and carry out sanity
 *			      checks on it
 *
 * Return 0 on success,  <0 on failure
 */
static int __init acpi_fadt_sanity_check(void)
{
	struct acpi_table_header *table;
	struct acpi_table_fadt *fadt;
	acpi_status status;
	int ret = 0;

	/*
	 * FADT is required on arm64; retrieve it to check its presence
	 * and carry out revision and ACPI HW reduced compliancy tests
	 */
	status = acpi_get_table(ACPI_SIG_FADT, 0, &table);
	if (ACPI_FAILURE(status)) {
		const char *msg = acpi_format_exception(status);

		pr_err("Failed to get FADT table, %s\n", msg);
		return -ENODEV;
	}

	fadt = (struct acpi_table_fadt *)table;

	/*
	 * Revision in table header is the FADT Major revision, and there
	 * is a minor revision of FADT which was introduced by ACPI 5.1,
	 * we only deal with ACPI 5.1 or newer revision to get GIC and SMP
	 * boot protocol configuration data.
	 */
	if (table->revision < 5 ||
	   (table->revision == 5 && fadt->minor_revision < 1)) {
		pr_err(FW_BUG "Unsupported FADT revision %d.%d, should be 5.1+\n",
		       table->revision, fadt->minor_revision);

		if (!fadt->arm_boot_flags) {
			ret = -EINVAL;
			goto out;
		}
		pr_err("FADT has ARM boot flags set, assuming 5.1\n");
	}

	if (!(fadt->flags & ACPI_FADT_HW_REDUCED)) {
		pr_err("FADT not ACPI hardware reduced compliant\n");
		ret = -EINVAL;
	}

out:
	/*
	 * acpi_get_table() creates FADT table mapping that
	 * should be released after parsing and before resuming boot
	 */
	acpi_put_table(table);
	return ret;
}

/*
 * acpi_boot_table_init() called from setup_arch(), always.
 *	1. find RSDP and get its address, and then find XSDT
 *	2. extract all tables and checksums them all
 *	3. check ACPI FADT revision
 *	4. check ACPI FADT HW reduced flag
 *
 * We can parse ACPI boot-time tables such as MADT after
 * this function is called.
 *
 * On return ACPI is enabled if either:
 *
 * - ACPI tables are initialized and sanity checks passed
 * - acpi=force was passed in the command line and ACPI was not disabled
 *   explicitly through acpi=off command line parameter
 *
 * ACPI is disabled on function return otherwise
 */
void __init acpi_boot_table_init(void)
{
	/*
	 * Enable ACPI instead of device tree unless
	 * - ACPI has been disabled explicitly (acpi=off), or
	 * - the device tree is not empty (it has more than just a /chosen node,
	 *   and a /hypervisor node when running on Xen)
	 *   and ACPI has not been [force] enabled (acpi=on|force)
	 */
	if (param_acpi_off ||
	    (!param_acpi_on && !param_acpi_force && !dt_is_stub()))
		goto done;

	/*
	 * ACPI is disabled at this point. Enable it in order to parse
	 * the ACPI tables and carry out sanity checks
	 */
	enable_acpi();

	/*
	 * If ACPI tables are initialized and FADT sanity checks passed,
	 * leave ACPI enabled and carry on booting; otherwise disable ACPI
	 * on initialization error.
	 * If acpi=force was passed on the command line it forces ACPI
	 * to be enabled even if its initialization failed.
	 */
	if (acpi_table_init() || acpi_fadt_sanity_check()) {
		pr_err("Failed to init ACPI tables\n");
		if (!param_acpi_force)
			disable_acpi();
	}

done:
	if (acpi_disabled) {
		if (earlycon_acpi_spcr_enable)
			early_init_dt_scan_chosen_stdout();
	} else {
		acpi_parse_spcr(earlycon_acpi_spcr_enable, true);
		if (IS_ENABLED(CONFIG_ACPI_BGRT))
			acpi_table_parse(ACPI_SIG_BGRT, acpi_parse_bgrt);
	}
}

static pgprot_t __acpi_get_writethrough_mem_attribute(void)
{
	/*
	 * Although UEFI specifies the use of Normal Write-through for
	 * EFI_MEMORY_WT, it is seldom used in practice and not implemented
	 * by most (all?) CPUs. Rather than allocate a MAIR just for this
	 * purpose, emit a warning and use Normal Non-cacheable instead.
	 */
	pr_warn_once("No MAIR allocation for EFI_MEMORY_WT; treating as Normal Non-cacheable\n");
	return __pgprot(PROT_NORMAL_NC);
}

pgprot_t __acpi_get_mem_attribute(phys_addr_t addr)
{
	/*
	 * According to "Table 8 Map: EFI memory types to AArch64 memory
	 * types" of UEFI 2.5 section 2.3.6.1, each EFI memory type is
	 * mapped to a corresponding MAIR attribute encoding.
	 * The EFI memory attribute advises all possible capabilities
	 * of a memory region.
	 */

	u64 attr;

	attr = efi_mem_attributes(addr);
	if (attr & EFI_MEMORY_WB)
		return PAGE_KERNEL;
	if (attr & EFI_MEMORY_WC)
		return __pgprot(PROT_NORMAL_NC);
	if (attr & EFI_MEMORY_WT)
		return __acpi_get_writethrough_mem_attribute();
	return __pgprot(PROT_DEVICE_nGnRnE);
}

void __iomem *acpi_os_ioremap(acpi_physical_address phys, acpi_size size)
{
	efi_memory_desc_t *md, *region = NULL;
	pgprot_t prot;

	if (WARN_ON_ONCE(!efi_enabled(EFI_MEMMAP)))
		return NULL;

	for_each_efi_memory_desc(md) {
		u64 end = md->phys_addr + (md->num_pages << EFI_PAGE_SHIFT);

		if (phys < md->phys_addr || phys >= end)
			continue;

		if (phys + size > end) {
			pr_warn(FW_BUG "requested region covers multiple EFI memory regions\n");
			return NULL;
		}
		region = md;
		break;
	}

	/*
	 * It is fine for AML to remap regions that are not represented in the
	 * EFI memory map at all, as it only describes normal memory, and MMIO
	 * regions that require a virtual mapping to make them accessible to
	 * the EFI runtime services.
	 */
	prot = __pgprot(PROT_DEVICE_nGnRnE);
	if (region) {
		switch (region->type) {
		case EFI_LOADER_CODE:
		case EFI_LOADER_DATA:
		case EFI_BOOT_SERVICES_CODE:
		case EFI_BOOT_SERVICES_DATA:
		case EFI_CONVENTIONAL_MEMORY:
		case EFI_PERSISTENT_MEMORY:
			if (memblock_is_map_memory(phys) ||
			    !memblock_is_region_memory(phys, size)) {
				pr_warn(FW_BUG "requested region covers kernel memory @ %pa\n", &phys);
				return NULL;
			}
			/*
			 * Mapping kernel memory is permitted if the region in
			 * question is covered by a single memblock with the
			 * NOMAP attribute set: this enables the use of ACPI
			 * table overrides passed via initramfs, which are
			 * reserved in memory using arch_reserve_mem_area()
			 * below. As this particular use case only requires
			 * read access, fall through to the R/O mapping case.
			 */
			fallthrough;

		case EFI_RUNTIME_SERVICES_CODE:
			/*
			 * This would be unusual, but not problematic per se,
			 * as long as we take care not to create a writable
			 * mapping for executable code.
			 */
			prot = PAGE_KERNEL_RO;
			break;

		case EFI_ACPI_RECLAIM_MEMORY:
			/*
			 * ACPI reclaim memory is used to pass firmware tables
			 * and other data that is intended for consumption by
			 * the OS only, which may decide it wants to reclaim
			 * that memory and use it for something else. We never
			 * do that, but we usually add it to the linear map
			 * anyway, in which case we should use the existing
			 * mapping.
			 */
			if (memblock_is_map_memory(phys))
				return (void __iomem *)__phys_to_virt(phys);
			fallthrough;

		default:
			if (region->attribute & EFI_MEMORY_WB)
				prot = PAGE_KERNEL;
			else if (region->attribute & EFI_MEMORY_WC)
				prot = __pgprot(PROT_NORMAL_NC);
			else if (region->attribute & EFI_MEMORY_WT)
				prot = __acpi_get_writethrough_mem_attribute();
		}
	}
	return ioremap_prot(phys, size, pgprot_val(prot));
}

/*
 * Claim Synchronous External Aborts as a firmware first notification.
 *
 * Used by KVM and the arch do_sea handler.
 * @regs may be NULL when called from process context.
 */
int apei_claim_sea(struct pt_regs *regs)
{
	int err = -ENOENT;
	bool return_to_irqs_enabled;
	unsigned long current_flags;

	if (!IS_ENABLED(CONFIG_ACPI_APEI_GHES))
		return err;

	current_flags = local_daif_save_flags();

	/* current_flags isn't useful here as daif doesn't tell us about pNMI */
	return_to_irqs_enabled = !irqs_disabled_flags(arch_local_save_flags());

	if (regs)
		return_to_irqs_enabled = interrupts_enabled(regs);

	/*
	 * SEA can interrupt SError, mask it and describe this as an NMI so
	 * that APEI defers the handling.
	 */
	local_daif_restore(DAIF_ERRCTX);
	nmi_enter();
	err = ghes_notify_sea();
	nmi_exit();

	/*
	 * APEI NMI-like notifications are deferred to irq_work. Unless
	 * we interrupted irqs-masked code, we can do that now.
	 */
	if (!err) {
		if (return_to_irqs_enabled) {
			local_daif_restore(DAIF_PROCCTX_NOIRQ);
			__irq_enter();
			irq_work_run();
			__irq_exit();
		} else {
			pr_warn_ratelimited("APEI work queued but not completed");
			err = -EINPROGRESS;
		}
	}

	local_daif_restore(current_flags);

	return err;
}

void arch_reserve_mem_area(acpi_physical_address addr, size_t size)
{
	memblock_mark_nomap(addr, size);
}

#ifdef CONFIG_ACPI_FFH
/*
 * Implements ARM64 specific callbacks to support ACPI FFH Operation Region as
 * specified in https://developer.arm.com/docs/den0048/latest
 */
struct acpi_ffh_data {
	struct acpi_ffh_info info;
	void (*invoke_ffh_fn)(unsigned long a0, unsigned long a1,
			      unsigned long a2, unsigned long a3,
			      unsigned long a4, unsigned long a5,
			      unsigned long a6, unsigned long a7,
			      struct arm_smccc_res *args,
			      struct arm_smccc_quirk *res);
	void (*invoke_ffh64_fn)(const struct arm_smccc_1_2_regs *args,
				struct arm_smccc_1_2_regs *res);
};

int acpi_ffh_address_space_arch_setup(void *handler_ctxt, void **region_ctxt)
{
	enum arm_smccc_conduit conduit;
	struct acpi_ffh_data *ffh_ctxt;

	ffh_ctxt = kzalloc(sizeof(*ffh_ctxt), GFP_KERNEL);
	if (!ffh_ctxt)
		return -ENOMEM;

	if (arm_smccc_get_version() < ARM_SMCCC_VERSION_1_2)
		return -EOPNOTSUPP;

	conduit = arm_smccc_1_1_get_conduit();
	if (conduit == SMCCC_CONDUIT_NONE) {
		pr_err("%s: invalid SMCCC conduit\n", __func__);
		return -EOPNOTSUPP;
	}

	if (conduit == SMCCC_CONDUIT_SMC) {
		ffh_ctxt->invoke_ffh_fn = __arm_smccc_smc;
		ffh_ctxt->invoke_ffh64_fn = arm_smccc_1_2_smc;
	} else {
		ffh_ctxt->invoke_ffh_fn = __arm_smccc_hvc;
		ffh_ctxt->invoke_ffh64_fn = arm_smccc_1_2_hvc;
	}

	memcpy(ffh_ctxt, handler_ctxt, sizeof(ffh_ctxt->info));

	*region_ctxt = ffh_ctxt;
	return AE_OK;
}

static bool acpi_ffh_smccc_owner_allowed(u32 fid)
{
	int owner = ARM_SMCCC_OWNER_NUM(fid);

	if (owner == ARM_SMCCC_OWNER_STANDARD ||
	    owner == ARM_SMCCC_OWNER_SIP || owner == ARM_SMCCC_OWNER_OEM)
		return true;

	return false;
}

int acpi_ffh_address_space_arch_handler(acpi_integer *value, void *region_context)
{
	int ret = 0;
	struct acpi_ffh_data *ffh_ctxt = region_context;

	if (ffh_ctxt->info.offset == 0) {
		/* SMC/HVC 32bit call */
		struct arm_smccc_res res;
		u32 a[8] = { 0 }, *ptr = (u32 *)value;

		if (!ARM_SMCCC_IS_FAST_CALL(*ptr) || ARM_SMCCC_IS_64(*ptr) ||
		    !acpi_ffh_smccc_owner_allowed(*ptr) ||
		    ffh_ctxt->info.length > 32) {
			ret = AE_ERROR;
		} else {
			int idx, len = ffh_ctxt->info.length >> 2;

			for (idx = 0; idx < len; idx++)
				a[idx] = *(ptr + idx);

			ffh_ctxt->invoke_ffh_fn(a[0], a[1], a[2], a[3], a[4],
						a[5], a[6], a[7], &res, NULL);
			memcpy(value, &res, sizeof(res));
		}

	} else if (ffh_ctxt->info.offset == 1) {
		/* SMC/HVC 64bit call */
		struct arm_smccc_1_2_regs *r = (struct arm_smccc_1_2_regs *)value;

		if (!ARM_SMCCC_IS_FAST_CALL(r->a0) || !ARM_SMCCC_IS_64(r->a0) ||
		    !acpi_ffh_smccc_owner_allowed(r->a0) ||
		    ffh_ctxt->info.length > sizeof(*r)) {
			ret = AE_ERROR;
		} else {
			ffh_ctxt->invoke_ffh64_fn(r, r);
			memcpy(value, r, ffh_ctxt->info.length);
		}
	} else {
		ret = AE_ERROR;
	}

	return ret;
}
#endif /* CONFIG_ACPI_FFH */
