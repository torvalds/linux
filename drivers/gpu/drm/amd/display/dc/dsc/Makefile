#
# Makefile for the 'dsc' sub-component of DAL.

CFLAGS_rc_calc.o := -mhard-float -msse -mpreferred-stack-boundary=4
CFLAGS_rc_calc_dpi.o := -mhard-float -msse -mpreferred-stack-boundary=4
CFLAGS_codec_main_amd.o := -mhard-float -msse -mpreferred-stack-boundary=4
CFLAGS_dc_dsc.o := -mhard-float -msse -mpreferred-stack-boundary=4

DSC = dc_dsc.o rc_calc.o rc_calc_dpi.o

AMD_DAL_DSC = $(addprefix $(AMDDALPATH)/dc/dsc/,$(DSC))

AMD_DISPLAY_FILES += $(AMD_DAL_DSC)
