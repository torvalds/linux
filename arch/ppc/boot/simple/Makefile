# This is far from simple, but I couldn't think of a good name.  This is
# for making the 'zImage' or 'zImage.initrd' on a number of targets.
#
# Author: Tom Rini <trini@mvista.com>
#
# Notes:
# (1) For machines that do not want to use the ELF image directly (including
# stripping just the ELF header off), they must set the variables
# zimage-$(CONFIG_MACHINE) and zimagerd-$(CONFIG_MACHINE) to the target
# that produces the desired image and they must set end-$(CONFIG_MACHINE)
# to what will be suffixed to the image filename.
# (2) Regardless of (1), to have the resulting image be something other
# than 'zImage.elf', set end-$(CONFIG_MACHINE) to be the suffix used for
# the zImage, znetboot, and znetbootrd targets.
# (3) For machine targets which use the mktree program, you can optionally
# set entrypoint-$(CONFIG_MACHINE) to the location which the image should be
# loaded at.  The optimal setting for entrypoint-$(CONFIG_MACHINE) is the link
# address.
# (4) It is advisable to pass in the memory size using BI_MEMSIZE and
# get_mem_size(), which is memory controller dependent.  Add in the correct
# XXX_memory.o file for this to work, as well as editing the
# misc-$(CONFIG_MACHINE) variable.

boot				:= arch/ppc/boot
common				:= $(boot)/common
utils				:= $(boot)/utils
bootlib				:= $(boot)/lib
images				:= $(boot)/images
of1275				:= $(boot)/of1275
tftpboot			:= /tftpboot

# Normally, we use the 'misc.c' file for decompress_kernel and
# whatnot.  Sometimes we need to override this however.
misc-y	:= misc.o

# Normally, we have our images end in .elf, but something we want to
# change this.
end-y := elf

# Additionally, we normally don't need to mess with the L2 / L3 caches
# if present on 'classic' PPC.
cacheflag-y	:= -DCLEAR_CACHES=""
# This file will flush / disable the L2, and L3 if present.
clear_L2_L3	:= $(srctree)/$(boot)/simple/clear.S

#
# See arch/ppc/kconfig and arch/ppc/platforms/Kconfig
# for definition of what platform each config option refer to.
#----------------------------------------------------------------------------
      zimage-$(CONFIG_CPCI690)		:= zImage-STRIPELF
zimageinitrd-$(CONFIG_CPCI690)		:= zImage.initrd-STRIPELF
     extra.o-$(CONFIG_CPCI690)		:= misc-cpci690.o
         end-$(CONFIG_CPCI690)		:= cpci690
   cacheflag-$(CONFIG_CPCI690)		:= -include $(clear_L2_L3)

      zimage-$(CONFIG_IBM_OPENBIOS)	:= zImage-TREE
zimageinitrd-$(CONFIG_IBM_OPENBIOS)	:= zImage.initrd-TREE
         end-$(CONFIG_IBM_OPENBIOS)	:= treeboot
        misc-$(CONFIG_IBM_OPENBIOS)	:= misc-embedded.o

         end-$(CONFIG_EMBEDDEDBOOT)	:= embedded
        misc-$(CONFIG_EMBEDDEDBOOT)	:= misc-embedded.o

      zimage-$(CONFIG_BAMBOO)		:= zImage-TREE
zimageinitrd-$(CONFIG_BAMBOO)		:= zImage.initrd-TREE
         end-$(CONFIG_BAMBOO)		:= bamboo
  entrypoint-$(CONFIG_BAMBOO)		:= 0x01000000
     extra.o-$(CONFIG_BAMBOO)		:= pibs.o

      zimage-$(CONFIG_BUBINGA)		:= zImage-TREE
zimageinitrd-$(CONFIG_BUBINGA)		:= zImage.initrd-TREE
         end-$(CONFIG_BUBINGA)		:= bubinga
  entrypoint-$(CONFIG_BUBINGA)		:= 0x01000000
     extra.o-$(CONFIG_BUBINGA)		:= openbios.o

      zimage-$(CONFIG_EBONY)		:= zImage-TREE
