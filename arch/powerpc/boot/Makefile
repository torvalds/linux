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

$(addprefix $(obj)/,$(zlib) gunzip_util.o main.o): \
	$(addprefix $(obj)/,$(zliblinuxheader)) $(addprefix $(obj)/,$(zlibheader))

src-wlib := string.S crt0.S stdio.c main.c flatdevtree.c flatdevtree_misc.c \
		ns16550.c serial.c simple_alloc.c div64.S util.S \
		gunzip_util.c elf_util.c $(zlib) devtree.c \
		44x.c ebony.c mv64x60.c mpsc.c mv64x60_i2c.c
src-plat := of.c cuboot-83xx.c cuboot-85xx.c holly.c \
		cuboot-ebony.c treeboot-ebony.c prpmc2800.c
src-boot := $(src-wlib) $(src-plat) empty.c

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
		empty.c zImage.coff.lds zImage.lds

quiet_cmd_bootcc = BOOTCC  $@
      cmd_bootcc = $(CROSS32CC) -Wp,-MD,$(depfile) $(BOOTCFLAGS) -c -o $@ $<

quiet_cmd_bootas = BOOTAS  $@
      cmd_bootas = $(CROSS32CC) -Wp,-MD,$(depfile) $(BOOTAFLAGS) -c -o $@ $<

quiet_cmd_bootar = BOOTAR  $@
      cmd_bootar = $(CROSS32AR) -cr $@.$$$$ $(filter-out FORCE,$^); mv $@.$$$$ $@

$(patsubst %.c,%.o, $(filter %.c, $(src-boot))): %.o: %.c FORCE
	$(call if_changed_dep,bootcc)
$(patsubst %.S,%.o, $(filter %.S, $(src-boot))): %.o: %.S FORCE
	$(call if_changed_dep,bootas)

$(obj)/wrapper.a: $(obj-wlib) FORCE
	$(call if_changed,bootar)

hostprogs-y	:= addnote addRamDisk hack-coff mktree

targets		+= $(patsubst $(obj)/%,%,$(obj-boot) wrapper.a)
extra-y		:= $(obj)/wrapper.a $(obj-plat) $(obj)/empty.o \
		   $(obj)/zImage.lds $(obj)/zImage.coff.lds

wrapper		:=$(srctree)/$(src)/wrapper
wrapperbits	:= $(extra-y) $(addprefix $(obj)/,addnote hack-coff mktree) \
			$(wrapper) FORCE

#############
# Bits for building various flavours of zImage

ifneq ($(CROSS32_COMPILE),)
CROSSWRAP := -C "$(CROSS32_COMPILE)"
else
ifneq ($(CROSS_COMPILE),)
CROSSWRAP := -C "$(CROSS_COMPILE)"
endif
endif

# args (to if_changed): 1 = (this rule), 2 = platform, 3 = dts 4=dtb 5=initrd
quiet_cmd_wrap	= WRAP    $@
      cmd_wrap	=$(CONFIG_SHELL) $(wrapper) -c -o $@ -p $2 $(CROSSWRAP) \
		$(if $3, -s $3)$(if $4, -d $4)$(if $5, -i $5) vmlinux

image-$(CONFIG_PPC_PSERIES)		+= zImage.pseries
image-$(CONFIG_PPC_MAPLE)		+= zImage.pseries
image-$(CONFIG_PPC_IBM_CELL_BLADE)	+= zImage.pseries
image-$(CONFIG_PPC_PS3)			+= zImage.ps3
image-$(CONFIG_PPC_CELLEB)		+= zImage.pseries
image-$(CONFIG_PPC_CHRP)		+= zImage.chrp
image-$(CONFIG_PPC_EFIKA)		+= zImage.chrp
image-$(CONFIG_PPC_PMAC)		+= zImage.pmac
image-$(CONFIG_PPC_HOLLY)		+= zImage.holly-elf
image-$(CONFIG_PPC_PRPMC2800)		+= zImage.prpmc2800
image-$(CONFIG_DEFAULT_UIMAGE)		+= uImage

ifneq ($(CONFIG_DEVICE_TREE),"")
image-$(CONFIG_PPC_83xx)		+= cuImage.83xx
image-$(CONFIG_PPC_85xx)		+= cuImage.85xx
image-$(CONFIG_EBONY)			+= treeImage.ebony cuImage.ebony
endif

# For 32-bit powermacs, build the COFF and miboot images
# as well as the ELF images.
ifeq ($(CONFIG_PPC32),y)
image-$(CONFIG_PPC_PMAC)	+= zImage.coff zImage.miboot
endif

