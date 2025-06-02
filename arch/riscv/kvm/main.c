// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019 Western Digital Corporation or its affiliates.
 *
 * Authors:
 *     Anup Patel <anup.patel@wdc.com>
 */

#include <linux/errno.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/kvm_host.h>
#include <asm/cpufeature.h>
#include <asm/kvm_nacl.h>
#include <asm/sbi.h>

long kvm_arch_dev_ioctl(struct file *filp,
			unsigned int ioctl, unsigned long arg)
{
	return -EINVAL;
}

int kvm_arch_enable_virtualization_cpu(void)
{
	int rc;

	rc = kvm_riscv_nacl_enable();
	if (rc)
		return rc;

	csr_write(CSR_HEDELEG, KVM_HEDELEG_DEFAULT);
	csr_write(CSR_HIDELEG, KVM_HIDELEG_DEFAULT);

	/* VS should access only the time counter directly. Everything else should trap */
	csr_write(CSR_HCOUNTEREN, 0x02);

	csr_write(CSR_HVIP, 0);

	kvm_riscv_aia_enable();

	return 0;
}

void kvm_arch_disable_virtualization_cpu(void)
{
	kvm_riscv_aia_disable();

	/*
	 * After clearing the hideleg CSR, the host kernel will receive
	 * spurious interrupts if hvip CSR has pending interrupts and the
	 * corresponding enable bits in vsie CSR are asserted. To avoid it,
	 * hvip CSR and vsie CSR must be cleared before clearing hideleg CSR.
	 */
	csr_write(CSR_VSIE, 0);
	csr_write(CSR_HVIP, 0);
	csr_write(CSR_HEDELEG, 0);
	csr_write(CSR_HIDELEG, 0);

	kvm_riscv_nacl_disable();
}

static void kvm_riscv_teardown(void)
{
	kvm_riscv_aia_exit();
	kvm_riscv_nacl_exit();
	kvm_unregister_perf_callbacks();
}

static int __init riscv_kvm_init(void)
{
	int rc;
	char slist[64];
	const char *str;

	if (!riscv_isa_extension_available(NULL, h)) {
		kvm_info("hypervisor extension not available\n");
		return -ENODEV;
	}

	if (sbi_spec_is_0_1()) {
		kvm_info("require SBI v0.2 or higher\n");
		return -ENODEV;
	}

	if (!sbi_probe_extension(SBI_EXT_RFENCE)) {
		kvm_info("require SBI RFENCE extension\n");
		return -ENODEV;
	}

	rc = kvm_riscv_nacl_init();
	if (rc && rc != -ENODEV)
		return rc;

	kvm_riscv_gstage_mode_detect();

	kvm_riscv_gstage_vmid_detect();

	rc = kvm_riscv_aia_init();
	if (rc && rc != -ENODEV) {
		kvm_riscv_nacl_exit();
		return rc;
	}

	kvm_info("hypervisor extension available\n");

	if (kvm_riscv_nacl_available()) {
		rc = 0;
		slist[0] = '\0';
		if (kvm_riscv_nacl_sync_csr_available()) {
			if (rc)
				strcat(slist, ", ");
			strcat(slist, "sync_csr");
			rc++;
		}
		if (kvm_riscv_nacl_sync_hfence_available()) {
			if (rc)
				strcat(slist, ", ");
			strcat(slist, "sync_hfence");
			rc++;
		}
		if (kvm_riscv_nacl_sync_sret_available()) {
			if (rc)
				strcat(slist, ", ");
			strcat(slist, "sync_sret");
			rc++;
		}
		if (kvm_riscv_nacl_autoswap_csr_available()) {
			if (rc)
				strcat(slist, ", ");
			strcat(slist, "autoswap_csr");
			rc++;
		}
		kvm_info("using SBI nested acceleration with %s\n",
			 (rc) ? slist : "no features");
	}

	switch (kvm_riscv_gstage_mode()) {
	case HGATP_MODE_SV32X4:
		str = "Sv32x4";
		break;
	case HGATP_MODE_SV39X4:
		str = "Sv39x4";
		break;
	case HGATP_MODE_SV48X4:
		str = "Sv48x4";
		break;
	case HGATP_MODE_SV57X4:
		str = "Sv57x4";
		break;
	default:
		return -ENODEV;
	}
	kvm_info("using %s G-stage page table format\n", str);

	kvm_info("VMID %ld bits available\n", kvm_riscv_gstage_vmid_bits());

	if (kvm_riscv_aia_available())
		kvm_info("AIA available with %d guest external interrupts\n",
			 kvm_riscv_aia_nr_hgei);

	kvm_register_perf_callbacks(NULL);

	rc = kvm_init(sizeof(struct kvm_vcpu), 0, THIS_MODULE);
	if (rc) {
		kvm_riscv_teardown();
		return rc;
	}

	return 0;
}
module_init(riscv_kvm_init);

static void __exit riscv_kvm_exit(void)
{
	kvm_exit();

	kvm_riscv_teardown();
}
module_exit(riscv_kvm_exit);
