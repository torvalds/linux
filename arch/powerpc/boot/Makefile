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
#	needed to build the 32 bit image.  That's normally the same
#	compiler for the rest of the kernel, with the -m32 flag added.
#	To make it easier to setup a cross compiler,
#	CROSS32_COMPILE is setup as a prefix just like CROSS_COMPILE
#	in the toplevel makefile.

all: $(obj)/zImage

BOOTCFLAGS    := -Wall -Wundef -Wstrict-prototypes -Wno-trigraphs \
		 -fno-strict-aliasing -Os -msoft-float -pipe \
		 -fomit-frame-pointer -fno-builtin -fPIC -nostdinc \
		 -isystem $(shell $(CROSS32CC) -print-file-name=include)
BOOTAFLAGS	:= -D__ASSEMBLY__ $(BOOTCFLAGS) -traditional -nostdinc

ifdef CONFIG_DEBUG_INFO
BOOTCFLAGS	+= -g
endif

ifeq ($(call cc-option-yn, -fstack-protector),y)
BOOTCFLAGS	+= -fno-stack-protector
endif

BOOTCFLAGS	+= -I$(obj) -I$(srctree)/$(obj) -I$(srctree)/$(src)/libfdt

$(obj)/4xx.o: BOOTCFLAGS += -mcpu=440
$(obj)/ebony.o: BOOTCFLAGS += -mcpu=440
$(obj)/cuboot-taishan.o: BOOTCFLAGS += -mcpu=440
$(obj)/cuboot-katmai.o: BOOTCFLAGS += -mcpu=440
$(obj)/treeboot-walnut.o: BOOTCFLAGS += -mcpu=405


zlib       := inffast.c inflate.c inftrees.c
zlibheader := inffast.h inffixed.h inflate.h inftrees.h infutil.h
zliblinuxheader := zlib.h zconf.h zutil.h

$(addprefix $(obj)/,$(zlib) gunzip_util.o main.o): \
	$(addprefix $(obj)/,$(zliblinuxheader)) $(addprefix $(obj)/,$(zlibheader))

src-libfdt := fdt.c fdt_ro.c fdt_wip.c fdt_sw.c fdt_rw.c fdt_strerror.c
src-wlib := string.S crt0.S stdio.c main.c \
		$(addprefix libfdt/,$(src-libfdt)) libfdt-wrapper.c \
		ns16550.c serial.c simple_alloc.c div64.S util.S \
		gunzip_util.c elf_util.c $(zlib) devtree.c oflib.c ofconsole.c \
		4xx.c ebony.c mv64x60.c mpsc.c mv64x60_i2c.c cuboot.c bamboo.c \
		cpm-serial.c stdlib.c mpc52xx-psc.c planetcore.c uartlite.c \
		fsl-soc.c mpc8xx.c pq2.c
src-plat := of.c cuboot-52xx.c cuboot-824x.c cuboot-83xx.c cuboot-85xx.c holly.c \
		cuboot-ebony.c treeboot-ebony.c prpmc2800.c \
		ps3-head.S ps3-hvcall.S ps3.c treeboot-bamboo.c cuboot-8xx.c \
		cuboot-pq2.c cuboot-sequoia.c treeboot-walnut.c \
		cuboot-bamboo.c cuboot-mpc7448hpc2.c cuboot-taishan.c \
		fixed-head.S ep88xc.c ep405.c \
		cuboot-katmai.c cuboot-rainier.c redboot-8xx.c ep8248e.c \
		cuboot-warp.c cuboot-85xx-cpm2.c
src-boot := $(src-wlib) $(src-plat) empty.c

src-boot := $(addprefix $(obj)/, $(src-boot))
obj-boot := $(addsuffix .o, $(basename $(src-boot)))
obj-wlib := $(addsuffix .o, $(basename $(addprefix $(obj)/, $(src-wlib))))
obj-plat := $(addsuffix .o, $(basename $(addprefix $(obj)/, $(src-plat))))

quiet_cmd_copy_zlib = COPY    $@
      cmd_copy_zlib = sed "s@__used@@;s@<linux/\([^>]*\).*@\"\1\"@" $< > $@

