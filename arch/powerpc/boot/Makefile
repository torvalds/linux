# SPDX-License-Identifier: GPL-2.0
# Makefile for making ELF bootable images for booting on CHRP
# using Open Firmware.
#
# Geert Uytterhoeven	September 1997
#
# Based on coffboot by Paul Mackerras
# Simplified for ppc64 by Todd Inglett
#
# NOTE:	this code may be built for 32 bit in ELF32 format even though
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

ifdef CROSS32_COMPILE
ifdef CONFIG_CC_IS_CLANG
    BOOTCC := $(CROSS32_COMPILE)clang
else
    BOOTCC := $(CROSS32_COMPILE)gcc
endif
    BOOTAR := $(CROSS32_COMPILE)ar
else
    BOOTCC := $(CC)
    BOOTAR := $(AR)
endif

BOOTCFLAGS    := -Wall -Wundef -Wstrict-prototypes -Wno-trigraphs \
		 -fno-strict-aliasing -O2 -msoft-float -mno-altivec -mno-vsx \
		 -pipe -fomit-frame-pointer -fno-builtin -fPIC -nostdinc \
		 $(LINUXINCLUDE)

ifdef CONFIG_PPC64_BOOT_WRAPPER
BOOTCFLAGS	+= -m64
else
BOOTCFLAGS	+= -m32
endif

BOOTCFLAGS	+= -isystem $(shell $(BOOTCC) -print-file-name=include)

ifdef CONFIG_CPU_BIG_ENDIAN
BOOTCFLAGS	+= -mbig-endian
else
BOOTCFLAGS	+= -mlittle-endian
BOOTCFLAGS	+= $(call cc-option,-mabi=elfv2)
endif

BOOTAFLAGS	:= -D__ASSEMBLY__ $(BOOTCFLAGS) -nostdinc

BOOTARFLAGS	:= -crD

ifdef CONFIG_CC_IS_CLANG
BOOTCFLAGS += $(CLANG_FLAGS)
BOOTAFLAGS += $(CLANG_FLAGS)
endif

ifdef CONFIG_DEBUG_INFO
BOOTCFLAGS	+= -g
endif

ifeq ($(call cc-option-yn, -fstack-protector),y)
BOOTCFLAGS	+= -fno-stack-protector
endif

BOOTCFLAGS	+= -include $(srctree)/include/linux/compiler_attributes.h
BOOTCFLAGS	+= -I$(objtree)/$(obj) -I$(srctree)/$(obj)

DTC_FLAGS	?= -p 1024

$(obj)/4xx.o: BOOTCFLAGS += -mcpu=405
$(obj)/ebony.o: BOOTCFLAGS += -mcpu=440
$(obj)/cuboot-hotfoot.o: BOOTCFLAGS += -mcpu=405
$(obj)/cuboot-taishan.o: BOOTCFLAGS += -mcpu=440
$(obj)/cuboot-katmai.o: BOOTCFLAGS += -mcpu=440
$(obj)/cuboot-acadia.o: BOOTCFLAGS += -mcpu=405
$(obj)/treeboot-iss4xx.o: BOOTCFLAGS += -mcpu=405
$(obj)/treeboot-currituck.o: BOOTCFLAGS += -mcpu=405
$(obj)/treeboot-akebono.o: BOOTCFLAGS += -mcpu=405

# The pre-boot decompressors pull in a lot of kernel headers and other source
# files. This creates a bit of a dependency headache since we need to copy
# these files into the build dir, fix up any includes and ensure that dependent
# files are copied in the right order.

# these need to be seperate variables because they are copied out of different
# directories in the kernel tree. Sure you COULd merge them, but it's a
# cure-is-worse-than-disease situation.
zlib-decomp-$(CONFIG_KERNEL_GZIP) := decompress_inflate.c
zlib-$(CONFIG_KERNEL_GZIP) := inffast.c inflate.c inftrees.c
zlibheader-$(CONFIG_KERNEL_GZIP) := inffast.h inffixed.h inflate.h inftrees.h infutil.h
zliblinuxheader-$(CONFIG_KERNEL_GZIP) := zlib.h zconf.h zutil.h

$(addprefix $(obj)/, decompress.o): \
	$(addprefix $(obj)/,$(zlib-decomp-y))

