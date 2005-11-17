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
BOOTCFLAGS	:= $(HOSTCFLAGS) -fno-builtin -nostdinc -isystem \
		   $(shell $(CROSS32CC) -print-file-name=include) -fPIC
BOOTAFLAGS	:= -D__ASSEMBLY__ $(BOOTCFLAGS) -traditional -nostdinc
BOOTLFLAGS	:= -T $(srctree)/$(src)/zImage.lds
OBJCOPYFLAGS    := contents,alloc,load,readonly,data

zlib       := infblock.c infcodes.c inffast.c inflate.c inftrees.c infutil.c
zlibheader := infblock.h infcodes.h inffast.h inftrees.h infutil.h
zliblinuxheader := zlib.h zconf.h zutil.h

$(addprefix $(obj)/,$(zlib) main.o): $(addprefix $(obj)/,$(zliblinuxheader)) $(addprefix $(obj)/,$(zlibheader))
#$(addprefix $(obj)/,main.o): $(addprefix $(obj)/,zlib.h)

src-boot := string.S prom.c main.c div64.S crt0.S
src-boot += $(zlib)
src-boot := $(addprefix $(obj)/, $(src-boot))
obj-boot := $(addsuffix .o, $(basename $(src-boot)))

BOOTCFLAGS	+= -I$(obj) -I$(srctree)/$(obj)

quiet_cmd_copy_zlib = COPY    $@
      cmd_copy_zlib = sed "s@__attribute_used__@@;s@<linux/\([^>]\+\).*@\"\1\"@" $< > $@

quiet_cmd_copy_zlibheader = COPY    $@
      cmd_copy_zlibheader = sed "s@<linux/\([^>]\+\).*@\"\1\"@" $< > $@
# stddef.h for NULL
quiet_cmd_copy_zliblinuxheader = COPY    $@
      cmd_copy_zliblinuxheader = sed "s@<linux/string.h>@\"string.h\"@;s@<linux/kernel.h>@<stddef.h>@;s@<linux/\([^>]\+\).*@\"\1\"@" $< > $@

$(addprefix $(obj)/,$(zlib)): $(obj)/%: $(srctree)/lib/zlib_inflate/%
	$(call cmd,copy_zlib)

$(addprefix $(obj)/,$(zlibheader)): $(obj)/%: $(srctree)/lib/zlib_inflate/%
	$(call cmd,copy_zlibheader)

$(addprefix $(obj)/,$(zliblinuxheader)): $(obj)/%: $(srctree)/include/linux/%
	$(call cmd,copy_zliblinuxheader)

clean-files := $(zlib) $(zlibheader) $(zliblinuxheader)


quiet_cmd_bootcc = BOOTCC  $@
      cmd_bootcc = $(CROSS32CC) -Wp,-MD,$(depfile) $(BOOTCFLAGS) -c -o $@ $<

quiet_cmd_bootas = BOOTAS  $@
      cmd_bootas = $(CROSS32CC) -Wp,-MD,$(depfile) $(BOOTAFLAGS) -c -o $@ $<

quiet_cmd_bootld = BOOTLD  $@
      cmd_bootld = $(CROSS32LD) $(BOOTLFLAGS) -o $@ $(2)

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

hostprogs-y		:= addnote addRamDisk
targets 		+= zImage.vmode zImage.initrd.vmode zImage zImage.initrd \
			   $(patsubst $(obj)/%,%, $(call obj-sec, $(required) $(initrd))) \
			   $(patsubst $(obj)/%,%, $(call src-sec, $(required) $(initrd))) \
			   $(patsubst $(obj)/%,%, $(call gz-sec, $(required) $(initrd))) \
			   vmlinux.initrd
extra-y			:= initrd.o

quiet_cmd_ramdisk = RAMDISK $@
      cmd_ramdisk = $(obj)/addRamDisk $(obj)/ramdisk.image.gz $< $@

quiet_cmd_stripvm = STRIP   $@
      cmd_stripvm = $(STRIP) -s -R .comment $< -o $@

vmlinux.strip: vmlinux
	$(call if_changed,stripvm)
$(obj)/vmlinux.initrd: vmlinux.strip $(obj)/addRamDisk $(obj)/ramdisk.image.gz
	$(call if_changed,ramdisk)

quiet_cmd_addsection = ADDSEC  $@
      cmd_addsection = $(CROSS32OBJCOPY) $@ \
		--add-section=.kernel:$(strip $(patsubst $(obj)/kernel-%.o,%, $@))=$(patsubst %.o,%.gz, $@) \
		--set-section-flags=.kernel:$(strip $(patsubst $(obj)/kernel-%.o,%, $@))=$(OBJCOPYFLAGS)

quiet_cmd_addnote = ADDNOTE $@
      cmd_addnote = $(obj)/addnote $@

$(call gz-sec, $(required)): $(obj)/kernel-%.gz: %
	$(call if_changed,gzip)

$(obj)/kernel-initrd.gz: $(obj)/ramdisk.image.gz
	cp -f $(obj)/ramdisk.image.gz $@

$(call src-sec, $(required) $(initrd)): $(obj)/kernel-%.c: $(obj)/kernel-%.gz
	@touch $@

$(call obj-sec, $(required) $(initrd)): $(obj)/kernel-%.o: $(obj)/kernel-%.c
	$(call if_changed_dep,bootcc)
	$(call cmd,addsection)

$(obj)/zImage.vmode: obj-boot += $(call obj-sec, $(required))
$(obj)/zImage.vmode: $(call obj-sec, $(required)) $(obj-boot) $(srctree)/$(src)/zImage.lds
	$(call cmd,bootld,$(obj-boot))

$(obj)/zImage.initrd.vmode: obj-boot += $(call obj-sec, $(required) $(initrd))
$(obj)/zImage.initrd.vmode: $(call obj-sec, $(required) $(initrd)) $(obj-boot) $(srctree)/$(src)/zImage.lds
	$(call cmd,bootld,$(obj-boot))

$(obj)/zImage: $(obj)/zImage.vmode $(obj)/addnote
	@cp -f $< $@
	$(call if_changed,addnote)

$(obj)/zImage.initrd: $(obj)/zImage.initrd.vmode $(obj)/addnote
	@cp -f $< $@
	$(call if_changed,addnote)

install: $(CONFIGURE) $(BOOTIMAGE)
	sh -x $(srctree)/$(src)/install.sh "$(KERNELRELEASE)" vmlinux System.map "$(INSTALL_PATH)" "$(BOOTIMAGE)"

clean-files := $(addprefix $(objtree)/, $(obj-boot) vmlinux.strip)
