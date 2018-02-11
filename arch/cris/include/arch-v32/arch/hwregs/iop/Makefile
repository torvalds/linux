# SPDX-License-Identifier: GPL-2.0
# $Id: Makefile,v 1.3 2004/01/07 20:34:55 johana Exp $
# Makefile to generate or copy the latest register definitions
# and related datastructures and helpermacros.
# The official place for these files is probably at:
RELEASE ?= r1_alfa5
IOPOFFICIAL_INCDIR = /n/asic/projects/guinness/releases/$(RELEASE)/design/top/sw/include/

IOPROCDIR = /n/asic/design/io/io_proc/rtl

IOPROCINCL_FILES =
IOPROCINCL_FILES2=
IOPROCINCL_FILES += iop_crc_par_defs.h
IOPROCINCL_FILES += iop_dmc_in_defs.h
IOPROCINCL_FILES += iop_dmc_out_defs.h
IOPROCINCL_FILES += iop_fifo_in_defs.h
IOPROCINCL_FILES += iop_fifo_in_xtra_defs.h
IOPROCINCL_FILES += iop_fifo_out_defs.h
IOPROCINCL_FILES += iop_fifo_out_xtra_defs.h
IOPROCINCL_FILES += iop_mpu_defs.h
IOPROCINCL_FILES2+= iop_mpu_macros.h
IOPROCINCL_FILES2+= iop_reg_space.h
IOPROCINCL_FILES += iop_sap_in_defs.h
IOPROCINCL_FILES += iop_sap_out_defs.h
IOPROCINCL_FILES += iop_scrc_in_defs.h
IOPROCINCL_FILES += iop_scrc_out_defs.h
IOPROCINCL_FILES += iop_spu_defs.h
# in guiness/
IOPROCINCL_FILES += iop_sw_cfg_defs.h
IOPROCINCL_FILES += iop_sw_cpu_defs.h
IOPROCINCL_FILES += iop_sw_mpu_defs.h
IOPROCINCL_FILES += iop_sw_spu_defs.h
#
IOPROCINCL_FILES += iop_timer_grp_defs.h
IOPROCINCL_FILES += iop_trigger_grp_defs.h
# in guiness/
IOPROCINCL_FILES += iop_version_defs.h

IOPROCASMINCL_FILES = $(patsubst %_defs.h,%_defs_asm.h,$(IOPROCINCL_FILES))
IOPROCASMINCL_FILES+= iop_reg_space_asm.h


IOPROCREGDESC =
IOPROCREGDESC += $(IOPROCDIR)/iop_crc_par.r
#IOPROCREGDESC += $(IOPROCDIR)/iop_crc_ser.r
IOPROCREGDESC += $(IOPROCDIR)/iop_dmc_in.r
IOPROCREGDESC += $(IOPROCDIR)/iop_dmc_out.r
IOPROCREGDESC += $(IOPROCDIR)/iop_fifo_in.r
IOPROCREGDESC += $(IOPROCDIR)/iop_fifo_in_xtra.r
IOPROCREGDESC += $(IOPROCDIR)/iop_fifo_out.r
IOPROCREGDESC += $(IOPROCDIR)/iop_fifo_out_xtra.r
IOPROCREGDESC += $(IOPROCDIR)/iop_mpu.r
IOPROCREGDESC += $(IOPROCDIR)/iop_sap_in.r
IOPROCREGDESC += $(IOPROCDIR)/iop_sap_out.r
IOPROCREGDESC += $(IOPROCDIR)/iop_scrc_in.r
IOPROCREGDESC += $(IOPROCDIR)/iop_scrc_out.r
IOPROCREGDESC += $(IOPROCDIR)/iop_spu.r
IOPROCREGDESC += $(IOPROCDIR)/guinness/iop_sw_cfg.r
IOPROCREGDESC += $(IOPROCDIR)/guinness/iop_sw_cpu.r
IOPROCREGDESC += $(IOPROCDIR)/guinness/iop_sw_mpu.r
IOPROCREGDESC += $(IOPROCDIR)/guinness/iop_sw_spu.r
IOPROCREGDESC += $(IOPROCDIR)/iop_timer_grp.r
IOPROCREGDESC += $(IOPROCDIR)/iop_trigger_grp.r
IOPROCREGDESC += $(IOPROCDIR)/guinness/iop_version.r


