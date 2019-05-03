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
# Makefile for the DAL (Display Abstract Layer), which is a  sub-component
# of the AMDGPU drm driver.
# It provides the HW control for display related functionalities.

AMDDALPATH = $(RELATIVE_AMD_DISPLAY_PATH)

subdir-ccflags-y += -I$(FULL_AMD_DISPLAY_PATH)/dc/inc/
subdir-ccflags-y += -I$(FULL_AMD_DISPLAY_PATH)/dc/inc/hw
subdir-ccflags-y += -I$(FULL_AMD_DISPLAY_PATH)/modules/inc
subdir-ccflags-y += -I$(FULL_AMD_DISPLAY_PATH)/modules/freesync
subdir-ccflags-y += -I$(FULL_AMD_DISPLAY_PATH)/modules/color
subdir-ccflags-y += -I$(FULL_AMD_DISPLAY_PATH)/modules/info_packet
subdir-ccflags-y += -I$(FULL_AMD_DISPLAY_PATH)/modules/power

#TODO: remove when Timing Sync feature is complete
subdir-ccflags-y += -DBUILD_FEATURE_TIMING_SYNC=0

DAL_LIBS = amdgpu_dm dc	modules/freesync modules/color modules/info_packet modules/power

AMD_DAL = $(addsuffix /Makefile, $(addprefix $(FULL_AMD_DISPLAY_PATH)/,$(DAL_LIBS)))

include $(AMD_DAL)
