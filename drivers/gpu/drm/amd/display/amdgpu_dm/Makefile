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
# Makefile for the 'dm' sub-component of DAL.
# It provides the control and status of dm blocks.



AMDGPUDM = amdgpu_dm.o amdgpu_dm_irq.o amdgpu_dm_mst_types.o amdgpu_dm_color.o

ifdef CONFIG_DRM_AMD_DC_DCN
AMDGPUDM += dc_fpu.o
endif

ifneq ($(CONFIG_DRM_AMD_DC),)
AMDGPUDM += amdgpu_dm_services.o amdgpu_dm_helpers.o amdgpu_dm_pp_smu.o amdgpu_dm_psr.o
endif

ifdef CONFIG_DRM_AMD_DC_HDCP
AMDGPUDM += amdgpu_dm_hdcp.o
endif

ifneq ($(CONFIG_DEBUG_FS),)
AMDGPUDM += amdgpu_dm_crc.o amdgpu_dm_debugfs.o
endif

subdir-ccflags-y += -I$(FULL_AMD_DISPLAY_PATH)/dc

AMDGPU_DM = $(addprefix $(AMDDALPATH)/amdgpu_dm/,$(AMDGPUDM))

AMD_DISPLAY_FILES += $(AMDGPU_DM)
