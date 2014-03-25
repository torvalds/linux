#
# Building vDSO images for x86.
#

VDSO64-$(CONFIG_X86_64)		:= y
VDSOX32-$(CONFIG_X86_X32_ABI)	:= y
VDSO32-$(CONFIG_X86_32)		:= y
VDSO32-$(CONFIG_COMPAT)		:= y

vdso-install-$(VDSO64-y)	+= vdso.so
vdso-install-$(VDSOX32-y)	+= vdsox32.so
vdso-install-$(VDSO32-y)	+= $(vdso32-images)


# files to link into the vdso
vobjs-y := vdso-note.o vclock_gettime.o vgetcpu.o

vobjs-$(VDSOX32-y) += $(vobjx32s-compat)

# Filter out x32 objects.
vobj64s := $(filter-out $(vobjx32s-compat),$(vobjs-y))

# files to link into kernel
obj-y				+= vma.o
obj-$(VDSO64-y)			+= vdso.o
obj-$(VDSOX32-y)		+= vdsox32.o
obj-$(VDSO32-y)			+= vdso32.o vdso32-setup.o

vobjs := $(foreach F,$(vobj64s),$(obj)/$F)

$(obj)/vdso.o: $(obj)/vdso.so

targets += vdso.so vdso.so.dbg vdso.lds $(vobjs-y)

export CPPFLAGS_vdso.lds += -P -C

VDSO_LDFLAGS_vdso.lds = -m64 -Wl,-soname=linux-vdso.so.1 \
			-Wl,--no-undefined \
		      	-Wl,-z,max-page-size=4096 -Wl,-z,common-page-size=4096

$(obj)/vdso.o: $(src)/vdso.S $(obj)/vdso.so

$(obj)/vdso.so.dbg: $(src)/vdso.lds $(vobjs) FORCE
	$(call if_changed,vdso)

$(obj)/%.so: OBJCOPYFLAGS := -S
$(obj)/%.so: $(obj)/%.so.dbg FORCE
	$(call if_changed,objcopy)

#
# Don't omit frame pointers for ease of userspace debugging, but do
# optimize sibling calls.
#
CFL := $(PROFILING) -mcmodel=small -fPIC -O2 -fasynchronous-unwind-tables -m64 \
       $(filter -g%,$(KBUILD_CFLAGS)) $(call cc-option, -fno-stack-protector) \
       -fno-omit-frame-pointer -foptimize-sibling-calls

$(vobjs): KBUILD_CFLAGS += $(CFL)

#
# vDSO code runs in userspace and -pg doesn't help with profiling anyway.
#
CFLAGS_REMOVE_vdso-note.o = -pg
CFLAGS_REMOVE_vclock_gettime.o = -pg
CFLAGS_REMOVE_vgetcpu.o = -pg
CFLAGS_REMOVE_vvar.o = -pg

targets += vdso-syms.lds
obj-$(VDSO64-y)			+= vdso-syms.lds

#
# Match symbols in the DSO that look like VDSO*; produce a file of constants.
#
sed-vdsosym := -e 's/^00*/0/' \
	-e 's/^\([0-9a-fA-F]*\) . \(VDSO[a-zA-Z0-9_]*\)$$/\2 = 0x\1;/p'
quiet_cmd_vdsosym = VDSOSYM $@
define cmd_vdsosym
	$(NM) $< | LC_ALL=C sed -n $(sed-vdsosym) | LC_ALL=C sort > $@
endef

$(obj)/%-syms.lds: $(obj)/%.so.dbg FORCE
	$(call if_changed,vdsosym)

#
# X32 processes use x32 vDSO to access 64bit kernel data.
#
# Build x32 vDSO image:
# 1. Compile x32 vDSO as 64bit.
# 2. Convert object files to x32.
# 3. Build x32 VDSO image with x32 objects, which contains 64bit codes
# so that it can reach 64bit address space with 64bit pointers.
#

targets += vdsox32-syms.lds
obj-$(VDSOX32-y)		+= vdsox32-syms.lds

CPPFLAGS_vdsox32.lds = $(CPPFLAGS_vdso.lds)
VDSO_LDFLAGS_vdsox32.lds = -Wl,-m,elf32_x86_64 \
			   -Wl,-soname=linux-vdso.so.1 \
			   -Wl,-z,max-page-size=4096 \
			   -Wl,-z,common-page-size=4096

vobjx32s-y := $(vobj64s:.o=-x32.o)
vobjx32s := $(foreach F,$(vobjx32s-y),$(obj)/$F)

# Convert 64bit object file to x32 for x32 vDSO.
quiet_cmd_x32 = X32     $@
      cmd_x32 = $(OBJCOPY) -O elf32-x86-64 $< $@

$(obj)/%-x32.o: $(obj)/%.o FORCE
	$(call if_changed,x32)

targets += vdsox32.so vdsox32.so.dbg vdsox32.lds $(vobjx32s-y)

$(obj)/vdsox32.o: $(src)/vdsox32.S $(obj)/vdsox32.so

$(obj)/vdsox32.so.dbg: $(src)/vdsox32.lds $(vobjx32s) FORCE
	$(call if_changed,vdso)

