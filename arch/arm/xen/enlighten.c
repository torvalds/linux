#include <xen/xen.h>
#include <xen/events.h>
#include <xen/grant_table.h>
#include <xen/hvm.h>
#include <xen/interface/vcpu.h>
#include <xen/interface/xen.h>
#include <xen/interface/memory.h>
#include <xen/interface/hvm/params.h>
#include <xen/features.h>
#include <xen/platform_pci.h>
#include <xen/xenbus.h>
#include <xen/page.h>
#include <xen/interface/sched.h>
#include <xen/xen-ops.h>
#include <asm/xen/hypervisor.h>
#include <asm/xen/hypercall.h>
#include <asm/system_misc.h>
#include <linux/interrupt.h>
#include <linux/irqreturn.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/cpuidle.h>
#include <linux/cpufreq.h>

#include <linux/mm.h>

struct start_info _xen_start_info;
struct start_info *xen_start_info = &_xen_start_info;
EXPORT_SYMBOL_GPL(xen_start_info);

enum xen_domain_type xen_domain_type = XEN_NATIVE;
EXPORT_SYMBOL_GPL(xen_domain_type);

struct shared_info xen_dummy_shared_info;
struct shared_info *HYPERVISOR_shared_info = (void *)&xen_dummy_shared_info;

DEFINE_PER_CPU(struct vcpu_info *, xen_vcpu);
static struct vcpu_info __percpu *xen_vcpu_info;

/* These are unused until we support booting "pre-ballooned" */
unsigned long xen_released_pages;
struct xen_memory_region xen_extra_mem[XEN_EXTRA_MEM_MAX_REGIONS] __initdata;

/* TODO: to be removed */
__read_mostly int xen_have_vector_callback;
EXPORT_SYMBOL_GPL(xen_have_vector_callback);

int xen_platform_pci_unplug = XEN_UNPLUG_ALL;
EXPORT_SYMBOL_GPL(xen_platform_pci_unplug);

static __read_mostly int xen_events_irq = -1;

/* map fgmfn of domid to lpfn in the current domain */
static int map_foreign_page(unsigned long lpfn, unsigned long fgmfn,
			    unsigned int domid)
{
	int rc;
	struct xen_add_to_physmap_range xatp = {
		.domid = DOMID_SELF,
		.foreign_domid = domid,
		.size = 1,
		.space = XENMAPSPACE_gmfn_foreign,
	};
	xen_ulong_t idx = fgmfn;
	xen_pfn_t gpfn = lpfn;
	int err = 0;

	set_xen_guest_handle(xatp.idxs, &idx);
	set_xen_guest_handle(xatp.gpfns, &gpfn);
	set_xen_guest_handle(xatp.errs, &err);

	rc = HYPERVISOR_memory_op(XENMEM_add_to_physmap_range, &xatp);
	if (rc || err) {
		pr_warn("Failed to map pfn to mfn rc:%d:%d pfn:%lx mfn:%lx\n",
			rc, err, lpfn, fgmfn);
		return 1;
	}
	return 0;
}

struct remap_data {
	xen_pfn_t fgmfn; /* foreign domain's gmfn */
	pgprot_t prot;
	domid_t  domid;
	struct vm_area_struct *vma;
	int index;
	struct page **pages;
	struct xen_remap_mfn_info *info;
};

static int remap_pte_fn(pte_t *ptep, pgtable_t token, unsigned long addr,
			void *data)
{
	struct remap_data *info = data;
	struct page *page = info->pages[info->index++];
	unsigned long pfn = page_to_pfn(page);
	pte_t pte = pfn_pte(pfn, info->prot);

	if (map_foreign_page(pfn, info->fgmfn, info->domid))
		return -EFAULT;
	set_pte_at(info->vma->vm_mm, addr, ptep, pte);

	return 0;
}

