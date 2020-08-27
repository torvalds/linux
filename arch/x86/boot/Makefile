#
# arch/x86/boot/Makefile
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 1994 by Linus Torvalds
# Changed by many, many contributors over the years.
#

# Sanitizer runtimes are unavailable and cannot be linked for early boot code.
KASAN_SANITIZE			:= n
KCSAN_SANITIZE			:= n
OBJECT_FILES_NON_STANDARD	:= y

# Kernel does not boot with kcov instrumentation here.
# One of the problems observed was insertion of __sanitizer_cov_trace_pc()
# callback into middle of per-cpu data enabling code. Thus the callback observed
# inconsistent state and crashed. We are interested mostly in syscall coverage,
# so boot code is not interesting anyway.
KCOV_INSTRUMENT		:= n

# If you want to preset the SVGA mode, uncomment the next line and
# set SVGA_MODE to whatever number you want.
# Set it to -DSVGA_MODE=NORMAL_VGA if you just want the EGA/VGA mode.
# The number is the same as you would ordinarily press at bootup.

SVGA_MODE	:= -DSVGA_MODE=NORMAL_VGA

targets		:= vmlinux.bin setup.bin setup.elf bzImage
targets		+= fdimage fdimage144 fdimage288 image.iso mtools.conf
subdir-		:= compressed

setup-y		+= a20.o bioscall.o cmdline.o copy.o cpu.o cpuflags.o cpucheck.o
setup-y		+= early_serial_console.o edd.o header.o main.o memory.o
setup-y		+= pm.o pmjump.o printf.o regs.o string.o tty.o video.o
setup-y		+= video-mode.o version.o
setup-$(CONFIG_X86_APM_BOOT) += apm.o

# The link order of the video-*.o modules can matter.  In particular,
# video-vga.o *must* be listed first, followed by video-vesa.o.
# Hardware-specific drivers should follow in the order they should be
# probed, and video-bios.o should typically be last.
setup-y		+= video-vga.o
setup-y		+= video-vesa.o
setup-y		+= video-bios.o

targets		+= $(setup-y)
hostprogs	:= tools/build
hostprogs	+= mkcpustr

HOST_EXTRACFLAGS += -I$(srctree)/tools/include \
		    -include include/generated/autoconf.h \
	            -D__EXPORTED_HEADERS__

ifdef CONFIG_X86_FEATURE_NAMES
$(obj)/cpu.o: $(obj)/cpustr.h

quiet_cmd_cpustr = CPUSTR  $@
      cmd_cpustr = $(obj)/mkcpustr > $@
$(obj)/cpustr.h: $(obj)/mkcpustr FORCE
	$(call if_changed,cpustr)
endif
targets += cpustr.h

# ---------------------------------------------------------------------------

KBUILD_CFLAGS	:= $(REALMODE_CFLAGS) -D_SETUP
KBUILD_AFLAGS	:= $(KBUILD_CFLAGS) -D__ASSEMBLY__
KBUILD_CFLAGS	+= $(call cc-option,-fmacro-prefix-map=$(srctree)/=)
KBUILD_CFLAGS	+= -fno-asynchronous-unwind-tables
GCOV_PROFILE := n
UBSAN_SANITIZE := n

$(obj)/bzImage: asflags-y  := $(SVGA_MODE)

quiet_cmd_image = BUILD   $@
silent_redirect_image = >/dev/null
cmd_image = $(obj)/tools/build $(obj)/setup.bin $(obj)/vmlinux.bin \
			       $(obj)/zoffset.h $@ $($(quiet)redirect_image)

$(obj)/bzImage: $(obj)/setup.bin $(obj)/vmlinux.bin $(obj)/tools/build FORCE
	$(call if_changed,image)
	@$(kecho) 'Kernel: $@ is ready' ' (#'`cat .version`')'

OBJCOPYFLAGS_vmlinux.bin := -O binary -R .note -R .comment -S
$(obj)/vmlinux.bin: $(obj)/compressed/vmlinux FORCE
	$(call if_changed,objcopy)

