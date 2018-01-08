#
# Makefile for Display Core (dc) component.
#

DC_LIBS = basics bios calcs dce gpio i2caux irq virtual

ifdef CONFIG_DRM_AMD_DC_DCN1_0
DC_LIBS += dcn10 dml
endif

DC_LIBS += dce120

DC_LIBS += dce112
DC_LIBS += dce110
DC_LIBS += dce100
DC_LIBS += dce80

AMD_DC = $(addsuffix /Makefile, $(addprefix $(FULL_AMD_DISPLAY_PATH)/dc/,$(DC_LIBS)))

include $(AMD_DC)

DISPLAY_CORE = dc.o dc_link.o dc_resource.o dc_hw_sequencer.o dc_sink.o \
dc_surface.o dc_link_hwss.o dc_link_dp.o dc_link_ddc.o dc_debug.o dc_stream.o

AMD_DISPLAY_CORE = $(addprefix $(AMDDALPATH)/dc/core/,$(DISPLAY_CORE))

AMD_DM_REG_UPDATE = $(addprefix $(AMDDALPATH)/dc/,dc_helper.o)

AMD_DISPLAY_FILES += $(AMD_DISPLAY_CORE)
AMD_DISPLAY_FILES += $(AMD_DM_REG_UPDATE)



