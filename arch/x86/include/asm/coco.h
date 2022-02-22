/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_COCO_H
#define _ASM_X86_COCO_H

enum cc_vendor {
	CC_VENDOR_NONE,
	CC_VENDOR_AMD,
	CC_VENDOR_HYPERV,
	CC_VENDOR_INTEL,
};

void cc_set_vendor(enum cc_vendor v);

#endif /* _ASM_X86_COCO_H */
