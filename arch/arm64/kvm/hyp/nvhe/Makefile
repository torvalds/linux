# SPDX-License-Identifier: GPL-2.0
#
# Makefile for Kernel-based Virtual Machine module, HYP/nVHE part
#

asflags-y := -D__KVM_NVHE_HYPERVISOR__
ccflags-y := -D__KVM_NVHE_HYPERVISOR__

obj-y := timer-sr.o sysreg-sr.o debug-sr.o switch.o tlb.o hyp-init.o
obj-y += ../vgic-v3-sr.o ../aarch32.o ../vgic-v2-cpuif-proxy.o ../entry.o \
	 ../fpsimd.o ../hyp-entry.o

obj-y := $(patsubst %.o,%.hyp.o,$(obj-y))
extra-y := $(patsubst %.hyp.o,%.hyp.tmp.o,$(obj-y))

$(obj)/%.hyp.tmp.o: $(src)/%.c FORCE
	$(call if_changed_rule,cc_o_c)
$(obj)/%.hyp.tmp.o: $(src)/%.S FORCE
	$(call if_changed_rule,as_o_S)
$(obj)/%.hyp.o: $(obj)/%.hyp.tmp.o FORCE
	$(call if_changed,hypcopy)

quiet_cmd_hypcopy = HYPCOPY $@
      cmd_hypcopy = $(OBJCOPY)	--prefix-symbols=__kvm_nvhe_		\
				--rename-section=.text=.hyp.text	\
				$< $@

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
