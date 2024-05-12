// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  64-bit pSeries and RS/6000 setup code.
 *
 *  Copyright (C) 1995  Linus Torvalds
 *  Adapted from 'alpha' version by Gary Thomas
 *  Modified by Cort Dougan (cort@cs.nmt.edu)
 *  Modified by PPC64 Team, IBM Corp
 */

/*
 * bootup setup stuff..
 */

#include <linux/cpu.h>
#include <linux/errno.h>
#include <linux/platform_device.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/user.h>
#include <linux/tty.h>
#include <linux/major.h>
#include <linux/interrupt.h>
#include <linux/reboot.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/console.h>
#include <linux/pci.h>
#include <linux/utsname.h>
#include <linux/adb.h>
#include <linux/export.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/seq_file.h>
#include <linux/root_dev.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_pci.h>
#include <linux/memblock.h>
#include <linux/swiotlb.h>
#include <linux/seq_buf.h>

#include <asm/mmu.h>
#include <asm/processor.h>
#include <asm/io.h>
#include <asm/rtas.h>
#include <asm/pci-bridge.h>
#include <asm/iommu.h>
#include <asm/dma.h>
#include <asm/machdep.h>
#include <asm/irq.h>
#include <asm/time.h>
#include <asm/nvram.h>
#include <asm/pmc.h>
#include <asm/xics.h>
#include <asm/xive.h>
#include <asm/papr-sysparm.h>
#include <asm/ppc-pci.h>
#include <asm/i8259.h>
#include <asm/udbg.h>
#include <asm/smp.h>
#include <asm/firmware.h>
#include <asm/eeh.h>
#include <asm/reg.h>
#include <asm/plpar_wrappers.h>
#include <asm/kexec.h>
#include <asm/isa-bridge.h>
#include <asm/security_features.h>
#include <asm/asm-const.h>
#include <asm/idle.h>
#include <asm/swiotlb.h>
#include <asm/svm.h>
#include <asm/dtl.h>
#include <asm/hvconsole.h>
#include <asm/setup.h>

#include "pseries.h"

DEFINE_STATIC_KEY_FALSE(shared_processor);
EXPORT_SYMBOL(shared_processor);

#ifdef CONFIG_PARAVIRT_TIME_ACCOUNTING
struct static_key paravirt_steal_enabled;
struct static_key paravirt_steal_rq_enabled;

static bool steal_acc = true;
static int __init parse_no_stealacc(char *arg)
{
	steal_acc = false;
	return 0;
}

early_param("no-steal-acc", parse_no_stealacc);
#endif

int CMO_PrPSP = -1;
int CMO_SecPSP = -1;
unsigned long CMO_PageSize = (ASM_CONST(1) << IOMMU_PAGE_SHIFT_4K);
EXPORT_SYMBOL(CMO_PageSize);

int fwnmi_active;  /* TRUE if an FWNMI handler is present */
int ibm_nmi_interlock_token;
u32 pseries_security_flavor;

static void pSeries_show_cpuinfo(struct seq_file *m)
{
	struct device_node *root;
	const char *model = "";

	root = of_find_node_by_path("/");
	if (root)
		model = of_get_property(root, "model", NULL);
	seq_printf(m, "machine\t\t: CHRP %s\n", model);
	of_node_put(root);
	if (radix_enabled())
		seq_printf(m, "MMU\t\t: Radix\n");
	else
		seq_printf(m, "MMU\t\t: Hash\n");
}

/* Initialize firmware assisted non-maskable interrupts if
 * the firmware supports this feature.
 */
static void __init fwnmi_init(void)
{
	unsigned long system_reset_addr, machine_check_addr;
	u8 *mce_data_buf;
	unsigned int i;
	int nr_cpus = num_possible_cpus();
#ifdef CONFIG_PPC_64S_HASH_MMU
	struct slb_entry *slb_ptr;
	size_t size;
#endif
	int ibm_nmi_register_token;

	ibm_nmi_register_token = rtas_function_token(RTAS_FN_IBM_NMI_REGISTER);
	if (ibm_nmi_register_token == RTAS_UNKNOWN_SERVICE)
		return;

	ibm_nmi_interlock_token = rtas_function_token(RTAS_FN_IBM_NMI_INTERLOCK);
	if (WARN_ON(ibm_nmi_interlock_token == RTAS_UNKNOWN_SERVICE))
		return;

	/* If the kernel's not linked at zero we point the firmware at low
	 * addresses anyway, and use a trampoline to get to the real code. */
	system_reset_addr  = __pa(system_reset_fwnmi) - PHYSICAL_START;
	machine_check_addr = __pa(machine_check_fwnmi) - PHYSICAL_START;

	if (0 == rtas_call(ibm_nmi_register_token, 2, 1, NULL,
			   system_reset_addr, machine_check_addr))
		fwnmi_active = 1;

	/*
	 * Allocate a chunk for per cpu buffer to hold rtas errorlog.
	 * It will be used in real mode mce handler, hence it needs to be
	 * below RMA.
	 */
	mce_data_buf = memblock_alloc_try_nid_raw(RTAS_ERROR_LOG_MAX * nr_cpus,
					RTAS_ERROR_LOG_MAX, MEMBLOCK_LOW_LIMIT,
					ppc64_rma_size, NUMA_NO_NODE);
	if (!mce_data_buf)
		panic("Failed to allocate %d bytes below %pa for MCE buffer\n",
		      RTAS_ERROR_LOG_MAX * nr_cpus, &ppc64_rma_size);

	for_each_possible_cpu(i) {
		paca_ptrs[i]->mce_data_buf = mce_data_buf +
						(RTAS_ERROR_LOG_MAX * i);
	}

#ifdef CONFIG_PPC_64S_HASH_MMU
	if (!radix_enabled()) {
		/* Allocate per cpu area to save old slb contents during MCE */
		size = sizeof(struct slb_entry) * mmu_slb_size * nr_cpus;
		slb_ptr = memblock_alloc_try_nid_raw(size,
				sizeof(struct slb_entry), MEMBLOCK_LOW_LIMIT,
				ppc64_rma_size, NUMA_NO_NODE);
		if (!slb_ptr)
			panic("Failed to allocate %zu bytes below %pa for slb area\n",
			      size, &ppc64_rma_size);

		for_each_possible_cpu(i)
			paca_ptrs[i]->mce_faulty_slbs = slb_ptr + (mmu_slb_size * i);
	}
#endif
}

