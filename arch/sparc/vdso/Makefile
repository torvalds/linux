# SPDX-License-Identifier: GPL-2.0-only
#
# Building vDSO images for sparc.
#

VDSO64-$(CONFIG_SPARC64)	:= y
VDSOCOMPAT-$(CONFIG_COMPAT)	:= y

# files to link into the vdso
vobjs-y := vdso-note.o vclock_gettime.o

# files to link into kernel
obj-y				+= vma.o

# vDSO images to build
vdso_img-$(VDSO64-y)		+= 64
vdso_img-$(VDSOCOMPAT-y)	+= 32

vobjs := $(foreach F,$(vobjs-y),$(obj)/$F)

$(obj)/vdso.o: $(obj)/vdso.so

targets += vdso.lds $(vobjs-y)

# Build the vDSO image C files and link them in.
vdso_img_objs := $(vdso_img-y:%=vdso-image-%.o)
vdso_img_cfiles := $(vdso_img-y:%=vdso-image-%.c)
vdso_img_sodbg := $(vdso_img-y:%=vdso%.so.dbg)
obj-y += $(vdso_img_objs)
targets += $(vdso_img_cfiles)
targets += $(vdso_img_sodbg) $(vdso_img-y:%=vdso%.so)

CPPFLAGS_vdso.lds += -P -C

VDSO_LDFLAGS_vdso.lds = -m elf64_sparc -soname linux-vdso.so.1 --no-undefined \
			-z max-page-size=8192

$(obj)/vdso64.so.dbg: $(obj)/vdso.lds $(vobjs) FORCE
	$(call if_changed,vdso)

HOST_EXTRACFLAGS += -I$(srctree)/tools/include
hostprogs += vdso2c

quiet_cmd_vdso2c = VDSO2C  $@
      cmd_vdso2c = $(obj)/vdso2c $< $(<:%.dbg=%) $@

$(obj)/vdso-image-%.c: $(obj)/vdso%.so.dbg $(obj)/vdso%.so $(obj)/vdso2c FORCE
	$(call if_changed,vdso2c)

#
# Don't omit frame pointers for ease of userspace debugging, but do
# optimize sibling calls.
#
CFL := $(PROFILING) -mcmodel=medlow -fPIC -O2 -fasynchronous-unwind-tables -m64 \
       $(filter -g%,$(KBUILD_CFLAGS)) -fno-stack-protector \
       -fno-omit-frame-pointer -foptimize-sibling-calls \
       -DDISABLE_BRANCH_PROFILING -DBUILD_VDSO

SPARC_REG_CFLAGS = -ffixed-g4 -ffixed-g5 -fcall-used-g5 -fcall-used-g7

$(vobjs): KBUILD_CFLAGS := $(filter-out $(RANDSTRUCT_CFLAGS) $(GCC_PLUGINS_CFLAGS) $(SPARC_REG_CFLAGS),$(KBUILD_CFLAGS)) $(CFL)

#
# vDSO code runs in userspace and -pg doesn't help with profiling anyway.
#
CFLAGS_REMOVE_vclock_gettime.o = -pg
CFLAGS_REMOVE_vdso32/vclock_gettime.o = -pg

$(obj)/%.so: OBJCOPYFLAGS := -S
$(obj)/%.so: $(obj)/%.so.dbg FORCE
	$(call if_changed,objcopy)

CPPFLAGS_vdso32/vdso32.lds = $(CPPFLAGS_vdso.lds)
VDSO_LDFLAGS_vdso32.lds = -m elf32_sparc -soname linux-gate.so.1

#This makes sure the $(obj) subdirectory exists even though vdso32/
#is not a kbuild sub-make subdirectory
override obj-dirs = $(dir $(obj)) $(obj)/vdso32/

targets += vdso32/vdso32.lds
targets += vdso32/vdso-note.o
targets += vdso32/vclock_gettime.o

KBUILD_AFLAGS_32 := $(filter-out -m64,$(KBUILD_AFLAGS)) -DBUILD_VDSO
$(obj)/vdso32.so.dbg: KBUILD_AFLAGS = $(KBUILD_AFLAGS_32)
$(obj)/vdso32.so.dbg: asflags-$(CONFIG_SPARC64) += -m32

KBUILD_CFLAGS_32 := $(filter-out -m64,$(KBUILD_CFLAGS))
KBUILD_CFLAGS_32 := $(filter-out -mcmodel=medlow,$(KBUILD_CFLAGS_32))
KBUILD_CFLAGS_32 := $(filter-out -fno-pic,$(KBUILD_CFLAGS_32))
KBUILD_CFLAGS_32 := $(filter-out $(RANDSTRUCT_CFLAGS),$(KBUILD_CFLAGS_32))
KBUILD_CFLAGS_32 := $(filter-out $(GCC_PLUGINS_CFLAGS),$(KBUILD_CFLAGS_32))
KBUILD_CFLAGS_32 := $(filter-out $(SPARC_REG_CFLAGS),$(KBUILD_CFLAGS_32))
KBUILD_CFLAGS_32 += -m32 -msoft-float -fpic
KBUILD_CFLAGS_32 += -fno-stack-protector
KBUILD_CFLAGS_32 += $(call cc-option, -foptimize-sibling-calls)
KBUILD_CFLAGS_32 += -fno-omit-frame-pointer
KBUILD_CFLAGS_32 += -DDISABLE_BRANCH_PROFILING
KBUILD_CFLAGS_32 += -mv8plus
$(obj)/vdso32.so.dbg: KBUILD_CFLAGS = $(KBUILD_CFLAGS_32)

$(obj)/vdso32.so.dbg: FORCE \
			$(obj)/vdso32/vdso32.lds \
			$(obj)/vdso32/vclock_gettime.o \
			$(obj)/vdso32/vdso-note.o
		$(call	if_changed,vdso)

#
# The DSO images are built using a special linker script.
#
quiet_cmd_vdso = VDSO    $@
      cmd_vdso = $(LD) -nostdlib -o $@ \
		       $(VDSO_LDFLAGS) $(VDSO_LDFLAGS_$(filter %.lds,$(^F))) \
		       -T $(filter %.lds,$^) $(filter %.o,$^) && \
		sh $(srctree)/$(src)/checkundef.sh '$(OBJDUMP)' '$@'

VDSO_LDFLAGS = -shared --hash-style=both --build-id=sha1 -Bsymbolic
GCOV_PROFILE := n
