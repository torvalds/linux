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
# Makefile for the 'audio' sub-component of DAL.
# It provides the control and status of HW adapter resources,
# that are global for the ASIC and sharable between pipes.

IRQ = irq_service.o

AMD_DAL_IRQ = $(addprefix $(AMDDALPATH)/dc/irq/,$(IRQ))

AMD_DISPLAY_FILES += $(AMD_DAL_IRQ)

###############################################################################
# DCE 8x
###############################################################################
IRQ_DCE80 = irq_service_dce80.o

AMD_DAL_IRQ_DCE80 = $(addprefix $(AMDDALPATH)/dc/irq/dce80/,$(IRQ_DCE80))

AMD_DISPLAY_FILES += $(AMD_DAL_IRQ_DCE80)

###############################################################################
# DCE 11x
###############################################################################
IRQ_DCE11 = irq_service_dce110.o

AMD_DAL_IRQ_DCE11 = $(addprefix $(AMDDALPATH)/dc/irq/dce110/,$(IRQ_DCE11))

AMD_DISPLAY_FILES += $(AMD_DAL_IRQ_DCE11)

###############################################################################
# DCE 12x
###############################################################################
IRQ_DCE12 = irq_service_dce120.o

AMD_DAL_IRQ_DCE12 = $(addprefix $(AMDDALPATH)/dc/irq/dce120/,$(IRQ_DCE12))

AMD_DISPLAY_FILES += $(AMD_DAL_IRQ_DCE12)

###############################################################################
# DCN 1x
###############################################################################
ifdef CONFIG_DRM_AMD_DC_DCN1_0
IRQ_DCN1 = irq_service_dcn10.o

AMD_DAL_IRQ_DCN1 = $(addprefix $(AMDDALPATH)/dc/irq/dcn10/,$(IRQ_DCN1))

AMD_DISPLAY_FILES += $(AMD_DAL_IRQ_DCN1)
endif
###############################################################################
# DCN 20
###############################################################################
ifdef CONFIG_DRM_AMD_DC_DCN2_0
IRQ_DCN2 = irq_service_dcn20.o

AMD_DAL_IRQ_DCN2 = $(addprefix $(AMDDALPATH)/dc/irq/dcn20/,$(IRQ_DCN2))

AMD_DISPLAY_FILES += $(AMD_DAL_IRQ_DCN2)
endif
###############################################################################
# DCN 21
###############################################################################
ifdef CONFIG_DRM_AMD_DC_DCN2_1
IRQ_DCN21 = irq_service_dcn21.o

AMD_DAL_IRQ_DCN21= $(addprefix $(AMDDALPATH)/dc/irq/dcn21/,$(IRQ_DCN21))

AMD_DISPLAY_FILES += $(AMD_DAL_IRQ_DCN21)
endif
