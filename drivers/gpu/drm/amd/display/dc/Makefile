#
# Copyright 2017 Advanced Micro Devices, Inc.
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
# THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
# OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
# ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
# OTHER DEALINGS IN THE SOFTWARE.
#
#
# Makefile for Display Core (dc) component.
#

DC_LIBS = basics bios calcs clk_mgr dce gpio irq virtual

ifdef CONFIG_DRM_AMD_DC_DCN2_0
DC_LIBS += dcn20
endif


ifdef CONFIG_DRM_AMD_DC_DSC_SUPPORT
DC_LIBS += dsc
endif

ifdef CONFIG_DRM_AMD_DC_DCN1_0
DC_LIBS += dcn10 dml
endif
ifdef CONFIG_DRM_AMD_DC_DCN2_1
DC_LIBS += dcn21
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

ifdef CONFIG_DRM_AMD_DC_DCN2_0
DISPLAY_CORE += dc_vm_helper.o
endif

AMD_DISPLAY_CORE = $(addprefix $(AMDDALPATH)/dc/core/,$(DISPLAY_CORE))

AMD_DM_REG_UPDATE = $(addprefix $(AMDDALPATH)/dc/,dc_helper.o)

AMD_DISPLAY_FILES += $(AMD_DISPLAY_CORE)
AMD_DISPLAY_FILES += $(AMD_DM_REG_UPDATE)



