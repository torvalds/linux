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

subdir-ccflags-y += \
		-I$(FULL_AMD_PATH)/include/asic_reg  \
		-I$(FULL_AMD_PATH)/include  \
		-I$(FULL_AMD_PATH)/pm/inc/  \
		-I$(FULL_AMD_PATH)/pm/swsmu \
		-I$(FULL_AMD_PATH)/pm/swsmu/inc \
		-I$(FULL_AMD_PATH)/pm/swsmu/inc/pmfw_if \
		-I$(FULL_AMD_PATH)/pm/swsmu/smu11 \
		-I$(FULL_AMD_PATH)/pm/swsmu/smu12 \
		-I$(FULL_AMD_PATH)/pm/swsmu/smu13 \
		-I$(FULL_AMD_PATH)/pm/powerplay/inc \
		-I$(FULL_AMD_PATH)/pm/powerplay/smumgr\
		-I$(FULL_AMD_PATH)/pm/powerplay/hwmgr \
		-I$(FULL_AMD_PATH)/pm/legacy-dpm

AMD_PM_PATH = ../pm

PM_LIBS = swsmu powerplay legacy-dpm

AMD_PM = $(addsuffix /Makefile,$(addprefix $(FULL_AMD_PATH)/pm/,$(PM_LIBS)))

include $(AMD_PM)

PM_MGR = amdgpu_dpm.o amdgpu_pm.o amdgpu_dpm_internal.o

AMD_PM_POWER = $(addprefix $(AMD_PM_PATH)/,$(PM_MGR))

AMD_POWERPLAY_FILES += $(AMD_PM_POWER)
