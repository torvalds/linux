# SPDX-License-Identifier: GPL-2.0

KBUILD_AFLAGS		+= -D__DISABLE_EXPORTS
KBUILD_CFLAGS		+= -D__DISABLE_EXPORTS -mcmodel=small -fPIC \
			   -Os -DDISABLE_BRANCH_PROFILING \
			   $(DISABLE_STACKLEAK_PLUGIN) \
			   $(DISABLE_LATENT_ENTROPY_PLUGIN) \
			   -fno-stack-protector -D__NO_FORTIFY \
			   -fno-jump-tables \
			   -include $(srctree)/include/linux/hidden.h

# disable ftrace hooks and LTO
KBUILD_CFLAGS	:= $(subst $(CC_FLAGS_FTRACE),,$(KBUILD_CFLAGS))
KBUILD_CFLAGS	:= $(filter-out $(CC_FLAGS_LTO),$(KBUILD_CFLAGS))
KASAN_SANITIZE	:= n
KCSAN_SANITIZE	:= n
KMSAN_SANITIZE	:= n
UBSAN_SANITIZE	:= n
KCOV_INSTRUMENT	:= n

obj-$(CONFIG_X86_64)		+= gdt_idt.o map_kernel.o
obj-$(CONFIG_AMD_MEM_ENCRYPT)	+= sme.o sev-startup.o
pi-objs				:= $(patsubst %.o,$(obj)/%.o,$(obj-y))

lib-$(CONFIG_X86_64)		+= la57toggle.o
lib-$(CONFIG_EFI_MIXED)		+= efi-mixed.o

#
# Disable objtool validation for all library code, which is intended
# to be linked into the decompressor or the EFI stub but not vmlinux
#
$(patsubst %.o,$(obj)/%.o,$(lib-y)): OBJECT_FILES_NON_STANDARD := y

#
# Invoke objtool for each object individually to check for absolute
# relocations, even if other objtool actions are being deferred.
#
$(pi-objs): objtool-enabled	= 1
$(pi-objs): objtool-args	= $(if $(delay-objtool),,$(objtool-args-y)) --noabs

#
# Confine the startup code by prefixing all symbols with __pi_ (for position
# independent). This ensures that startup code can only call other startup
# code, or code that has explicitly been made accessible to it via a symbol
# alias.
#
$(obj)/%.pi.o: OBJCOPYFLAGS := --prefix-symbols=__pi_
$(obj)/%.pi.o: $(obj)/%.o FORCE
	$(call if_changed,objcopy)

targets	+= $(obj-y)
obj-y	:= $(patsubst %.o,%.pi.o,$(obj-y))