zimageinitrd-$(CONFIG_EBONY)		:= zImage.initrd-TREE
         end-$(CONFIG_EBONY)		:= ebony
  entrypoint-$(CONFIG_EBONY)		:= 0x01000000
     extra.o-$(CONFIG_EBONY)		:= openbios.o

      zimage-$(CONFIG_LUAN)		:= zImage-TREE
zimageinitrd-$(CONFIG_LUAN)		:= zImage.initrd-TREE
         end-$(CONFIG_LUAN)		:= luan
  entrypoint-$(CONFIG_LUAN)		:= 0x01000000
     extra.o-$(CONFIG_LUAN)		:= pibs.o

      zimage-$(CONFIG_YUCCA)		:= zImage-TREE
zimageinitrd-$(CONFIG_YUCCA)		:= zImage.initrd-TREE
         end-$(CONFIG_YUCCA)		:= yucca
  entrypoint-$(CONFIG_YUCCA)		:= 0x01000000
     extra.o-$(CONFIG_YUCCA)		:= pibs.o

      zimage-$(CONFIG_OCOTEA)		:= zImage-TREE
zimageinitrd-$(CONFIG_OCOTEA)		:= zImage.initrd-TREE
         end-$(CONFIG_OCOTEA)		:= ocotea
  entrypoint-$(CONFIG_OCOTEA)		:= 0x01000000
     extra.o-$(CONFIG_OCOTEA)		:= pibs.o

      zimage-$(CONFIG_SYCAMORE)		:= zImage-TREE
zimageinitrd-$(CONFIG_SYCAMORE)		:= zImage.initrd-TREE
         end-$(CONFIG_SYCAMORE)		:= sycamore
  entrypoint-$(CONFIG_SYCAMORE)		:= 0x01000000
     extra.o-$(CONFIG_SYCAMORE)		:= openbios.o

      zimage-$(CONFIG_WALNUT)		:= zImage-TREE
zimageinitrd-$(CONFIG_WALNUT)		:= zImage.initrd-TREE
         end-$(CONFIG_WALNUT)		:= walnut
  entrypoint-$(CONFIG_WALNUT)		:= 0x01000000
     extra.o-$(CONFIG_WALNUT)		:= openbios.o

     extra.o-$(CONFIG_EV64260)		:= misc-ev64260.o
         end-$(CONFIG_EV64260)		:= ev64260
   cacheflag-$(CONFIG_EV64260)		:= -include $(clear_L2_L3)

     extra.o-$(CONFIG_CHESTNUT)		:= misc-chestnut.o
         end-$(CONFIG_CHESTNUT)		:= chestnut

      zimage-$(CONFIG_GEMINI)		:= zImage-STRIPELF
zimageinitrd-$(CONFIG_GEMINI)		:= zImage.initrd-STRIPELF
         end-$(CONFIG_GEMINI)		:= gemini

     extra.o-$(CONFIG_KATANA)		:= misc-katana.o
         end-$(CONFIG_KATANA)		:= katana
   cacheflag-$(CONFIG_KATANA)		:= -include $(clear_L2_L3)

     extra.o-$(CONFIG_RADSTONE_PPC7D)	:= misc-radstone_ppc7d.o
         end-$(CONFIG_RADSTONE_PPC7D)	:= radstone_ppc7d
   cacheflag-$(CONFIG_RADSTONE_PPC7D)	:= -include $(clear_L2_L3)

     extra.o-$(CONFIG_EV64360)          := misc-ev64360.o
         end-$(CONFIG_EV64360)          := ev64360
   cacheflag-$(CONFIG_EV64360)          := -include $(clear_L2_L3)

# kconfig 'feature', only one of these will ever be 'y' at a time.
# The rest will be unset.
motorola := $(CONFIG_MVME5100)$(CONFIG_PRPMC750) \
$(CONFIG_PRPMC800)$(CONFIG_LOPEC)$(CONFIG_PPLUS)
motorola := $(strip $(motorola))

      zimage-$(motorola)		:= zImage-PPLUS
zimageinitrd-$(motorola)		:= zImage.initrd-PPLUS
         end-$(motorola)		:= pplus