/*
 * Affix a device for the first timer to the platform bus if
 * we have firmware support for the H_WATCHDOG hypercall.
 */
static __init int pseries_wdt_init(void)
{
	if (firmware_has_feature(FW_FEATURE_WATCHDOG))
		platform_device_register_simple("pseries-wdt", 0, NULL, 0);
	return 0;
}
machine_subsys_initcall(pseries, pseries_wdt_init);

static void pseries_8259_cascade(struct irq_desc *desc)
{
	struct irq_chip *chip = irq_desc_get_chip(desc);
	unsigned int cascade_irq = i8259_irq();

	if (cascade_irq)
		generic_handle_irq(cascade_irq);

	chip->irq_eoi(&desc->irq_data);
}

static void __init pseries_setup_i8259_cascade(void)
{
	struct device_node *np, *old, *found = NULL;
	unsigned int cascade;
	const u32 *addrp;
	unsigned long intack = 0;
	int naddr;

	for_each_node_by_type(np, "interrupt-controller") {
		if (of_device_is_compatible(np, "chrp,iic")) {
			found = np;
			break;
		}
	}

	if (found == NULL) {
		printk(KERN_DEBUG "pic: no ISA interrupt controller\n");
		return;
	}

	cascade = irq_of_parse_and_map(found, 0);
	if (!cascade) {
		printk(KERN_ERR "pic: failed to map cascade interrupt");
		return;
	}
	pr_debug("pic: cascade mapped to irq %d\n", cascade);

	for (old = of_node_get(found); old != NULL ; old = np) {
		np = of_get_parent(old);
		of_node_put(old);
		if (np == NULL)
			break;
		if (!of_node_name_eq(np, "pci"))
			continue;
		addrp = of_get_property(np, "8259-interrupt-acknowledge", NULL);
		if (addrp == NULL)
			continue;
		naddr = of_n_addr_cells(np);
		intack = addrp[naddr-1];
		if (naddr > 1)
			intack |= ((unsigned long)addrp[naddr-2]) << 32;
	}
	if (intack)
		printk(KERN_DEBUG "pic: PCI 8259 intack at 0x%016lx\n", intack);
	i8259_init(found, intack);
	of_node_put(found);
	irq_set_chained_handler(cascade, pseries_8259_cascade);
}

static void __init pseries_init_irq(void)
{
	/* Try using a XIVE if available, otherwise use a XICS */
	if (!xive_spapr_init()) {
		xics_init();
		pseries_setup_i8259_cascade();
	}
}

static void pseries_lpar_enable_pmcs(void)
{
	unsigned long set, reset;

	set = 1UL << 63;
	reset = 0;
	plpar_hcall_norets(H_PERFMON, set, reset);
}

static int pci_dn_reconfig_notifier(struct notifier_block *nb, unsigned long action, void *data)
{
	struct of_reconfig_data *rd = data;
	struct device_node *parent, *np = rd->dn;
	struct pci_dn *pdn;
	int err = NOTIFY_OK;

	switch (action) {
	case OF_RECONFIG_ATTACH_NODE:
		parent = of_get_parent(np);
		pdn = parent ? PCI_DN(parent) : NULL;
		if (pdn)
			pci_add_device_node_info(pdn->phb, np);

		of_node_put(parent);
		break;
	case OF_RECONFIG_DETACH_NODE:
		pdn = PCI_DN(np);
		if (pdn)
			list_del(&pdn->list);
		break;
	default:
		err = NOTIFY_DONE;
		break;
	}
	return err;
}

static struct notifier_block pci_dn_reconfig_nb = {
	.notifier_call = pci_dn_reconfig_notifier,
};

struct kmem_cache *dtl_cache;

#ifdef CONFIG_VIRT_CPU_ACCOUNTING_NATIVE
/*
 * Allocate space for the dispatch trace log for all possible cpus
 * and register the buffers with the hypervisor.  This is used for
 * computing time stolen by the hypervisor.
 */
