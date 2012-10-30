
ccflags-y += -Ivirt/kvm -Iarch/x86/kvm

CFLAGS_x86.o := -I.
CFLAGS_svm.o := -I.
CFLAGS_vmx.o := -I.

kvm-y			+= $(addprefix ../../../virt/kvm/, kvm_main.o ioapic.o \
				coalesced_mmio.o irq_comm.o eventfd.o \
				assigned-dev.o)
kvm-$(CONFIG_IOMMU_API)	+= $(addprefix ../../../virt/kvm/, iommu.o)
kvm-$(CONFIG_KVM_ASYNC_PF)	+= $(addprefix ../../../virt/kvm/, async_pf.o)

kvm-y			+= x86.o mmu.o emulate.o i8259.o irq.o lapic.o \
			   i8254.o cpuid.o pmu.o
kvm-intel-y		+= vmx.o
kvm-amd-y		+= svm.o

obj-$(CONFIG_KVM)	+= kvm.o
obj-$(CONFIG_KVM_INTEL)	+= kvm-intel.o
obj-$(CONFIG_KVM_AMD)	+= kvm-amd.o
