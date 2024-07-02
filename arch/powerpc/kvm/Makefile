# SPDX-License-Identifier: GPL-2.0
#
# Makefile for Kernel-based Virtual Machine module
#

ccflags-y := -Ivirt/kvm -Iarch/powerpc/kvm

include $(srctree)/virt/kvm/Makefile.kvm

common-objs-y += powerpc.o emulate_loadstore.o
obj-$(CONFIG_KVM_EXIT_TIMING) += timing.o
obj-$(CONFIG_KVM_BOOK3S_HANDLER) += book3s_exports.o

AFLAGS_booke_interrupts.o := -I$(objtree)/$(obj)

kvm-e500-objs := \
	$(common-objs-y) \
	emulate.o \
	booke.o \
	booke_emulate.o \
	booke_interrupts.o \
	e500.o \
	e500_mmu.o \
	e500_mmu_host.o \
	e500_emulate.o
kvm-objs-$(CONFIG_KVM_E500V2) := $(kvm-e500-objs)

kvm-e500mc-objs := \
	$(common-objs-y) \
	emulate.o \
	booke.o \
	booke_emulate.o \
	bookehv_interrupts.o \
	e500mc.o \
	e500_mmu.o \
	e500_mmu_host.o \
	e500_emulate.o
kvm-objs-$(CONFIG_KVM_E500MC) := $(kvm-e500mc-objs)

kvm-pr-y := \
	fpu.o \
	emulate.o \
	book3s_paired_singles.o \
	book3s_pr.o \
	book3s_pr_papr.o \
	book3s_emulate.o \
	book3s_interrupts.o \
	book3s_mmu_hpte.o \
	book3s_64_mmu_host.o \
	book3s_64_mmu.o \
	book3s_32_mmu.o

kvm-book3s_64-builtin-objs-$(CONFIG_KVM_BOOK3S_64_HANDLER) += \
	book3s_64_entry.o \
	tm.o

ifdef CONFIG_KVM_BOOK3S_PR_POSSIBLE
kvm-book3s_64-builtin-objs-$(CONFIG_KVM_BOOK3S_64_HANDLER) += \
	book3s_rmhandlers.o
endif

kvm-hv-y += \
	book3s_hv.o \
	book3s_hv_interrupts.o \
	book3s_64_mmu_hv.o \
	book3s_64_mmu_radix.o \
	book3s_hv_nested.o

kvm-hv-$(CONFIG_PPC_UV) += \
	book3s_hv_uvmem.o

kvm-hv-$(CONFIG_PPC_TRANSACTIONAL_MEM) += \
	book3s_hv_tm.o

kvm-book3s_64-builtin-xics-objs-$(CONFIG_KVM_XICS) := \
	book3s_hv_rm_xics.o

kvm-book3s_64-builtin-tm-objs-$(CONFIG_PPC_TRANSACTIONAL_MEM) += \
	book3s_hv_tm_builtin.o

ifdef CONFIG_KVM_BOOK3S_HV_POSSIBLE
kvm-book3s_64-builtin-objs-$(CONFIG_KVM_BOOK3S_64_HANDLER) += \
	book3s_hv_hmi.o \
	book3s_hv_p9_entry.o \
	book3s_hv_rmhandlers.o \
	book3s_hv_rm_mmu.o \
	book3s_hv_ras.o \
	book3s_hv_builtin.o \
	book3s_hv_p9_perf.o \
	book3s_hv_nestedv2.o \
	guest-state-buffer.o \
	$(kvm-book3s_64-builtin-tm-objs-y) \
	$(kvm-book3s_64-builtin-xics-objs-y)

obj-$(CONFIG_GUEST_STATE_BUFFER_TEST) += test-guest-state-buffer.o
endif

kvm-book3s_64-objs-$(CONFIG_KVM_XICS) += \
	book3s_xics.o

kvm-book3s_64-objs-$(CONFIG_KVM_XIVE) += book3s_xive.o book3s_xive_native.o
kvm-book3s_64-objs-$(CONFIG_SPAPR_TCE_IOMMU) += book3s_64_vio.o

kvm-book3s_64-module-objs := \
	$(common-objs-y) \
	book3s.o \
	book3s_rtas.o \
	$(kvm-book3s_64-objs-y)

kvm-objs-$(CONFIG_KVM_BOOK3S_64) := $(kvm-book3s_64-module-objs)

kvm-book3s_32-objs := \
	$(common-objs-y) \
	emulate.o \
	fpu.o \
	book3s_paired_singles.o \
	book3s.o \
	book3s_pr.o \
	book3s_emulate.o \
	book3s_interrupts.o \
	book3s_mmu_hpte.o \
	book3s_32_mmu_host.o \
	book3s_32_mmu.o
kvm-objs-$(CONFIG_KVM_BOOK3S_32) := $(kvm-book3s_32-objs)

kvm-objs-$(CONFIG_KVM_MPIC) += mpic.o

kvm-y += $(kvm-objs-m) $(kvm-objs-y)

obj-$(CONFIG_KVM_E500V2) += kvm.o
obj-$(CONFIG_KVM_E500MC) += kvm.o
obj-$(CONFIG_KVM_BOOK3S_64) += kvm.o
obj-$(CONFIG_KVM_BOOK3S_32) += kvm.o

obj-$(CONFIG_KVM_BOOK3S_64_PR) += kvm-pr.o
obj-$(CONFIG_KVM_BOOK3S_64_HV) += kvm-hv.o

obj-y += $(kvm-book3s_64-builtin-objs-y)

# KVM does a lot in real-mode, and 64-bit Book3S KASAN doesn't support that
ifdef CONFIG_PPC_BOOK3S_64
KASAN_SANITIZE := n
endif
