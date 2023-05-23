# SPDX-License-Identifier: GPL-2.0
#
# Makefile for Kernel-based Virtual Machine module, HYP/nVHE part
#

asflags-y := -D__KVM_NVHE_HYPERVISOR__ -D__DISABLE_EXPORTS

# Tracepoint and MMIO logging symbols should not be visible at nVHE KVM as
# there is no way to execute them and any such MMIO access from nVHE KVM
# will explode instantly (Words of Marc Zyngier). So introduce a generic flag
# __DISABLE_TRACE_MMIO__ to disable MMIO tracing for nVHE KVM.
ccflags-y := -D__KVM_NVHE_HYPERVISOR__ -D__DISABLE_EXPORTS -D__DISABLE_TRACE_MMIO__
ccflags-y += -fno-stack-protector	\
	     -DDISABLE_BRANCH_PROFILING	\
	     $(DISABLE_STACKLEAK_PLUGIN)

hostprogs := gen-hyprel
HOST_EXTRACFLAGS += -I$(objtree)/include

lib-objs := clear_page.o copy_page.o memcpy.o memset.o
lib-objs := $(addprefix ../../../lib/, $(lib-objs))

hyp-obj-y := timer-sr.o sysreg-sr.o debug-sr.o switch.o tlb.o hyp-init.o host.o \
	 hyp-main.o hyp-smp.o psci-relay.o early_alloc.o page_alloc.o \
	 cache.o setup.o mm.o mem_protect.o sys_regs.o pkvm.o stacktrace.o ffa.o
hyp-obj-y += ../vgic-v3-sr.o ../aarch32.o ../vgic-v2-cpuif-proxy.o ../entry.o \
	 ../fpsimd.o ../hyp-entry.o ../exception.o ../pgtable.o
hyp-obj-$(CONFIG_DEBUG_LIST) += list_debug.o
hyp-obj-y += $(lib-objs)

##
## Build rules for compiling nVHE hyp code
## Output of this folder is `kvm_nvhe.o`, a partially linked object
## file containing all nVHE hyp code and data.
##

hyp-obj := $(patsubst %.o,%.nvhe.o,$(hyp-obj-y))
obj-y := kvm_nvhe.o
targets += $(hyp-obj) kvm_nvhe.tmp.o kvm_nvhe.rel.o hyp.lds hyp-reloc.S hyp-reloc.o

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

# 4) Generate list of hyp code/data positions that need to be relocated at
#    runtime. Because the hypervisor is part of the kernel binary, relocations
#    produce a kernel VA. We enumerate relocations targeting hyp at build time
#    and convert the kernel VAs at those positions to hyp VAs.
$(obj)/hyp-reloc.S: $(obj)/kvm_nvhe.tmp.o $(obj)/gen-hyprel FORCE
	$(call if_changed,hyprel)

# 5) Compile hyp-reloc.S and link it into the existing partially linked object.
#    The object file now contains a section with pointers to hyp positions that
#    will contain kernel VAs at runtime. These pointers have relocations on them
#    so that they get updated as the hyp object is linked into `vmlinux`.
LDFLAGS_kvm_nvhe.rel.o := -r
$(obj)/kvm_nvhe.rel.o: $(obj)/kvm_nvhe.tmp.o $(obj)/hyp-reloc.o FORCE
	$(call if_changed,ld)

# 6) Produce the final 'kvm_nvhe.o', ready to be linked into 'vmlinux'.
#    Prefixes names of ELF symbols with '__kvm_nvhe_'.
$(obj)/kvm_nvhe.o: $(obj)/kvm_nvhe.rel.o FORCE
	$(call if_changed,hypcopy)

# The HYPREL command calls `gen-hyprel` to generate an assembly file with
# a list of relocations targeting hyp code/data.
quiet_cmd_hyprel = HYPREL  $@
      cmd_hyprel = $(obj)/gen-hyprel $< > $@

# The HYPCOPY command uses `objcopy` to prefix all ELF symbol names
# to avoid clashes with VHE code/data.
quiet_cmd_hypcopy = HYPCOPY $@
      cmd_hypcopy = $(OBJCOPY) --prefix-symbols=__kvm_nvhe_ $< $@

# Remove ftrace, Shadow Call Stack, and CFI CFLAGS.
# This is equivalent to the 'notrace', '__noscs', and '__nocfi' annotations.
KBUILD_CFLAGS := $(filter-out $(CC_FLAGS_FTRACE) $(CC_FLAGS_SCS) $(CC_FLAGS_CFI), $(KBUILD_CFLAGS))
# Starting from 13.0.0 llvm emits SHT_REL section '.llvm.call-graph-profile'
# when profile optimization is applied. gen-hyprel does not support SHT_REL and
# causes a build failure. Remove profile optimization flags.
KBUILD_CFLAGS := $(filter-out -fprofile-sample-use=% -fprofile-use=%, $(KBUILD_CFLAGS))
KBUILD_CFLAGS += -fno-asynchronous-unwind-tables -fno-unwind-tables

# KVM nVHE code is run at a different exception code with a different map, so
# compiler instrumentation that inserts callbacks or checks into the code may
# cause crashes. Just disable it.
GCOV_PROFILE	:= n
KASAN_SANITIZE	:= n
KCSAN_SANITIZE	:= n
UBSAN_SANITIZE	:= n
KCOV_INSTRUMENT	:= n

# Skip objtool checking for this directory because nVHE code is compiled with
# non-standard build rules.
OBJECT_FILES_NON_STANDARD := y