$(addprefix $(obj)/, $(zlib-decomp-y)): \
	$(addprefix $(obj)/,$(zliblinuxheader-y)) \
	$(addprefix $(obj)/,$(zlibheader-y)) \
	$(addprefix $(obj)/,$(zlib-y))

$(addprefix $(obj)/,$(zlib-y)): \
	$(addprefix $(obj)/,$(zliblinuxheader-y)) \
	$(addprefix $(obj)/,$(zlibheader-y))

libfdt       := fdt.c fdt_ro.c fdt_wip.c fdt_sw.c fdt_rw.c fdt_strerror.c
libfdtheader := fdt.h libfdt.h libfdt_internal.h

$(addprefix $(obj)/,$(libfdt) libfdt-wrapper.o simpleboot.o epapr.o opal.o \
	treeboot-akebono.o treeboot-currituck.o treeboot-iss4xx.o): \
	$(addprefix $(obj)/,$(libfdtheader))

src-wlib-y := string.S crt0.S stdio.c decompress.c main.c \
		$(libfdt) libfdt-wrapper.c \
		ns16550.c serial.c simple_alloc.c div64.S util.S \
		elf_util.c $(zlib-y) devtree.c stdlib.c \
		oflib.c ofconsole.c cuboot.c

src-wlib-$(CONFIG_PPC_MPC52xx) += mpc52xx-psc.c
src-wlib-$(CONFIG_PPC_POWERNV) += opal-calls.S opal.c
ifndef CONFIG_PPC64_BOOT_WRAPPER
src-wlib-y += crtsavres.S
endif
src-wlib-$(CONFIG_40x) += 4xx.c planetcore.c
src-wlib-$(CONFIG_44x) += 4xx.c ebony.c bamboo.c
src-wlib-$(CONFIG_PPC_8xx) += mpc8xx.c planetcore.c fsl-soc.c
src-wlib-$(CONFIG_PPC_82xx) += pq2.c fsl-soc.c planetcore.c
src-wlib-$(CONFIG_EMBEDDED6xx) += ugecon.c fsl-soc.c
src-wlib-$(CONFIG_CPM) += cpm-serial.c

src-plat-y := of.c epapr.c
src-plat-$(CONFIG_40x) += fixed-head.S cuboot-hotfoot.c \
				cuboot-acadia.c \
				cuboot-kilauea.c simpleboot.c
src-plat-$(CONFIG_44x) += treeboot-ebony.c cuboot-ebony.c treeboot-bamboo.c \
				cuboot-bamboo.c cuboot-sam440ep.c \
				cuboot-sequoia.c cuboot-rainier.c \
				cuboot-taishan.c cuboot-katmai.c \
				cuboot-warp.c cuboot-yosemite.c \
				treeboot-iss4xx.c treeboot-currituck.c \
				treeboot-akebono.c \
				simpleboot.c fixed-head.S
src-plat-$(CONFIG_PPC_8xx) += cuboot-8xx.c fixed-head.S ep88xc.c redboot-8xx.c
src-plat-$(CONFIG_PPC_MPC52xx) += cuboot-52xx.c
src-plat-$(CONFIG_PPC_82xx) += cuboot-pq2.c fixed-head.S ep8248e.c cuboot-824x.c
src-plat-$(CONFIG_PPC_83xx) += cuboot-83xx.c fixed-head.S redboot-83xx.c
src-plat-$(CONFIG_FSL_SOC_BOOKE) += cuboot-85xx.c cuboot-85xx-cpm2.c
src-plat-$(CONFIG_EMBEDDED6xx) += cuboot-pq2.c cuboot-mpc7448hpc2.c \
					gamecube-head.S gamecube.c \
					wii-head.S wii.c holly.c \
					fixed-head.S mvme5100.c
src-plat-$(CONFIG_AMIGAONE) += cuboot-amigaone.c
src-plat-$(CONFIG_PPC_PS3) += ps3-head.S ps3-hvcall.S ps3.c
src-plat-$(CONFIG_EPAPR_BOOT) += epapr.c epapr-wrapper.c
src-plat-$(CONFIG_PPC_PSERIES) += pseries-head.S
src-plat-$(CONFIG_PPC_POWERNV) += pseries-head.S
src-plat-$(CONFIG_PPC_IBM_CELL_BLADE) += pseries-head.S
src-plat-$(CONFIG_MVME7100) += motload-head.S mvme7100.c

src-plat-$(CONFIG_PPC_MICROWATT) += fixed-head.S microwatt.c