static int alloc_dispatch_logs(void)
{
	if (!firmware_has_feature(FW_FEATURE_SPLPAR))
		return 0;

	if (!dtl_cache)
		return 0;

	alloc_dtl_buffers(0);

	/* Register the DTL for the current (boot) cpu */
	register_dtl_buffer(smp_processor_id());

	return 0;
}
#else /* !CONFIG_VIRT_CPU_ACCOUNTING_NATIVE */
static inline int alloc_dispatch_logs(void)
{
	return 0;
}
#endif /* CONFIG_VIRT_CPU_ACCOUNTING_NATIVE */

static int alloc_dispatch_log_kmem_cache(void)
{
	void (*ctor)(void *) = get_dtl_cache_ctor();

	dtl_cache = kmem_cache_create("dtl", DISPATCH_LOG_BYTES,
						DISPATCH_LOG_BYTES, 0, ctor);
	if (!dtl_cache) {
		pr_warn("Failed to create dispatch trace log buffer cache\n");
		pr_warn("Stolen time statistics will be unreliable\n");
		return 0;
	}

	return alloc_dispatch_logs();
}
machine_early_initcall(pseries, alloc_dispatch_log_kmem_cache);

DEFINE_PER_CPU(u64, idle_spurr_cycles);
DEFINE_PER_CPU(u64, idle_entry_purr_snap);
DEFINE_PER_CPU(u64, idle_entry_spurr_snap);
static void pseries_lpar_idle(void)
{
	/*
	 * Default handler to go into low thread priority and possibly
	 * low power mode by ceding processor to hypervisor
	 */

	if (!prep_irq_for_idle())
		return;

	/* Indicate to hypervisor that we are idle. */
	pseries_idle_prolog();

	/*
	 * Yield the processor to the hypervisor.  We return if
	 * an external interrupt occurs (which are driven prior
	 * to returning here) or if a prod occurs from another
	 * processor. When returning here, external interrupts
	 * are enabled.
	 */
	cede_processor();

	pseries_idle_epilog();
}

static bool pseries_reloc_on_exception_enabled;

bool pseries_reloc_on_exception(void)
{
	return pseries_reloc_on_exception_enabled;
}
EXPORT_SYMBOL_GPL(pseries_reloc_on_exception);

/*
 * Enable relocation on during exceptions. This has partition wide scope and
 * may take a while to complete, if it takes longer than one second we will
 * just give up rather than wasting any more time on this - if that turns out
 * to ever be a problem in practice we can move this into a kernel thread to
 * finish off the process later in boot.
 */
bool pseries_enable_reloc_on_exc(void)
{
	long rc;
	unsigned int delay, total_delay = 0;

	while (1) {
		rc = enable_reloc_on_exceptions();
		if (!H_IS_LONG_BUSY(rc)) {
			if (rc == H_P2) {
				pr_info("Relocation on exceptions not"
					" supported\n");
				return false;
			} else if (rc != H_SUCCESS) {
				pr_warn("Unable to enable relocation"
					" on exceptions: %ld\n", rc);
				return false;
			}
			pseries_reloc_on_exception_enabled = true;
			return true;
		}

		delay = get_longbusy_msecs(rc);
		total_delay += delay;
		if (total_delay > 1000) {
			pr_warn("Warning: Giving up waiting to enable "
				"relocation on exceptions (%u msec)!\n",
				total_delay);
			return false;
		}

		mdelay(delay);
	}
}
EXPORT_SYMBOL(pseries_enable_reloc_on_exc);

void pseries_disable_reloc_on_exc(void)
{
	long rc;

	while (1) {
		rc = disable_reloc_on_exceptions();
		if (!H_IS_LONG_BUSY(rc))
			break;
		mdelay(get_longbusy_msecs(rc));
	}
	if (rc == H_SUCCESS)
		pseries_reloc_on_exception_enabled = false;
	else
		pr_warn("Warning: Failed to disable relocation on exceptions: %ld\n",
			rc);
}
EXPORT_SYMBOL(pseries_disable_reloc_on_exc);

#ifdef __LITTLE_ENDIAN__
void pseries_big_endian_exceptions(void)
{
	long rc;

	while (1) {
		rc = enable_big_endian_exceptions();
		if (!H_IS_LONG_BUSY(rc))
			break;
		mdelay(get_longbusy_msecs(rc));
	}

	/*
	 * At this point it is unlikely panic() will get anything
	 * out to the user, since this is called very late in kexec
	 * but at least this will stop us from continuing on further
	 * and creating an even more difficult to debug situation.
	 *
	 * There is a known problem when kdump'ing, if cpus are offline
	 * the above call will fail. Rather than panicking again, keep
	 * going and hope the kdump kernel is also little endian, which
	 * it usually is.
	 */
	if (rc && !kdump_in_progress())
		panic("Could not enable big endian exceptions");
}

