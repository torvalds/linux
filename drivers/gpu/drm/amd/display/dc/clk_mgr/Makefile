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


ifdef CONFIG_DRM_AMD_DC_SI
###############################################################################
# DCE 60
###############################################################################
CLK_MGR_DCE60 = dce60_clk_mgr.o

AMD_DAL_CLK_MGR_DCE60 = $(addprefix $(AMDDALPATH)/dc/clk_mgr/dce60/,$(CLK_MGR_DCE60))

AMD_DISPLAY_FILES += $(AMD_DAL_CLK_MGR_DCE60)
endif

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
ifdef CONFIG_DRM_AMD_DC_FP
###############################################################################
# DCN10
###############################################################################
CLK_MGR_DCN10 = rv1_clk_mgr.o rv1_clk_mgr_vbios_smu.o rv2_clk_mgr.o

AMD_DAL_CLK_MGR_DCN10 = $(addprefix $(AMDDALPATH)/dc/clk_mgr/dcn10/,$(CLK_MGR_DCN10))

AMD_DISPLAY_FILES += $(AMD_DAL_CLK_MGR_DCN10)

###############################################################################
# DCN20
###############################################################################
CLK_MGR_DCN20 = dcn20_clk_mgr.o

AMD_DAL_CLK_MGR_DCN20 = $(addprefix $(AMDDALPATH)/dc/clk_mgr/dcn20/,$(CLK_MGR_DCN20))

AMD_DISPLAY_FILES += $(AMD_DAL_CLK_MGR_DCN20)

###############################################################################
# DCN201
###############################################################################
CLK_MGR_DCN201 = dcn201_clk_mgr.o

AMD_DAL_CLK_MGR_DCN201 = $(addprefix $(AMDDALPATH)/dc/clk_mgr/dcn201/,$(CLK_MGR_DCN201))

AMD_DISPLAY_FILES += $(AMD_DAL_CLK_MGR_DCN201)

###############################################################################
# DCN21
###############################################################################
CLK_MGR_DCN21 = rn_clk_mgr.o rn_clk_mgr_vbios_smu.o

AMD_DAL_CLK_MGR_DCN21 = $(addprefix $(AMDDALPATH)/dc/clk_mgr/dcn21/,$(CLK_MGR_DCN21))

AMD_DISPLAY_FILES += $(AMD_DAL_CLK_MGR_DCN21)
###############################################################################
# DCN30
###############################################################################
CLK_MGR_DCN30 = dcn30_clk_mgr.o dcn30_clk_mgr_smu_msg.o

AMD_DAL_CLK_MGR_DCN30 = $(addprefix $(AMDDALPATH)/dc/clk_mgr/dcn30/,$(CLK_MGR_DCN30))

AMD_DISPLAY_FILES += $(AMD_DAL_CLK_MGR_DCN30)
###############################################################################
# DCN301
###############################################################################
CLK_MGR_DCN301 = vg_clk_mgr.o dcn301_smu.o

AMD_DAL_CLK_MGR_DCN301 = $(addprefix $(AMDDALPATH)/dc/clk_mgr/dcn301/,$(CLK_MGR_DCN301))

AMD_DISPLAY_FILES += $(AMD_DAL_CLK_MGR_DCN301)

###############################################################################
# DCN31
###############################################################################
CLK_MGR_DCN31 = dcn31_smu.o dcn31_clk_mgr.o

AMD_DAL_CLK_MGR_DCN31 = $(addprefix $(AMDDALPATH)/dc/clk_mgr/dcn31/,$(CLK_MGR_DCN31))

AMD_DISPLAY_FILES += $(AMD_DAL_CLK_MGR_DCN31)

###############################################################################
# DCN314
###############################################################################
CLK_MGR_DCN314 = dcn314_smu.o dcn314_clk_mgr.o

AMD_DAL_CLK_MGR_DCN314 = $(addprefix $(AMDDALPATH)/dc/clk_mgr/dcn314/,$(CLK_MGR_DCN314))

AMD_DISPLAY_FILES += $(AMD_DAL_CLK_MGR_DCN314)

###############################################################################
# DCN315
###############################################################################
CLK_MGR_DCN315 = dcn315_smu.o dcn315_clk_mgr.o

AMD_DAL_CLK_MGR_DCN315 = $(addprefix $(AMDDALPATH)/dc/clk_mgr/dcn315/,$(CLK_MGR_DCN315))

AMD_DISPLAY_FILES += $(AMD_DAL_CLK_MGR_DCN315)

###############################################################################
# DCN316
###############################################################################
CLK_MGR_DCN316 = dcn316_smu.o dcn316_clk_mgr.o

AMD_DAL_CLK_MGR_DCN316 = $(addprefix $(AMDDALPATH)/dc/clk_mgr/dcn316/,$(CLK_MGR_DCN316))

AMD_DISPLAY_FILES += $(AMD_DAL_CLK_MGR_DCN316)

###############################################################################
# DCN32
###############################################################################
CLK_MGR_DCN32 = dcn32_clk_mgr.o dcn32_clk_mgr_smu_msg.o

AMD_DAL_CLK_MGR_DCN32 = $(addprefix $(AMDDALPATH)/dc/clk_mgr/dcn32/,$(CLK_MGR_DCN32))

AMD_DISPLAY_FILES += $(AMD_DAL_CLK_MGR_DCN32)

endif
