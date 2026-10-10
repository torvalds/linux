# SPDX-License-Identifier: MIT
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
# Makefile for dml2_wrapper.

dml2_wrapper_ccflags := $(CC_FLAGS_FPU)
dml2_wrapper_rcflags := $(CC_FLAGS_NO_FPU)

subdir-ccflags-y += -I$(FULL_AMD_DISPLAY_PATH)/dc/dml2_wrapper
subdir-ccflags-y += -I$(FULL_AMD_DISPLAY_PATH)/dc/dml2_wrapper/dml21_wrapper

# Add FPU flags to all dml2_wrapper files by default, remove NO_FPU flags.
# FPU flags step 1: Find all .c files in dal/dc/dml2_wrapper and it's subfolders
THIS_MAKEFILE_PATH := $(abspath $(lastword $(MAKEFILE_LIST)))
DML2_WRAPPER_ABS_PATH := $(patsubst %/,%,$(dir $(THIS_MAKEFILE_PATH)))
DML2_WRAPPER_C_FILES := $(shell find $(DML2_WRAPPER_ABS_PATH) -name '*.c')

# FPU flags step 2: Convert to .o and make paths relative to $(AMDDALPATH)/dc/dml2_wrapper/
DML2_WRAPPER_RELATIVE_O_FILES := $(patsubst $(DML2_WRAPPER_ABS_PATH)/%,dc/dml2_wrapper/%,$(patsubst %.c,%.o,$(DML2_WRAPPER_C_FILES)))

# FPU flags step 3: Apply FPU flags to all .o files from dal/dc/dml2_wrapper and it's subfolders
$(foreach obj,$(DML2_WRAPPER_RELATIVE_O_FILES),$(eval CFLAGS_$(AMDDALPATH)/$(obj) := $(dml2_wrapper_ccflags)))
$(foreach obj,$(DML2_WRAPPER_RELATIVE_O_FILES),$(eval CFLAGS_REMOVE_$(AMDDALPATH)/$(obj) := $(dml2_wrapper_rcflags)))

# FPU flags step 4: Replace CFLAGS per file for files with additional flags beyond dml2_ccflags and dml2_rcflags
CFLAGS_$(AMDDALPATH)/dc/dml2_wrapper/dml2_wrapper.o := $(dml2_wrapper_rcflags)
CFLAGS_REMOVE_$(AMDDALPATH)/dc/dml2_wrapper/dml2_wrapper.o := $(dml2_wrapper_ccflags)
CFLAGS_$(AMDDALPATH)/dc/dml2_wrapper/dml21_wrapper/dml21_wrapper.o := $(dml2_wrapper_rcflags)
CFLAGS_REMOVE_$(AMDDALPATH)/dc/dml2_wrapper/dml21_wrapper/dml21_wrapper.o := $(dml2_wrapper_ccflags)

DML2_WRAPPER = dml2_wrapper_fpu.o dml2_wrapper.o \
		dml2_utils.o dml2_policy.o dml2_translation_helper.o dml2_dc_resource_mgmt.o dml2_mall_phantom.o

DML21_WRAPPER = dml21_wrapper/dml21_wrapper_fpu.o dml21_wrapper/dml21_wrapper.o \
		dml21_wrapper/dml21_utils.o dml21_wrapper/dml21_translation_helper.o

AMD_DAL_DML2_WRAPPER = $(addprefix $(AMDDALPATH)/dc/dml2_wrapper/,$(DML2_WRAPPER))
AMD_DAL_DML21_WRAPPER = $(addprefix $(AMDDALPATH)/dc/dml2_wrapper/,$(DML21_WRAPPER))

AMD_DISPLAY_FILES += $(AMD_DAL_DML2_WRAPPER)
AMD_DISPLAY_FILES += $(AMD_DAL_DML21_WRAPPER)