void __init pseries_little_endian_exceptions(void)
{
	long rc;

	while (1) {
		rc = enable_little_endian_exceptions();
		if (!H_IS_LONG_BUSY(rc))
			break;
		mdelay(get_longbusy_msecs(rc));
	}
	if (rc) {
		ppc_md.progress("H_SET_MODE LE exception fail", 0);
		panic("Could not enable little endian exceptions");
	}
}
#endif

static void __init pSeries_discover_phbs(void)
{
	struct device_node *node;
	struct pci_controller *phb;
	struct device_node *root = of_find_node_by_path("/");

	for_each_child_of_node(root, node) {
		if (!of_node_is_type(node, "pci") &&
		    !of_node_is_type(node, "pciex"))
			continue;

		phb = pcibios_alloc_controller(node);
		if (!phb)
			continue;
		rtas_setup_phb(phb);
		pci_process_bridge_OF_ranges(phb, node, 0);
		isa_bridge_find_early(phb);
		phb->controller_ops = pseries_pci_controller_ops;

		/* create pci_dn's for DT nodes under this PHB */
		pci_devs_phb_init_dynamic(phb);

		pseries_msi_allocate_domains(phb);
	}

	of_node_put(root);

	/*
	 * PCI_PROBE_ONLY and PCI_REASSIGN_ALL_BUS can be set via properties
	 * in chosen.
	 */
	of_pci_check_probe_only();
}

static void init_cpu_char_feature_flags(struct h_cpu_char_result *result)
{
	/*
	 * The features below are disabled by default, so we instead look to see
	 * if firmware has *enabled* them, and set them if so.
	 */
	if (result->character & H_CPU_CHAR_SPEC_BAR_ORI31)
		security_ftr_set(SEC_FTR_SPEC_BAR_ORI31);

	if (result->character & H_CPU_CHAR_BCCTRL_SERIALISED)
		security_ftr_set(SEC_FTR_BCCTRL_SERIALISED);

	if (result->character & H_CPU_CHAR_L1D_FLUSH_ORI30)
		security_ftr_set(SEC_FTR_L1D_FLUSH_ORI30);

	if (result->character & H_CPU_CHAR_L1D_FLUSH_TRIG2)
		security_ftr_set(SEC_FTR_L1D_FLUSH_TRIG2);

	if (result->character & H_CPU_CHAR_L1D_THREAD_PRIV)
		security_ftr_set(SEC_FTR_L1D_THREAD_PRIV);

	if (result->character & H_CPU_CHAR_COUNT_CACHE_DISABLED)
		security_ftr_set(SEC_FTR_COUNT_CACHE_DISABLED);

	if (result->character & H_CPU_CHAR_BCCTR_FLUSH_ASSIST)
		security_ftr_set(SEC_FTR_BCCTR_FLUSH_ASSIST);

	if (result->character & H_CPU_CHAR_BCCTR_LINK_FLUSH_ASSIST)
		security_ftr_set(SEC_FTR_BCCTR_LINK_FLUSH_ASSIST);

	if (result->behaviour & H_CPU_BEHAV_FLUSH_COUNT_CACHE)
		security_ftr_set(SEC_FTR_FLUSH_COUNT_CACHE);

	if (result->behaviour & H_CPU_BEHAV_FLUSH_LINK_STACK)
		security_ftr_set(SEC_FTR_FLUSH_LINK_STACK);

	/*
	 * The features below are enabled by default, so we instead look to see
	 * if firmware has *disabled* them, and clear them if so.
	 * H_CPU_BEHAV_FAVOUR_SECURITY_H could be set only if
	 * H_CPU_BEHAV_FAVOUR_SECURITY is.
	 */
	if (!(result->behaviour & H_CPU_BEHAV_FAVOUR_SECURITY)) {
		security_ftr_clear(SEC_FTR_FAVOUR_SECURITY);
		pseries_security_flavor = 0;
	} else if (result->behaviour & H_CPU_BEHAV_FAVOUR_SECURITY_H)
		pseries_security_flavor = 1;
	else
		pseries_security_flavor = 2;

	if (!(result->behaviour & H_CPU_BEHAV_L1D_FLUSH_PR))
		security_ftr_clear(SEC_FTR_L1D_FLUSH_PR);

	if (result->behaviour & H_CPU_BEHAV_NO_L1D_FLUSH_ENTRY)
		security_ftr_clear(SEC_FTR_L1D_FLUSH_ENTRY);

	if (result->behaviour & H_CPU_BEHAV_NO_L1D_FLUSH_UACCESS)
		security_ftr_clear(SEC_FTR_L1D_FLUSH_UACCESS);

	if (result->behaviour & H_CPU_BEHAV_NO_STF_BARRIER)
		security_ftr_clear(SEC_FTR_STF_BARRIER);

	if (!(result->behaviour & H_CPU_BEHAV_BNDS_CHK_SPEC_BAR))
		security_ftr_clear(SEC_FTR_BNDS_CHK_SPEC_BAR);
}

