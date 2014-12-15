#
# Makefile for sound control interface
#

# Toplevel Module Dependency
amlogic_amaudio-objs := amaudio.o

ifndef CONFIG_MESON_TRUSTZONE
ifneq ($(KBUILD_SRC),)
TOP_KBUILD_SRC := $(KBUILD_SRC)/
endif

$(obj)/audio_data.o:
	@cp $(TOP_KBUILD_SRC)drivers/amlogic/amaudio/audio_data.z \
        drivers/amlogic/amaudio/audio_data.o
#amlogic_amaudio-objs += audio_data.o
endif

obj-$(CONFIG_AMAUDIO) += amlogic_amaudio.o