#
# Build multiple 32-bit vDSO images to choose from at boot time.
#
obj-$(VDSO32-y)			+= vdso32-syms.lds
vdso32.so-$(VDSO32-y)		+= int80
vdso32.so-$(CONFIG_COMPAT)	+= syscall
vdso32.so-$(VDSO32-y)		+= sysenter

vdso32-images			= $(vdso32.so-y:%=vdso32-%.so)

CPPFLAGS_vdso32.lds = $(CPPFLAGS_vdso.lds)
VDSO_LDFLAGS_vdso32.lds = -m32 -Wl,-soname=linux-gate.so.1

# This makes sure the $(obj) subdirectory exists even though vdso32/
# is not a kbuild sub-make subdirectory.
override obj-dirs = $(dir $(obj)) $(obj)/vdso32/

targets += vdso32/vdso32.lds
targets += $(vdso32-images) $(vdso32-images:=.dbg)
targets += vdso32/note.o vdso32/vclock_gettime.o $(vdso32.so-y:%=vdso32/%.o)

extra-y	+= $(vdso32-images)

$(obj)/vdso32.o: $(vdso32-images:%=$(obj)/%)

KBUILD_AFLAGS_32 := $(filter-out -m64,$(KBUILD_AFLAGS))
$(vdso32-images:%=$(obj)/%.dbg): KBUILD_AFLAGS = $(KBUILD_AFLAGS_32)
$(vdso32-images:%=$(obj)/%.dbg): asflags-$(CONFIG_X86_64) += -m32

KBUILD_CFLAGS_32 := $(filter-out -m64,$(KBUILD_CFLAGS))
KBUILD_CFLAGS_32 := $(filter-out -mcmodel=kernel,$(KBUILD_CFLAGS_32))
KBUILD_CFLAGS_32 := $(filter-out -fno-pic,$(KBUILD_CFLAGS_32))
KBUILD_CFLAGS_32 := $(filter-out -mfentry,$(KBUILD_CFLAGS_32))
KBUILD_CFLAGS_32 += -m32 -msoft-float -mregparm=0 -fpic
KBUILD_CFLAGS_32 += $(call cc-option, -fno-stack-protector)
KBUILD_CFLAGS_32 += $(call cc-option, -foptimize-sibling-calls)
KBUILD_CFLAGS_32 += -fno-omit-frame-pointer
$(vdso32-images:%=$(obj)/%.dbg): KBUILD_CFLAGS = $(KBUILD_CFLAGS_32)

$(vdso32-images:%=$(obj)/%.dbg): $(obj)/vdso32-%.so.dbg: FORCE \
				 $(obj)/vdso32/vdso32.lds \
				 $(obj)/vdso32/vclock_gettime.o \
				 $(obj)/vdso32/note.o \
				 $(obj)/vdso32/%.o
	$(call if_changed,vdso)

# Make vdso32-*-syms.lds from each image, and then make sure they match.
# The only difference should be that some do not define VDSO32_SYSENTER_RETURN.

targets += vdso32-syms.lds $(vdso32.so-y:%=vdso32-%-syms.lds)

quiet_cmd_vdso32sym = VDSOSYM $@
define cmd_vdso32sym
	if LC_ALL=C sort -u $(filter-out FORCE,$^) > $(@D)/.tmp_$(@F) && \
	   $(foreach H,$(filter-out FORCE,$^),\
		     if grep -q VDSO32_SYSENTER_RETURN $H; \
		     then diff -u $(@D)/.tmp_$(@F) $H; \
		     else sed /VDSO32_SYSENTER_RETURN/d $(@D)/.tmp_$(@F) | \
			  diff -u - $H; fi &&) : ;\
	then mv -f $(@D)/.tmp_$(@F) $@; \
	else rm -f $(@D)/.tmp_$(@F); exit 1; \
	fi
endef

$(obj)/vdso32-syms.lds: $(vdso32.so-y:%=$(obj)/vdso32-%-syms.lds) FORCE
	$(call if_changed,vdso32sym)

#
# The DSO images are built using a special linker script.
#
quiet_cmd_vdso = VDSO    $@
      cmd_vdso = $(CC) -nostdlib -o $@ \
		       $(VDSO_LDFLAGS) $(VDSO_LDFLAGS_$(filter %.lds,$(^F))) \
		       -Wl,-T,$(filter %.lds,$^) $(filter %.o,$^) && \
		 sh $(srctree)/$(src)/checkundef.sh '$(NM)' '$@'

VDSO_LDFLAGS = -fPIC -shared $(call cc-ldoption, -Wl$(comma)--hash-style=sysv)
GCOV_PROFILE := n

#
# Install the unstripped copy of vdso*.so listed in $(vdso-install-y).
#
quiet_cmd_vdso_install = INSTALL $@
      cmd_vdso_install = cp $(obj)/$@.dbg $(MODLIB)/vdso/$@
$(vdso-install-y): %.so: $(obj)/%.so.dbg FORCE
	@mkdir -p $(MODLIB)/vdso
	$(call cmd,vdso_install)

PHONY += vdso_install $(vdso-install-y)
vdso_install: $(vdso-install-y)

clean-files := vdso32-syscall* vdso32-sysenter* vdso32-int80*
