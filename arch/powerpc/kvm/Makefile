#
# Makefile for Kernel-based Virtual Machine module
#

EXTRA_CFLAGS += -Ivirt/kvm -Iarch/powerpc/kvm

common-objs-y = $(addprefix ../../../virt/kvm/, kvm_main.o coalesced_mmio.o)

common-objs-$(CONFIG_KVM_TRACE)  += $(addprefix ../../../virt/kvm/, kvm_trace.o)

kvm-objs := $(common-objs-y) powerpc.o emulate.o booke_guest.o
obj-$(CONFIG_KVM) += kvm.o

AFLAGS_booke_interrupts.o := -I$(obj)

kvm-booke-host-objs := booke_host.o booke_interrupts.o 44x_tlb.o
obj-$(CONFIG_KVM_BOOKE_HOST) += kvm-booke-host.o
