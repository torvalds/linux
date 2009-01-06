#
# Makefile for Kernel-based Virtual Machine module
#

EXTRA_CFLAGS += -Ivirt/kvm -Iarch/powerpc/kvm

common-objs-y = $(addprefix ../../../virt/kvm/, kvm_main.o coalesced_mmio.o)

common-objs-$(CONFIG_KVM_TRACE)  += $(addprefix ../../../virt/kvm/, kvm_trace.o)

kvm-objs := $(common-objs-y) powerpc.o emulate.o
obj-$(CONFIG_KVM_EXIT_TIMING) += timing.o
obj-$(CONFIG_KVM) += kvm.o

AFLAGS_booke_interrupts.o := -I$(obj)

kvm-440-objs := \
	booke.o \
	booke_interrupts.o \
	44x.o \
	44x_tlb.o \
	44x_emulate.o
obj-$(CONFIG_KVM_440) += kvm-440.o
