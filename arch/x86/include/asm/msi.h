/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_MSI_H
#define _ASM_X86_MSI_H
#include <asm/hw_irq.h>
#include <asm/irqdomain.h>

typedef struct irq_alloc_info msi_alloc_info_t;

int pci_msi_prepare(struct irq_domain *domain, struct device *dev, int nvec,
		    msi_alloc_info_t *arg);

/* Structs and defines for the X86 specific MSI message format */

typedef struct x86_msi_data {
	union {
		struct {
			u32	vector			:  8,
				delivery_mode		:  3,
				dest_mode_logical	:  1,
				reserved		:  2,
				active_low		:  1,
				is_level		:  1;
		};
		u32	dmar_subhandle;
	};
} __attribute__ ((packed)) arch_msi_msg_data_t;
#define arch_msi_msg_data	x86_msi_data

typedef struct x86_msi_addr_lo {
	union {
		struct {
			u32	reserved_0		:  2,
				dest_mode_logical	:  1,
				redirect_hint		:  1,
				reserved_1		:  1,
				virt_destid_8_14	:  7,
				destid_0_7		:  8,
				base_address		: 12;
		};
		struct {
			u32	dmar_reserved_0		:  2,
				dmar_index_15		:  1,
				dmar_subhandle_valid	:  1,
				dmar_format		:  1,
				dmar_index_0_14		: 15,
				dmar_base_address	: 12;
		};
	};
} __attribute__ ((packed)) arch_msi_msg_addr_lo_t;
#define arch_msi_msg_addr_lo	x86_msi_addr_lo

#define X86_MSI_BASE_ADDRESS_LOW	(0xfee00000 >> 20)

typedef struct x86_msi_addr_hi {
	u32	reserved		:  8,
		destid_8_31		: 24;
} __attribute__ ((packed)) arch_msi_msg_addr_hi_t;
#define arch_msi_msg_addr_hi	x86_msi_addr_hi

#define X86_MSI_BASE_ADDRESS_HIGH	(0)

struct msi_msg;
u32 x86_msi_msg_get_destid(struct msi_msg *msg, bool extid);

#define X86_VECTOR_MSI_FLAGS_SUPPORTED					\
	(MSI_GENERIC_FLAGS_MASK | MSI_FLAG_PCI_MSIX | MSI_FLAG_PCI_MSIX_ALLOC_DYN)

#define X86_VECTOR_MSI_FLAGS_REQUIRED					\
	(MSI_FLAG_USE_DEF_DOM_OPS | MSI_FLAG_USE_DEF_CHIP_OPS)

#endif /* _ASM_X86_MSI_H */
