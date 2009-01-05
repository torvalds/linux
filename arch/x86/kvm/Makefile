#
# Makefile for Kernel-based Virtual Machine module
#

common-objs = $(addprefix ../../../virt/kvm/, kvm_main.o ioapic.o \
                coalesced_mmio.o irq_comm.o)
ifeq ($(CONFIG_KVM_TRACE),y)
common-objs += $(addprefix ../../../virt/kvm/, kvm_trace.o)
endif
ifeq ($(CONFIG_IOMMU_API),y)
common-objs += $(addprefix ../../../virt/kvm/, iommu.o)
endif

EXTRA_CFLAGS += -Ivirt/kvm -Iarch/x86/kvm

kvm-objs := $(common-objs) x86.o mmu.o x86_emulate.o i8259.o irq.o lapic.o \
	i8254.o
obj-$(CONFIG_KVM) += kvm.o
kvm-intel-objs = vmx.o
obj-$(CONFIG_KVM_INTEL) += kvm-intel.o
kvm-amd-objs = svm.o
obj-$(CONFIG_KVM_AMD) += kvm-amd.o