src-wlib := $(sort $(src-wlib-y))
src-plat := $(sort $(src-plat-y))
src-boot := $(src-wlib) $(src-plat) empty.c

src-boot := $(addprefix $(obj)/, $(src-boot))
obj-boot := $(addsuffix .o, $(basename $(src-boot)))
obj-wlib := $(addsuffix .o, $(basename $(addprefix $(obj)/, $(src-wlib))))
obj-plat := $(addsuffix .o, $(basename $(addprefix $(obj)/, $(src-plat))))
obj-plat: $(libfdt)

quiet_cmd_copy_kern_src = COPY    $@
      cmd_copy_kern_src = sed -f $(srctree)/arch/powerpc/boot/fixup-headers.sed $< > $@

$(addprefix $(obj)/,$(zlib-y)): $(obj)/%: $(srctree)/lib/zlib_inflate/%
	$(call cmd,copy_kern_src)

$(addprefix $(obj)/,$(zlibheader-y)): $(obj)/%: $(srctree)/lib/zlib_inflate/%
	$(call cmd,copy_kern_src)

$(addprefix $(obj)/,$(zliblinuxheader-y)): $(obj)/%: $(srctree)/include/linux/%
	$(call cmd,copy_kern_src)

$(addprefix $(obj)/,$(zlib-decomp-y)): $(obj)/%: $(srctree)/lib/%
	$(call cmd,copy_kern_src)

quiet_cmd_copy_libfdt = COPY    $@
      cmd_copy_libfdt = cp $< $@

$(addprefix $(obj)/,$(libfdt) $(libfdtheader)): $(obj)/%: $(srctree)/scripts/dtc/libfdt/%
	$(call cmd,copy_libfdt)

$(obj)/empty.c:
	$(Q)touch $@

$(obj)/zImage.coff.lds $(obj)/zImage.ps3.lds : $(obj)/%: $(srctree)/$(src)/%.S
	$(Q)cp $< $@

clean-files := $(zlib-) $(zlibheader-) $(zliblinuxheader-) \
		$(zlib-decomp-) $(libfdt) $(libfdtheader) \
		empty.c zImage.coff.lds zImage.ps3.lds zImage.lds

quiet_cmd_bootcc = BOOTCC  $@
      cmd_bootcc = $(BOOTCC) -Wp,-MD,$(depfile) $(BOOTCFLAGS) -c -o $@ $<

quiet_cmd_bootas = BOOTAS  $@
      cmd_bootas = $(BOOTCC) -Wp,-MD,$(depfile) $(BOOTAFLAGS) -c -o $@ $<

quiet_cmd_bootar = BOOTAR  $@
      cmd_bootar = $(BOOTAR) $(BOOTARFLAGS) $@.$$$$ $(real-prereqs); mv $@.$$$$ $@

$(obj-libfdt): $(obj)/%.o: $(srctree)/scripts/dtc/libfdt/%.c FORCE
	$(call if_changed_dep,bootcc)
$(patsubst %.c,%.o, $(filter %.c, $(src-boot))): %.o: %.c FORCE
	$(Q)mkdir -p $(dir $@)
	$(call if_changed_dep,bootcc)
$(patsubst %.S,%.o, $(filter %.S, $(src-boot))): %.o: %.S FORCE
	$(Q)mkdir -p $(dir $@)
	$(call if_changed_dep,bootas)

$(obj)/wrapper.a: $(obj-wlib) FORCE
	$(call if_changed,bootar)

hostprogs	:= addnote hack-coff mktree

targets		+= $(patsubst $(obj)/%,%,$(obj-boot) wrapper.a) zImage.lds
extra-y		:= $(obj)/wrapper.a $(obj-plat) $(obj)/empty.o \
		   $(obj)/zImage.lds $(obj)/zImage.coff.lds $(obj)/zImage.ps3.lds

dtstree		:= $(srctree)/$(src)/dts

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

compressor-$(CONFIG_KERNEL_GZIP) := gz
compressor-$(CONFIG_KERNEL_XZ)   := xz
compressor-$(CONFIG_KERNEL_LZMA)   := lzma
compressor-$(CONFIG_KERNEL_LZO) := lzo

