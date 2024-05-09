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

DC_LIBS = basics bios dml clk_mgr dce gpio hwss irq link virtual dsc

ifdef CONFIG_DRM_AMD_DC_FP

KCOV_INSTRUMENT := n

DC_LIBS += dcn10
DC_LIBS += dcn20
DC_LIBS += dcn21
DC_LIBS += dcn201
DC_LIBS += dcn30
DC_LIBS += dcn301
DC_LIBS += dcn302
DC_LIBS += dcn303
DC_LIBS += dcn31
DC_LIBS += dcn314
DC_LIBS += dcn315
DC_LIBS += dcn316
DC_LIBS += dcn32
DC_LIBS += dcn321
DC_LIBS += dcn35
DC_LIBS += dml
DC_LIBS += dml2
endif

DC_LIBS += dce120

DC_LIBS += dce112
DC_LIBS += dce110
DC_LIBS += dce100
DC_LIBS += dce80

ifdef CONFIG_DRM_AMD_DC_SI
DC_LIBS += dce60
endif

DC_LIBS += hdcp

AMD_DC = $(addsuffix /Makefile, $(addprefix $(FULL_AMD_DISPLAY_PATH)/dc/,$(DC_LIBS)))

include $(AMD_DC)

DISPLAY_CORE = dc.o dc_stat.o dc_resource.o dc_hw_sequencer.o dc_sink.o \
dc_surface.o dc_debug.o dc_stream.o dc_link_enc_cfg.o dc_link_exports.o

DISPLAY_CORE += dc_vm_helper.o

AMD_DISPLAY_CORE = $(addprefix $(AMDDALPATH)/dc/core/,$(DISPLAY_CORE))

AMD_DM_REG_UPDATE = $(addprefix $(AMDDALPATH)/dc/,dc_helper.o)

AMD_DISPLAY_FILES += $(AMD_DISPLAY_CORE)
AMD_DISPLAY_FILES += $(AMD_DM_REG_UPDATE)

DC_DMUB += dc_dmub_srv.o
DC_EDID += dc_edid_parser.o
AMD_DISPLAY_DMUB = $(addprefix $(AMDDALPATH)/dc/,$(DC_DMUB))
AMD_DISPLAY_EDID = $(addprefix $(AMDDALPATH)/dc/,$(DC_EDID))
AMD_DISPLAY_FILES += $(AMD_DISPLAY_DMUB) $(AMD_DISPLAY_EDID)

