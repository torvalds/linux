# SPDX-License-Identifier: GPL-2.0
# Makefile for kernel virtual machines on s390
#
# Copyright IBM Corp. 2008

include $(srctree)/virt/kvm/Makefile.kvm

ccflags-y := -Ivirt/kvm -Iarch/s390/kvm

kvm-y += kvm-s390.o intercept.o interrupt.o priv.o sigp.o
kvm-y += diag.o gaccess.o guestdbg.o vsie.o pv.o gmap.o gmap-vsie.o

kvm-$(CONFIG_VFIO_PCI_ZDEV_KVM) += pci.o
obj-$(CONFIG_KVM) += kvm.o
