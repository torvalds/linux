# Makefile for making bootable images on various OpenFirmware machines.
#
# Paul Mackerras	January 1997
#	XCOFF bootable images for PowerMacs
# Geert Uytterhoeven	September 1997
#	ELF bootable iamges for CHRP machines.
# Tom Rini		January 2001
# 	Cleaned up, moved into arch/ppc/boot/pmac
# Tom Rini		July/August 2002
#	Merged 'chrp' and 'pmac' into 'openfirmware', and cleaned up the
#	rules.

zImage.initrd znetboot.initrd: del-ramdisk-sec	:= -R .ramdisk
zImage.initrd znetboot.initrd: initrd		:= .initrd


boot	:= arch/ppc/boot
common	:= $(boot)/common
utils	:= $(boot)/utils
bootlib	:= $(boot)/lib
of1275	:= $(boot)/of1275
images	:= $(boot)/images

OBJCOPY_ARGS	:= -O aixcoff-rs6000 -R .stab -R .stabstr -R .comment
COFF_LD_ARGS	:= -T $(srctree)/$(boot)/ld.script -e _start -Ttext 0x00500000 \
			-Bstatic
CHRP_LD_ARGS	:= -T $(srctree)/$(boot)/ld.script -e _start -Ttext 0x00800000
NEWWORLD_LD_ARGS:= -T $(srctree)/$(boot)/ld.script -e _start -Ttext 0x01000000

COMMONOBJS	:= start.o misc.o common.o
COFFOBJS	:= coffcrt0.o $(COMMONOBJS) coffmain.o
CHRPOBJS	:= crt0.o     $(COMMONOBJS) chrpmain.o
NEWWORLDOBJS	:= crt0.o     $(COMMONOBJS) newworldmain.o

targets 	:= $(COFFOBJS) $(CHRPOBJS) $(NEWWORLDOBJS) dummy.o
COFFOBJS	:= $(addprefix $(obj)/, $(COFFOBJS))
CHRPOBJS	:= $(addprefix $(obj)/, $(CHRPOBJS))
NEWWORLDOBJS	:= $(addprefix $(obj)/, $(NEWWORLDOBJS))

LIBS		:= lib/lib.a $(bootlib)/lib.a $(of1275)/lib.a $(common)/lib.a

HACKCOFF := $(utils)/hack-coff

ifdef CONFIG_SMP
END := .smp
endif
ifdef CONFIG_PPC64BRIDGE
END += .64
endif


$(images)/ramdisk.image.gz:
	@echo '  MISSING $@'
	@echo '          RAM disk image must be provided separately'
	@/bin/false

quiet_cmd_genimage = GEN     $@
      cmd_genimage = $(OBJCOPY) -R .comment       \
	--add-section=.image=$(images)/vmlinux.gz \
	--set-section-flags=.image=contents,alloc,load,readonly,data $< $@

targets += image.o
$(obj)/image.o: $(obj)/dummy.o $(images)/vmlinux.gz FORCE
	$(call if_changed,genimage)

# Place the ramdisk in the initrd image.
quiet_cmd_genimage-initrd = GEN     $@
      cmd_genimage-initrd = $(OBJCOPY) $< $@ \
	--add-section=.ramdisk=$(images)/ramdisk.image.gz \
	--set-section-flags=.ramdisk=contents,alloc,load,readonly,data
targets += image.initrd.o
$(obj)/image.initrd.o: $(obj)/image.o $(images)/ramdisk.image.gz FORCE
	$(call if_changed,genimage-initrd)

# Create the note section for New-World PowerMacs.
quiet_cmd_mknote = MKNOTE  $@
     cmd_mknote  = $(utils)/mknote > $@
targets		+= note
$(obj)/note: $(utils)/mknote FORCE
	$(call if_changed,mknote)


$(obj)/coffcrt0.o: EXTRA_AFLAGS := -DXCOFF
targets += coffcrt0.o crt0.o
$(obj)/coffcrt0.o $(obj)/crt0.o: $(common)/crt0.S FORCE
	$(call if_changed_dep,as_o_S)

quiet_cmd_gencoffb = COFF    $@
      cmd_gencoffb = $(LD) -o $@ $(COFF_LD_ARGS) $(COFFOBJS) $< $(LIBS) && \
                     $(OBJCOPY) $@ $@ -R .comment $(del-ramdisk-sec)
targets += coffboot
$(obj)/coffboot: $(obj)/image.o $(COFFOBJS) $(LIBS) $(srctree)/$(boot)/ld.script FORCE
	$(call if_changed,gencoffb)
targets += coffboot.initrd
$(obj)/coffboot.initrd: $(obj)/image.initrd.o $(COFFOBJS) $(LIBS) \
			$(srctree)/$(boot)/ld.script FORCE
	$(call if_changed,gencoffb)


