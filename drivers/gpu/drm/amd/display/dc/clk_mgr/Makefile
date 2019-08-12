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
# Makefile for the 'clk_mgr' sub-component of DAL.
# It provides the control and status of HW CLK_MGR pins.

CLK_MGR = clk_mgr.o

AMD_DAL_CLK_MGR = $(addprefix $(AMDDALPATH)/dc/clk_mgr/,$(CLK_MGR))

AMD_DISPLAY_FILES += $(AMD_DAL_CLK_MGR)


###############################################################################
# DCE 100 and DCE8x
###############################################################################
CLK_MGR_DCE100 = dce_clk_mgr.o

AMD_DAL_CLK_MGR_DCE100 = $(addprefix $(AMDDALPATH)/dc/clk_mgr/dce100/,$(CLK_MGR_DCE100))

AMD_DISPLAY_FILES += $(AMD_DAL_CLK_MGR_DCE100)

###############################################################################
# DCE 100 and DCE8x
###############################################################################
CLK_MGR_DCE110 = dce110_clk_mgr.o

AMD_DAL_CLK_MGR_DCE110 = $(addprefix $(AMDDALPATH)/dc/clk_mgr/dce110/,$(CLK_MGR_DCE110))

AMD_DISPLAY_FILES += $(AMD_DAL_CLK_MGR_DCE110)
###############################################################################
# DCE 112
###############################################################################
CLK_MGR_DCE112 = dce112_clk_mgr.o

AMD_DAL_CLK_MGR_DCE112 = $(addprefix $(AMDDALPATH)/dc/clk_mgr/dce112/,$(CLK_MGR_DCE112))

AMD_DISPLAY_FILES += $(AMD_DAL_CLK_MGR_DCE112)
###############################################################################
# DCE 120
###############################################################################
CLK_MGR_DCE120 = dce120_clk_mgr.o

AMD_DAL_CLK_MGR_DCE120 = $(addprefix $(AMDDALPATH)/dc/clk_mgr/dce120/,$(CLK_MGR_DCE120))

AMD_DISPLAY_FILES += $(AMD_DAL_CLK_MGR_DCE120)
ifdef CONFIG_DRM_AMD_DC_DCN1_0
###############################################################################
# DCN10
###############################################################################
CLK_MGR_DCN10 = rv1_clk_mgr.o rv1_clk_mgr_vbios_smu.o rv2_clk_mgr.o

AMD_DAL_CLK_MGR_DCN10 = $(addprefix $(AMDDALPATH)/dc/clk_mgr/dcn10/,$(CLK_MGR_DCN10))

AMD_DISPLAY_FILES += $(AMD_DAL_CLK_MGR_DCN10)
endif

ifdef CONFIG_DRM_AMD_DC_DCN2_0
###############################################################################
# DCN20
###############################################################################
CLK_MGR_DCN20 = dcn20_clk_mgr.o

AMD_DAL_CLK_MGR_DCN20 = $(addprefix $(AMDDALPATH)/dc/clk_mgr/dcn20/,$(CLK_MGR_DCN20))

AMD_DISPLAY_FILES += $(AMD_DAL_CLK_MGR_DCN20)
endif

