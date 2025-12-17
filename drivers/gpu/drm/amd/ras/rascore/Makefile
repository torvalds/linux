#
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
#
RAS_CORE_FILES = ras_core.o \
			ras_mp1.o \
			ras_mp1_v13_0.o \
			ras_aca.o  \
			ras_aca_v1_0.o \
			ras_eeprom.o \
			ras_umc.o \
			ras_umc_v12_0.o \
			ras_cmd.o \
			ras_gfx.o \
			ras_gfx_v9_0.o \
			ras_process.o \
			ras_nbio.o \
			ras_nbio_v7_9.o \
			ras_log_ring.o \
			ras_cper.o \
			ras_psp.o \
			ras_psp_v13_0.o


RAS_CORE = $(addprefix $(AMD_GPU_RAS_PATH)/rascore/,$(RAS_CORE_FILES))

AMD_GPU_RAS_FILES += $(RAS_CORE)
