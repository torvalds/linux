
EXTRA_CFLAGS += -Ivirt/kvm -Iarch/x86/kvm

kvm-y			+= $(addprefix ../../../virt/kvm/, kvm_main.o ioapic.o \
				coalesced_mmio.o irq_comm.o eventfd.o)
kvm-$(CONFIG_KVM_TRACE)	+= $(addprefix ../../../virt/kvm/, kvm_trace.o)
kvm-$(CONFIG_IOMMU_API)	+= $(addprefix ../../../virt/kvm/, iommu.o)

kvm-y			+= x86.o mmu.o x86_emulate.o i8259.o irq.o lapic.o \
			   i8254.o timer.o
kvm-intel-y		+= vmx.o
kvm-amd-y		+= svm.o

obj-$(CONFIG_KVM)	+= kvm.o
obj-$(CONFIG_KVM_INTEL)	+= kvm-intel.o
obj-$(CONFIG_KVM_AMD)	+= kvm-amd.o
