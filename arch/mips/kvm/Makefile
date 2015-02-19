# Makefile for KVM support for MIPS
#

common-objs = $(addprefix ../../../virt/kvm/, kvm_main.o coalesced_mmio.o)

EXTRA_CFLAGS += -Ivirt/kvm -Iarch/mips/kvm

kvm-objs := $(common-objs) mips.o emulate.o locore.o \
	    interrupt.o stats.o commpage.o \
	    dyntrans.o trap_emul.o

obj-$(CONFIG_KVM)	+= kvm.o
obj-y			+= callback.o tlb.o
