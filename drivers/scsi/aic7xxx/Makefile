#
# Makefile for the Linux aic7xxx SCSI driver.
#
# $Id: //depot/linux-aic79xx-2.5.0/drivers/scsi/aic7xxx/Makefile#8 $
#

# Let kbuild descend into aicasm when cleaning
subdir-				+= aicasm

obj-$(CONFIG_SCSI_AIC7XXX)	+= aic7xxx.o
obj-$(CONFIG_SCSI_AIC79XX)	+= aic79xx.o

# Core Fast -> U160 files
aic7xxx-y					+= aic7xxx_core.o	\
						   aic7xxx_93cx6.o
aic7xxx-$(CONFIG_EISA)				+= aic7770.o
aic7xxx-$(CONFIG_PCI)				+= aic7xxx_pci.o
aic7xxx-$(CONFIG_AIC7XXX_REG_PRETTY_PRINT)	+= aic7xxx_reg_print.o

# Platform Specific Fast -> U160 Files
aic7xxx-y					+= aic7xxx_osm.o	\
						   aic7xxx_proc.o
aic7xxx-$(CONFIG_EISA)				+= aic7770_osm.o
aic7xxx-$(CONFIG_PCI)				+= aic7xxx_osm_pci.o

# Core U320 files
aic79xx-y					+= aic79xx_core.o	\
						   aic79xx_pci.o
aic79xx-$(CONFIG_AIC79XX_REG_PRETTY_PRINT)	+= aic79xx_reg_print.o

# Platform Specific U320 Files
aic79xx-y					+= aic79xx_osm.o	\
						   aic79xx_proc.o	\
						   aic79xx_osm_pci.o

EXTRA_CFLAGS += -Idrivers/scsi
ifdef WARNINGS_BECOME_ERRORS
EXTRA_CFLAGS += -Werror
endif
#EXTRA_CFLAGS += -g

# Files generated that shall be removed upon make clean
clean-files := aic7xxx_seq.h aic7xxx_reg.h aic7xxx_reg_print.c
clean-files += aic79xx_seq.h aic79xx_reg.h aic79xx_reg_print.c

# Dependencies for generated files need to be listed explicitly

$(obj)/aic7xxx_core.o: $(obj)/aic7xxx_seq.h
$(obj)/aic79xx_core.o: $(obj)/aic79xx_seq.h
$(obj)/aic79xx_reg_print.c: $(src)/aic79xx_reg_print.c_shipped
$(obj)/aic7xxx_reg_print.c: $(src)/aic7xxx_reg_print.c_shipped

$(addprefix $(obj)/,$(aic7xxx-y)): $(obj)/aic7xxx_reg.h
$(addprefix $(obj)/,$(aic79xx-y)): $(obj)/aic79xx_reg.h

aic7xxx-gen-$(CONFIG_AIC7XXX_BUILD_FIRMWARE)	:= $(obj)/aic7xxx_seq.h \
						   $(obj)/aic7xxx_reg.h
aic7xxx-gen-$(CONFIG_AIC7XXX_REG_PRETTY_PRINT)	+= $(obj)/aic7xxx_reg_print.c

aicasm-7xxx-opts-$(CONFIG_AIC7XXX_REG_PRETTY_PRINT) := \
	-p $(obj)/aic7xxx_reg_print.c -i aic7xxx_osm.h

ifeq ($(CONFIG_AIC7XXX_BUILD_FIRMWARE),y)
# Create a dependency chain in generated files
# to avoid concurrent invocations of the single
# rule that builds them all.
aic7xxx_seq.h: aic7xxx_reg.h
ifeq ($(CONFIG_AIC7XXX_REG_PRETTY_PRINT),y)
aic7xxx_reg.h: aic7xxx_reg_print.c
endif
$(aic7xxx-gen-y): $(src)/aic7xxx.seq $(src)/aic7xxx.reg $(obj)/aicasm/aicasm
	$(obj)/aicasm/aicasm -I$(src) -r $(obj)/aic7xxx_reg.h \
			      $(aicasm-7xxx-opts-y) -o $(obj)/aic7xxx_seq.h \
			      $(src)/aic7xxx.seq
endif

aic79xx-gen-$(CONFIG_AIC79XX_BUILD_FIRMWARE)	:= $(obj)/aic79xx_seq.h \
						   $(obj)/aic79xx_reg.h
aic79xx-gen-$(CONFIG_AIC79XX_REG_PRETTY_PRINT)	+= $(obj)/aic79xx_reg_print.c

aicasm-79xx-opts-$(CONFIG_AIC79XX_REG_PRETTY_PRINT) := \
	-p $(obj)/aic79xx_reg_print.c -i aic79xx_osm.h

ifeq ($(CONFIG_AIC79XX_BUILD_FIRMWARE),y)
# Create a dependency chain in generated files
# to avoid concurrent invocations of the single
# rule that builds them all.
aic79xx_seq.h: aic79xx_reg.h
ifeq ($(CONFIG_AIC79XX_REG_PRETTY_PRINT),y)
aic79xx_reg.h: aic79xx_reg_print.c
endif
$(aic79xx-gen-y): $(src)/aic79xx.seq $(src)/aic79xx.reg $(obj)/aicasm/aicasm
	$(obj)/aicasm/aicasm -I$(src) -r $(obj)/aic79xx_reg.h \
			      $(aicasm-79xx-opts-y) -o $(obj)/aic79xx_seq.h \
			      $(src)/aic79xx.seq
endif

$(obj)/aicasm/aicasm: $(src)/aicasm/*.[chyl]
	$(MAKE) -C $(src)/aicasm