quiet_cmd_copy_zlibheader = COPY    $@
      cmd_copy_zlibheader = sed "s@<linux/\([^>]*\).*@\"\1\"@" $< > $@
# stddef.h for NULL
quiet_cmd_copy_zliblinuxheader = COPY    $@
      cmd_copy_zliblinuxheader = sed "s@<linux/string.h>@\"string.h\"@;s@<linux/kernel.h>@<stddef.h>@;s@<linux/\([^>]*\).*@\"\1\"@" $< > $@

$(addprefix $(obj)/,$(zlib)): $(obj)/%: $(srctree)/lib/zlib_inflate/%
	$(call cmd,copy_zlib)

$(addprefix $(obj)/,$(zlibheader)): $(obj)/%: $(srctree)/lib/zlib_inflate/%
	$(call cmd,copy_zlibheader)

$(addprefix $(obj)/,$(zliblinuxheader)): $(obj)/%: $(srctree)/include/linux/%
	$(call cmd,copy_zliblinuxheader)

$(obj)/empty.c:
	@touch $@

$(obj)/zImage.lds $(obj)/zImage.coff.lds $(obj)/zImage.ps3.lds: $(obj)/%: $(srctree)/$(src)/%.S
	@cp $< $@

clean-files := $(zlib) $(zlibheader) $(zliblinuxheader) \
		empty.c zImage zImage.coff.lds zImage.ps3.lds zImage.lds

quiet_cmd_bootcc = BOOTCC  $@
      cmd_bootcc = $(CROSS32CC) -Wp,-MD,$(depfile) $(BOOTCFLAGS) -c -o $@ $<

quiet_cmd_bootas = BOOTAS  $@
      cmd_bootas = $(CROSS32CC) -Wp,-MD,$(depfile) $(BOOTAFLAGS) -c -o $@ $<

quiet_cmd_bootar = BOOTAR  $@
      cmd_bootar = $(CROSS32AR) -cr $@.$$$$ $(filter-out FORCE,$^); mv $@.$$$$ $@

$(patsubst %.c,%.o, $(filter %.c, $(src-boot))): %.o: %.c FORCE
	$(Q)mkdir -p $(dir $@)
	$(call if_changed_dep,bootcc)
$(patsubst %.S,%.o, $(filter %.S, $(src-boot))): %.o: %.S FORCE
	$(Q)mkdir -p $(dir $@)
	$(call if_changed_dep,bootas)

$(obj)/wrapper.a: $(obj-wlib) FORCE
	$(call if_changed,bootar)

hostprogs-y	:= addnote addRamDisk hack-coff mktree dtc

targets		+= $(patsubst $(obj)/%,%,$(obj-boot) wrapper.a)
extra-y		:= $(obj)/wrapper.a $(obj-plat) $(obj)/empty.o \
		   $(obj)/zImage.lds $(obj)/zImage.coff.lds $(obj)/zImage.ps3.lds

dtstree		:= $(srctree)/$(src)/dts

wrapper		:=$(srctree)/$(src)/wrapper
wrapperbits	:= $(extra-y) $(addprefix $(obj)/,addnote hack-coff mktree dtc) \
			$(wrapper) FORCE

#############
# Bits for building dtc
# DTC_GENPARSER      := 1    # Uncomment to rebuild flex/bison output

dtc-objs := dtc.o flattree.o fstree.o data.o livetree.o treesource.o srcpos.o checks.o
dtc-objs += dtc-lexer.lex.o dtc-parser.tab.o
dtc-objs := $(addprefix dtc-src/, $(dtc-objs))

# prerequisites on generated files needs to be explicit
$(obj)/dtc-src/dtc-parser.tab.o: $(obj)/dtc-src/dtc-parser.tab.c $(obj)/dtc-src/dtc-parser.tab.h
$(obj)/dtc-src/dtc-lexer.lex.o:  $(obj)/dtc-src/dtc-lexer.lex.c $(obj)/dtc-src/dtc-parser.tab.h

HOSTCFLAGS += -I$(src)/dtc-src/ -I$(src)/libfdt/

targets += dtc-src/dtc-parser.tab.c
targets += dtc-src/dtc-lexer.lex.c

