#
# Makefile for Kernel-based Virtual Machine module
#

subdir-ccflags-$(CONFIG_PPC_WERROR) := -Werror

ccflags-y := -Ivirt/kvm -Iarch/powerpc/kvm

common-objs-y = $(addprefix ../../../virt/kvm/, kvm_main.o coalesced_mmio.o \
						eventfd.o)

CFLAGS_44x_tlb.o  := -I.
CFLAGS_e500_mmu.o := -I.
CFLAGS_e500_mmu_host.o := -I.
CFLAGS_emulate.o  := -I.

common-objs-y += powerpc.o emulate.o
obj-$(CONFIG_KVM_EXIT_TIMING) += timing.o
obj-$(CONFIG_KVM_BOOK3S_HANDLER) += book3s_exports.o

AFLAGS_booke_interrupts.o := -I$(obj)

kvm-440-objs := \
	$(common-objs-y) \
	booke.o \
	booke_emulate.o \
	booke_interrupts.o \
	44x.o \
	44x_tlb.o \
	44x_emulate.o
kvm-objs-$(CONFIG_KVM_440) := $(kvm-440-objs)

kvm-e500-objs := \
	$(common-objs-y) \
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
	booke.o \
	booke_emulate.o \
	bookehv_interrupts.o \
	e500mc.o \
	e500_mmu.o \
	e500_mmu_host.o \
	e500_emulate.o
kvm-objs-$(CONFIG_KVM_E500MC) := $(kvm-e500mc-objs)

kvm-book3s_64-objs-$(CONFIG_KVM_BOOK3S_64_PR) := \
	../../../virt/kvm/coalesced_mmio.o \
	fpu.o \
	book3s_paired_singles.o \
	book3s_pr.o \
	book3s_pr_papr.o \
	book3s_64_vio_hv.o \
	book3s_emulate.o \
	book3s_interrupts.o \
	book3s_mmu_hpte.o \
	book3s_64_mmu_host.o \
	book3s_64_mmu.o \
	book3s_32_mmu.o
kvm-book3s_64-builtin-objs-$(CONFIG_KVM_BOOK3S_64_PR) := \
	book3s_rmhandlers.o

kvm-book3s_64-objs-$(CONFIG_KVM_BOOK3S_64_HV) := \
	book3s_hv.o \
	book3s_hv_interrupts.o \
	book3s_64_mmu_hv.o
kvm-book3s_64-builtin-xics-objs-$(CONFIG_KVM_XICS) := \
	book3s_hv_rm_xics.o
kvm-book3s_64-builtin-objs-$(CONFIG_KVM_BOOK3S_64_HV) := \
	book3s_hv_rmhandlers.o \
	book3s_hv_rm_mmu.o \
	book3s_64_vio_hv.o \
	book3s_hv_ras.o \
	book3s_hv_builtin.o \
	$(kvm-book3s_64-builtin-xics-objs-y)

kvm-book3s_64-objs-$(CONFIG_KVM_XICS) += \
	book3s_xics.o

kvm-book3s_64-module-objs := \
	../../../virt/kvm/kvm_main.o \
	../../../virt/kvm/eventfd.o \
	powerpc.o \
	emulate.o \
	book3s.o \
	book3s_64_vio.o \
	book3s_rtas.o \
	$(kvm-book3s_64-objs-y)

kvm-objs-$(CONFIG_KVM_BOOK3S_64) := $(kvm-book3s_64-module-objs)

kvm-book3s_32-objs := \
	$(common-objs-y) \
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
kvm-objs-$(CONFIG_HAVE_KVM_IRQ_ROUTING) += $(addprefix ../../../virt/kvm/, irqchip.o)

kvm-objs := $(kvm-objs-m) $(kvm-objs-y)

obj-$(CONFIG_KVM_440) += kvm.o
obj-$(CONFIG_KVM_E500V2) += kvm.o
obj-$(CONFIG_KVM_E500MC) += kvm.o
obj-$(CONFIG_KVM_BOOK3S_64) += kvm.o
obj-$(CONFIG_KVM_BOOK3S_32) += kvm.o

obj-y += $(kvm-book3s_64-builtin-objs-y)