# Overrides previous assingment
     extra.o-$(CONFIG_PPLUS)		:= prepmap.o
     extra.o-$(CONFIG_LOPEC)		:= mpc10x_memory.o

# Really only valid if CONFIG_6xx=y
      zimage-$(CONFIG_PPC_PREP)		:= zImage-PPLUS
zimageinitrd-$(CONFIG_PPC_PREP)		:= zImage.initrd-PPLUS
ifeq ($(CONFIG_6xx),y)
     extra.o-$(CONFIG_PPC_PREP)		:= prepmap.o
        misc-$(CONFIG_PPC_PREP)		+= misc-prep.o mpc10x_memory.o
endif
         end-$(CONFIG_PPC_PREP)		:= prep

         end-$(CONFIG_SANDPOINT)	:= sandpoint
   cacheflag-$(CONFIG_SANDPOINT)	:= -include $(clear_L2_L3)

      zimage-$(CONFIG_SPRUCE)		:= zImage-TREE
zimageinitrd-$(CONFIG_SPRUCE)		:= zImage.initrd-TREE
         end-$(CONFIG_SPRUCE)		:= spruce
  entrypoint-$(CONFIG_SPRUCE)		:= 0x00800000
        misc-$(CONFIG_SPRUCE)		+= misc-spruce.o

      zimage-$(CONFIG_LITE5200)		:= zImage-STRIPELF
zimageinitrd-$(CONFIG_LITE5200)		:= zImage.initrd-STRIPELF
         end-$(CONFIG_LITE5200)		:= lite5200
   cacheflag-$(CONFIG_LITE5200)		:= -include $(clear_L2_L3)


# SMP images should have a '.smp' suffix.
         end-$(CONFIG_SMP)             := $(end-y).smp

# This is a treeboot that needs init functions until the
# boot rom is sorted out (i.e. this is short lived)
EXTRA_AFLAGS := $(extra-aflags-y)
# head.o needs to get the cacheflags defined.
AFLAGS_head.o				+= $(cacheflag-y)

# Linker args.  This specifies where the image will be run at.
LD_ARGS					:= -T $(srctree)/$(boot)/ld.script \
				   -Ttext $(CONFIG_BOOT_LOAD) -Bstatic
OBJCOPY_ARGS			:= -O elf32-powerpc

# head.o and relocate.o must be at the start.
boot-y				:= head.o relocate.o $(extra.o-y) $(misc-y)
boot-$(CONFIG_REDWOOD_5)	+= embed_config.o
boot-$(CONFIG_REDWOOD_6)	+= embed_config.o
boot-$(CONFIG_8xx)		+= embed_config.o
boot-$(CONFIG_8260)		+= embed_config.o
boot-$(CONFIG_EP405)		+= embed_config.o
boot-$(CONFIG_XILINX_ML300)	+= embed_config.o
boot-$(CONFIG_XILINX_ML403)	+= embed_config.o
boot-$(CONFIG_BSEIP)		+= iic.o
boot-$(CONFIG_MBX)		+= iic.o pci.o qspan_pci.o
boot-$(CONFIG_MV64X60)		+= misc-mv64x60.o
boot-$(CONFIG_RPXCLASSIC)	+= iic.o pci.o qspan_pci.o
boot-$(CONFIG_RPXLITE)		+= iic.o
# Different boards need different serial implementations.
ifeq ($(CONFIG_SERIAL_CPM_CONSOLE),y)
boot-$(CONFIG_8xx)		+= m8xx_tty.o
boot-$(CONFIG_8260)		+= m8260_tty.o
endif
boot-$(CONFIG_SERIAL_MPC52xx_CONSOLE)	+= mpc52xx_tty.o
boot-$(CONFIG_SERIAL_MPSC_CONSOLE)	+= mv64x60_tty.o

LIBS				:= $(common)/lib.a $(bootlib)/lib.a
ifeq ($(CONFIG_PPC_PREP),y)
LIBS 				+= $(of1275)/lib.a
endif

OBJS				:= $(addprefix $(obj)/,$(boot-y))

