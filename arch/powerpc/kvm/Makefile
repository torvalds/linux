#
# Makefile for Kernel-based Virtual Machine module
#

subdir-ccflags-$(CONFIG_PPC_WERROR) := -Werror

EXTRA_CFLAGS += -Ivirt/kvm -Iarch/powerpc/kvm

common-objs-y = $(addprefix ../../../virt/kvm/, kvm_main.o coalesced_mmio.o)

CFLAGS_44x_tlb.o  := -I.
CFLAGS_e500_tlb.o := -I.
CFLAGS_emulate.o  := -I.

common-objs-y += powerpc.o emulate.o
obj-$(CONFIG_KVM_EXIT_TIMING) += timing.o
obj-$(CONFIG_KVM_BOOK3S_64_HANDLER) += book3s_64_exports.o

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
	e500_tlb.o \
	e500_emulate.o
kvm-objs-$(CONFIG_KVM_E500) := $(kvm-e500-objs)

kvm-book3s_64-objs := \
	$(common-objs-y) \
	book3s.o \
	book3s_64_emulate.o \
	book3s_64_interrupts.o \
	book3s_64_mmu_host.o \
	book3s_64_mmu.o \
	book3s_32_mmu.o
kvm-objs-$(CONFIG_KVM_BOOK3S_64) := $(kvm-book3s_64-objs)

kvm-objs := $(kvm-objs-m) $(kvm-objs-y)

obj-$(CONFIG_KVM_440) += kvm.o
obj-$(CONFIG_KVM_E500) += kvm.o
obj-$(CONFIG_KVM_BOOK3S_64) += kvm.o

