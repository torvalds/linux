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

DC_LIBS = basics bios dml clk_mgr dce gpio hwss irq link virtual dsc resource optc dpp hubbub dccg hubp dio dwb hpo mmhubbub mpc opp pg

ifdef CONFIG_DRM_AMD_DC_FP

KCOV_INSTRUMENT := n

DC_LIBS += dcn10
DC_LIBS += dcn20
DC_LIBS += dcn21
DC_LIBS += dcn201
DC_LIBS += dcn30
DC_LIBS += dcn301
DC_LIBS += dcn31
DC_LIBS += dml
DC_LIBS += dml2
DC_LIBS += soc_and_ip_translator
endif

DC_LIBS += dce120

DC_LIBS += dce112
DC_LIBS += dce110
DC_LIBS += dce80

ifdef CONFIG_DRM_AMD_DC_SI
DC_LIBS += dce60
endif

DC_LIBS += hdcp

ifdef CONFIG_DRM_AMD_DC_FP
DC_LIBS += sspl
AMD_DISPLAY_FILES += $(addprefix $(AMDDALPATH)/dc/, dc_spl_translate.o)
endif

AMD_DC = $(addsuffix /Makefile, $(addprefix $(FULL_AMD_DISPLAY_PATH)/dc/,$(DC_LIBS)))

include $(AMD_DC)

FILES =
FILES += dc_dmub_srv.o
FILES += dc_edid_parser.o
FILES += dc_fused_io.o
FILES += dc_helper.o
FILES += core/dc.o
FILES += core/dc_debug.o
FILES += core/dc_hw_sequencer.o
FILES += core/dc_link_enc_cfg.o
FILES += core/dc_link_exports.o
FILES += core/dc_resource.o
FILES += core/dc_sink.o
FILES += core/dc_stat.o
FILES += core/dc_state.o
FILES += core/dc_stream.o
FILES += core/dc_surface.o
FILES += core/dc_vm_helper.o

AMD_DISPLAY_FILES += $(addprefix $(AMDDALPATH)/dc/, $(FILES))

