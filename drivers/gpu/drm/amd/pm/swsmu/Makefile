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

AMD_SWSMU_PATH = ../pm/swsmu

SWSMU_LIBS = smu11 smu12 smu13

AMD_SWSMU = $(addsuffix /Makefile,$(addprefix $(FULL_AMD_PATH)/pm/swsmu/,$(SWSMU_LIBS)))

include $(AMD_SWSMU)

SWSMU_MGR = amdgpu_smu.o \
	    smu_cmn.o \

AMD_SWSMU_POWER = $(addprefix $(AMD_SWSMU_PATH)/,$(SWSMU_MGR))

AMD_POWERPLAY_FILES += $(AMD_SWSMU_POWER)