void pseries_setup_security_mitigations(void)
{
	struct h_cpu_char_result result;
	enum l1d_flush_type types;
	bool enable;
	long rc;

	/*
	 * Set features to the defaults assumed by init_cpu_char_feature_flags()
	 * so it can set/clear again any features that might have changed after
	 * migration, and in case the hypercall fails and it is not even called.
	 */
	powerpc_security_features = SEC_FTR_DEFAULT;

	rc = plpar_get_cpu_characteristics(&result);
	if (rc == H_SUCCESS)
		init_cpu_char_feature_flags(&result);

	/*
	 * We're the guest so this doesn't apply to us, clear it to simplify
	 * handling of it elsewhere.
	 */
	security_ftr_clear(SEC_FTR_L1D_FLUSH_HV);

	types = L1D_FLUSH_FALLBACK;

	if (security_ftr_enabled(SEC_FTR_L1D_FLUSH_TRIG2))
		types |= L1D_FLUSH_MTTRIG;

	if (security_ftr_enabled(SEC_FTR_L1D_FLUSH_ORI30))
		types |= L1D_FLUSH_ORI;

	enable = security_ftr_enabled(SEC_FTR_FAVOUR_SECURITY) && \
		 security_ftr_enabled(SEC_FTR_L1D_FLUSH_PR);

	setup_rfi_flush(types, enable);
	setup_count_cache_flush();

	enable = security_ftr_enabled(SEC_FTR_FAVOUR_SECURITY) &&
		 security_ftr_enabled(SEC_FTR_L1D_FLUSH_ENTRY);
	setup_entry_flush(enable);

	enable = security_ftr_enabled(SEC_FTR_FAVOUR_SECURITY) &&
		 security_ftr_enabled(SEC_FTR_L1D_FLUSH_UACCESS);
	setup_uaccess_flush(enable);

	setup_stf_barrier();
}

#ifdef CONFIG_PCI_IOV
enum rtas_iov_fw_value_map {
	NUM_RES_PROPERTY  = 0, /* Number of Resources */
	LOW_INT           = 1, /* Lowest 32 bits of Address */
	START_OF_ENTRIES  = 2, /* Always start of entry */
	APERTURE_PROPERTY = 2, /* Start of entry+ to  Aperture Size */
	WDW_SIZE_PROPERTY = 4, /* Start of entry+ to Window Size */
	NEXT_ENTRY        = 7  /* Go to next entry on array */
};

enum get_iov_fw_value_index {
	BAR_ADDRS     = 1,    /*  Get Bar Address */
	APERTURE_SIZE = 2,    /*  Get Aperture Size */
	WDW_SIZE      = 3     /*  Get Window Size */
};

static resource_size_t pseries_get_iov_fw_value(struct pci_dev *dev, int resno,
						enum get_iov_fw_value_index value)
{
	const int *indexes;
	struct device_node *dn = pci_device_to_OF_node(dev);
	int i, num_res, ret = 0;

	indexes = of_get_property(dn, "ibm,open-sriov-vf-bar-info", NULL);
	if (!indexes)
		return  0;

	/*
	 * First element in the array is the number of Bars
	 * returned.  Search through the list to find the matching
	 * bar
	 */
	num_res = of_read_number(&indexes[NUM_RES_PROPERTY], 1);
	if (resno >= num_res)
		return 0; /* or an error */

	i = START_OF_ENTRIES + NEXT_ENTRY * resno;
	switch (value) {
	case BAR_ADDRS:
		ret = of_read_number(&indexes[i], 2);
		break;
	case APERTURE_SIZE:
		ret = of_read_number(&indexes[i + APERTURE_PROPERTY], 2);
		break;
	case WDW_SIZE:
		ret = of_read_number(&indexes[i + WDW_SIZE_PROPERTY], 2);
		break;
	}

	return ret;
}

static void of_pci_set_vf_bar_size(struct pci_dev *dev, const int *indexes)
{
	struct resource *res;
	resource_size_t base, size;
	int i, r, num_res;

	num_res = of_read_number(&indexes[NUM_RES_PROPERTY], 1);
	num_res = min_t(int, num_res, PCI_SRIOV_NUM_BARS);
	for (i = START_OF_ENTRIES, r = 0; r < num_res && r < PCI_SRIOV_NUM_BARS;
	     i += NEXT_ENTRY, r++) {
		res = &dev->resource[r + PCI_IOV_RESOURCES];
		base = of_read_number(&indexes[i], 2);
		size = of_read_number(&indexes[i + APERTURE_PROPERTY], 2);
		res->flags = pci_parse_of_flags(of_read_number
						(&indexes[i + LOW_INT], 1), 0);
		res->flags |= (IORESOURCE_MEM_64 | IORESOURCE_PCI_FIXED);
		res->name = pci_name(dev);
		res->start = base;
		res->end = base + size - 1;
	}
}

