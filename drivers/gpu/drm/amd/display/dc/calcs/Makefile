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
# Makefile for the 'calcs' sub-component of DAL.
# It calculates Bandwidth and Watermarks values for HW programming
#

CFLAGS_dcn_calcs.o := -mhard-float -msse -mpreferred-stack-boundary=4
CFLAGS_dcn_calc_auto.o := -mhard-float -msse -mpreferred-stack-boundary=4
CFLAGS_dcn_calc_math.o := -mhard-float -msse -mpreferred-stack-boundary=4 -Wno-tautological-compare

BW_CALCS = dce_calcs.o bw_fixed.o custom_float.o

ifdef CONFIG_DRM_AMD_DC_DCN1_0
BW_CALCS += dcn_calcs.o dcn_calc_math.o dcn_calc_auto.o
endif

AMD_DAL_BW_CALCS = $(addprefix $(AMDDALPATH)/dc/calcs/,$(BW_CALCS))

AMD_DISPLAY_FILES += $(AMD_DAL_BW_CALCS)
