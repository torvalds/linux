
ccflags-y += -Ivirt/kvm -Iarch/x86/kvm

CFLAGS_x86.o := -I.
CFLAGS_svm.o := -I.
CFLAGS_vmx.o := -I.

KVM := ../../../virt/kvm

kvm-y			+= $(KVM)/kvm_main.o $(KVM)/ioapic.o \
				$(KVM)/coalesced_mmio.o $(KVM)/irq_comm.o \
				$(KVM)/eventfd.o $(KVM)/irqchip.o
kvm-$(CONFIG_KVM_DEVICE_ASSIGNMENT)	+= $(KVM)/assigned-dev.o $(KVM)/iommu.o
kvm-$(CONFIG_KVM_ASYNC_PF)	+= $(KVM)/async_pf.o

kvm-y			+= x86.o mmu.o emulate.o i8259.o irq.o lapic.o \
			   i8254.o cpuid.o pmu.o
kvm-intel-y		+= vmx.o
kvm-amd-y		+= svm.o

obj-$(CONFIG_KVM)	+= kvm.o
obj-$(CONFIG_KVM_INTEL)	+= kvm-intel.o
obj-$(CONFIG_KVM_AMD)	+= kvm-amd.o