static void of_pci_parse_iov_addrs(struct pci_dev *dev, const int *indexes)
{
	struct resource *res, *root, *conflict;
	resource_size_t base, size;
	int i, r, num_res;

	/*
	 * First element in the array is the number of Bars
	 * returned.  Search through the list to find the matching
	 * bars assign them from firmware into resources structure.
	 */
	num_res = of_read_number(&indexes[NUM_RES_PROPERTY], 1);
	for (i = START_OF_ENTRIES, r = 0; r < num_res && r < PCI_SRIOV_NUM_BARS;
	     i += NEXT_ENTRY, r++) {
		res = &dev->resource[r + PCI_IOV_RESOURCES];
		base = of_read_number(&indexes[i], 2);
		size = of_read_number(&indexes[i + WDW_SIZE_PROPERTY], 2);
		res->name = pci_name(dev);
		res->start = base;
		res->end = base + size - 1;
		root = &iomem_resource;
		dev_dbg(&dev->dev,
			"pSeries IOV BAR %d: trying firmware assignment %pR\n",
			 r + PCI_IOV_RESOURCES, res);
		conflict = request_resource_conflict(root, res);
		if (conflict) {
			dev_info(&dev->dev,
				 "BAR %d: %pR conflicts with %s %pR\n",
				 r + PCI_IOV_RESOURCES, res,
				 conflict->name, conflict);
			res->flags |= IORESOURCE_UNSET;
		}
	}
}

static void pseries_disable_sriov_resources(struct pci_dev *pdev)
{
	int i;

	pci_warn(pdev, "No hypervisor support for SR-IOV on this device, IOV BARs disabled.\n");
	for (i = 0; i < PCI_SRIOV_NUM_BARS; i++)
		pdev->resource[i + PCI_IOV_RESOURCES].flags = 0;
}

static void pseries_pci_fixup_resources(struct pci_dev *pdev)
{
	const int *indexes;
	struct device_node *dn = pci_device_to_OF_node(pdev);

	/*Firmware must support open sriov otherwise dont configure*/
	indexes = of_get_property(dn, "ibm,open-sriov-vf-bar-info", NULL);
	if (indexes)
		of_pci_set_vf_bar_size(pdev, indexes);
	else
		pseries_disable_sriov_resources(pdev);
}

static void pseries_pci_fixup_iov_resources(struct pci_dev *pdev)
{
	const int *indexes;
	struct device_node *dn = pci_device_to_OF_node(pdev);

	if (!pdev->is_physfn)
		return;
	/*Firmware must support open sriov otherwise don't configure*/
	indexes = of_get_property(dn, "ibm,open-sriov-vf-bar-info", NULL);
	if (indexes)
		of_pci_parse_iov_addrs(pdev, indexes);
	else
		pseries_disable_sriov_resources(pdev);
}

static resource_size_t pseries_pci_iov_resource_alignment(struct pci_dev *pdev,
							  int resno)
{
	const __be32 *reg;
	struct device_node *dn = pci_device_to_OF_node(pdev);

	/*Firmware must support open sriov otherwise report regular alignment*/
	reg = of_get_property(dn, "ibm,is-open-sriov-pf", NULL);
	if (!reg)
		return pci_iov_resource_size(pdev, resno);

	if (!pdev->is_physfn)
		return 0;
	return pseries_get_iov_fw_value(pdev,
					resno - PCI_IOV_RESOURCES,
					APERTURE_SIZE);
}
#endif

static void __init pSeries_setup_arch(void)
{
	set_arch_panic_timeout(10, ARCH_PANIC_TIMEOUT);

	/* Discover PIC type and setup ppc_md accordingly */
	smp_init_pseries();


	if (radix_enabled() && !mmu_has_feature(MMU_FTR_GTSE))
		if (!firmware_has_feature(FW_FEATURE_RPT_INVALIDATE))
			panic("BUG: Radix support requires either GTSE or RPT_INVALIDATE\n");


	/* openpic global configuration register (64-bit format). */
	/* openpic Interrupt Source Unit pointer (64-bit format). */
	/* python0 facility area (mmio) (64-bit format) REAL address. */

	/* init to some ~sane value until calibrate_delay() runs */
	loops_per_jiffy = 50000000;

	fwnmi_init();

	pseries_setup_security_mitigations();
	if (!radix_enabled())
		pseries_lpar_read_hblkrm_characteristics();

	/* By default, only probe PCI (can be overridden by rtas_pci) */
	pci_add_flags(PCI_PROBE_ONLY);

	/* Find and initialize PCI host bridges */
	init_pci_config_tokens();
	of_reconfig_notifier_register(&pci_dn_reconfig_nb);

	pSeries_nvram_init();

	if (firmware_has_feature(FW_FEATURE_LPAR)) {
		vpa_init(boot_cpuid);

		if (lppaca_shared_proc(get_lppaca())) {
			static_branch_enable(&shared_processor);
			pv_spinlocks_init();
#ifdef CONFIG_PARAVIRT_TIME_ACCOUNTING
			static_key_slow_inc(&paravirt_steal_enabled);
			if (steal_acc)
				static_key_slow_inc(&paravirt_steal_rq_enabled);
#endif
		}

		ppc_md.power_save = pseries_lpar_idle;
		ppc_md.enable_pmcs = pseries_lpar_enable_pmcs;
#ifdef CONFIG_PCI_IOV
		ppc_md.pcibios_fixup_resources =
			pseries_pci_fixup_resources;
		ppc_md.pcibios_fixup_sriov =
			pseries_pci_fixup_iov_resources;
		ppc_md.pcibios_iov_resource_alignment =
			pseries_pci_iov_resource_alignment;
#endif
	} else {
		/* No special idle routine */
		ppc_md.enable_pmcs = power4_enable_pmcs;
	}

	ppc_md.pcibios_root_bridge_prepare = pseries_root_bridge_prepare;
	pseries_rng_init();
}

