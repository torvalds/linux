# SPDX-License-Identifier: GPL-2.0
#
# This file contains the font map for the default (hardware) font
#
FONTMAPFILE = cp437.uni

obj-$(CONFIG_VT)			+= vt_ioctl.o vc_screen.o \
					   selection.o keyboard.o \
					   vt.o defkeymap.o
obj-$(CONFIG_CONSOLE_TRANSLATIONS)	+= consolemap.o consolemap_deftbl.o \
					   ucs.o

# Files generated that shall be removed upon make clean
clean-files :=	consolemap_deftbl.c defkeymap.c \
		ucs_width_table.h ucs_recompose_table.h ucs_fallback_table.h

hostprogs += conmakehash

quiet_cmd_conmk = CONMK   $@
      cmd_conmk = $(obj)/conmakehash $< > $@

$(obj)/consolemap_deftbl.c: $(src)/$(FONTMAPFILE) $(obj)/conmakehash
	$(call cmd,conmk)

$(obj)/defkeymap.o:  $(obj)/defkeymap.c

# Uncomment if you're changing the keymap and have an appropriate
# loadkeys version for the map. By default, we'll use the shipped
# versions.
# GENERATE_KEYMAP := 1

ifdef GENERATE_KEYMAP

$(obj)/defkeymap.c: $(obj)/%.c: $(src)/%.map
	loadkeys --mktable --unicode $< > $@

endif

$(obj)/ucs.o:	$(src)/ucs.c $(obj)/ucs_width_table.h \
		$(obj)/ucs_recompose_table.h $(obj)/ucs_fallback_table.h

# You may uncomment one of those to have the UCS tables be regenerated
# during the build process. By default the _shipped versions are used.
#
#GENERATE_UCS_TABLES := 1
#GENERATE_UCS_TABLES := 2  # invokes gen_ucs_recompose_table.py with --full

ifdef GENERATE_UCS_TABLES

$(obj)/ucs_width_table.h: $(src)/gen_ucs_width_table.py
	$(PYTHON3) $< -o $@

ifeq ($(GENERATE_UCS_TABLES),2)
gen_recomp_arg := --full
else
gen_recomp_arg :=
endif

$(obj)/ucs_recompose_table.h: $(src)/gen_ucs_recompose_table.py
	$(PYTHON3) $< -o $@ $(gen_recomp_arg)

$(obj)/ucs_fallback_table.h: $(src)/gen_ucs_fallback_table.py
	$(PYTHON3) $< -o $@

endif
