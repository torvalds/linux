# Makefile for the Linux logos

obj-$(CONFIG_LOGO)			+= logo.o
obj-$(CONFIG_LOGO_LINUX_MONO)		+= logo_linux_mono.o
obj-$(CONFIG_LOGO_LINUX_VGA16)		+= logo_linux_vga16.o
obj-$(CONFIG_LOGO_LINUX_CLUT224)	+= logo_linux_clut224.o
obj-$(CONFIG_LOGO_BLACKFIN_CLUT224)	+= logo_blackfin_clut224.o
obj-$(CONFIG_LOGO_BLACKFIN_VGA16)	+= logo_blackfin_vga16.o
obj-$(CONFIG_LOGO_DEC_CLUT224)		+= logo_dec_clut224.o
obj-$(CONFIG_LOGO_MAC_CLUT224)		+= logo_mac_clut224.o
obj-$(CONFIG_LOGO_PARISC_CLUT224)	+= logo_parisc_clut224.o
obj-$(CONFIG_LOGO_SGI_CLUT224)		+= logo_sgi_clut224.o
obj-$(CONFIG_LOGO_SUN_CLUT224)		+= logo_sun_clut224.o
obj-$(CONFIG_LOGO_SUPERH_MONO)		+= logo_superh_mono.o
obj-$(CONFIG_LOGO_SUPERH_VGA16)		+= logo_superh_vga16.o
obj-$(CONFIG_LOGO_SUPERH_CLUT224)	+= logo_superh_clut224.o
obj-$(CONFIG_LOGO_M32R_CLUT224)		+= logo_m32r_clut224.o
obj-$(CONFIG_LOGO_G3_CLUT224)		+= logo_g3_clut224.o
obj-$(CONFIG_LOGO_LINUX_BMP_SUNSET)	+= logo_sunset_bmp.o


obj-$(CONFIG_LOGO_CHARGER_CLUT224)         += logo_charger00_clut224.o logo_charger01_clut224.o logo_charger02_clut224.o logo_charger03_clut224.o logo_charger04_clut224.o logo_charger05_clut224.o logo_charger06_clut224.o logo_charger07_clut224.o logo_charger08_clut224.o 

obj-$(CONFIG_SPU_BASE)			+= logo_spe_clut224.o

# How to generate logo's

# Use logo-cfiles to retrieve list of .c files to be built
logo-cfiles = $(notdir $(patsubst %.$(2), %.c, \
              $(wildcard $(srctree)/$(src)/*$(1).$(2))))


# Mono logos
extra-y += $(call logo-cfiles,_mono,pbm)

# VGA16 logos
extra-y += $(call logo-cfiles,_vga16,ppm)

# 224 Logos
extra-y += $(call logo-cfiles,_clut224,ppm)

# Gray 256
extra-y += $(call logo-cfiles,_gray256,pgm)

extra-y += $(call logo-cfiles,_bmp,bmp)

bmptologo := scripts/bmptologo
pnmtologo := scripts/pnmtologo

# Create commands like "pnmtologo -t mono -n logo_mac_mono -o ..."
quiet_cmd_logo = LOGO    $@
	cmd_logo = $(pnmtologo) \
			-t $(patsubst $*_%,%,$(notdir $(basename $<))) \
			-n $(notdir $(basename $<)) -o $@ $<

quiet_cmd_bmplogo = LOGO    $@
	cmd_bmplogo = $(bmptologo) \
			-t $(patsubst $*_%,%,$(notdir $(basename $<))) \
			-n $(notdir $(basename $<)) -o $@ $<

$(obj)/%_mono.c: $(src)/%_mono.pbm $(pnmtologo) FORCE
	$(call if_changed,logo)

$(obj)/%_vga16.c: $(src)/%_vga16.ppm $(pnmtologo) FORCE
	$(call if_changed,logo)

$(obj)/%_clut224.c: $(src)/%_clut224.ppm $(pnmtologo) FORCE
	$(call if_changed,logo)

$(obj)/%_gray256.c: $(src)/%_gray256.pgm $(pnmtologo) FORCE
	$(call if_changed,logo)

$(obj)/%_bmp.c: $(src)/%_bmp.bmp $(bmptologo) FORCE
	$(call if_changed,bmplogo)

# Files generated that shall be removed upon make clean
clean-files := *.o *_mono.c *_vga16.c *_clut224.c *_gray256.c *_bmp.c
