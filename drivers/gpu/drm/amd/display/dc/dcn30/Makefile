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


DCN30 = dcn30_init.o dcn30_hubbub.o dcn30_hubp.o dcn30_dpp.o dcn30_optc.o \
	dcn30_dccg.o dcn30_hwseq.o dcn30_mpc.o dcn30_vpg.o \
	dcn30_afmt.o dcn30_dio_stream_encoder.o dcn30_dwb.o \
	dcn30_dpp_cm.o dcn30_dwb_cm.o dcn30_cm_common.o dcn30_mmhubbub.o \
	dcn30_dio_link_encoder.o dcn30_resource.o


ifdef CONFIG_X86
CFLAGS_$(AMDDALPATH)/dc/dcn30/dcn30_resource.o := -mhard-float -msse
CFLAGS_$(AMDDALPATH)/dc/dcn30/dcn30_optc.o := -mhard-float -msse
endif

ifdef CONFIG_PPC64
CFLAGS_$(AMDDALPATH)/dc/dcn30/dcn30_resource.o := -mhard-float -maltivec
CFLAGS_$(AMDDALPATH)/dc/dcn30/dcn30_optc.o := -mhard-float -maltivec
endif

ifdef CONFIG_ARM64
CFLAGS_REMOVE_$(AMDDALPATH)/dc/dcn30/dcn30_resource.o := -mgeneral-regs-only
CFLAGS_REMOVE_$(AMDDALPATH)/dc/dcn30/dcn30_optc.o := -mgeneral-regs-only
endif

ifdef CONFIG_CC_IS_GCC
ifeq ($(call cc-ifversion, -lt, 0701, y), y)
IS_OLD_GCC = 1
endif
endif

ifdef IS_OLD_GCC
# Stack alignment mismatch, proceed with caution.
# GCC < 7.1 cannot compile code using `double` and -mpreferred-stack-boundary=3
# (8B stack alignment).
CFLAGS_$(AMDDALPATH)/dc/dcn30/dcn30_resource.o += -mpreferred-stack-boundary=4
CFLAGS_$(AMDDALPATH)/dc/dcn30/dcn30_optc.o += -mpreferred-stack-boundary=4
else
CFLAGS_$(AMDDALPATH)/dc/dcn30/dcn30_resource.o += -msse2
CFLAGS_$(AMDDALPATH)/dc/dcn30/dcn30_optc.o += -msse2
endif

AMD_DAL_DCN30 = $(addprefix $(AMDDALPATH)/dc/dcn30/,$(DCN30))

AMD_DISPLAY_FILES += $(AMD_DAL_DCN30)