RDES2C = /n/asic/bin/rdes2c
RDES2C = /n/asic/design/tools/rdesc/rdes2c
RDES2INTR = /n/asic/design/tools/rdesc/rdes2intr
RDES2TXT = /n/asic/design/tools/rdesc/rdes2txt

## all    - Just print help - you probably want to do 'make gen'
all: help

## help   - This help
help:
	@grep '^## ' Makefile

## gen    - Generate include files
gen: $(IOPROCINCL_FILES) $(IOPROCINCL_FILES2) $(IOPROCASMINCL_FILES)
	echo "INCL: $(IOPROCINCL_FILES)"
	echo "INCL2: $(IOPROCINCL_FILES2)"
	echo "ASMINCL: $(IOPROCASMINCL_FILES)"

# From the official location...
iop_reg_space.h: $(IOPOFFICIAL_INCDIR)/iop_reg_space.h
	cat $< | sed -e 's/\$$Id\:/id\:/g' >$@
iop_mpu_macros.h: $(IOPOFFICIAL_INCDIR)/iop_mpu_macros.h
	cat $< | sed -e 's/\$$Id\:/id\:/g' >$@

## copy   - Copy files from official location
copy:
	@echo "## Copying and fixing iop files ##"
	@for HFILE in $(IOPROCINCL_FILES); do \
		echo "  $$HFILE"; \
		cat $(IOPOFFICIAL_INCDIR)$$HFILE | sed -e 's/\$$Id\:/id\:/g' > $$HFILE; \
	done
	@for HFILE in $(IOPROCINCL_FILES2); do \
		echo "  $$HFILE"; \
		cat $(IOPOFFICIAL_INCDIR)$$HFILE | sed -e 's/\$$Id\:/id\:/g' > $$HFILE; \
	done
	@echo "## Copying and fixing iop asm files ##"
	@for HFILE in $(IOPROCASMINCL_FILES); do \
		echo "  $$HFILE"; \
		cat $(IOPOFFICIAL_INCDIR)asm/$$HFILE | sed -e 's/\$$Id\:/id\:/g' > asm/$$HFILE; \
	done

# I/O processor files:
## iop    - Generate I/O processor include files
iop: $(IOPROCINCL_FILES) $(IOPROCINCL_FILES2) $(IOPROCASMINCL_FILES)
iop_sw_%_defs.h: $(IOPROCDIR)/guinness/iop_sw_%.r
	$(RDES2C) $<
iop_version_defs.h: $(IOPROCDIR)/guinness/iop_version.r
	$(RDES2C) $<
%_defs.h: $(IOPROCDIR)/%.r
	$(RDES2C) $<
%_defs_asm.h: $(IOPROCDIR)/%.r
	$(RDES2C) -asm $<
iop_version_defs_asm.h: $(IOPROCDIR)/guinness/iop_version.r
	$(RDES2C) -asm $<

## doc    - Generate .axw files from register description.
doc: $(IOPROCREGDESC)
	for RDES in $^; do \
		$(RDES2TXT) $$RDES; \
	done

.PHONY: axw
## %.axw  - Generate the specified .axw file (doesn't work for all files
##          due to inconsistent naming of .r files.
%.axw: axw
	@for RDES in $(IOPROCREGDESC); do \
		if echo "$$RDES" | grep $* ; then \
		  $(RDES2TXT) $$RDES; \
		fi \
	done

.PHONY: clean
## clean  - Remove .h files and .axw files.
clean:
	rm -rf $(IOPROCINCL_FILES) *.axw

.PHONY: cleandoc
## cleandoc  - Remove .axw files.
cleandoc:
	rm -rf *.axw

