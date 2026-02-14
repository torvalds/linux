# SPDX-License-Identifier: GPL-2.0
# Makefile for the Linux logos

obj-$(CONFIG_LOGO)			+= logo.o
obj-$(CONFIG_LOGO_LINUX_MONO)		+= logo_linux_mono.o
obj-$(CONFIG_LOGO_LINUX_VGA16)		+= logo_linux_vga16.o
obj-$(CONFIG_LOGO_LINUX_CLUT224)	+= logo_linux_clut224.o

obj-$(CONFIG_SPU_BASE)			+= logo_spe_clut224.o

# How to generate logo's

hostprogs := pnmtologo

# Create commands like "pnmtologo -t mono -n logo_linux_mono -o ..."
quiet_cmd_logo = LOGO    $@
      cmd_logo = $(obj)/pnmtologo -t $2 -n $(basename $(notdir $@)) -o $@ $<

$(obj)/logo_linux_mono.c: $(CONFIG_LOGO_LINUX_MONO_FILE) $(obj)/pnmtologo FORCE
	$(call if_changed,logo,mono)

$(obj)/logo_linux_vga16.c: $(CONFIG_LOGO_LINUX_VGA16_FILE) $(obj)/pnmtologo FORCE
	$(call if_changed,logo,vga16)

$(obj)/logo_linux_clut224.c: $(CONFIG_LOGO_LINUX_CLUT224_FILE) $(obj)/pnmtologo FORCE
	$(call if_changed,logo,clut224)

$(obj)/%_clut224.c: $(src)/%_clut224.ppm $(obj)/pnmtologo FORCE
	$(call if_changed,logo,clut224)

# generated C files
targets += *_mono.c *_vga16.c *_clut224.c
