# SPDX-License-Identifier: GPL-2.0
#
# Kbuild for top-level directory of the kernel

#####
# Generate bounds.h

bounds-file := include/generated/bounds.h

always  := $(bounds-file)
targets := kernel/bounds.s

$(bounds-file): kernel/bounds.s FORCE
	$(call filechk,offsets,__LINUX_BOUNDS_H__)

#####
# Generate timeconst.h

timeconst-file := include/generated/timeconst.h

targets += $(timeconst-file)

filechk_gentimeconst = echo $(CONFIG_HZ) | bc -q $<

$(timeconst-file): kernel/time/timeconst.bc FORCE
	$(call filechk,gentimeconst)

#####
# Generate asm-offsets.h

offsets-file := include/generated/asm-offsets.h

always  += $(offsets-file)
targets += arch/$(SRCARCH)/kernel/asm-offsets.s

arch/$(SRCARCH)/kernel/asm-offsets.s: $(timeconst-file) $(bounds-file)

$(offsets-file): arch/$(SRCARCH)/kernel/asm-offsets.s FORCE
	$(call filechk,offsets,__ASM_OFFSETS_H__)

#####
# Check for missing system calls

always += missing-syscalls
targets += missing-syscalls

quiet_cmd_syscalls = CALL    $<
      cmd_syscalls = $(CONFIG_SHELL) $< $(CC) $(c_flags) $(missing_syscalls_flags)

missing-syscalls: scripts/checksyscalls.sh $(offsets-file) FORCE
	$(call cmd,syscalls)

#####
# Check atomic headers are up-to-date

always += old-atomics
targets += old-atomics

quiet_cmd_atomics = CALL    $<
      cmd_atomics = $(CONFIG_SHELL) $<

old-atomics: scripts/atomic/check-atomics.sh FORCE
	$(call cmd,atomics)

# Keep these three files during make clean
no-clean-files := $(bounds-file) $(offsets-file) $(timeconst-file)
