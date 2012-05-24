# Makefile for the Sparc boot stuff.
#
# Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
# Copyright (C) 1997,1998 Jakub Jelinek (jj@ultra.linux.cz)

ROOT_IMG	:= /usr/src/root.img
ELFTOAOUT	:= elftoaout

hostprogs-y	:= piggyback btfixupprep
targets		:= tftpboot.img btfix.o btfix.S image zImage vmlinux.aout
clean-files	:= System.map

quiet_cmd_elftoaout	= ELFTOAOUT $@
      cmd_elftoaout	= $(ELFTOAOUT) $(obj)/image -o $@
quiet_cmd_piggy		= PIGGY   $@
      cmd_piggy		= $(obj)/piggyback $(BITS) $@ System.map $(ROOT_IMG)
quiet_cmd_strip		= STRIP   $@
      cmd_strip		= $(STRIP) -R .comment -R .note -K sun4u_init -K _end -K _start $< -o $@

ifeq ($(CONFIG_SPARC32),y)
quiet_cmd_btfix		= BTFIX   $@
      cmd_btfix		= $(OBJDUMP) -x vmlinux | $(obj)/btfixupprep > $@
quiet_cmd_sysmap        = SYSMAP  $(obj)/System.map
      cmd_sysmap        = $(CONFIG_SHELL) $(srctree)/scripts/mksysmap
quiet_cmd_image = LD      $@
      cmd_image = $(LD) $(LDFLAGS) $(EXTRA_LDFLAGS) $(LDFLAGS_$(@F)) -o $@

define rule_image
	$(if $($(quiet)cmd_image),               \
	  echo '  $($(quiet)cmd_image)' &&)      \
	  $(cmd_image);                          \
	$(if $($(quiet)cmd_sysmap),              \
	  echo '  $($(quiet)cmd_sysmap)' &&)  \
	$(cmd_sysmap) $@ $(obj)/System.map;      \
	if [ $$? -ne 0 ]; then                   \
		rm -f $@;                        \
		/bin/false;                      \
	fi;                                      \
	echo 'cmd_$@ := $(cmd_image)' > $(@D)/.$(@F).cmd
endef

BTOBJS := $(patsubst %/, %/built-in.o, $(VMLINUX_INIT))
BTLIBS := $(patsubst %/, %/built-in.o, $(VMLINUX_MAIN))
LDFLAGS_image := -T arch/sparc/kernel/vmlinux.lds $(BTOBJS) \
                  --start-group $(BTLIBS) --end-group \
                  $(kallsyms.o) $(obj)/btfix.o

# Link the final image including btfixup'ed symbols.
# This is a replacement for the link done in the top-level Makefile.
# Note: No dependency on the prerequisite files since that would require
# make to try check if they are updated - and due to changes
# in gcc options (path for example) this would result in
# these files being recompiled for each build.
$(obj)/image: $(obj)/btfix.o FORCE
	$(call if_changed_rule,image)

$(obj)/zImage: $(obj)/image
	$(call if_changed,strip)
	@echo '  kernel: $@ is ready'

$(obj)/btfix.S: $(obj)/btfixupprep vmlinux FORCE
	$(call if_changed,btfix)

endif

ifeq ($(CONFIG_SPARC64),y)

# Actual linking
$(obj)/image: vmlinux FORCE
	$(call if_changed,strip)
	@echo '  kernel: $@ is ready'

$(obj)/zImage: $(obj)/image
	$(call if_changed,gzip)
	@echo '  kernel: $@ is ready'

$(obj)/vmlinux.aout: vmlinux FORCE
	$(call if_changed,elftoaout)
	@echo '  kernel: $@ is ready'
else

# The following lines make a readable image for U-Boot.
#  uImage   - Binary file read by U-boot
#  uImage.o - object file of uImage for loading with a
#             flash programmer understanding ELF.

OBJCOPYFLAGS_image.bin := -S -O binary -R .note -R .comment
$(obj)/image.bin: $(obj)/image FORCE
	$(call if_changed,objcopy)

$(obj)/image.gz: $(obj)/image.bin
	$(call if_changed,gzip)

UIMAGE_LOADADDR = $(CONFIG_UBOOT_LOAD_ADDR)
UIMAGE_ENTRYADDR = $(CONFIG_UBOOT_ENTRY_ADDR)
UIMAGE_COMPRESSION = gzip

quiet_cmd_uimage.o = UIMAGE.O $@
      cmd_uimage.o = $(LD) -Tdata $(CONFIG_UBOOT_FLASH_ADDR) \
                     -r -b binary $@ -o $@.o

targets += uImage
$(obj)/uImage: $(obj)/image.gz
	$(call if_changed,uimage)
	$(call if_changed,uimage.o)
	@echo '  Image $@ is ready'

endif

$(obj)/tftpboot.img: $(obj)/image $(obj)/piggyback System.map $(ROOT_IMG) FORCE
	$(call if_changed,elftoaout)
	$(call if_changed,piggy)
