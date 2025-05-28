# SPDX-License-Identifier: GPL-2.0
#
# Makefile for LoongArch KVM support
#

include $(srctree)/virt/kvm/Makefile.kvm

obj-$(CONFIG_KVM) += kvm.o

kvm-y += exit.o
kvm-y += interrupt.o
kvm-y += main.o
kvm-y += mmu.o
kvm-y += switch.o
kvm-y += timer.o
kvm-y += tlb.o
kvm-y += vcpu.o
kvm-y += vm.o
kvm-y += intc/ipi.o
kvm-y += intc/eiointc.o
kvm-y += intc/pch_pic.o
kvm-y += irqfd.o

CFLAGS_exit.o	+= $(call cc-disable-warning, override-init)
