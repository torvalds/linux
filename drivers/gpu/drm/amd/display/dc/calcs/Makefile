#
# Makefile for the 'calcs' sub-component of DAL.
# It calculates Bandwidth and Watermarks values for HW programming
#

BW_CALCS = bandwidth_calcs.o bw_fixed.o

AMD_DAL_BW_CALCS = $(addprefix $(AMDDALPATH)/dc/calcs/,$(BW_CALCS))

AMD_DISPLAY_FILES += $(AMD_DAL_BW_CALCS)