initrd-  := $(patsubst zImage%, zImage.initrd%, $(image-n) $(image-))
initrd-y := $(patsubst zImage%, zImage.initrd%, \
		$(patsubst treeImage%, treeImage.initrd%, $(image-y)))
initrd-y := $(filter-out $(image-y), $(initrd-y))
targets	+= $(image-y) $(initrd-y)

$(addprefix $(obj)/, $(initrd-y)): $(obj)/ramdisk.image.gz

dts-  := $(patsubst zImage%, zImage.dts%, $(image-n) $(image-))
dts-y := $(patsubst zImage%, zImage.dts%, $(image-y))
dts-y := $(filter-out $(image-y), $(dts-y))
targets	+= $(image-y) $(dts-y)

dts_initrd-  := $(patsubst zImage%, zImage.dts_initrd%, $(image-n) $(image-))
dts_initrd-y := $(patsubst zImage%, zImage.dts_initrd%, $(image-y))
dts_initrd-y := $(filter-out $(image-y), $(dts_initrd-y))
targets	+= $(image-y) $(dts_initrd-y)

$(addprefix $(obj)/, $(dts_initrd-y)): $(obj)/ramdisk.image.gz

# Don't put the ramdisk on the pattern rule; when its missing make will try
# the pattern rule with less dependencies that also matches (even with the
# hard dependency listed).
$(obj)/zImage.dts_initrd.%: vmlinux $(wrapperbits) $(dts) $(obj)/ramdisk.image.gz
	$(call if_changed,wrap,$*,$(dts),,$(obj)/ramdisk.image.gz)

$(obj)/zImage.dts.%: vmlinux $(wrapperbits) $(dts)
	$(call if_changed,wrap,$*,$(dts))

$(obj)/zImage.initrd.%: vmlinux $(wrapperbits)
	$(call if_changed,wrap,$*,,,$(obj)/ramdisk.image.gz)

$(obj)/zImage.%: vmlinux $(wrapperbits)
	$(call if_changed,wrap,$*)

$(obj)/zImage.ps3: vmlinux
	$(STRIP) -s -R .comment $< -o $@

$(obj)/zImage.initrd.ps3: vmlinux
	@echo "  WARNING zImage.initrd.ps3 not supported (yet)"

$(obj)/zImage.holly-elf: vmlinux $(wrapperbits)
	$(call if_changed,wrap,holly,$(obj)/dts/holly.dts,,)

$(obj)/zImage.initrd.holly-elf: vmlinux $(wrapperbits) $(obj)/ramdisk.image.gz
	$(call if_changed,wrap,holly,$(obj)/dts/holly.dts,,$(obj)/ramdisk.image.gz)

$(obj)/uImage: vmlinux $(wrapperbits)
	$(call if_changed,wrap,uboot)

# CONFIG_DEVICE_TREE will have "" around it, make sure to strip them
dts = $(if $(shell echo $(CONFIG_DEVICE_TREE) | grep '^/'),\
	,$(srctree)/$(src)/dts/)$(CONFIG_DEVICE_TREE:"%"=%)

$(obj)/cuImage.%: vmlinux $(dts) $(wrapperbits)
	$(call if_changed,wrap,cuboot-$*,$(dts))

$(obj)/treeImage.%: vmlinux $(dts) $(wrapperbits)
	$(call if_changed,wrap,treeboot-$*,$(dts))

$(obj)/treeImage.initrd.%: vmlinux $(dts) $(wrapperbits)
	$(call if_changed,wrap,treeboot-$*,$(dts),,$(obj)/ramdisk.image.gz)

$(obj)/zImage:		$(addprefix $(obj)/, $(image-y))
	@rm -f $@; ln $< $@
$(obj)/zImage.initrd:	$(addprefix $(obj)/, $(initrd-y))
	@rm -f $@; ln $< $@
$(obj)/zImage.dts:	$(addprefix $(obj)/, $(dts-y))
	@rm -f $@; ln $< $@
$(obj)/zImage.dts_initrd:	$(addprefix $(obj)/, $(dts_initrd-y))
	@rm -f $@; ln $< $@


install: $(CONFIGURE) $(addprefix $(obj)/, $(image-y))
	sh -x $(srctree)/$(src)/install.sh "$(KERNELRELEASE)" vmlinux System.map "$(INSTALL_PATH)" $<

# anything not in $(targets)
clean-files += $(image-) $(initrd-) zImage zImage.initrd cuImage.* \
	treeImage.* zImage.dts zImage.dts_initrd

# clean up files cached by wrapper
clean-kernel := vmlinux.strip vmlinux.bin
clean-kernel += $(addsuffix .gz,$(clean-kernel))
# If not absolute clean-files are relative to $(obj).
clean-files += $(addprefix $(objtree)/, $(clean-kernel))
