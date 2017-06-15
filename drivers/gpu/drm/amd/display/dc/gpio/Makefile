#
# Makefile for the 'gpio' sub-component of DAL.
# It provides the control and status of HW GPIO pins.

GPIO = gpio_base.o gpio_service.o hw_factory.o \
       hw_gpio.o hw_hpd.o hw_ddc.o hw_translate.o

AMD_DAL_GPIO = $(addprefix $(AMDDALPATH)/dc/gpio/,$(GPIO))

AMD_DISPLAY_FILES += $(AMD_DAL_GPIO)

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
# Diagnostics on FPGA
###############################################################################
GPIO_DIAG_FPGA = hw_translate_diag.o hw_factory_diag.o

AMD_DAL_GPIO_DIAG_FPGA = $(addprefix $(AMDDALPATH)/dc/gpio/diagnostics/,$(GPIO_DIAG_FPGA))

AMD_DISPLAY_FILES += $(AMD_DAL_GPIO_DIAG_FPGA)
