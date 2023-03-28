# SPDX-License-Identifier: GPL-2.0
#
# Makefile for RISC-V KVM support
#

ccflags-y += -I $(srctree)/$(src)

include $(srctree)/virt/kvm/Makefile.kvm

obj-$(CONFIG_KVM) += kvm.o

kvm-y += main.o
kvm-y += vm.o
kvm-y += vmid.o
kvm-y += tlb.o
kvm-y += mmu.o
kvm-y += vcpu.o
kvm-y += vcpu_exit.o
kvm-y += vcpu_fp.o
kvm-y += vcpu_insn.o
kvm-y += vcpu_switch.o
kvm-y += vcpu_sbi.o
kvm-$(CONFIG_RISCV_SBI_V01) += vcpu_sbi_v01.o
kvm-y += vcpu_sbi_base.o
kvm-y += vcpu_sbi_replace.o
kvm-y += vcpu_sbi_hsm.o
kvm-y += vcpu_timer.o
kvm-$(CONFIG_RISCV_PMU_SBI) += vcpu_pmu.o vcpu_sbi_pmu.o
