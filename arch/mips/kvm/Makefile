# SPDX-License-Identifier: GPL-2.0
# Makefile for KVM support for MIPS
#

include $(srctree)/virt/kvm/Makefile.kvm

ccflags-y += -Ivirt/kvm -Iarch/mips/kvm

kvm-$(CONFIG_CPU_HAS_MSA) += msa.o

kvm-y +=    mips.o emulate.o entry.o \
	    interrupt.o stats.o \
	    fpu.o
kvm-y += hypcall.o
kvm-y += mmu.o
kvm-$(CONFIG_CPU_LOONGSON64) += loongson_ipi.o

kvm-y		+= vz.o
obj-$(CONFIG_KVM)	+= kvm.o
obj-y			+= tlb.o