SETUP_OBJS = $(addprefix $(obj)/,$(setup-y))

sed-zoffset := -e 's/^\([0-9a-fA-F]*\) [a-zA-Z] \(startup_32\|startup_64\|efi32_stub_entry\|efi64_stub_entry\|efi_pe_entry\|efi32_pe_entry\|input_data\|kernel_info\|_end\|_ehead\|_text\|z_.*\)$$/\#define ZO_\2 0x\1/p'

quiet_cmd_zoffset = ZOFFSET $@
      cmd_zoffset = $(NM) $< | sed -n $(sed-zoffset) > $@

targets += zoffset.h
$(obj)/zoffset.h: $(obj)/compressed/vmlinux FORCE
	$(call if_changed,zoffset)


AFLAGS_header.o += -I$(objtree)/$(obj)
$(obj)/header.o: $(obj)/zoffset.h

LDFLAGS_setup.elf	:= -m elf_i386 -T
$(obj)/setup.elf: $(src)/setup.ld $(SETUP_OBJS) FORCE
	$(call if_changed,ld)

OBJCOPYFLAGS_setup.bin	:= -O binary
$(obj)/setup.bin: $(obj)/setup.elf FORCE
	$(call if_changed,objcopy)

$(obj)/compressed/vmlinux: FORCE
	$(Q)$(MAKE) $(build)=$(obj)/compressed $@

# Set this if you want to pass append arguments to the
# bzdisk/fdimage/isoimage kernel
FDARGS =
# Set this if you want an initrd included with the
# bzdisk/fdimage/isoimage kernel
FDINITRD =

image_cmdline = default linux $(FDARGS) $(if $(FDINITRD),initrd=initrd.img,)

$(obj)/mtools.conf: $(src)/mtools.conf.in
	sed -e 's|@OBJ@|$(obj)|g' < $< > $@

quiet_cmd_genimage = GENIMAGE $3
cmd_genimage = sh $(srctree)/$(src)/genimage.sh $2 $3 $(obj)/bzImage \
			$(obj)/mtools.conf '$(image_cmdline)' $(FDINITRD)

PHONY += bzdisk fdimage fdimage144 fdimage288 isoimage bzlilo install

# This requires write access to /dev/fd0
bzdisk: $(obj)/bzImage $(obj)/mtools.conf
	$(call cmd,genimage,bzdisk,/dev/fd0)

# These require being root or having syslinux 2.02 or higher installed
fdimage fdimage144: $(obj)/bzImage $(obj)/mtools.conf
	$(call cmd,genimage,fdimage144,$(obj)/fdimage)
	@$(kecho) 'Kernel: $(obj)/fdimage is ready'

fdimage288: $(obj)/bzImage $(obj)/mtools.conf
	$(call cmd,genimage,fdimage288,$(obj)/fdimage)
	@$(kecho) 'Kernel: $(obj)/fdimage is ready'

isoimage: $(obj)/bzImage
	$(call cmd,genimage,isoimage,$(obj)/image.iso)
	@$(kecho) 'Kernel: $(obj)/image.iso is ready'

bzlilo:
	if [ -f $(INSTALL_PATH)/vmlinuz ]; then mv $(INSTALL_PATH)/vmlinuz $(INSTALL_PATH)/vmlinuz.old; fi
	if [ -f $(INSTALL_PATH)/System.map ]; then mv $(INSTALL_PATH)/System.map $(INSTALL_PATH)/System.old; fi
	cat $(obj)/bzImage > $(INSTALL_PATH)/vmlinuz
	cp System.map $(INSTALL_PATH)/
	if [ -x /sbin/lilo ]; then /sbin/lilo; else /etc/lilo/install; fi

install:
	sh $(srctree)/$(src)/install.sh $(KERNELRELEASE) $(obj)/bzImage \
		System.map "$(INSTALL_PATH)"