# Tools
MKBUGBOOT			:= $(utils)/mkbugboot
MKPREP				:= $(utils)/mkprep
MKTREE				:= $(utils)/mktree

targets := dummy.o

$(obj)/zvmlinux: $(OBJS) $(LIBS) $(srctree)/$(boot)/ld.script \
		$(images)/vmlinux.gz $(obj)/dummy.o
	$(OBJCOPY) $(OBJCOPY_ARGS) \
		--add-section=.image=$(images)/vmlinux.gz \
		--set-section-flags=.image=contents,alloc,load,readonly,data \
		$(obj)/dummy.o $(obj)/image.o
	$(LD) $(LD_ARGS) -o $@ $(OBJS) $(obj)/image.o $(LIBS)
	$(OBJCOPY) $(OBJCOPY_ARGS) $@ $@ -R .comment -R .stab \
		-R .stabstr -R .ramdisk

$(obj)/zvmlinux.initrd: $(OBJS) $(LIBS) $(srctree)/$(boot)/ld.script \
		$(images)/vmlinux.gz $(obj)/dummy.o
	$(OBJCOPY) $(OBJCOPY_ARGS) \
		--add-section=.ramdisk=$(images)/ramdisk.image.gz \
		--set-section-flags=.ramdisk=contents,alloc,load,readonly,data \
		--add-section=.image=$(images)/vmlinux.gz \
		--set-section-flags=.image=contents,alloc,load,readonly,data \
		$(obj)/dummy.o $(obj)/image.o
	$(LD) $(LD_ARGS) -o $@ $(OBJS) $(obj)/image.o $(LIBS)
	$(OBJCOPY) $(OBJCOPY_ARGS) $@ $@ -R .comment -R .stab \
		-R .stabstr

# Sort-of dummy rules, that let us format the image we want.
zImage: $(images)/$(zimage-y) $(obj)/zvmlinux
	cp -f $(obj)/zvmlinux $(images)/zImage.elf
	rm -f $(obj)/zvmlinux

zImage.initrd: $(images)/$(zimageinitrd-y) $(obj)/zvmlinux.initrd
	cp -f $(obj)/zvmlinux.initrd $(images)/zImage.initrd.elf
	rm -f $(obj)/zvmlinux.initrd

znetboot: zImage
	cp $(images)/zImage.$(end-y) $(tftpboot)/zImage.$(end-y)

znetboot.initrd: zImage.initrd
	cp $(images)/zImage.initrd.$(end-y) $(tftpboot)/zImage.initrd.$(end-y)

$(images)/zImage-STRIPELF: $(obj)/zvmlinux
	dd if=$(obj)/zvmlinux of=$(images)/zImage.$(end-y) skip=64 bs=1k

$(images)/zImage.initrd-STRIPELF: $(obj)/zvmlinux.initrd
	dd if=$(obj)/zvmlinux.initrd of=$(images)/zImage.initrd.$(end-y) \
		skip=64 bs=1k

$(images)/zImage-TREE: $(obj)/zvmlinux $(MKTREE)
	$(MKTREE) $(obj)/zvmlinux $(images)/zImage.$(end-y) $(entrypoint-y)

$(images)/zImage.initrd-TREE: $(obj)/zvmlinux.initrd $(MKTREE)
	$(MKTREE) $(obj)/zvmlinux.initrd $(images)/zImage.initrd.$(end-y) \
		$(entrypoint-y)

$(images)/zImage-PPLUS: $(obj)/zvmlinux $(MKPREP) $(MKBUGBOOT)
	$(MKPREP) -pbp $(obj)/zvmlinux $(images)/zImage.$(end-y)
	$(MKBUGBOOT) $(obj)/zvmlinux $(images)/zImage.bugboot

$(images)/zImage.initrd-PPLUS: $(obj)/zvmlinux.initrd $(MKPREP) $(MKBUGBOOT)
	$(MKPREP) -pbp $(obj)/zvmlinux.initrd $(images)/zImage.initrd.$(end-y)
	$(MKBUGBOOT) $(obj)/zvmlinux.initrd $(images)/zImage.initrd.bugboot