static void pseries_panic(char *str)
{
	panic_flush_kmsg_end();
	rtas_os_term(str);
}

static int __init pSeries_init_panel(void)
{
	/* Manually leave the kernel version on the panel. */
#ifdef __BIG_ENDIAN__
	ppc_md.progress("Linux ppc64\n", 0);
#else
	ppc_md.progress("Linux ppc64le\n", 0);
#endif
	ppc_md.progress(init_utsname()->version, 0);

	return 0;
}
machine_arch_initcall(pseries, pSeries_init_panel);

static int pseries_set_dabr(unsigned long dabr, unsigned long dabrx)
{
	return plpar_hcall_norets(H_SET_DABR, dabr);
}

static int pseries_set_xdabr(unsigned long dabr, unsigned long dabrx)
{
	/* Have to set at least one bit in the DABRX according to PAPR */
	if (dabrx == 0 && dabr == 0)
		dabrx = DABRX_USER;
	/* PAPR says we can only set kernel and user bits */
	dabrx &= DABRX_KERNEL | DABRX_USER;

	return plpar_hcall_norets(H_SET_XDABR, dabr, dabrx);
}

static int pseries_set_dawr(int nr, unsigned long dawr, unsigned long dawrx)
{
	/* PAPR says we can't set HYP */
	dawrx &= ~DAWRX_HYP;

	if (nr == 0)
		return plpar_set_watchpoint0(dawr, dawrx);
	else
		return plpar_set_watchpoint1(dawr, dawrx);
}

#define CMO_CHARACTERISTICS_TOKEN 44
#define CMO_MAXLENGTH 1026

void pSeries_coalesce_init(void)
{
	struct hvcall_mpp_x_data mpp_x_data;

	if (firmware_has_feature(FW_FEATURE_CMO) && !h_get_mpp_x(&mpp_x_data))
		powerpc_firmware_features |= FW_FEATURE_XCMO;
	else
		powerpc_firmware_features &= ~FW_FEATURE_XCMO;
}

/**
 * fw_cmo_feature_init - FW_FEATURE_CMO is not stored in ibm,hypertas-functions,
 * handle that here. (Stolen from parse_system_parameter_string)
 */
static void __init pSeries_cmo_feature_init(void)
{
	static struct papr_sysparm_buf buf __initdata;
	static_assert(sizeof(buf.val) >= CMO_MAXLENGTH);
	char *ptr, *key, *value, *end;
	int page_order = IOMMU_PAGE_SHIFT_4K;

	pr_debug(" -> fw_cmo_feature_init()\n");

	if (papr_sysparm_get(PAPR_SYSPARM_COOP_MEM_OVERCOMMIT_ATTRS, &buf)) {
		pr_debug("CMO not available\n");
		pr_debug(" <- fw_cmo_feature_init()\n");
		return;
	}

	end = &buf.val[CMO_MAXLENGTH];
	ptr = &buf.val[0];
	key = value = ptr;

	while (*ptr && (ptr <= end)) {
		/* Separate the key and value by replacing '=' with '\0' and
		 * point the value at the string after the '='
		 */
		if (ptr[0] == '=') {
			ptr[0] = '\0';
			value = ptr + 1;
		} else if (ptr[0] == '\0' || ptr[0] == ',') {
			/* Terminate the string containing the key/value pair */
			ptr[0] = '\0';

			if (key == value) {
				pr_debug("Malformed key/value pair\n");
				/* Never found a '=', end processing */
				break;
			}

			if (0 == strcmp(key, "CMOPageSize"))
				page_order = simple_strtol(value, NULL, 10);
			else if (0 == strcmp(key, "PrPSP"))
				CMO_PrPSP = simple_strtol(value, NULL, 10);
			else if (0 == strcmp(key, "SecPSP"))
				CMO_SecPSP = simple_strtol(value, NULL, 10);
			value = key = ptr + 1;
		}
		ptr++;
	}

	/* Page size is returned as the power of 2 of the page size,
	 * convert to the page size in bytes before returning
	 */
	CMO_PageSize = 1 << page_order;
	pr_debug("CMO_PageSize = %lu\n", CMO_PageSize);

	if (CMO_PrPSP != -1 || CMO_SecPSP != -1) {
		pr_info("CMO enabled\n");
		pr_debug("CMO enabled, PrPSP=%d, SecPSP=%d\n", CMO_PrPSP,
		         CMO_SecPSP);
		powerpc_firmware_features |= FW_FEATURE_CMO;
		pSeries_coalesce_init();
	} else
		pr_debug("CMO not enabled, PrPSP=%d, SecPSP=%d\n", CMO_PrPSP,
		         CMO_SecPSP);
	pr_debug(" <- fw_cmo_feature_init()\n");
}

