# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the linux s390-specific parts of the memory manager.
#

KCOV_INSTRUMENT := n
GCOV_PROFILE := n
UBSAN_SANITIZE := n
KASAN_SANITIZE := n

KBUILD_AFLAGS := $(KBUILD_AFLAGS_DECOMPRESSOR)
KBUILD_CFLAGS := $(KBUILD_CFLAGS_DECOMPRESSOR)

#
# Use minimum architecture for als.c to be able to print an error
# message if the kernel is started on a machine which is too old
#
ifndef CONFIG_CC_IS_CLANG
CC_FLAGS_MARCH_MINIMUM := -march=z900
else
CC_FLAGS_MARCH_MINIMUM := -march=z10
endif

ifneq ($(CC_FLAGS_MARCH),$(CC_FLAGS_MARCH_MINIMUM))
AFLAGS_REMOVE_head.o		+= $(CC_FLAGS_MARCH)
AFLAGS_head.o			+= $(CC_FLAGS_MARCH_MINIMUM)
AFLAGS_REMOVE_mem.o		+= $(CC_FLAGS_MARCH)
AFLAGS_mem.o			+= $(CC_FLAGS_MARCH_MINIMUM)
CFLAGS_REMOVE_als.o		+= $(CC_FLAGS_MARCH)
CFLAGS_als.o			+= $(CC_FLAGS_MARCH_MINIMUM)
CFLAGS_REMOVE_sclp_early_core.o	+= $(CC_FLAGS_MARCH)
CFLAGS_sclp_early_core.o	+= $(CC_FLAGS_MARCH_MINIMUM)
endif

CFLAGS_sclp_early_core.o += -I$(srctree)/drivers/s390/char

obj-y	:= head.o als.o startup.o mem_detect.o ipl_parm.o ipl_report.o
obj-y	+= string.o ebcdic.o sclp_early_core.o mem.o ipl_vmparm.o cmdline.o
obj-y	+= ctype.o text_dma.o
obj-$(CONFIG_PROTECTED_VIRTUALIZATION_GUEST)	+= uv.o
obj-$(CONFIG_RELOCATABLE)	+= machine_kexec_reloc.o
obj-$(CONFIG_RANDOMIZE_BASE)	+= kaslr.o
targets	:= bzImage startup.a section_cmp.boot.data section_cmp.boot.preserved.data $(obj-y)
subdir-	:= compressed

OBJECTS := $(addprefix $(obj)/,$(obj-y))

quiet_cmd_section_cmp = SECTCMP $*
define cmd_section_cmp
	s1=`$(OBJDUMP) -t -j "$*" "$<" | sort | \
		sed -n "/0000000000000000/! s/.*\s$*\s\+//p" | sha256sum`; \
	s2=`$(OBJDUMP) -t -j "$*" "$(word 2,$^)" | sort | \
		sed -n "/0000000000000000/! s/.*\s$*\s\+//p" | sha256sum`; \
	if [ "$$s1" != "$$s2" ]; then \
		echo "error: section $* differs between $< and $(word 2,$^)" >&2; \
		exit 1; \
	fi; \
	touch $@
endef

OBJCOPYFLAGS_bzImage := --pad-to $$(readelf -s $(obj)/compressed/vmlinux | awk '/\<_end\>/ {print or(strtonum("0x"$$2),4095)+1}')
$(obj)/bzImage: $(obj)/compressed/vmlinux $(obj)/section_cmp.boot.data $(obj)/section_cmp.boot.preserved.data FORCE
	$(call if_changed,objcopy)

$(obj)/section_cmp%: vmlinux $(obj)/compressed/vmlinux FORCE
	$(call if_changed,section_cmp)

$(obj)/compressed/vmlinux: $(obj)/startup.a FORCE
	$(Q)$(MAKE) $(build)=$(obj)/compressed $@

$(obj)/startup.a: $(OBJECTS) FORCE
	$(call if_changed,ar)

install: $(CONFIGURE) $(obj)/bzImage
	sh -x  $(srctree)/$(obj)/install.sh $(KERNELRELEASE) $(obj)/bzImage \
	      System.map "$(INSTALL_PATH)"

chkbss := $(obj-y)
chkbss-target := startup.a
include $(srctree)/arch/s390/scripts/Makefile.chkbss