clean-files += dtc-src/dtc-parser.tab.h

ifdef DTC_GENPARSER
BISON = bison
FLEX = flex

quiet_cmd_bison = BISON   $@
      cmd_bison = $(BISON) -o$@ -d $<; cp $@ $@_shipped
quiet_cmd_flex = FLEX    $@
      cmd_flex = $(FLEX) -o$@ $<; cp $@ $@_shipped

$(obj)/dtc-src/dtc-parser.tab.c: $(src)/dtc-src/dtc-parser.y FORCE
     $(call if_changed,bison)

$(obj)/dtc-src/dtc-parser.tab.h: $(obj)/dtc-src/dtc-parser.tab.c

$(obj)/dtc-src/dtc-lexer.lex.c: $(src)/dtc-src/dtc-lexer.l FORCE
     $(call if_changed,flex)
endif

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
image-$(CONFIG_PPC_PS3)			+= zImage-dtb.ps3
image-$(CONFIG_PPC_CELLEB)		+= zImage.pseries
image-$(CONFIG_PPC_CHRP)		+= zImage.chrp
image-$(CONFIG_PPC_EFIKA)		+= zImage.chrp
image-$(CONFIG_PPC_PMAC)		+= zImage.pmac
image-$(CONFIG_PPC_HOLLY)		+= zImage.holly
image-$(CONFIG_PPC_PRPMC2800)		+= zImage.prpmc2800
image-$(CONFIG_PPC_ISERIES)		+= zImage.iseries
image-$(CONFIG_DEFAULT_UIMAGE)		+= uImage

#
# Targets which embed a device tree blob
#
# Theses are default targets to build images which embed device tree blobs.
# They are only required on boards which do not have FDT support in firmware.
# Boards with newish u-boot firmare can use the uImage target above
#

# Board ports in arch/powerpc/platform/40x/Kconfig
image-$(CONFIG_EP405)			+= zImage-dtb.ep405
image-$(CONFIG_WALNUT)			+= treeImage.walnut

# Board ports in arch/powerpc/platform/44x/Kconfig
image-$(CONFIG_EBONY)			+= treeImage.ebony cuImage.ebony
image-$(CONFIG_BAMBOO)			+= treeImage.bamboo cuImage.bamboo
image-$(CONFIG_SEQUOIA)			+= cuImage.sequoia
image-$(CONFIG_RAINIER)			+= cuImage.rainier
image-$(CONFIG_TAISHAN)			+= cuImage.taishan
image-$(CONFIG_KATMAI)			+= cuImage.katmai
image-$(CONFIG_WARP)			+= cuImage.warp

# Board ports in arch/powerpc/platform/8xx/Kconfig
image-$(CONFIG_PPC_MPC86XADS)		+= cuImage.mpc866ads
image-$(CONFIG_PPC_MPC885ADS)		+= cuImage.mpc885ads
image-$(CONFIG_PPC_EP88XC)		+= zImage-dtb.ep88xc
image-$(CONFIG_PPC_ADDER875)		+= cuImage.adder875-uboot \
					   zImage-dtb.adder875-redboot

# Board ports in arch/powerpc/platform/52xx/Kconfig
image-$(CONFIG_PPC_LITE5200)		+= cuImage.lite5200 cuImage.lite5200b

# Board ports in arch/powerpc/platform/82xx/Kconfig
image-$(CONFIG_MPC8272_ADS)		+= cuImage.mpc8272ads
image-$(CONFIG_PQ2FADS)			+= cuImage.pq2fads
image-$(CONFIG_EP8248E)			+= zImage-dtb.ep8248e

# Board ports in arch/powerpc/platform/83xx/Kconfig
image-$(CONFIG_MPC832x_MDS)		+= cuImage.mpc832x_mds
image-$(CONFIG_MPC832x_RDB)		+= cuImage.mpc832x_rdb
image-$(CONFIG_MPC834x_ITX)		+= cuImage.mpc8349emitx \
					   cuImage.mpc8349emitxgp
image-$(CONFIG_MPC834x_MDS)		+= cuImage.mpc834x_mds
image-$(CONFIG_MPC836x_MDS)		+= cuImage.mpc836x_mds

