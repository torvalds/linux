#include <xen/arm/page.h>
#include <asm/mmu.h>

static inline bool xen_kernel_unmapped_at_usr(void)
{
	return arm64_kernel_unmapped_at_el0();
}
