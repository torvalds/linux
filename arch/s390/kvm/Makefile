# SPDX-License-Identifier: GPL-2.0
# Makefile for kernel virtual machines on s390
#
# Copyright IBM Corp. 2008

KVM := ../../../virt/kvm
common-objs = $(KVM)/kvm_main.o $(KVM)/eventfd.o  $(KVM)/async_pf.o \
	      $(KVM)/irqchip.o $(KVM)/vfio.o $(KVM)/binary_stats.o

ccflags-y := -Ivirt/kvm -Iarch/s390/kvm

kvm-objs := $(common-objs) kvm-s390.o intercept.o interrupt.o priv.o sigp.o
kvm-objs += diag.o gaccess.o guestdbg.o vsie.o pv.o

obj-$(CONFIG_KVM) += kvm.o