static void __init pseries_add_hw_description(void)
{
	struct device_node *dn;
	const char *s;

	dn = of_find_node_by_path("/openprom");
	if (dn) {
		if (of_property_read_string(dn, "model", &s) == 0)
			seq_buf_printf(&ppc_hw_desc, "of:%s ", s);

		of_node_put(dn);
	}

	dn = of_find_node_by_path("/hypervisor");
	if (dn) {
		if (of_property_read_string(dn, "compatible", &s) == 0)
			seq_buf_printf(&ppc_hw_desc, "hv:%s ", s);

		of_node_put(dn);
		return;
	}

	if (of_property_read_bool(of_root, "ibm,powervm-partition") ||
	    of_property_read_bool(of_root, "ibm,fw-net-version"))
		seq_buf_printf(&ppc_hw_desc, "hv:phyp ");
}

/*
 * Early initialization.  Relocation is on but do not reference unbolted pages
 */
static void __init pseries_init(void)
{
	pr_debug(" -> pseries_init()\n");

	pseries_add_hw_description();

#ifdef CONFIG_HVC_CONSOLE
	if (firmware_has_feature(FW_FEATURE_LPAR))
		hvc_vio_init_early();
#endif
	if (firmware_has_feature(FW_FEATURE_XDABR))
		ppc_md.set_dabr = pseries_set_xdabr;
	else if (firmware_has_feature(FW_FEATURE_DABR))
		ppc_md.set_dabr = pseries_set_dabr;

	if (firmware_has_feature(FW_FEATURE_SET_MODE))
		ppc_md.set_dawr = pseries_set_dawr;

	pSeries_cmo_feature_init();
	iommu_init_early_pSeries();

	pr_debug(" <- pseries_init()\n");
}

/**
 * pseries_power_off - tell firmware about how to power off the system.
 *
 * This function calls either the power-off rtas token in normal cases
 * or the ibm,power-off-ups token (if present & requested) in case of
 * a power failure. If power-off token is used, power on will only be
 * possible with power button press. If ibm,power-off-ups token is used
 * it will allow auto poweron after power is restored.
 */
static void pseries_power_off(void)
{
	int rc;
	int rtas_poweroff_ups_token = rtas_function_token(RTAS_FN_IBM_POWER_OFF_UPS);

	if (rtas_flash_term_hook)
		rtas_flash_term_hook(SYS_POWER_OFF);

	if (rtas_poweron_auto == 0 ||
		rtas_poweroff_ups_token == RTAS_UNKNOWN_SERVICE) {
		rc = rtas_call(rtas_function_token(RTAS_FN_POWER_OFF), 2, 1, NULL, -1, -1);
		printk(KERN_INFO "RTAS power-off returned %d\n", rc);
	} else {
		rc = rtas_call(rtas_poweroff_ups_token, 0, 1, NULL);
		printk(KERN_INFO "RTAS ibm,power-off-ups returned %d\n", rc);
	}
	for (;;);
}

static int __init pSeries_probe(void)
{
	if (!of_node_is_type(of_root, "chrp"))
		return 0;

	/* Cell blades firmware claims to be chrp while it's not. Until this
	 * is fixed, we need to avoid those here.
	 */
	if (of_machine_is_compatible("IBM,CPBW-1.0") ||
	    of_machine_is_compatible("IBM,CBEA"))
		return 0;

	pm_power_off = pseries_power_off;

	pr_debug("Machine is%s LPAR !\n",
	         (powerpc_firmware_features & FW_FEATURE_LPAR) ? "" : " not");

	pseries_init();

	return 1;
}

static int pSeries_pci_probe_mode(struct pci_bus *bus)
{
	if (firmware_has_feature(FW_FEATURE_LPAR))
		return PCI_PROBE_DEVTREE;
	return PCI_PROBE_NORMAL;
}

struct pci_controller_ops pseries_pci_controller_ops = {
	.probe_mode		= pSeries_pci_probe_mode,
#ifdef CONFIG_SPAPR_TCE_IOMMU
	.device_group		= pSeries_pci_device_group,
#endif
};

define_machine(pseries) {
	.name			= "pSeries",
	.probe			= pSeries_probe,
	.setup_arch		= pSeries_setup_arch,
	.init_IRQ		= pseries_init_irq,
	.show_cpuinfo		= pSeries_show_cpuinfo,
	.log_error		= pSeries_log_error,
	.discover_phbs		= pSeries_discover_phbs,
	.pcibios_fixup		= pSeries_final_fixup,
	.restart		= rtas_restart,
	.halt			= rtas_halt,
	.panic			= pseries_panic,
	.get_boot_time		= rtas_get_boot_time,
	.get_rtc_time		= rtas_get_rtc_time,
	.set_rtc_time		= rtas_set_rtc_time,
	.progress		= rtas_progress,
	.system_reset_exception = pSeries_system_reset_exception,
	.machine_check_early	= pseries_machine_check_realmode,
	.machine_check_exception = pSeries_machine_check_exception,
	.machine_check_log_err	= pSeries_machine_check_log_err,
#ifdef CONFIG_KEXEC_CORE
	.machine_kexec          = pseries_machine_kexec,
	.kexec_cpu_down         = pseries_kexec_cpu_down,
#endif
#ifdef CONFIG_MEMORY_HOTPLUG
	.memory_block_size	= pseries_memory_block_size,
#endif
};
