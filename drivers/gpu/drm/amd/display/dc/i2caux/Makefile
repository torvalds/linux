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
# Makefile for the 'i2c' sub-component of DAL.
# It provides the control and status of HW i2c engine of the adapter.

I2CAUX = aux_engine.o engine_base.o i2caux.o i2c_engine.o \
	 i2c_generic_hw_engine.o i2c_hw_engine.o i2c_sw_engine.o

AMD_DAL_I2CAUX = $(addprefix $(AMDDALPATH)/dc/i2caux/,$(I2CAUX))

AMD_DISPLAY_FILES += $(AMD_DAL_I2CAUX)

###############################################################################
# DCE 8x family
###############################################################################
I2CAUX_DCE80 = i2caux_dce80.o i2c_hw_engine_dce80.o \
	i2c_sw_engine_dce80.o

AMD_DAL_I2CAUX_DCE80 = $(addprefix $(AMDDALPATH)/dc/i2caux/dce80/,$(I2CAUX_DCE80))

AMD_DISPLAY_FILES += $(AMD_DAL_I2CAUX_DCE80)

###############################################################################
# DCE 100 family
###############################################################################
I2CAUX_DCE100 = i2caux_dce100.o

AMD_DAL_I2CAUX_DCE100 = $(addprefix $(AMDDALPATH)/dc/i2caux/dce100/,$(I2CAUX_DCE100))

AMD_DISPLAY_FILES += $(AMD_DAL_I2CAUX_DCE100)

###############################################################################
# DCE 110 family
###############################################################################
I2CAUX_DCE110 = i2caux_dce110.o i2c_sw_engine_dce110.o i2c_hw_engine_dce110.o \
	aux_engine_dce110.o

AMD_DAL_I2CAUX_DCE110 = $(addprefix $(AMDDALPATH)/dc/i2caux/dce110/,$(I2CAUX_DCE110))

AMD_DISPLAY_FILES += $(AMD_DAL_I2CAUX_DCE110)

###############################################################################
# DCE 112 family
###############################################################################
I2CAUX_DCE112 = i2caux_dce112.o

AMD_DAL_I2CAUX_DCE112 = $(addprefix $(AMDDALPATH)/dc/i2caux/dce112/,$(I2CAUX_DCE112))

AMD_DISPLAY_FILES += $(AMD_DAL_I2CAUX_DCE112)

###############################################################################
# DCN 1.0 family
###############################################################################
ifdef CONFIG_DRM_AMD_DC_DCN1_0
I2CAUX_DCN1 = i2caux_dcn10.o

AMD_DAL_I2CAUX_DCN1 = $(addprefix $(AMDDALPATH)/dc/i2caux/dcn10/,$(I2CAUX_DCN1))

AMD_DISPLAY_FILES += $(AMD_DAL_I2CAUX_DCN1)
endif

###############################################################################
# DCE 120 family
###############################################################################
I2CAUX_DCE120 = i2caux_dce120.o

AMD_DAL_I2CAUX_DCE120 = $(addprefix $(AMDDALPATH)/dc/i2caux/dce120/,$(I2CAUX_DCE120))

AMD_DISPLAY_FILES += $(AMD_DAL_I2CAUX_DCE120)

###############################################################################
# Diagnostics on FPGA
###############################################################################
I2CAUX_DIAG = i2caux_diag.o

AMD_DAL_I2CAUX_DIAG = $(addprefix $(AMDDALPATH)/dc/i2caux/diagnostics/,$(I2CAUX_DIAG))

AMD_DISPLAY_FILES += $(AMD_DAL_I2CAUX_DIAG)

