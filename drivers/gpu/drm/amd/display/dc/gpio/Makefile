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
# Makefile for the 'gpio' sub-component of DAL.
# It provides the control and status of HW GPIO pins.

GPIO = gpio_base.o gpio_service.o hw_factory.o \
       hw_gpio.o hw_hpd.o hw_ddc.o hw_generic.o hw_translate.o

AMD_DAL_GPIO = $(addprefix $(AMDDALPATH)/dc/gpio/,$(GPIO))

AMD_DISPLAY_FILES += $(AMD_DAL_GPIO)

###############################################################################
# DCE 6x
###############################################################################
# all DCE6.x are derived from DCE6.0
ifdef CONFIG_DRM_AMD_DC_SI
GPIO_DCE60 = hw_translate_dce60.o hw_factory_dce60.o

AMD_DAL_GPIO_DCE60 = $(addprefix $(AMDDALPATH)/dc/gpio/dce60/,$(GPIO_DCE60))

AMD_DISPLAY_FILES += $(AMD_DAL_GPIO_DCE60)
endif

###############################################################################
# DCE 8x
###############################################################################
# all DCE8.x are derived from DCE8.0
GPIO_DCE80 = hw_translate_dce80.o hw_factory_dce80.o

AMD_DAL_GPIO_DCE80 = $(addprefix $(AMDDALPATH)/dc/gpio/dce80/,$(GPIO_DCE80))

AMD_DISPLAY_FILES += $(AMD_DAL_GPIO_DCE80)

###############################################################################
# DCE 11x
###############################################################################
GPIO_DCE110 = hw_translate_dce110.o hw_factory_dce110.o

AMD_DAL_GPIO_DCE110 = $(addprefix $(AMDDALPATH)/dc/gpio/dce110/,$(GPIO_DCE110))

AMD_DISPLAY_FILES += $(AMD_DAL_GPIO_DCE110)

###############################################################################
# DCE 12x
###############################################################################
GPIO_DCE120 = hw_translate_dce120.o hw_factory_dce120.o

AMD_DAL_GPIO_DCE120 = $(addprefix $(AMDDALPATH)/dc/gpio/dce120/,$(GPIO_DCE120))

AMD_DISPLAY_FILES += $(AMD_DAL_GPIO_DCE120)

###############################################################################
# DCN 1x
###############################################################################
ifdef CONFIG_DRM_AMD_DC_DCN
GPIO_DCN10 = hw_translate_dcn10.o hw_factory_dcn10.o

AMD_DAL_GPIO_DCN10 = $(addprefix $(AMDDALPATH)/dc/gpio/dcn10/,$(GPIO_DCN10))

AMD_DISPLAY_FILES += $(AMD_DAL_GPIO_DCN10)

###############################################################################
# DCN 2
###############################################################################
GPIO_DCN20 = hw_translate_dcn20.o hw_factory_dcn20.o

AMD_DAL_GPIO_DCN20 = $(addprefix $(AMDDALPATH)/dc/gpio/dcn20/,$(GPIO_DCN20))

AMD_DISPLAY_FILES += $(AMD_DAL_GPIO_DCN20)

###############################################################################
# DCN 21
###############################################################################
GPIO_DCN21 = hw_translate_dcn21.o hw_factory_dcn21.o

AMD_DAL_GPIO_DCN21 = $(addprefix $(AMDDALPATH)/dc/gpio/dcn21/,$(GPIO_DCN21))

AMD_DISPLAY_FILES += $(AMD_DAL_GPIO_DCN21)

###############################################################################
# DCN 3
###############################################################################
GPIO_DCN30 = hw_translate_dcn30.o hw_factory_dcn30.o

AMD_DAL_GPIO_DCN30 = $(addprefix $(AMDDALPATH)/dc/gpio/dcn30/,$(GPIO_DCN30))

AMD_DISPLAY_FILES += $(AMD_DAL_GPIO_DCN30)
endif
###############################################################################
# Diagnostics on FPGA
###############################################################################
GPIO_DIAG_FPGA = hw_translate_diag.o hw_factory_diag.o

AMD_DAL_GPIO_DIAG_FPGA = $(addprefix $(AMDDALPATH)/dc/gpio/diagnostics/,$(GPIO_DIAG_FPGA))

AMD_DISPLAY_FILES += $(AMD_DAL_GPIO_DIAG_FPGA)
