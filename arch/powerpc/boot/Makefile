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

all: $(obj)/zImage

HOSTCC		:= gcc
BOOTCFLAGS	:= $(HOSTCFLAGS) -fno-builtin -nostdinc -isystem \
		   $(shell $(CROSS32CC) -print-file-name=include) -fPIC
BOOTAFLAGS	:= -D__ASSEMBLY__ $(BOOTCFLAGS) -traditional -nostdinc

ifeq ($(call cc-option-yn, -fstack-protector),y)
BOOTCFLAGS	+= -fno-stack-protector
endif

BOOTCFLAGS	+= -I$(obj) -I$(srctree)/$(obj)

zlib       := inffast.c inflate.c inftrees.c
zlibheader := inffast.h inffixed.h inflate.h inftrees.h infutil.h
zliblinuxheader := zlib.h zconf.h zutil.h

$(addprefix $(obj)/,$(zlib) main.o): $(addprefix $(obj)/,$(zliblinuxheader)) \
		$(addprefix $(obj)/,$(zlibheader))

src-wlib := string.S stdio.c main.c div64.S $(zlib)
src-plat := of.c
src-boot := crt0.S $(src-wlib) $(src-plat) empty.c

src-boot := $(addprefix $(obj)/, $(src-boot))
obj-boot := $(addsuffix .o, $(basename $(src-boot)))
obj-wlib := $(addsuffix .o, $(basename $(addprefix $(obj)/, $(src-wlib))))
obj-plat := $(addsuffix .o, $(basename $(addprefix $(obj)/, $(src-plat))))

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

$(obj)/empty.c:
	@touch $@

$(obj)/zImage.lds $(obj)/zImage.coff.lds: $(obj)/%: $(srctree)/$(src)/%.S
	@cp $< $@

clean-files := $(zlib) $(zlibheader) $(zliblinuxheader) \
		$(obj)/empty.c

quiet_cmd_bootcc = BOOTCC  $@
      cmd_bootcc = $(CROSS32CC) -Wp,-MD,$(depfile) $(BOOTCFLAGS) -c -o $@ $<

quiet_cmd_bootas = BOOTAS  $@
      cmd_bootas = $(CROSS32CC) -Wp,-MD,$(depfile) $(BOOTAFLAGS) -c -o $@ $<

quiet_cmd_bootar = BOOTAR  $@
      cmd_bootar = $(CROSS32AR) -cr $@.$$$$ $^; mv $@.$$$$ $@

$(patsubst %.c,%.o, $(filter %.c, $(src-boot))): %.o: %.c
	$(call if_changed_dep,bootcc)
$(patsubst %.S,%.o, $(filter %.S, $(src-boot))): %.o: %.S
	$(call if_changed_dep,bootas)

$(obj)/wrapper.a: $(obj-wlib)
	$(call cmd,bootar)

hostprogs-y	:= addnote addRamDisk hack-coff

extra-y		:= $(obj)/crt0.o $(obj)/wrapper.a $(obj-plat) $(obj)/empty.o \
		   $(obj)/zImage.lds $(obj)/zImage.coff.lds

wrapper		:=$(srctree)/$(src)/wrapper
wrapperbits	:= $(extra-y) $(addprefix $(obj)/,addnote hack-coff)

#############
# Bits for building various flavours of zImage

ifneq ($(CROSS32_COMPILE),)
CROSSWRAP := -C "$(CROSS32_COMPILE)"
else
ifneq ($(CROSS_COMPILE),)
CROSSWRAP := -C "$(CROSS_COMPILE)"
endif
endif

quiet_cmd_wrap	= WRAP    $@
      cmd_wrap	=$(CONFIG_SHELL) $(wrapper) -c -o $@ -p $2 $(CROSSWRAP) vmlinux
quiet_cmd_wrap_initrd = WRAP    $@
      cmd_wrap_initrd =$(CONFIG_SHELL) $(wrapper) -c -o $@ -p $2 $(CROSSWRAP) \
				-i $(obj)/ramdisk.image.gz vmlinux

$(obj)/zImage.chrp: vmlinux $(wrapperbits)
	$(call cmd,wrap,chrp)

$(obj)/zImage.initrd.chrp: vmlinux $(wrapperbits)
	$(call cmd,wrap_initrd,chrp)

$(obj)/zImage.pseries:	vmlinux $(wrapperbits)
	$(call cmd,wrap,pseries)

$(obj)/zImage.initrd.pseries: vmlinux $(wrapperbits)
	$(call cmd,wrap_initrd,pseries)

$(obj)/zImage.pmac: vmlinux $(wrapperbits)
	$(call cmd,wrap,pmac)

$(obj)/zImage.initrd.pmac: vmlinux $(wrapperbits)
	$(call cmd,wrap_initrd,pmac)

$(obj)/zImage.coff: vmlinux $(wrapperbits)
	$(call cmd,wrap,pmaccoff)

$(obj)/zImage.initrd.coff: vmlinux $(wrapperbits)
	$(call cmd,wrap_initrd,pmaccoff)

$(obj)/zImage.miboot: vmlinux $(wrapperbits)
	$(call cmd,wrap,miboot)

$(obj)/zImage.initrd.miboot: vmlinux $(wrapperbits)
	$(call cmd,wrap_initrd,miboot)

$(obj)/uImage: vmlinux $(wrapperbits)
	$(call cmd,wrap,uboot)

image-$(CONFIG_PPC_PSERIES)		+= zImage.pseries
image-$(CONFIG_PPC_MAPLE)		+= zImage.pseries
image-$(CONFIG_PPC_IBM_CELL_BLADE)	+= zImage.pseries
image-$(CONFIG_PPC_CHRP)		+= zImage.chrp
image-$(CONFIG_PPC_PMAC)		+= zImage.pmac
image-$(CONFIG_DEFAULT_UIMAGE)		+= uImage

# For 32-bit powermacs, build the COFF and miboot images
# as well as the ELF images.
ifeq ($(CONFIG_PPC32),y)
image-$(CONFIG_PPC_PMAC)	+= zImage.coff zImage.miboot
endif

initrd-y := $(patsubst zImage%, zImage.initrd%, $(image-y))

$(obj)/zImage:		$(addprefix $(obj)/, $(image-y))
	@rm -f $@; ln $< $@
$(obj)/zImage.initrd:	$(addprefix $(obj)/, $(initrd-y))
	@rm -f $@; ln $< $@

install: $(CONFIGURE) $(image-y)
	sh -x $(srctree)/$(src)/install.sh "$(KERNELRELEASE)" vmlinux System.map "$(INSTALL_PATH)" $<

clean-files += $(addprefix $(objtree)/, $(obj-boot) vmlinux.strip.gz)
clean-files += $(addprefix $(objtree)/, $(obj-boot) vmlinux.bin.gz)
