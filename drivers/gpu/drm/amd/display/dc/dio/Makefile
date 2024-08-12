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
# Authors: AMD
#
#

ifdef CONFIG_DRM_AMD_DC_FP
###############################################################################
# DCN10
###############################################################################
DIO_DCN10 = dcn10_link_encoder.o dcn10_stream_encoder.o

AMD_DAL_DIO_DCN10 = $(addprefix $(AMDDALPATH)/dc/dio/dcn10/,$(DIO_DCN10))

AMD_DISPLAY_FILES += $(AMD_DAL_DIO_DCN10)

###############################################################################
# DCN20
###############################################################################
DIO_DCN20 = dcn20_link_encoder.o dcn20_stream_encoder.o

AMD_DAL_DIO_DCN20 = $(addprefix $(AMDDALPATH)/dc/dio/dcn20/,$(DIO_DCN20))

AMD_DISPLAY_FILES += $(AMD_DAL_DIO_DCN20)

###############################################################################
# DCN30
###############################################################################
DIO_DCN30 = dcn30_dio_link_encoder.o dcn30_dio_stream_encoder.o

AMD_DAL_DIO_DCN30 = $(addprefix $(AMDDALPATH)/dc/dio/dcn30/,$(DIO_DCN30))

AMD_DISPLAY_FILES += $(AMD_DAL_DIO_DCN30)

###############################################################################
# DCN31
###############################################################################
DIO_DCN31 = dcn31_dio_link_encoder.o

AMD_DAL_DIO_DCN31 = $(addprefix $(AMDDALPATH)/dc/dio/dcn31/,$(DIO_DCN31))

AMD_DISPLAY_FILES += $(AMD_DAL_DIO_DCN31)

###############################################################################
# DCN32
###############################################################################
DIO_DCN32 = dcn32_dio_link_encoder.o dcn32_dio_stream_encoder.o

AMD_DAL_DIO_DCN32 = $(addprefix $(AMDDALPATH)/dc/dio/dcn32/,$(DIO_DCN32))

AMD_DISPLAY_FILES += $(AMD_DAL_DIO_DCN32)

###############################################################################
# DCN35
###############################################################################
DIO_DCN35 = dcn35_dio_link_encoder.o dcn35_dio_stream_encoder.o

AMD_DAL_DIO_DCN35 = $(addprefix $(AMDDALPATH)/dc/dio/dcn35/,$(DIO_DCN35))

AMD_DISPLAY_FILES += $(AMD_DAL_DIO_DCN35)

###############################################################################
# DCN321
###############################################################################
DIO_DCN321 = dcn321_dio_link_encoder.o

AMD_DAL_DIO_DCN321 = $(addprefix $(AMDDALPATH)/dc/dio/dcn321/,$(DIO_DCN321))

AMD_DISPLAY_FILES += $(AMD_DAL_DIO_DCN321)


###############################################################################
# DCN401
###############################################################################
DIO_DCN401 = dcn401_dio_link_encoder.o dcn401_dio_stream_encoder.o

AMD_DAL_DIO_DCN401 = $(addprefix $(AMDDALPATH)/dc/dio/dcn401/,$(DIO_DCN401))

AMD_DISPLAY_FILES += $(AMD_DAL_DIO_DCN401)
endif
