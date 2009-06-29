#
# Building vDSO images for x86.
#

VDSO64-$(CONFIG_X86_64)		:= y
VDSO32-$(CONFIG_X86_32)		:= y
VDSO32-$(CONFIG_COMPAT)		:= y

vdso-install-$(VDSO64-y)	+= vdso.so
vdso-install-$(VDSO32-y)	+= $(vdso32-images)


# files to link into the vdso
vobjs-y := vdso-note.o vclock_gettime.o vgetcpu.o vvar.o

# files to link into kernel
obj-$(VDSO64-y)			+= vma.o vdso.o
obj-$(VDSO32-y)			+= vdso32.o vdso32-setup.o

vobjs := $(foreach F,$(vobjs-y),$(obj)/$F)

$(obj)/vdso.o: $(obj)/vdso.so

targets += vdso.so vdso.so.dbg vdso.lds $(vobjs-y)

export CPPFLAGS_vdso.lds += -P -C

VDSO_LDFLAGS_vdso.lds = -m elf_x86_64 -Wl,-soname=linux-vdso.so.1 \
		      	-Wl,-z,max-page-size=4096 -Wl,-z,common-page-size=4096

$(obj)/vdso.o: $(src)/vdso.S $(obj)/vdso.so

$(obj)/vdso.so.dbg: $(src)/vdso.lds $(vobjs) FORCE
	$(call if_changed,vdso)

$(obj)/%.so: OBJCOPYFLAGS := -S
$(obj)/%.so: $(obj)/%.so.dbg FORCE
	$(call if_changed,objcopy)

CFL := $(PROFILING) -mcmodel=small -fPIC -O2 -fasynchronous-unwind-tables -m64 \
       $(filter -g%,$(KBUILD_CFLAGS)) $(call cc-option, -fno-stack-protector)

$(vobjs): KBUILD_CFLAGS += $(CFL)

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
# Build multiple 32-bit vDSO images to choose from at boot time.
#
obj-$(VDSO32-y)			+= vdso32-syms.lds
vdso32.so-$(VDSO32-y)		+= int80
vdso32.so-$(CONFIG_COMPAT)	+= syscall
vdso32.so-$(VDSO32-y)		+= sysenter

vdso32-images			= $(vdso32.so-y:%=vdso32-%.so)

CPPFLAGS_vdso32.lds = $(CPPFLAGS_vdso.lds)
VDSO_LDFLAGS_vdso32.lds = -m elf_i386 -Wl,-soname=linux-gate.so.1

# This makes sure the $(obj) subdirectory exists even though vdso32/
# is not a kbuild sub-make subdirectory.
override obj-dirs = $(dir $(obj)) $(obj)/vdso32/

targets += vdso32/vdso32.lds
targets += $(vdso32-images) $(vdso32-images:=.dbg)
targets += vdso32/note.o $(vdso32.so-y:%=vdso32/%.o)

extra-y	+= $(vdso32-images)

$(obj)/vdso32.o: $(vdso32-images:%=$(obj)/%)

KBUILD_AFLAGS_32 := $(filter-out -m64,$(KBUILD_AFLAGS))
$(vdso32-images:%=$(obj)/%.dbg): KBUILD_AFLAGS = $(KBUILD_AFLAGS_32)
$(vdso32-images:%=$(obj)/%.dbg): asflags-$(CONFIG_X86_64) += -m32

$(vdso32-images:%=$(obj)/%.dbg): $(obj)/vdso32-%.so.dbg: FORCE \
				 $(obj)/vdso32/vdso32.lds \
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
		       -Wl,-T,$(filter %.lds,$^) $(filter %.o,$^)

VDSO_LDFLAGS = -fPIC -shared $(call ld-option, -Wl$(comma)--hash-style=sysv)
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