# Board ports in arch/powerpc/platform/85xx/Kconfig
image-$(CONFIG_MPC8540_ADS)		+= cuImage.mpc8540ads
image-$(CONFIG_MPC8560_ADS)		+= cuImage.mpc8560ads
image-$(CONFIG_MPC85xx_CDS)		+= cuImage.mpc8541cds \
					   cuImage.mpc8548cds \
					   cuImage.mpc8555cds
image-$(CONFIG_MPC85xx_MDS)		+= cuImage.mpc8568mds
image-$(CONFIG_MPC85xx_DS)		+= cuImage.mpc8544ds \
					   cuImage.mpc8572ds
image-$(CONFIG_TQM8540)			+= cuImage.tqm8540
image-$(CONFIG_TQM8541)			+= cuImage.tqm8541
image-$(CONFIG_TQM8555)			+= cuImage.tqm8555
image-$(CONFIG_TQM8560)			+= cuImage.tqm8560
image-$(CONFIG_SBC8548)			+= cuImage.tqm8548
image-$(CONFIG_SBC8560)			+= cuImage.tqm8560

# Board ports in arch/powerpc/platform/embedded6xx/Kconfig
image-$(CONFIG_STORCENTER)		+= cuImage.storcenter
image-$(CONFIG_MPC7448HPC2)		+= cuImage.mpc7448hpc2

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

# Don't put the ramdisk on the pattern rule; when its missing make will try
# the pattern rule with less dependencies that also matches (even with the
# hard dependency listed).
$(obj)/zImage.initrd.%: vmlinux $(wrapperbits)
	$(call if_changed,wrap,$*,,,$(obj)/ramdisk.image.gz)

$(obj)/zImage.%: vmlinux $(wrapperbits)
	$(call if_changed,wrap,$*)

$(obj)/zImage-dtb.initrd.%: vmlinux $(wrapperbits) $(dtstree)/%.dts
	$(call if_changed,wrap,$*,$(dtstree)/$*.dts,,$(obj)/ramdisk.image.gz)

$(obj)/zImage-dtb.%: vmlinux $(wrapperbits) $(dtstree)/%.dts
	$(call if_changed,wrap,$*,$(dtstree)/$*.dts)

# This cannot be in the root of $(src) as the zImage rule always adds a $(obj)
# prefix
$(obj)/vmlinux.strip: vmlinux
	$(STRIP) -s -R .comment $< -o $@

$(obj)/zImage.iseries: vmlinux
	$(STRIP) -s -R .comment $< -o $@

$(obj)/uImage: vmlinux $(wrapperbits)
	$(call if_changed,wrap,uboot)

$(obj)/cuImage.%: vmlinux $(dtstree)/%.dts $(wrapperbits)
	$(call if_changed,wrap,cuboot-$*,$(dtstree)/$*.dts)

$(obj)/treeImage.initrd.%: vmlinux $(dtstree)/%.dts $(wrapperbits)
	$(call if_changed,wrap,treeboot-$*,$(dtstree)/$*.dts,,$(obj)/ramdisk.image.gz)

$(obj)/treeImage.%: vmlinux $(dtstree)/%.dts $(wrapperbits)
	$(call if_changed,wrap,treeboot-$*,$(dtstree)/$*.dts)

# If there isn't a platform selected then just strip the vmlinux.
ifeq (,$(image-y))
image-y := vmlinux.strip
endif

$(obj)/zImage:		$(addprefix $(obj)/, $(image-y))
	@rm -f $@; ln $< $@
$(obj)/zImage.initrd:	$(addprefix $(obj)/, $(initrd-y))
	@rm -f $@; ln $< $@

install: $(CONFIGURE) $(addprefix $(obj)/, $(image-y))
	sh -x $(srctree)/$(src)/install.sh "$(KERNELRELEASE)" vmlinux System.map "$(INSTALL_PATH)" $<

# anything not in $(targets)
clean-files += $(image-) $(initrd-) zImage zImage.initrd cuImage.* treeImage.* \
	otheros.bld

