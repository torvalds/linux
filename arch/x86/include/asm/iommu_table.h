/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_IOMMU_TABLE_H
#define _ASM_X86_IOMMU_TABLE_H

#include <asm/swiotlb.h>

/*
 * History lesson:
 * The execution chain of IOMMUs in 2.6.36 looks as so:
 *
 *            [xen-swiotlb]
 *                 |
 *         +----[swiotlb *]--+
 *        /         |         \
 *       /          |          \
 *    [GART]     [Calgary]  [Intel VT-d]
 *     /
 *    /
 * [AMD-Vi]
 *
 * *: if SWIOTLB detected 'iommu=soft'/'swiotlb=force' it would skip
 * over the rest of IOMMUs and unconditionally initialize the SWIOTLB.
 * Also it would surreptitiously initialize set the swiotlb=1 if there were
 * more than 4GB and if the user did not pass in 'iommu=off'. The swiotlb
 * flag would be turned off by all IOMMUs except the Calgary one.
 *
 * The IOMMU_INIT* macros allow a similar tree (or more complex if desired)
 * to be built by defining who we depend on.
 *
 * And all that needs to be done is to use one of the macros in the IOMMU
 * and the pci-dma.c will take care of the rest.
 */

struct iommu_table_entry {
	initcall_t	detect;
	initcall_t	depend;
	void		(*early_init)(void); /* No memory allocate available. */
	void		(*late_init)(void); /* Yes, can allocate memory. */
#define IOMMU_FINISH_IF_DETECTED (1<<0)
#define IOMMU_DETECTED		 (1<<1)
	int		flags;
};
/*
 * Macro fills out an entry in the .iommu_table that is equivalent
 * to the fields that 'struct iommu_table_entry' has. The entries
 * that are put in the .iommu_table section are not put in any order
 * hence during boot-time we will have to resort them based on
 * dependency. */


#define __IOMMU_INIT(_detect, _depend, _early_init, _late_init, _finish)\
	static const struct iommu_table_entry				\
		__iommu_entry_##_detect __used				\
	__attribute__ ((unused, __section__(".iommu_table"),		\
			aligned((sizeof(void *)))))	\
	= {_detect, _depend, _early_init, _late_init,			\
	   _finish ? IOMMU_FINISH_IF_DETECTED : 0}
/*
 * The simplest IOMMU definition. Provide the detection routine
 * and it will be run after the SWIOTLB and the other IOMMUs
 * that utilize this macro. If the IOMMU is detected (ie, the
 * detect routine returns a positive value), the other IOMMUs
 * are also checked. You can use IOMMU_INIT_POST_FINISH if you prefer
 * to stop detecting the other IOMMUs after yours has been detected.
 */
#define IOMMU_INIT_POST(_detect)					\
	__IOMMU_INIT(_detect, pci_swiotlb_detect_4gb,  NULL, NULL, 0)

#define IOMMU_INIT_POST_FINISH(detect)					\
	__IOMMU_INIT(_detect, pci_swiotlb_detect_4gb,  NULL, NULL, 1)

/*
 * A more sophisticated version of IOMMU_INIT. This variant requires:
 *  a). A detection routine function.
 *  b). The name of the detection routine we depend on to get called
 *      before us.
 *  c). The init routine which gets called if the detection routine
 *      returns a positive value from the pci_iommu_alloc. This means
 *      no presence of a memory allocator.
 *  d). Similar to the 'init', except that this gets called from pci_iommu_init
 *      where we do have a memory allocator.
 *
 * The standard IOMMU_INIT differs from the IOMMU_INIT_FINISH variant
 * in that the former will continue detecting other IOMMUs in the call
 * list after the detection routine returns a positive number, while the
 * latter will stop the execution chain upon first successful detection.
 * Both variants will still call the 'init' and 'late_init' functions if
 * they are set.
 */
#define IOMMU_INIT_FINISH(_detect, _depend, _init, _late_init)		\
	__IOMMU_INIT(_detect, _depend, _init, _late_init, 1)

#define IOMMU_INIT(_detect, _depend, _init, _late_init)			\
	__IOMMU_INIT(_detect, _depend, _init, _late_init, 0)

void sort_iommu_table(struct iommu_table_entry *start,
		      struct iommu_table_entry *finish);

void check_iommu_entries(struct iommu_table_entry *start,
			 struct iommu_table_entry *finish);

#endif /* _ASM_X86_IOMMU_TABLE_H */