# args (to if_changed): 1 = (this rule), 2 = platform, 3 = dts 4=dtb 5=initrd
quiet_cmd_wrap	= WRAP    $@
      cmd_wrap	=$(CONFIG_SHELL) $(wrapper) -Z $(compressor-y) -c -o $@ -p $2 \
		$(CROSSWRAP) $(if $3, -s $3)$(if $4, -d $4)$(if $5, -i $5) \
		vmlinux

image-$(CONFIG_PPC_PSERIES)		+= zImage.pseries
image-$(CONFIG_PPC_POWERNV)		+= zImage.pseries
image-$(CONFIG_PPC_MAPLE)		+= zImage.maple
image-$(CONFIG_PPC_IBM_CELL_BLADE)	+= zImage.pseries
image-$(CONFIG_PPC_PS3)			+= dtbImage.ps3
image-$(CONFIG_PPC_CHRP)		+= zImage.chrp
image-$(CONFIG_PPC_EFIKA)		+= zImage.chrp
image-$(CONFIG_PPC_PMAC)		+= zImage.pmac
image-$(CONFIG_PPC_HOLLY)		+= dtbImage.holly
image-$(CONFIG_DEFAULT_UIMAGE)		+= uImage
image-$(CONFIG_EPAPR_BOOT)		+= zImage.epapr

#
# Targets which embed a device tree blob
#
# Theses are default targets to build images which embed device tree blobs.
# They are only required on boards which do not have FDT support in firmware.
# Boards with newish u-boot firmware can use the uImage target above
#

# Board ports in arch/powerpc/platform/40x/Kconfig
image-$(CONFIG_HOTFOOT)			+= cuImage.hotfoot
image-$(CONFIG_ACADIA)			+= cuImage.acadia
image-$(CONFIG_OBS600)			+= uImage.obs600

# Board ports in arch/powerpc/platform/44x/Kconfig
image-$(CONFIG_EBONY)			+= treeImage.ebony cuImage.ebony
image-$(CONFIG_BAMBOO)			+= treeImage.bamboo cuImage.bamboo
image-$(CONFIG_SAM440EP)		+= cuImage.sam440ep
image-$(CONFIG_SEQUOIA)			+= cuImage.sequoia
image-$(CONFIG_RAINIER)			+= cuImage.rainier
image-$(CONFIG_TAISHAN)			+= cuImage.taishan
image-$(CONFIG_KATMAI)			+= cuImage.katmai
image-$(CONFIG_WARP)			+= cuImage.warp
image-$(CONFIG_YOSEMITE)		+= cuImage.yosemite
image-$(CONFIG_ISS4xx)			+= treeImage.iss4xx \
					   treeImage.iss4xx-mpic
image-$(CONFIG_CURRITUCK)			+= treeImage.currituck
image-$(CONFIG_AKEBONO)			+= treeImage.akebono

# Board ports in arch/powerpc/platform/8xx/Kconfig
image-$(CONFIG_MPC86XADS)		+= cuImage.mpc866ads
image-$(CONFIG_MPC885ADS)		+= cuImage.mpc885ads
image-$(CONFIG_PPC_EP88XC)		+= dtbImage.ep88xc
image-$(CONFIG_PPC_ADDER875)		+= cuImage.adder875-uboot \
					   dtbImage.adder875-redboot

# Board ports in arch/powerpc/platform/52xx/Kconfig
image-$(CONFIG_PPC_LITE5200)		+= cuImage.lite5200
image-$(CONFIG_PPC_LITE5200)		+= cuImage.lite5200b
image-$(CONFIG_PPC_MEDIA5200)		+= cuImage.media5200

# Board ports in arch/powerpc/platform/82xx/Kconfig
image-$(CONFIG_MPC8272_ADS)		+= cuImage.mpc8272ads
image-$(CONFIG_PQ2FADS)			+= cuImage.pq2fads
image-$(CONFIG_EP8248E)			+= dtbImage.ep8248e

# Board ports in arch/powerpc/platform/83xx/Kconfig
image-$(CONFIG_MPC832x_MDS)		+= cuImage.mpc832x_mds
image-$(CONFIG_MPC832x_RDB)		+= cuImage.mpc832x_rdb
image-$(CONFIG_MPC834x_ITX)		+= cuImage.mpc8349emitx \
					   cuImage.mpc8349emitxgp
image-$(CONFIG_MPC834x_MDS)		+= cuImage.mpc834x_mds
image-$(CONFIG_MPC836x_MDS)		+= cuImage.mpc836x_mds
image-$(CONFIG_ASP834x)			+= dtbImage.asp834x-redboot