int xen_remap_domain_mfn_range(struct vm_area_struct *vma,
			       unsigned long addr,
			       xen_pfn_t mfn, int nr,
			       pgprot_t prot, unsigned domid,
			       struct page **pages)
{
	int err;
	struct remap_data data;

	/* TBD: Batching, current sole caller only does page at a time */
	if (nr > 1)
		return -EINVAL;

	data.fgmfn = mfn;
	data.prot = prot;
	data.domid = domid;
	data.vma = vma;
	data.index = 0;
	data.pages = pages;
	err = apply_to_page_range(vma->vm_mm, addr, nr << PAGE_SHIFT,
				  remap_pte_fn, &data);
	return err;
}
EXPORT_SYMBOL_GPL(xen_remap_domain_mfn_range);

int xen_unmap_domain_mfn_range(struct vm_area_struct *vma,
			       int nr, struct page **pages)
{
	int i;

	for (i = 0; i < nr; i++) {
		struct xen_remove_from_physmap xrp;
		unsigned long rc, pfn;

		pfn = page_to_pfn(pages[i]);

		xrp.domid = DOMID_SELF;
		xrp.gpfn = pfn;
		rc = HYPERVISOR_memory_op(XENMEM_remove_from_physmap, &xrp);
		if (rc) {
			pr_warn("Failed to unmap pfn:%lx rc:%ld\n",
				pfn, rc);
			return rc;
		}
	}
	return 0;
}
EXPORT_SYMBOL_GPL(xen_unmap_domain_mfn_range);

static void __init xen_percpu_init(void *unused)
{
	struct vcpu_register_vcpu_info info;
	struct vcpu_info *vcpup;
	int err;
	int cpu = get_cpu();

	pr_info("Xen: initializing cpu%d\n", cpu);
	vcpup = per_cpu_ptr(xen_vcpu_info, cpu);

	info.mfn = __pa(vcpup) >> PAGE_SHIFT;
	info.offset = offset_in_page(vcpup);

	err = HYPERVISOR_vcpu_op(VCPUOP_register_vcpu_info, cpu, &info);
	BUG_ON(err);
	per_cpu(xen_vcpu, cpu) = vcpup;

	enable_percpu_irq(xen_events_irq, 0);
}

static void xen_restart(enum reboot_mode reboot_mode, const char *cmd)
{
	struct sched_shutdown r = { .reason = SHUTDOWN_reboot };
	int rc;
	rc = HYPERVISOR_sched_op(SCHEDOP_shutdown, &r);
	if (rc)
		BUG();
}

static void xen_power_off(void)
{
	struct sched_shutdown r = { .reason = SHUTDOWN_poweroff };
	int rc;
	rc = HYPERVISOR_sched_op(SCHEDOP_shutdown, &r);
	if (rc)
		BUG();
}

/*
 * see Documentation/devicetree/bindings/arm/xen.txt for the
 * documentation of the Xen Device Tree format.
 */
