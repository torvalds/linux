#include <linux/mm.h>
#include <asm/page.h>
#include <asm/mman.h>

const char *arch_vma_name(struct vm_area_struct *vma)
{
	if (vma->vm_mm && vma->vm_start == um_vdso_addr)
		return "[vdso]";

	return NULL;
}
