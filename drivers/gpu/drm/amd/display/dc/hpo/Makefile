#
# Copyright 2020 Advanced Micro Devices, Inc.
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
# Authors: AMD
#
#

ifdef CONFIG_DRM_AMD_DC_FP
###############################################################################
# DCN30
###############################################################################

AMD_DAL_HPO_DCN30 = $(addprefix $(AMDDALPATH)/dc/hpo/dcn30/,$(HPO_DCN30))

AMD_DISPLAY_FILES += $(AMD_DAL_HPO_DCN30)
###############################################################################
# DCN31
###############################################################################
HPO_DCN31 = dcn31_hpo_dp_stream_encoder.o dcn31_hpo_dp_link_encoder.o

AMD_DAL_HPO_DCN31 = $(addprefix $(AMDDALPATH)/dc/hpo/dcn31/,$(HPO_DCN31))

AMD_DISPLAY_FILES += $(AMD_DAL_HPO_DCN31)
###############################################################################
# DCN32
###############################################################################
HPO_DCN32 = dcn32_hpo_dp_link_encoder.o

AMD_DAL_HPO_DCN32 = $(addprefix $(AMDDALPATH)/dc/hpo/dcn32/,$(HPO_DCN32))

AMD_DISPLAY_FILES += $(AMD_DAL_HPO_DCN32)
endif