#define GRANT_TABLE_PHYSADDR 0
static int __init xen_guest_init(void)
{
	struct xen_add_to_physmap xatp;
	static struct shared_info *shared_info_page = 0;
	struct device_node *node;
	int len;
	const char *s = NULL;
	const char *version = NULL;
	const char *xen_prefix = "xen,xen-";
	struct resource res;

	node = of_find_compatible_node(NULL, NULL, "xen,xen");
	if (!node) {
		pr_debug("No Xen support\n");
		return 0;
	}
	s = of_get_property(node, "compatible", &len);
	if (strlen(xen_prefix) + 3  < len &&
			!strncmp(xen_prefix, s, strlen(xen_prefix)))
		version = s + strlen(xen_prefix);
	if (version == NULL) {
		pr_debug("Xen version not found\n");
		return 0;
	}
	if (of_address_to_resource(node, GRANT_TABLE_PHYSADDR, &res))
		return 0;
	xen_hvm_resume_frames = res.start >> PAGE_SHIFT;
	xen_events_irq = irq_of_parse_and_map(node, 0);
	pr_info("Xen %s support found, events_irq=%d gnttab_frame_pfn=%lx\n",
			version, xen_events_irq, xen_hvm_resume_frames);
	xen_domain_type = XEN_HVM_DOMAIN;

	xen_setup_features();
	if (xen_feature(XENFEAT_dom0))
		xen_start_info->flags |= SIF_INITDOMAIN|SIF_PRIVILEGED;
	else
		xen_start_info->flags &= ~(SIF_INITDOMAIN|SIF_PRIVILEGED);

	if (!shared_info_page)
		shared_info_page = (struct shared_info *)
			get_zeroed_page(GFP_KERNEL);
	if (!shared_info_page) {
		pr_err("not enough memory\n");
		return -ENOMEM;
	}
	xatp.domid = DOMID_SELF;
	xatp.idx = 0;
	xatp.space = XENMAPSPACE_shared_info;
	xatp.gpfn = __pa(shared_info_page) >> PAGE_SHIFT;
	if (HYPERVISOR_memory_op(XENMEM_add_to_physmap, &xatp))
		BUG();

	HYPERVISOR_shared_info = (struct shared_info *)shared_info_page;

	/* xen_vcpu is a pointer to the vcpu_info struct in the shared_info
	 * page, we use it in the event channel upcall and in some pvclock
	 * related functions. 
	 * The shared info contains exactly 1 CPU (the boot CPU). The guest
	 * is required to use VCPUOP_register_vcpu_info to place vcpu info
	 * for secondary CPUs as they are brought up.
	 * For uniformity we use VCPUOP_register_vcpu_info even on cpu0.
	 */
	xen_vcpu_info = __alloc_percpu(sizeof(struct vcpu_info),
			                       sizeof(struct vcpu_info));
	if (xen_vcpu_info == NULL)
		return -ENOMEM;

	gnttab_init();
	if (!xen_initial_domain())
		xenbus_probe(NULL);

	/*
	 * Making sure board specific code will not set up ops for
	 * cpu idle and cpu freq.
	 */
	disable_cpuidle();
	disable_cpufreq();

	return 0;
}
core_initcall(xen_guest_init);

static int __init xen_pm_init(void)
{
	if (!xen_domain())
		return -ENODEV;

	pm_power_off = xen_power_off;
	arm_pm_restart = xen_restart;

	return 0;
}
late_initcall(xen_pm_init);

static irqreturn_t xen_arm_callback(int irq, void *arg)
{
	xen_hvm_evtchn_do_upcall();
	return IRQ_HANDLED;
}

static int __init xen_init_events(void)
{
	if (!xen_domain() || xen_events_irq < 0)
		return -ENODEV;

	xen_init_IRQ();

	if (request_percpu_irq(xen_events_irq, xen_arm_callback,
			"events", &xen_vcpu)) {
		pr_err("Error requesting IRQ %d\n", xen_events_irq);
		return -EINVAL;
	}

	on_each_cpu(xen_percpu_init, NULL, 0);

	return 0;
}
postcore_initcall(xen_init_events);

/* In the hypervisor.S file. */
EXPORT_SYMBOL_GPL(HYPERVISOR_event_channel_op);
EXPORT_SYMBOL_GPL(HYPERVISOR_grant_table_op);
EXPORT_SYMBOL_GPL(HYPERVISOR_xen_version);
EXPORT_SYMBOL_GPL(HYPERVISOR_console_io);
EXPORT_SYMBOL_GPL(HYPERVISOR_sched_op);
EXPORT_SYMBOL_GPL(HYPERVISOR_hvm_op);
EXPORT_SYMBOL_GPL(HYPERVISOR_memory_op);
EXPORT_SYMBOL_GPL(HYPERVISOR_physdev_op);
EXPORT_SYMBOL_GPL(HYPERVISOR_vcpu_op);
EXPORT_SYMBOL_GPL(HYPERVISOR_tmem_op);
EXPORT_SYMBOL_GPL(privcmd_call);
