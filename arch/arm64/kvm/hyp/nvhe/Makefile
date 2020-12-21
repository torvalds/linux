# SPDX-License-Identifier: GPL-2.0
#
# Makefile for Kernel-based Virtual Machine module, HYP/nVHE part
#

asflags-y := -D__KVM_NVHE_HYPERVISOR__
ccflags-y := -D__KVM_NVHE_HYPERVISOR__

obj-y := timer-sr.o sysreg-sr.o debug-sr.o switch.o tlb.o hyp-init.o host.o \
	 hyp-main.o hyp-smp.o psci-relay.o
obj-y += ../vgic-v3-sr.o ../aarch32.o ../vgic-v2-cpuif-proxy.o ../entry.o \
	 ../fpsimd.o ../hyp-entry.o ../exception.o

##
## Build rules for compiling nVHE hyp code
## Output of this folder is `kvm_nvhe.o`, a partially linked object
## file containing all nVHE hyp code and data.
##

hyp-obj := $(patsubst %.o,%.nvhe.o,$(obj-y))
obj-y := kvm_nvhe.o
extra-y := $(hyp-obj) kvm_nvhe.tmp.o hyp.lds

# 1) Compile all source files to `.nvhe.o` object files. The file extension
#    avoids file name clashes for files shared with VHE.
$(obj)/%.nvhe.o: $(src)/%.c FORCE
	$(call if_changed_rule,cc_o_c)
$(obj)/%.nvhe.o: $(src)/%.S FORCE
	$(call if_changed_rule,as_o_S)

# 2) Compile linker script.
$(obj)/hyp.lds: $(src)/hyp.lds.S FORCE
	$(call if_changed_dep,cpp_lds_S)

# 3) Partially link all '.nvhe.o' files and apply the linker script.
#    Prefixes names of ELF sections with '.hyp', eg. '.hyp.text'.
#    Note: The following rule assumes that the 'ld' rule puts LDFLAGS before
#          the list of dependencies to form '-T $(obj)/hyp.lds'. This is to
#          keep the dependency on the target while avoiding an error from
#          GNU ld if the linker script is passed to it twice.
LDFLAGS_kvm_nvhe.tmp.o := -r -T
$(obj)/kvm_nvhe.tmp.o: $(obj)/hyp.lds $(addprefix $(obj)/,$(hyp-obj)) FORCE
	$(call if_changed,ld)

# 4) Produce the final 'kvm_nvhe.o', ready to be linked into 'vmlinux'.
#    Prefixes names of ELF symbols with '__kvm_nvhe_'.
$(obj)/kvm_nvhe.o: $(obj)/kvm_nvhe.tmp.o FORCE
	$(call if_changed,hypcopy)

# The HYPCOPY command uses `objcopy` to prefix all ELF symbol names
# to avoid clashes with VHE code/data.
quiet_cmd_hypcopy = HYPCOPY $@
      cmd_hypcopy = $(OBJCOPY) --prefix-symbols=__kvm_nvhe_ $< $@

# Remove ftrace and Shadow Call Stack CFLAGS.
# This is equivalent to the 'notrace' and '__noscs' annotations.
KBUILD_CFLAGS := $(filter-out $(CC_FLAGS_FTRACE) $(CC_FLAGS_SCS), $(KBUILD_CFLAGS))

# KVM nVHE code is run at a different exception code with a different map, so
# compiler instrumentation that inserts callbacks or checks into the code may
# cause crashes. Just disable it.
GCOV_PROFILE	:= n
KASAN_SANITIZE	:= n
UBSAN_SANITIZE	:= n
KCOV_INSTRUMENT	:= n

# Skip objtool checking for this directory because nVHE code is compiled with
# non-standard build rules.
OBJECT_FILES_NON_STANDARD := y