# clean up files cached by wrapper
clean-kernel := vmlinux.strip vmlinux.bin
clean-kernel += $(addsuffix .gz,$(clean-kernel))
# If not absolute clean-files are relative to $(obj).
clean-files += $(addprefix $(objtree)/, $(clean-kernel))

WRAPPER_OBJDIR := /usr/lib/kernel-wrapper
WRAPPER_DTSDIR := /usr/lib/kernel-wrapper/dts
WRAPPER_BINDIR := /usr/sbin
INSTALL := install

extra-installed		:= $(patsubst $(obj)/%, $(DESTDIR)$(WRAPPER_OBJDIR)/%, $(extra-y))
hostprogs-installed	:= $(patsubst %, $(DESTDIR)$(WRAPPER_BINDIR)/%, $(hostprogs-y))
wrapper-installed	:= $(DESTDIR)$(WRAPPER_BINDIR)/wrapper
dts-installed		:= $(patsubst $(obj)/dts/%, $(DESTDIR)$(WRAPPER_DTSDIR)/%, $(wildcard $(obj)/dts/*.dts))

all-installed		:= $(extra-installed) $(hostprogs-installed) $(wrapper-installed) $(dts-installed)

quiet_cmd_mkdir           = MKDIR   $(patsubst $(INSTALL_HDR_PATH)/%,%,$@)
      cmd_mkdir           = mkdir -p $@

quiet_cmd_install	  = INSTALL $(patsubst $(DESTDIR)$(WRAPPER_OBJDIR)/%,%,$@)
      cmd_install	  = $(INSTALL)  -m0644 $(patsubst $(DESTDIR)$(WRAPPER_OBJDIR)/%,$(obj)/%,$@) $@

quiet_cmd_install_dts	  = INSTALL $(patsubst $(DESTDIR)$(WRAPPER_DTSDIR)/%,dts/%,$@)
      cmd_install_dts	  = $(INSTALL)  -m0644 $(patsubst $(DESTDIR)$(WRAPPER_DTSDIR)/%,$(srctree)/$(obj)/dts/%,$@) $@

quiet_cmd_install_exe	  = INSTALL $(patsubst $(DESTDIR)$(WRAPPER_BINDIR)/%,%,$@)
      cmd_install_exe	  = $(INSTALL)  -m0755 $(patsubst $(DESTDIR)$(WRAPPER_BINDIR)/%,$(obj)/%,$@) $@

quiet_cmd_install_wrapper = INSTALL $(patsubst $(DESTDIR)$(WRAPPER_BINDIR)/%,%,$@)
      cmd_install_wrapper = $(INSTALL)  -m0755 $(patsubst $(DESTDIR)$(WRAPPER_BINDIR)/%,$(srctree)/$(obj)/%,$@) $@ ;\
				sed -i $@ -e 's%^object=.*%object=$(WRAPPER_OBJDIR)%' \
					  -e 's%^objbin=.*%objbin=$(WRAPPER_BINDIR)%' \


$(DESTDIR)$(WRAPPER_OBJDIR) $(DESTDIR)$(WRAPPER_DTSDIR) $(DESTDIR)$(WRAPPER_BINDIR):
	$(call cmd,mkdir)

$(extra-installed)	: $(DESTDIR)$(WRAPPER_OBJDIR)/% : $(obj)/% | $(DESTDIR)$(WRAPPER_OBJDIR)
	$(call cmd,install)

$(hostprogs-installed)  : $(DESTDIR)$(WRAPPER_BINDIR)/% : $(obj)/% | $(DESTDIR)$(WRAPPER_BINDIR)
	$(call cmd,install_exe)

$(dts-installed)	: $(DESTDIR)$(WRAPPER_DTSDIR)/% : $(srctree)/$(obj)/dts/% | $(DESTDIR)$(WRAPPER_DTSDIR)
	$(call cmd,install_dts)

$(wrapper-installed): $(DESTDIR)$(WRAPPER_BINDIR) $(srctree)/$(obj)/wrapper | $(DESTDIR)$(WRAPPER_BINDIR)
	$(call cmd,install_wrapper)

$(obj)/bootwrapper_install: $(all-installed)

