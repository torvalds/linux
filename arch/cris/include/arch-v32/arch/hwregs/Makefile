# SPDX-License-Identifier: GPL-2.0
# Makefile to generate or copy the latest register definitions
# and related datastructures and helpermacros.
# The official place for these files is at:
RELEASE ?= r1_alfa5
OFFICIAL_INCDIR = /n/asic/projects/guinness/releases/$(RELEASE)/design/top/sw/include/

# which is updated on each new release.
INCL_ASMFILES   =
INCL_FILES      = ata_defs.h
INCL_FILES     += bif_core_defs.h
INCL_ASMFILES  += bif_core_defs_asm.h
INCL_FILES     += bif_slave_defs.h
#INCL_FILES     += bif_slave_ext_defs.h
INCL_FILES     += config_defs.h
INCL_ASMFILES  += config_defs_asm.h
INCL_FILES     += cpu_vect.h
#INCL_FILES     += cris_defs.h
#INCL_FILES     += cris_supp_reg.h # In handcrafted supp_reg.h
INCL_FILES     += dma.h
INCL_FILES     += dma_defs.h
INCL_FILES     += eth_defs.h
INCL_FILES     += extmem_defs.h
INCL_FILES     += gio_defs.h
INCL_ASMFILES  += gio_defs_asm.h
INCL_FILES     += intr_vect.h
INCL_FILES     += intr_vect_defs.h
INCL_ASMFILES  += intr_vect_defs_asm.h
INCL_FILES     += marb_bp_defs.h
INCL_FILES     += marb_defs.h
INCL_ASMFILES  += mmu_defs_asm.h
#INCL_FILES     += mmu_supp_reg.h # In handcrafted supp_reg.h
#INCL_FILES     += par_defs.h # No useful content
INCL_FILES     += pinmux_defs.h
INCL_FILES     += reg_map.h
INCL_ASMFILES  += reg_map_asm.h
INCL_FILES     += reg_rdwr.h
INCL_FILES     += ser_defs.h
#INCL_FILES     += spec_reg.h # In handcrafted supp_reg.h
INCL_FILES     += sser_defs.h
INCL_FILES     += strcop_defs.h
#INCL_FILES     += strcop.h # Where is this?
INCL_FILES     += strmux_defs.h
#INCL_FILES     += supp_reg.h # Handcrafted instead
INCL_FILES     += timer_defs.h

REGDESC =
REGDESC += $(BASEDIR)/io/ata/rtl/ata_regs.r
REGDESC += $(BASEDIR)/io/bif/rtl/bif_core_regs.r
REGDESC += $(BASEDIR)/io/bif/rtl/bif_slave_regs.r
#REGDESC += $(BASEDIR)/io/bif/sw/bif_slave_ext_regs.r
REGDESC += $(DESIGNDIR)/top/rtl/config_regs.r
REGDESC += $(BASEDIR)/mod/dma_common/rtl/dma_regdes.r
REGDESC += $(BASEDIR)/io/eth/rtl/eth_regs.r
REGDESC += $(BASEDIR)/io/bif/mod/extmem/extmem_regs.r
REGDESC += $(DESIGNDIR)/gio/rtl/gio_regs.r
REGDESC += $(BASEDIR)/core/cpu/intr_vect/rtl/guinness/ivmask.config.r
REGDESC += $(BASEDIR)/core/memarb/rtl/guinness/marb_top.r
REGDESC += $(BASEDIR)/core/cpu/mmu/doc/mmu_regs.r
#REGDESC += $(BASEDIR)/io/par_port/rtl/par_regs.r
REGDESC += $(BASEDIR)/io/pinmux/rtl/guinness/pinmux_regs.r
REGDESC += $(BASEDIR)/io/ser/rtl/ser_regs.r
REGDESC += $(BASEDIR)/core/strcop/rtl/strcop_regs.r
REGDESC += $(BASEDIR)/io/strmux/rtl/guinness/strmux_regs.r
REGDESC += $(BASEDIR)/io/timer/rtl/timer_regs.r
#REGDESC += $(BASEDIR)/io/usb/usb1_1/rtl/usb_regs.r


BASEDIR = /n/asic/design
DESIGNDIR = /n/asic/projects/guinness/design
RDES2C = /n/asic/bin/rdes2c
RDES2C = /n/asic/design/tools/rdesc/rdes2c
RDES2INTR = /n/asic/design/tools/rdesc/rdes2intr
RDES2TXT = /n/asic/design/tools/rdesc/rdes2txt

## all    - Just print help - you probably want to do 'make gen'
all: help

# Disable implicit rule that may generate deleted files from RCS/ directory.
%.r:

%.h:

## help   - This help
help:
	@grep '^## ' Makefile

## gen    - Generate include files
gen: $(INCL_FILES) $(INCL_ASMFILES)

ata_defs.h: $(BASEDIR)/io/ata/rtl/ata_regs.r
	$(RDES2C) $<
