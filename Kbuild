# SPDX-License-Identifier: GPL-2.0
#
# Kbuild for top-level directory of the kernel

# Prepare global headers and check sanity before descending into sub-directories
# ---------------------------------------------------------------------------

# Generate bounds.h

bounds-file := include/generated/bounds.h

targets := kernel/bounds.s

$(bounds-file): kernel/bounds.s FORCE
	$(call filechk,offsets,__LINUX_BOUNDS_H__)

# Generate timeconst.h

timeconst-file := include/generated/timeconst.h

filechk_gentimeconst = echo $(CONFIG_HZ) | bc -q $<

$(timeconst-file): kernel/time/timeconst.bc FORCE
	$(call filechk,gentimeconst)

# Generate asm-offsets.h

offsets-file := include/generated/asm-offsets.h

targets += arch/$(SRCARCH)/kernel/asm-offsets.s

arch/$(SRCARCH)/kernel/asm-offsets.s: $(timeconst-file) $(bounds-file)

$(offsets-file): arch/$(SRCARCH)/kernel/asm-offsets.s FORCE
	$(call filechk,offsets,__ASM_OFFSETS_H__)

# Check for missing system calls

quiet_cmd_syscalls = CALL    $<
      cmd_syscalls = $(CONFIG_SHELL) $< $(CC) $(c_flags) $(missing_syscalls_flags)

PHONY += missing-syscalls
missing-syscalls: scripts/checksyscalls.sh $(offsets-file)
	$(call cmd,syscalls)

# Check atomic headers are up-to-date

quiet_cmd_atomics = CALL    $<
      cmd_atomics = $(CONFIG_SHELL) $<

PHONY += old-atomics
old-atomics: scripts/atomic/check-atomics.sh
	$(call cmd,atomics)

# A phony target that depends on all the preparation targets

PHONY += prepare
prepare: $(offsets-file) missing-syscalls old-atomics
	@:
