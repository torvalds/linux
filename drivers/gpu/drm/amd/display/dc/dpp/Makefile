
# Copyright 2022 Advanced Micro Devices, Inc.
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
# Makefile for the 'dpp' sub-component of DAL.
#
ifdef CONFIG_DRM_AMD_DC_FP
###############################################################################
# DCN
###############################################################################

DPP_DCN10 = dcn10_dpp.o dcn10_dpp_dscl.o dcn10_dpp_cm.o

AMD_DAL_DPP_DCN10 = $(addprefix $(AMDDALPATH)/dc/dpp/dcn10/,$(DPP_DCN10))

AMD_DISPLAY_FILES += $(AMD_DAL_DPP_DCN10)

###############################################################################

DPP_DCN20 = dcn20_dpp.o dcn20_dpp_cm.o

AMD_DAL_DPP_DCN20 = $(addprefix $(AMDDALPATH)/dc/dpp/dcn20/,$(DPP_DCN20))

AMD_DISPLAY_FILES += $(AMD_DAL_DPP_DCN20)

###############################################################################

DPP_DCN201 = dcn201_dpp.o

AMD_DAL_DPP_DCN201 = $(addprefix $(AMDDALPATH)/dc/dpp/dcn201/,$(DPP_DCN201))

AMD_DISPLAY_FILES += $(AMD_DAL_DPP_DCN201)

###############################################################################

DPP_DCN30 = dcn30_dpp.o dcn30_dpp_cm.o

AMD_DAL_DPP_DCN30 = $(addprefix $(AMDDALPATH)/dc/dpp/dcn30/,$(DPP_DCN30))

AMD_DISPLAY_FILES += $(AMD_DAL_DPP_DCN30)

###############################################################################

DPP_DCN32 = dcn32_dpp.o

AMD_DAL_DPP_DCN32 = $(addprefix $(AMDDALPATH)/dc/dpp/dcn32/,$(DPP_DCN32))

AMD_DISPLAY_FILES += $(AMD_DAL_DPP_DCN32)

###############################################################################

DPP_DCN35 = dcn35_dpp.o

AMD_DAL_DPP_DCN35 = $(addprefix $(AMDDALPATH)/dc/dpp/dcn35/,$(DPP_DCN35))

AMD_DISPLAY_FILES += $(AMD_DAL_DPP_DCN35)

###############################################################################

DPP_DCN401 = dcn401_dpp.o dcn401_dpp_cm.o dcn401_dpp_dscl.o

AMD_DAL_DPP_DCN401 = $(addprefix $(AMDDALPATH)/dc/dpp/dcn401/,$(DPP_DCN401))

AMD_DISPLAY_FILES += $(AMD_DAL_DPP_DCN401)

endif