quiet_cmd_gen-coff = COFF    $@
      cmd_gen-coff = $(OBJCOPY) $(OBJCOPY_ARGS) $< $@ && \
			$(HACKCOFF) $@ && \
			ln -sf $(notdir $@) $(images)/zImage$(initrd).pmac

$(images)/vmlinux.coff: $(obj)/coffboot
	$(call cmd,gen-coff)

$(images)/vmlinux.initrd.coff: $(obj)/coffboot.initrd
	$(call cmd,gen-coff)

quiet_cmd_gen-elf-pmac = ELF     $@
      cmd_gen-elf-pmac = $(LD) $(NEWWORLD_LD_ARGS) -o $@ \
				$(NEWWORLDOBJS) $(LIBS) $< && \
			$(OBJCOPY) $@ $@ --add-section=.note=$(obj)/note \
					 -R .comment $(del-ramdisk-sec)

$(images)/vmlinux.elf-pmac: $(obj)/image.o $(NEWWORLDOBJS) $(LIBS) \
			$(obj)/note $(srctree)/$(boot)/ld.script
	$(call cmd,gen-elf-pmac)
$(images)/vmlinux.initrd.elf-pmac: $(obj)/image.initrd.o $(NEWWORLDOBJS) \
				   $(LIBS) $(obj)/note \
				   $(srctree)/$(boot)/ld.script
	$(call cmd,gen-elf-pmac)

quiet_cmd_gen-chrp = CHRP    $@
      cmd_gen-chrp = $(LD) $(CHRP_LD_ARGS) -o $@ $(CHRPOBJS) $< $(LIBS) && \
			$(OBJCOPY) $@ $@ -R .comment $(del-ramdisk-sec)

$(images)/zImage.chrp: $(obj)/image.o $(CHRPOBJS) $(LIBS) \
				   $(srctree)/$(boot)/ld.script
	$(call cmd,gen-chrp)
$(images)/zImage.initrd.chrp: $(obj)/image.initrd.o $(CHRPOBJS) $(LIBS) \
				   $(srctree)/$(boot)/ld.script
	$(call cmd,gen-chrp)

quiet_cmd_addnote = ADDNOTE $@
      cmd_addnote = cat $< > $@ && $(utils)/addnote $@
$(images)/zImage.chrp-rs6k $(images)/zImage.initrd.chrp-rs6k: \
	%-rs6k: %
	$(call cmd,addnote)

quiet_cmd_gen-miboot = GEN     $@
      cmd_gen-miboot = $(OBJCOPY) $(OBJCOPY_ARGS) \
		       --add-section=$1=$(word 2, $^) $< $@
$(images)/miboot.image: $(obj)/dummy.o $(images)/vmlinux.gz
	$(call cmd,gen-miboot,image)

$(images)/miboot.initrd.image: $(images)/miboot.image $(images)/ramdisk.image.gz
	$(call cmd,gen-miboot,initrd)

# The targets used on the make command-line

.PHONY: zImage zImage.initrd
zImage:		 $(images)/vmlinux.coff 	\
		 $(images)/vmlinux.elf-pmac	\
		 $(images)/zImage.chrp		\
		 $(images)/zImage.chrp-rs6k	\
		 $(images)/miboot.image
	@echo '  kernel: $@ is ready ($<)'
zImage.initrd:	 $(images)/vmlinux.initrd.coff 		\
		 $(images)/vmlinux.initrd.elf-pmac	\
		 $(images)/zImage.initrd.chrp		\
		 $(images)/zImage.initrd.chrp-rs6k	\
		 $(images)/miboot.initrd.image
	@echo '  kernel: $@ is ready ($<)'

TFTPIMAGE	:= /tftpboot/zImage

.PHONY: znetboot znetboot.initrd
znetboot:	$(images)/vmlinux.coff		\
		$(images)/vmlinux.elf-pmac	\
		$(images)/zImage.chrp
	cp $(images)/vmlinux.coff     $(TFTPIMAGE).pmac$(END)
	cp $(images)/vmlinux.elf-pmac $(TFTPIMAGE).pmac$(END).elf
	cp $(images)/zImage.chrp      $(TFTPIMAGE).chrp$(END)
	@echo '  kernel: $@ is ready ($<)'
znetboot.initrd:$(images)/vmlinux.initrd.coff		\
		$(images)/vmlinux.initrd.elf-pmac	\
		$(images)/zImage.initrd.chrp
	cp $(images)/vmlinux.initrd.coff     $(TFTPIMAGE).pmac$(END)
	cp $(images)/vmlinux.initrd.elf-pmac $(TFTPIMAGE).pmac$(END).elf
	cp $(images)/zImage.initrd.chrp      $(TFTPIMAGE).chrp$(END)
	@echo '  kernel: $@ is ready ($<)'

