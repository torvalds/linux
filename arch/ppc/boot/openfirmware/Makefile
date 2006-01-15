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

CHRP_LD_ARGS	:= -T $(srctree)/$(boot)/ld.script -e _start -Ttext 0x00800000

COMMONOBJS	:= start.o misc.o common.o
CHRPOBJS	:= crt0.o     $(COMMONOBJS) chrpmain.o

targets 	:= $(CHRPOBJS) dummy.o
CHRPOBJS	:= $(addprefix $(obj)/, $(CHRPOBJS))

LIBS		:= lib/lib.a $(bootlib)/lib.a $(of1275)/lib.a $(common)/lib.a

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


targets += crt0.o
$(obj)/crt0.o: $(common)/crt0.S FORCE
	$(call if_changed_dep,as_o_S)

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

# The targets used on the make command-line

.PHONY: zImage zImage.initrd
zImage:		 $(images)/zImage.chrp		\
		 $(images)/zImage.chrp-rs6k
	@echo '  kernel: $@ is ready ($<)'
zImage.initrd:	 $(images)/zImage.initrd.chrp		\
		 $(images)/zImage.initrd.chrp-rs6k
	@echo '  kernel: $@ is ready ($<)'

TFTPIMAGE	:= /tftpboot/zImage

.PHONY: znetboot znetboot.initrd
znetboot:	$(images)/zImage.chrp
	cp $(images)/zImage.chrp      $(TFTPIMAGE).chrp$(END)
	@echo '  kernel: $@ is ready ($<)'
znetboot.initrd:$(images)/zImage.initrd.chrp
	cp $(images)/zImage.initrd.chrp      $(TFTPIMAGE).chrp$(END)
	@echo '  kernel: $@ is ready ($<)'

