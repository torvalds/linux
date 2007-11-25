#
# x86-64 vDSO.
#

# files to link into the vdso
# vdso-start.o has to be first
vobjs-y := vdso-start.o vdso-note.o vclock_gettime.o vgetcpu.o vvar.o

# files to link into kernel
obj-y := vma.o vdso.o vdso-syms.o

vobjs := $(foreach F,$(vobjs-y),$(obj)/$F)

$(obj)/vdso.o: $(obj)/vdso.so

targets += vdso.so vdso.so.dbg vdso.lds $(vobjs-y) vdso-syms.o

# The DSO images are built using a special linker script.
quiet_cmd_syscall = SYSCALL $@
      cmd_syscall = $(CC) -m elf_x86_64 -nostdlib $(SYSCFLAGS_$(@F)) \
		          -Wl,-T,$(filter-out FORCE,$^) -o $@

export CPPFLAGS_vdso.lds += -P -C

vdso-flags = -fPIC -shared -Wl,-soname=linux-vdso.so.1 \
		 $(call ld-option, -Wl$(comma)--hash-style=sysv) \
		-Wl,-z,max-page-size=4096 -Wl,-z,common-page-size=4096
SYSCFLAGS_vdso.so = $(vdso-flags)
SYSCFLAGS_vdso.so.dbg = $(vdso-flags)

$(obj)/vdso.o: $(src)/vdso.S $(obj)/vdso.so

$(obj)/vdso.so: $(src)/vdso.lds $(vobjs) FORCE

$(obj)/vdso.so.dbg: $(src)/vdso.lds $(vobjs) FORCE
	$(call if_changed,syscall)

$(obj)/%.so: OBJCOPYFLAGS := -S
$(obj)/%.so: $(obj)/%.so.dbg FORCE
	$(call if_changed,objcopy)

CFL := $(PROFILING) -mcmodel=small -fPIC -g0 -O2 -fasynchronous-unwind-tables -m64

$(obj)/vclock_gettime.o: KBUILD_CFLAGS = $(CFL)
$(obj)/vgetcpu.o: KBUILD_CFLAGS = $(CFL)

# We also create a special relocatable object that should mirror the symbol
# table and layout of the linked DSO.  With ld -R we can then refer to
# these symbols in the kernel code rather than hand-coded addresses.
extra-y += vdso-syms.o
$(obj)/built-in.o: $(obj)/vdso-syms.o
$(obj)/built-in.o: ld_flags += -R $(obj)/vdso-syms.o

SYSCFLAGS_vdso-syms.o = -r -d
$(obj)/vdso-syms.o: $(src)/vdso.lds $(vobjs) FORCE
	$(call if_changed,syscall)

quiet_cmd_vdso_install = INSTALL $@
      cmd_vdso_install = cp $(obj)/$@.dbg $(MODLIB)/vdso/$@
vdso.so:
	@mkdir -p $(MODLIB)/vdso
	$(call cmd,vdso_install)

vdso_install: vdso.so
