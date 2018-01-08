#
# Makefile for the 'calcs' sub-component of DAL.
# It calculates Bandwidth and Watermarks values for HW programming
#

CFLAGS_dcn_calcs.o := -mhard-float -msse -mpreferred-stack-boundary=4
CFLAGS_dcn_calc_auto.o := -mhard-float -msse -mpreferred-stack-boundary=4
CFLAGS_dcn_calc_math.o := -mhard-float -msse -mpreferred-stack-boundary=4 -Wno-tautological-compare

BW_CALCS = dce_calcs.o bw_fixed.o custom_float.o

ifdef CONFIG_DRM_AMD_DC_DCN1_0
BW_CALCS += dcn_calcs.o dcn_calc_math.o dcn_calc_auto.o
endif

AMD_DAL_BW_CALCS = $(addprefix $(AMDDALPATH)/dc/calcs/,$(BW_CALCS))

AMD_DISPLAY_FILES += $(AMD_DAL_BW_CALCS)
