#
# Copyright 2019 Advanced Micro Devices, Inc.
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

DMUB = dmub_srv.o dmub_srv_stat.o dmub_reg.o dmub_dcn20.o dmub_dcn21.o
DMUB += dmub_dcn30.o dmub_dcn301.o dmub_dcn302.o dmub_dcn303.o
DMUB += dmub_dcn31.o dmub_dcn314.o dmub_dcn315.o dmub_dcn316.o
DMUB += dmub_dcn32.o
DMUB += dmub_dcn35.o
DMUB += dmub_dcn351.o
DMUB += dmub_dcn36.o
DMUB += dmub_dcn401.o

AMD_DAL_DMUB = $(addprefix $(AMDDALPATH)/dmub/src/,$(DMUB))

AMD_DISPLAY_FILES += $(AMD_DAL_DMUB)