config_defs.h: $(DESIGNDIR)/top/rtl/config_regs.r
	$(RDES2C) $<
config_defs_asm.h: $(DESIGNDIR)/top/rtl/config_regs.r
	$(RDES2C) -asm $<
# Can't generate cpu_vect.h yet
#cpu_vect.h: $(DESIGNDIR)/top/rtl/cpu_vect.r # ????
#	$(RDES2INTR) $<
cpu_vect.h: $(OFFICIAL_INCDIR)cpu_vect.h
	cat $< | sed -e 's/\$$Id\:/id\:/g' >$@
dma_defs.h: $(BASEDIR)/core/dma/rtl/common/dma_regdes.r
	$(RDES2C) $<
$(BASEDIR)/core/dma/sw/dma.h:
dma.h: $(BASEDIR)/core/dma/sw/dma.h
	cat $< | sed -e 's/\$$Id\:/id\:/g' >$@
eth_defs.h: $(BASEDIR)/io/eth/rtl/eth_regs.r
	$(RDES2C) $<
extmem_defs.h: $(BASEDIR)/io/bif/mod/extmem/extmem_regs.r
	$(RDES2C) $<
gio_defs.h: $(DESIGNDIR)/gio/rtl/gio_regs.r
	$(RDES2C) $<
intr_vect_defs.h: $(BASEDIR)/core/cpu/intr_vect/rtl/guinness/ivmask.config.r
	$(RDES2C) $<
intr_vect_defs_asm.h: $(BASEDIR)/core/cpu/intr_vect/rtl/guinness/ivmask.config.r
	$(RDES2C) -asm $<
# Can't generate intr_vect.h yet
#intr_vect.h: $(BASEDIR)/core/cpu/intr_vect/rtl/guinness/ivmask.config.r
#	$(RDES2INTR) $<
intr_vect.h: $(OFFICIAL_INCDIR)intr_vect.h
	cat $< | sed -e 's/\$$Id\:/id\:/g' >$@
mmu_defs_asm.h: $(BASEDIR)/core/cpu/mmu/doc/mmu_regs.r
	$(RDES2C) -asm $<
par_defs.h: $(BASEDIR)/io/par_port/rtl/par_regs.r
	$(RDES2C) $<

# From /n/asic/projects/guinness/design/
reg_map.h: $(DESIGNDIR)/top/rtl/global.rmap $(DESIGNDIR)/top/mod/modreg.rmap
	$(RDES2C) -base 0xb0000000 $^
reg_map_asm.h: $(DESIGNDIR)/top/rtl/global.rmap $(DESIGNDIR)/top/mod/modreg.rmap
	$(RDES2C) -base 0xb0000000 -asm -outfile $@ $^

reg_rdwr.h: $(DESIGNDIR)/top/sw/include/reg_rdwr.h
	cat $< | sed -e 's/\$$Id\:/id\:/g' >$@

ser_defs.h: $(BASEDIR)/io/ser/rtl/ser_regs.r
	$(RDES2C) $<
strcop_defs.h: $(BASEDIR)/core/strcop/rtl/strcop_regs.r
	$(RDES2C) $<
strcop.h: $(BASEDIR)/core/strcop/rtl/strcop.h
	cat $< | sed -e 's/\$$Id\:/id\:/g' >$@
strmux_defs.h: $(BASEDIR)/io/strmux/rtl/guinness/strmux_regs.r
	$(RDES2C) $<
timer_defs.h: $(BASEDIR)/io/timer/rtl/timer_regs.r
	$(RDES2C) $<
usb_defs.h: $(BASEDIR)/io/usb/usb1_1/rtl/usb_regs.r
	$(RDES2C) $<

## copy   - Copy files from official location
copy:
	@for HFILE in $(INCL_FILES); do \
		echo "  $$HFILE"; \
		cat $(OFFICIAL_INCDIR)$$HFILE | sed -e 's/\$$Id\:/id\:/g' > $$HFILE; \
	done
	@for HFILE in $(INCL_ASMFILES); do \
		echo "  $$HFILE"; \
		cat $(OFFICIAL_INCDIR)asm/$$HFILE | sed -e 's/\$$Id\:/id\:/g' > $$HFILE; \
	done
## ls_official - List official location
ls_official:
	(cd $(OFFICIAL_INCDIR); ls -l *.h )

## diff_official - Diff current directory with official location
diff_official:
	diff . $(OFFICIAL_INCDIR)

## doc    - Generate .axw files from register description.
doc: $(REGDESC)
	for RDES in $^; do \
		$(RDES2TXT) $$RDES; \
	done

.PHONY: axw
## %.axw  - Generate the specified .axw file (doesn't work for all files
##          due to inconsistent naming ir .r files.
%.axw: axw
	@for RDES in $(REGDESC); do \
		if echo "$$RDES" | grep $* ; then \
		  $(RDES2TXT) $$RDES; \
		fi \
	done

.PHONY: clean
## clean  - Remove .h files and .axw files.
clean:
	rm -rf $(INCL_FILES) *.axw

