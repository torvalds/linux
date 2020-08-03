# SPDX-License-Identifier: GPL-2.0
# List of files in the vdso, has to be asm only for now

KCOV_INSTRUMENT := n

obj-vdso64 = gettimeofday.o clock_getres.o clock_gettime.o note.o getcpu.o

# Build rules

targets := $(obj-vdso64) vdso64.so vdso64.so.dbg
obj-vdso64 := $(addprefix $(obj)/, $(obj-vdso64))

KBUILD_AFLAGS += -DBUILD_VDSO
KBUILD_CFLAGS += -DBUILD_VDSO

KBUILD_AFLAGS_64 := $(filter-out -m64,$(KBUILD_AFLAGS))
KBUILD_AFLAGS_64 += -m64 -s

KBUILD_CFLAGS_64 := $(filter-out -m64,$(KBUILD_CFLAGS))
KBUILD_CFLAGS_64 += -m64 -fPIC -shared -fno-common -fno-builtin
ldflags-y := -fPIC -shared -nostdlib -soname=linux-vdso64.so.1 \
	     --hash-style=both --build-id -T

$(targets:%=$(obj)/%.dbg): KBUILD_CFLAGS = $(KBUILD_CFLAGS_64)
$(targets:%=$(obj)/%.dbg): KBUILD_AFLAGS = $(KBUILD_AFLAGS_64)

obj-y += vdso64_wrapper.o
extra-y += vdso64.lds
CPPFLAGS_vdso64.lds += -P -C -U$(ARCH)

# Disable gcov profiling, ubsan and kasan for VDSO code
GCOV_PROFILE := n
UBSAN_SANITIZE := n
KASAN_SANITIZE := n

# Force dependency (incbin is bad)
$(obj)/vdso64_wrapper.o : $(obj)/vdso64.so

# link rule for the .so file, .lds has to be first
$(obj)/vdso64.so.dbg: $(obj)/vdso64.lds $(obj-vdso64) FORCE
	$(call if_changed,ld)

# strip rule for the .so file
$(obj)/%.so: OBJCOPYFLAGS := -S
$(obj)/%.so: $(obj)/%.so.dbg FORCE
	$(call if_changed,objcopy)

# assembly rules for the .S files
$(obj-vdso64): %.o: %.S FORCE
	$(call if_changed_dep,vdso64as)

# actual build commands
quiet_cmd_vdso64as = VDSO64A $@
      cmd_vdso64as = $(CC) $(a_flags) -c -o $@ $<

# install commands for the unstripped file
quiet_cmd_vdso_install = INSTALL $@
      cmd_vdso_install = cp $(obj)/$@.dbg $(MODLIB)/vdso/$@

vdso64.so: $(obj)/vdso64.so.dbg
	@mkdir -p $(MODLIB)/vdso
	$(call cmd,vdso_install)

vdso_install: vdso64.so
