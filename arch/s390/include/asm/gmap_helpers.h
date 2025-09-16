/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  Helper functions for KVM guest address space mapping code
 *
 *    Copyright IBM Corp. 2025
 */

#ifndef _ASM_S390_GMAP_HELPERS_H
#define _ASM_S390_GMAP_HELPERS_H

void gmap_helper_zap_one_page(struct mm_struct *mm, unsigned long vmaddr);
void gmap_helper_discard(struct mm_struct *mm, unsigned long vmaddr, unsigned long end);
int gmap_helper_disable_cow_sharing(void);

#endif /* _ASM_S390_GMAP_HELPERS_H */
