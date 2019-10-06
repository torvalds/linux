/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_CPU_DEVICE_ID
#define _ASM_X86_CPU_DEVICE_ID

/*
 * Declare drivers belonging to specific x86 CPUs
 * Similar in spirit to pci_device_id and related PCI functions
 */

#include <linux/mod_devicetable.h>

/*
 * Match specific microcode revisions.
 *
 * vendor/family/model/stepping must be all set.
 *
 * Only checks against the boot CPU.  When mixed-stepping configs are
 * valid for a CPU model, add a quirk for every valid stepping and
 * do the fine-tuning in the quirk handler.
 */

struct x86_cpu_desc {
	u8	x86_family;
	u8	x86_vendor;
	u8	x86_model;
	u8	x86_stepping;
	u32	x86_microcode_rev;
};

#define INTEL_CPU_DESC(model, stepping, revision) {		\
	.x86_family		= 6,				\
	.x86_vendor		= X86_VENDOR_INTEL,		\
	.x86_model		= (model),			\
	.x86_stepping		= (stepping),			\
	.x86_microcode_rev	= (revision),			\
}

extern const struct x86_cpu_id *x86_match_cpu(const struct x86_cpu_id *match);
extern bool x86_cpu_has_min_microcode_rev(const struct x86_cpu_desc *table);

#endif /* _ASM_X86_CPU_DEVICE_ID */
