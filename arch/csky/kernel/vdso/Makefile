# SPDX-License-Identifier: GPL-2.0-only

# Include the generic Makefile to check the built vdso.
include $(srctree)/lib/vdso/Makefile

# Symbols present in the vdso
vdso-syms  += rt_sigreturn
vdso-syms  += vgettimeofday

# Files to link into the vdso
obj-vdso = $(patsubst %, %.o, $(vdso-syms)) note.o

ifneq ($(c-gettimeofday-y),)
	CFLAGS_vgettimeofday.o += -include $(c-gettimeofday-y)
endif

ccflags-y := -fno-stack-protector -DBUILD_VDSO32

# Build rules
targets := $(obj-vdso) vdso.so vdso.so.dbg vdso.lds vdso-dummy.o
obj-vdso := $(addprefix $(obj)/, $(obj-vdso))

obj-y += vdso.o vdso-syms.o
CPPFLAGS_vdso.lds += -P -C -U$(ARCH)

# Disable gcov profiling for VDSO code
GCOV_PROFILE := n
KCOV_INSTRUMENT := n

# Force dependency
$(obj)/vdso.o: $(obj)/vdso.so

SYSCFLAGS_vdso.so.dbg = $(c_flags)
$(obj)/vdso.so.dbg: $(src)/vdso.lds $(obj-vdso) FORCE
	$(call if_changed,vdsold)
SYSCFLAGS_vdso.so.dbg = -shared -s -Wl,-soname=linux-vdso.so.1 \
	-Wl,--build-id=sha1 -Wl,--hash-style=both

$(obj)/vdso-syms.S: $(obj)/vdso.so FORCE
	$(call if_changed,so2s)

# strip rule for the .so file
$(obj)/%.so: OBJCOPYFLAGS := -S
$(obj)/%.so: $(obj)/%.so.dbg FORCE
	$(call if_changed,objcopy)

# actual build commands
# The DSO images are built using a special linker script
# Make sure only to export the intended __vdso_xxx symbol offsets.
quiet_cmd_vdsold = VDSOLD  $@
      cmd_vdsold = $(CC) $(KBUILD_CFLAGS) $(call cc-option, -no-pie) -nostdlib -nostartfiles $(SYSCFLAGS_$(@F)) \
                           -Wl,-T,$(filter-out FORCE,$^) -o $@.tmp && \
                   $(CROSS_COMPILE)objcopy \
                           $(patsubst %, -G __vdso_%, $(vdso-syms)) $@.tmp $@ && \
                   rm $@.tmp

# Extracts symbol offsets from the VDSO, converting them into an assembly file
# that contains the same symbols at the same offsets.
quiet_cmd_so2s = SO2S    $@
      cmd_so2s = $(NM) -D $< | $(srctree)/$(src)/so2s.sh > $@
