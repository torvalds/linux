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
# Makefile for common 'dce' logic
# HW object file under this folder follow similar pattern for HW programming
#   - register offset and/or shift + mask stored in the dec_hw struct
#   - register programming through common macros that look up register 
#     offset/shift/mask stored in dce_hw struct

DCE = dce_audio.o dce_stream_encoder.o dce_link_encoder.o dce_hwseq.o \
dce_mem_input.o dce_clock_source.o dce_scl_filters.o dce_transform.o \
dce_opp.o dce_dmcu.o dce_abm.o dce_ipp.o dce_aux.o \
dce_i2c.o dce_i2c_hw.o dce_i2c_sw.o dmub_psr.o dmub_abm.o dce_panel_cntl.o \
dmub_hw_lock_mgr.o dmub_outbox.o

AMD_DAL_DCE = $(addprefix $(AMDDALPATH)/dc/dce/,$(DCE))

AMD_DISPLAY_FILES += $(AMD_DAL_DCE)
