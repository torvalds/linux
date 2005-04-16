# Makefile for making ELF bootable images for booting on CHRP
# using Open Firmware.
#
# Geert Uytterhoeven	September 1997
#
# Based on coffboot by Paul Mackerras
# Simplified for ppc64 by Todd Inglett
#
# NOTE:	this code is built for 32 bit in ELF32 format even though
#	it packages a 64 bit kernel.  We do this to simplify the
#	bootloader and increase compatibility with OpenFirmware.
#
#	To this end we need to define BOOTCC, etc, as the tools
#	needed to build the 32 bit image.  These are normally HOSTCC,
#	but may be a third compiler if, for example, you are cross
#	compiling from an intel box.  Once the 64bit ppc gcc is
#	stable it will probably simply be a compiler switch to
#	compile for 32bit mode.
#	To make it easier to setup a cross compiler,
#	CROSS32_COMPILE is setup as a prefix just like CROSS_COMPILE
#	in the toplevel makefile.


HOSTCC		:= gcc
BOOTCFLAGS	:= $(HOSTCFLAGS) $(LINUXINCLUDE) -fno-builtin 
BOOTAFLAGS	:= -D__ASSEMBLY__ $(BOOTCFLAGS) -traditional
BOOTLFLAGS	:= -Ttext 0x00400000 -e _start -T $(srctree)/$(src)/zImage.lds
OBJCOPYFLAGS    := contents,alloc,load,readonly,data

src-boot := crt0.S string.S prom.c main.c zlib.c imagesize.c div64.S
src-boot := $(addprefix $(obj)/, $(src-boot))
obj-boot := $(addsuffix .o, $(basename $(src-boot)))

quiet_cmd_bootcc = BOOTCC  $@
      cmd_bootcc = $(CROSS32CC) -Wp,-MD,$(depfile) $(BOOTCFLAGS) -c -o $@ $<

quiet_cmd_bootas = BOOTAS  $@
      cmd_bootas = $(CROSS32CC) -Wp,-MD,$(depfile) $(BOOTAFLAGS) -c -o $@ $<

$(patsubst %.c,%.o, $(filter %.c, $(src-boot))): %.o: %.c
	$(call if_changed_dep,bootcc)
$(patsubst %.S,%.o, $(filter %.S, $(src-boot))): %.o: %.S
	$(call if_changed_dep,bootas)

#-----------------------------------------------------------
# ELF sections within the zImage bootloader/wrapper
#-----------------------------------------------------------
required := vmlinux.strip
initrd   := initrd

obj-sec = $(foreach section, $(1), $(patsubst %,$(obj)/kernel-%.o, $(section)))
src-sec = $(foreach section, $(1), $(patsubst %,$(obj)/kernel-%.c, $(section)))
gz-sec  = $(foreach section, $(1), $(patsubst %,$(obj)/kernel-%.gz, $(section)))

hostprogs-y		:= piggy addnote addRamDisk
targets 		+= zImage zImage.initrd imagesize.c \
			   $(patsubst $(obj)/%,%, $(call obj-sec, $(required) $(initrd))) \
			   $(patsubst $(obj)/%,%, $(call src-sec, $(required) $(initrd))) \
			   $(patsubst $(obj)/%,%, $(call gz-sec, $(required) $(initrd))) \
			   vmlinux.initrd
extra-y			:= initrd.o

quiet_cmd_ramdisk = RAMDISK $@
      cmd_ramdisk = $(obj)/addRamDisk $(obj)/ramdisk.image.gz $< $@

quiet_cmd_stripvm = STRIP $@
      cmd_stripvm = $(STRIP) -s $< -o $@

vmlinux.strip: vmlinux FORCE
	$(call if_changed,stripvm)
$(obj)/vmlinux.initrd: vmlinux.strip $(obj)/addRamDisk $(obj)/ramdisk.image.gz FORCE
	$(call if_changed,ramdisk)

addsection = $(CROSS32OBJCOPY) $(1) \
		--add-section=.kernel:$(strip $(patsubst $(obj)/kernel-%.o,%, $(1)))=$(patsubst %.o,%.gz, $(1)) \
		--set-section-flags=.kernel:$(strip $(patsubst $(obj)/kernel-%.o,%, $(1)))=$(OBJCOPYFLAGS)

quiet_cmd_addnote = ADDNOTE $@ 
      cmd_addnote = $(CROSS32LD) $(BOOTLFLAGS) -o $@ $(obj-boot) && $(obj)/addnote $@

quiet_cmd_piggy = PIGGY   $@
      cmd_piggy = $(obj)/piggyback $(@:.o=) < $< | $(CROSS32AS) -o $@

$(call gz-sec, $(required)): $(obj)/kernel-%.gz: % FORCE
	$(call if_changed,gzip)

$(obj)/kernel-initrd.gz: $(obj)/ramdisk.image.gz
	cp -f $(obj)/ramdisk.image.gz $@

$(call src-sec, $(required) $(initrd)): $(obj)/kernel-%.c: $(obj)/kernel-%.gz FORCE
	touch $@

$(call obj-sec, $(required) $(initrd)): $(obj)/kernel-%.o: $(obj)/kernel-%.c FORCE
	$(call if_changed_dep,bootcc)
	$(call addsection, $@)

$(obj)/zImage: obj-boot += $(call obj-sec, $(required))
$(obj)/zImage: $(call obj-sec, $(required)) $(obj-boot) $(obj)/addnote FORCE
	$(call if_changed,addnote)

$(obj)/zImage.initrd: obj-boot += $(call obj-sec, $(required) $(initrd))
$(obj)/zImage.initrd: $(call obj-sec, $(required) $(initrd)) $(obj-boot) $(obj)/addnote FORCE
	$(call if_changed,addnote)

$(obj)/imagesize.c: vmlinux.strip
	@echo Generating $@
	ls -l vmlinux.strip | \
	awk '{printf "/* generated -- do not edit! */\n" \
		"unsigned long vmlinux_filesize = %d;\n", $$5}' > $(obj)/imagesize.c
	$(CROSS_COMPILE)nm -n vmlinux | tail -n 1 | \
	awk '{printf "unsigned long vmlinux_memsize = 0x%s;\n", substr($$1,8)}' \
		>> $(obj)/imagesize.c

install: $(CONFIGURE) $(BOOTIMAGE)
	sh -x $(srctree)/$(src)/install.sh "$(KERNELRELEASE)" vmlinux System.map "$(INSTALL_PATH)" "$(BOOTIMAGE)"

clean-files := $(addprefix $(objtree)/, $(obj-boot) vmlinux.strip)
