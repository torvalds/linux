#include <xen/xen.h>
#include <xen/interface/xen.h>
#include <xen/interface/memory.h>
#include <xen/platform_pci.h>
#include <asm/xen/hypervisor.h>
#include <asm/xen/hypercall.h>
#include <linux/module.h>

struct start_info _xen_start_info;
struct start_info *xen_start_info = &_xen_start_info;
EXPORT_SYMBOL_GPL(xen_start_info);

enum xen_domain_type xen_domain_type = XEN_NATIVE;
EXPORT_SYMBOL_GPL(xen_domain_type);

struct shared_info xen_dummy_shared_info;
struct shared_info *HYPERVISOR_shared_info = (void *)&xen_dummy_shared_info;

DEFINE_PER_CPU(struct vcpu_info *, xen_vcpu);

/* TODO: to be removed */
__read_mostly int xen_have_vector_callback;
EXPORT_SYMBOL_GPL(xen_have_vector_callback);

int xen_platform_pci_unplug = XEN_UNPLUG_ALL;
EXPORT_SYMBOL_GPL(xen_platform_pci_unplug);

int xen_remap_domain_mfn_range(struct vm_area_struct *vma,
			       unsigned long addr,
			       unsigned long mfn, int nr,
			       pgprot_t prot, unsigned domid)
{
	return -ENOSYS;
}
EXPORT_SYMBOL_GPL(xen_remap_domain_mfn_range);
