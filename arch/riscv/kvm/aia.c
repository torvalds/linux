// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 Western Digital Corporation or its affiliates.
 * Copyright (C) 2022 Ventana Micro Systems Inc.
 *
 * Authors:
 *	Anup Patel <apatel@ventanamicro.com>
 */

#include <linux/kvm_host.h>
#include <asm/hwcap.h>

DEFINE_STATIC_KEY_FALSE(kvm_riscv_aia_available);

static void aia_set_hvictl(bool ext_irq_pending)
{
	unsigned long hvictl;

	/*
	 * HVICTL.IID == 9 and HVICTL.IPRIO == 0 represents
	 * no interrupt in HVICTL.
	 */

	hvictl = (IRQ_S_EXT << HVICTL_IID_SHIFT) & HVICTL_IID;
	hvictl |= ext_irq_pending;
	csr_write(CSR_HVICTL, hvictl);
}

void kvm_riscv_aia_enable(void)
{
	if (!kvm_riscv_aia_available())
		return;

	aia_set_hvictl(false);
	csr_write(CSR_HVIPRIO1, 0x0);
	csr_write(CSR_HVIPRIO2, 0x0);
#ifdef CONFIG_32BIT
	csr_write(CSR_HVIPH, 0x0);
	csr_write(CSR_HIDELEGH, 0x0);
	csr_write(CSR_HVIPRIO1H, 0x0);
	csr_write(CSR_HVIPRIO2H, 0x0);
#endif
}

void kvm_riscv_aia_disable(void)
{
	if (!kvm_riscv_aia_available())
		return;

	aia_set_hvictl(false);
}

int kvm_riscv_aia_init(void)
{
	if (!riscv_isa_extension_available(NULL, SxAIA))
		return -ENODEV;

	/* Enable KVM AIA support */
	static_branch_enable(&kvm_riscv_aia_available);

	return 0;
}

void kvm_riscv_aia_exit(void)
{
}
