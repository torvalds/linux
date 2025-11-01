# Copyright 2025 Advanced Micro Devices, Inc.
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

RAS_MGR_FILES = amdgpu_ras_sys.o  \
		amdgpu_ras_mgr.o \
		amdgpu_ras_eeprom_i2c.o \
		amdgpu_ras_mp1_v13_0.o \
		amdgpu_ras_cmd.o \
		amdgpu_ras_process.o \
		amdgpu_ras_nbio_v7_9.o


RAS_MGR = $(addprefix $(AMD_GPU_RAS_PATH)/ras_mgr/, $(RAS_MGR_FILES))

AMD_GPU_RAS_FILES += $(RAS_MGR)

