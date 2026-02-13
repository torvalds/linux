# SPDX-License-Identifier: GPL-2.0

ccflags-y += -I $(srctree)/arch/x86/kvm
ccflags-$(CONFIG_KVM_WERROR) += -Werror

include $(srctree)/virt/kvm/Makefile.kvm

kvm-y			+= x86.o emulate.o irq.o lapic.o cpuid.o pmu.o mtrr.o \
			   debugfs.o mmu/mmu.o mmu/page_track.o mmu/spte.o

kvm-$(CONFIG_X86_64) += mmu/tdp_iter.o mmu/tdp_mmu.o
kvm-$(CONFIG_KVM_IOAPIC) += i8259.o i8254.o ioapic.o
kvm-$(CONFIG_KVM_HYPERV) += hyperv.o
kvm-$(CONFIG_KVM_XEN)	+= xen.o
kvm-$(CONFIG_KVM_SMM)	+= smm.o

kvm-intel-y		+= vmx/vmx.o vmx/vmenter.o vmx/pmu_intel.o vmx/vmcs12.o \
			   vmx/nested.o vmx/posted_intr.o vmx/main.o

kvm-intel-$(CONFIG_X86_SGX_KVM)	+= vmx/sgx.o
kvm-intel-$(CONFIG_KVM_HYPERV)	+= vmx/hyperv.o vmx/hyperv_evmcs.o
kvm-intel-$(CONFIG_KVM_INTEL_TDX)	+= vmx/tdx.o

kvm-amd-y		+= svm/svm.o svm/vmenter.o svm/pmu.o svm/nested.o svm/avic.o

kvm-amd-$(CONFIG_KVM_AMD_SEV)	+= svm/sev.o
kvm-amd-$(CONFIG_KVM_HYPERV)	+= svm/hyperv.o

ifdef CONFIG_HYPERV
kvm-y			+= kvm_onhyperv.o
kvm-intel-y		+= vmx/vmx_onhyperv.o vmx/hyperv_evmcs.o
kvm-amd-y		+= svm/svm_onhyperv.o
endif

obj-$(CONFIG_KVM_X86)	+= kvm.o
obj-$(CONFIG_KVM_INTEL)	+= kvm-intel.o
obj-$(CONFIG_KVM_AMD)	+= kvm-amd.o

AFLAGS_svm/vmenter.o    := -iquote $(obj)
$(obj)/svm/vmenter.o: $(obj)/kvm-asm-offsets.h

AFLAGS_vmx/vmenter.o    := -iquote $(obj)
$(obj)/vmx/vmenter.o: $(obj)/kvm-asm-offsets.h

$(obj)/kvm-asm-offsets.h: $(obj)/kvm-asm-offsets.s FORCE
	$(call filechk,offsets,__KVM_ASM_OFFSETS_H__)

targets += kvm-asm-offsets.s
clean-files += kvm-asm-offsets.h


# Fail the build if there is unexpected EXPORT_SYMBOL_GPL (or EXPORT_SYMBOL)
# usage.  All KVM-internal exports should use EXPORT_SYMBOL_FOR_KVM_INTERNAL.
# Only a handful of exports intended for other modules (VFIO, KVMGT) should
# use EXPORT_SYMBOL_GPL, and EXPORT_SYMBOL should never be used.
ifdef CONFIG_KVM_X86
# Search recursively for whole words and print line numbers.  Filter out the
# allowed set of exports, i.e. those that are intended for external usage.
exports_grep_trailer := --include='*.[ch]' -nrw $(srctree)/virt/kvm $(srctree)/arch/x86/kvm | \
			grep -v -e kvm_page_track_register_notifier \
				-e kvm_page_track_unregister_notifier \
				-e kvm_write_track_add_gfn \
				-e kvm_write_track_remove_gfn \
				-e kvm_get_kvm \
				-e kvm_get_kvm_safe \
				-e kvm_put_kvm

# Force grep to emit a goofy group separator that can in turn be replaced with
# the above newline macro (newlines in Make are a nightmare).  Note, grep only
# prints the group separator when N lines of context are requested via -C,
# a.k.a. --NUM.  Simply request zero lines.  Print the separator only after
# filtering out expected exports to avoid extra newlines in the error message.
define get_kvm_exports
$(shell grep "$(1)" -C0 $(exports_grep_trailer) | grep "$(1)" -C0 --group-separator="!SEP!")
endef

define check_kvm_exports
nr_kvm_exports := $(shell grep "$(1)" $(exports_grep_trailer) | wc -l)

ifneq (0,$$(nr_kvm_exports))
$$(error ERROR ***\
$$(newline)found $$(nr_kvm_exports) unwanted occurrences of $(1):\
$$(newline)  $(subst !SEP!,$$(newline) ,$(call get_kvm_exports,$(1)))\
$$(newline)in directories:\
$$(newline)  $(srctree)/arch/x86/kvm\
$$(newline)  $(srctree)/virt/kvm\
$$(newline)Use EXPORT_SYMBOL_FOR_KVM_INTERNAL, not $(1))
endif # nr_kvm_exports != 0
undefine nr_kvm_exports
endef # check_kvm_exports

$(eval $(call check_kvm_exports,EXPORT_SYMBOL_GPL))
$(eval $(call check_kvm_exports,EXPORT_SYMBOL))

undefine check_kvm_exports
undefine get_kvm_exports
undefine exports_grep_trailer
endif # CONFIG_KVM_X86
