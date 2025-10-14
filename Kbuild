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

# Generate rq-offsets.h

rq-offsets-file := include/generated/rq-offsets.h

targets += kernel/sched/rq-offsets.s

kernel/sched/rq-offsets.s: $(offsets-file)

$(rq-offsets-file): kernel/sched/rq-offsets.s FORCE
	$(call filechk,offsets,__RQ_OFFSETS_H__)

# Check for missing system calls

quiet_cmd_syscalls = CALL    $<
      cmd_syscalls = $(CONFIG_SHELL) $< $(CC) $(c_flags) $(missing_syscalls_flags)

PHONY += missing-syscalls
missing-syscalls: scripts/checksyscalls.sh $(rq-offsets-file)
	$(call cmd,syscalls)

# Check the manual modification of atomic headers

quiet_cmd_check_sha1 = CHKSHA1 $<
      cmd_check_sha1 = \
	if ! command -v sha1sum >/dev/null; then \
		echo "warning: cannot check the header due to sha1sum missing"; \
		exit 0; \
	fi; \
	if [ "$$(sed -n '$$s:// ::p' $<)" != \
	     "$$(sed '$$d' $< | sha1sum | sed 's/ .*//')" ]; then \
		echo "error: $< has been modified." >&2; \
		exit 1; \
	fi; \
	touch $@

atomic-checks += $(addprefix $(obj)/.checked-, \
	  atomic-arch-fallback.h \
	  atomic-instrumented.h \
	  atomic-long.h)

targets += $(atomic-checks)
$(atomic-checks): $(obj)/.checked-%: include/linux/atomic/%  FORCE
	$(call if_changed,check_sha1)

# A phony target that depends on all the preparation targets

PHONY += prepare
prepare: $(offsets-file) missing-syscalls $(atomic-checks)
	@:

# Ordinary directory descending
# ---------------------------------------------------------------------------

obj-y			+= init/
obj-y			+= usr/
obj-y			+= arch/$(SRCARCH)/
obj-y			+= $(ARCH_CORE)
obj-y			+= kernel/
obj-y			+= certs/
obj-y			+= mm/
obj-y			+= fs/
obj-y			+= ipc/
obj-y			+= security/
obj-y			+= crypto/
obj-$(CONFIG_BLOCK)	+= block/
obj-$(CONFIG_IO_URING)	+= io_uring/
obj-$(CONFIG_RUST)	+= rust/
obj-y			+= $(ARCH_LIB)
obj-y			+= drivers/
obj-y			+= sound/
obj-$(CONFIG_SAMPLES)	+= samples/
obj-$(CONFIG_NET)	+= net/
obj-y			+= virt/
obj-y			+= $(ARCH_DRIVERS)
obj-$(CONFIG_DRM_HEADER_TEST)	+= include/
