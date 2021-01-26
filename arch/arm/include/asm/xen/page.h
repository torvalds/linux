#include <xen/arm/page.h>

static inline bool xen_kernel_unmapped_at_usr(void)
{
	return false;
}