# Board ports in arch/powerpc/platform/85xx/Kconfig
image-$(CONFIG_MPC8540_ADS)		+= cuImage.mpc8540ads
image-$(CONFIG_MPC8560_ADS)		+= cuImage.mpc8560ads
image-$(CONFIG_MPC85xx_CDS)		+= cuImage.mpc8541cds \
					   cuImage.mpc8548cds_32b \
					   cuImage.mpc8555cds
image-$(CONFIG_MPC85xx_MDS)		+= cuImage.mpc8568mds
image-$(CONFIG_MPC85xx_DS)		+= cuImage.mpc8544ds \
					   cuImage.mpc8572ds
image-$(CONFIG_TQM8540)			+= cuImage.tqm8540
image-$(CONFIG_TQM8541)			+= cuImage.tqm8541
image-$(CONFIG_TQM8548)			+= cuImage.tqm8548
image-$(CONFIG_TQM8555)			+= cuImage.tqm8555
image-$(CONFIG_TQM8560)			+= cuImage.tqm8560
image-$(CONFIG_KSI8560)			+= cuImage.ksi8560

# Board ports in arch/powerpc/platform/86xx/Kconfig
image-$(CONFIG_MVME7100)                += dtbImage.mvme7100

# Board ports in arch/powerpc/platform/embedded6xx/Kconfig
image-$(CONFIG_STORCENTER)		+= cuImage.storcenter
image-$(CONFIG_MPC7448HPC2)		+= cuImage.mpc7448hpc2
image-$(CONFIG_GAMECUBE)		+= dtbImage.gamecube
image-$(CONFIG_WII)			+= dtbImage.wii
image-$(CONFIG_MVME5100)		+= dtbImage.mvme5100

# Board port in arch/powerpc/platform/amigaone/Kconfig
image-$(CONFIG_AMIGAONE)		+= cuImage.amigaone

image-$(CONFIG_PPC_MICROWATT)		+= dtbImage.microwatt

# For 32-bit powermacs, build the COFF and miboot images
# as well as the ELF images.
ifdef CONFIG_PPC32
image-$(CONFIG_PPC_PMAC)	+= zImage.coff zImage.miboot
endif

# Allow extra targets to be added to the defconfig
image-y	+= $(CONFIG_EXTRA_TARGETS)

initrd-  := $(patsubst zImage%, zImage.initrd%, $(image-))
initrd-y := $(patsubst zImage%, zImage.initrd%, \
		$(patsubst dtbImage%, dtbImage.initrd%, \
		$(patsubst simpleImage%, simpleImage.initrd%, \
		$(patsubst treeImage%, treeImage.initrd%, $(image-y)))))
initrd-y := $(filter-out $(image-y), $(initrd-y))
targets	+= $(image-y) $(initrd-y)
targets += $(foreach x, dtbImage uImage cuImage simpleImage treeImage, \
		$(patsubst $(x).%, dts/%.dtb, $(filter $(x).%, $(image-y))))
targets += $(foreach x, dtbImage uImage cuImage simpleImage treeImage, \
		$(patsubst $(x).%, dts/fsl/%.dtb, $(filter $(x).%, $(image-y))))

$(addprefix $(obj)/, $(initrd-y)): $(obj)/ramdisk.image.gz

# Don't put the ramdisk on the pattern rule; when its missing make will try
# the pattern rule with less dependencies that also matches (even with the
# hard dependency listed).
$(obj)/zImage.initrd.%: vmlinux $(wrapperbits) FORCE
	$(call if_changed,wrap,$*,,,$(obj)/ramdisk.image.gz)

$(addprefix $(obj)/, $(sort $(filter zImage.%, $(image-y)))): vmlinux $(wrapperbits) FORCE
	$(call if_changed,wrap,$(subst $(obj)/zImage.,,$@))

# dtbImage% - a dtbImage is a zImage with an embedded device tree blob
$(obj)/dtbImage.initrd.%: vmlinux $(wrapperbits) $(obj)/dts/%.dtb FORCE
	$(call if_changed,wrap,$*,,$(obj)/dts/$*.dtb,$(obj)/ramdisk.image.gz)

$(obj)/dtbImage.%: vmlinux $(wrapperbits) $(obj)/dts/%.dtb FORCE
	$(call if_changed,wrap,$*,,$(obj)/dts/$*.dtb)

