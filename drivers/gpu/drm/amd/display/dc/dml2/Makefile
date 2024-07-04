# SPDX-License-Identifier: MIT */
#
# Copyright 2023 Advanced Micro Devices, Inc.
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
# Makefile for dml2.

dml2_ccflags := $(CC_FLAGS_FPU)
dml2_rcflags := $(CC_FLAGS_NO_FPU)

ifneq ($(CONFIG_FRAME_WARN),0)
ifeq ($(filter y,$(CONFIG_KASAN)$(CONFIG_KCSAN)),y)
frame_warn_flag := -Wframe-larger-than=3072
else
frame_warn_flag := -Wframe-larger-than=2048
endif
endif

subdir-ccflags-y += -I$(FULL_AMD_DISPLAY_PATH)/dc/dml2
CFLAGS_$(AMDDALPATH)/dc/dml2/display_mode_core.o := $(dml2_ccflags) $(frame_warn_flag)
CFLAGS_$(AMDDALPATH)/dc/dml2/display_mode_util.o := $(dml2_ccflags)
CFLAGS_$(AMDDALPATH)/dc/dml2/dml2_wrapper.o := $(dml2_ccflags)
CFLAGS_$(AMDDALPATH)/dc/dml2/dml2_utils.o := $(dml2_ccflags)
CFLAGS_$(AMDDALPATH)/dc/dml2/dml2_policy.o := $(dml2_ccflags)
CFLAGS_$(AMDDALPATH)/dc/dml2/dml2_translation_helper.o := $(dml2_ccflags)
CFLAGS_$(AMDDALPATH)/dc/dml2/dml2_mall_phantom.o := $(dml2_ccflags)
CFLAGS_$(AMDDALPATH)/dc/dml2/dml_display_rq_dlg_calc.o := $(dml2_ccflags)
CFLAGS_$(AMDDALPATH)/dc/dml2/dml2_dc_resource_mgmt.o := $(dml2_ccflags)

CFLAGS_REMOVE_$(AMDDALPATH)/dc/dml2/display_mode_core.o := $(dml2_rcflags)
CFLAGS_REMOVE_$(AMDDALPATH)/dc/dml2/display_mode_util.o := $(dml2_rcflags)
CFLAGS_REMOVE_$(AMDDALPATH)/dc/dml2/dml2_wrapper.o := $(dml2_rcflags)
CFLAGS_REMOVE_$(AMDDALPATH)/dc/dml2/dml2_utils.o := $(dml2_rcflags)
CFLAGS_REMOVE_$(AMDDALPATH)/dc/dml2/dml2_policy.o := $(dml2_rcflags)
CFLAGS_REMOVE_$(AMDDALPATH)/dc/dml2/dml2_translation_helper.o := $(dml2_rcflags)
CFLAGS_REMOVE_$(AMDDALPATH)/dc/dml2/dml2_mall_phantom.o := $(dml2_rcflags)
CFLAGS_REMOVE_$(AMDDALPATH)/dc/dml2/dml_display_rq_dlg_calc.o := $(dml2_rcflags)
CFLAGS_REMOVE_$(AMDDALPATH)/dc/dml2/dml2_dc_resource_mgmt.o := $(dml2_rcflags)

DML2 = display_mode_core.o display_mode_util.o dml2_wrapper.o \
		dml2_utils.o dml2_policy.o dml2_translation_helper.o dml2_dc_resource_mgmt.o dml2_mall_phantom.o \
		dml_display_rq_dlg_calc.o

AMD_DAL_DML2 = $(addprefix $(AMDDALPATH)/dc/dml2/,$(DML2))

AMD_DISPLAY_FILES += $(AMD_DAL_DML2)