# This cannot be in the root of $(src) as the zImage rule always adds a $(obj)
# prefix
$(obj)/vmlinux.strip: vmlinux
	$(STRIP) -s -R .comment $< -o $@

$(obj)/uImage: vmlinux $(wrapperbits) FORCE
	$(call if_changed,wrap,uboot)

$(obj)/uImage.initrd.%: vmlinux $(obj)/dts/%.dtb $(wrapperbits) FORCE
	$(call if_changed,wrap,uboot-$*,,$(obj)/dts/$*.dtb,$(obj)/ramdisk.image.gz)

$(obj)/uImage.%: vmlinux $(obj)/dts/%.dtb $(wrapperbits) FORCE
	$(call if_changed,wrap,uboot-$*,,$(obj)/dts/$*.dtb)

$(obj)/cuImage.initrd.%: vmlinux $(obj)/dts/%.dtb $(wrapperbits) FORCE
	$(call if_changed,wrap,cuboot-$*,,$(obj)/dts/$*.dtb,$(obj)/ramdisk.image.gz)

$(obj)/cuImage.%: vmlinux $(obj)/dts/%.dtb $(wrapperbits) FORCE
	$(call if_changed,wrap,cuboot-$*,,$(obj)/dts/$*.dtb)

$(obj)/simpleImage.initrd.%: vmlinux $(obj)/dts/%.dtb $(wrapperbits) FORCE
	$(call if_changed,wrap,simpleboot-$*,,$(obj)/dts/$*.dtb,$(obj)/ramdisk.image.gz)

$(obj)/simpleImage.%: vmlinux $(obj)/dts/%.dtb $(wrapperbits) FORCE
	$(call if_changed,wrap,simpleboot-$*,,$(obj)/dts/$*.dtb)

$(obj)/treeImage.initrd.%: vmlinux $(obj)/dts/%.dtb $(wrapperbits) FORCE
	$(call if_changed,wrap,treeboot-$*,,$(obj)/dts/$*.dtb,$(obj)/ramdisk.image.gz)

$(obj)/treeImage.%: vmlinux $(obj)/dts/%.dtb $(wrapperbits) FORCE
	$(call if_changed,wrap,treeboot-$*,,$(obj)/dts/$*.dtb)

# Needed for the above targets to work with dts/fsl/ files
$(obj)/dts/%.dtb: $(obj)/dts/fsl/%.dtb
	@cp $< $@

# If there isn't a platform selected then just strip the vmlinux.
ifeq (,$(image-y))
image-y := vmlinux.strip
endif

$(obj)/zImage:		$(addprefix $(obj)/, $(image-y))
	$(Q)rm -f $@; ln $< $@
$(obj)/zImage.initrd:	$(addprefix $(obj)/, $(initrd-y))
	$(Q)rm -f $@; ln $< $@

# anything not in $(targets)
clean-files += $(image-) $(initrd-) cuImage.* dtbImage.* treeImage.* \
	zImage zImage.initrd zImage.chrp zImage.coff zImage.holly \
	zImage.miboot zImage.pmac zImage.pseries \
	zImage.maple simpleImage.* otheros.bld

# clean up files cached by wrapper
clean-kernel-base := vmlinux.strip vmlinux.bin
clean-kernel := $(addsuffix .gz,$(clean-kernel-base))
clean-kernel += $(addsuffix .xz,$(clean-kernel-base))
# If not absolute clean-files are relative to $(obj).
clean-files += $(addprefix $(objtree)/, $(clean-kernel))

WRAPPER_OBJDIR := /usr/lib/kernel-wrapper
WRAPPER_DTSDIR := /usr/lib/kernel-wrapper/dts
WRAPPER_BINDIR := /usr/sbin
INSTALL := install

extra-installed		:= $(patsubst $(obj)/%, $(DESTDIR)$(WRAPPER_OBJDIR)/%, $(extra-y))
hostprogs-installed	:= $(patsubst %, $(DESTDIR)$(WRAPPER_BINDIR)/%, $(hostprogs))
wrapper-installed	:= $(DESTDIR)$(WRAPPER_BINDIR)/wrapper
dts-installed		:= $(patsubst $(dtstree)/%, $(DESTDIR)$(WRAPPER_DTSDIR)/%, $(wildcard $(dtstree)/*.dts))

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
