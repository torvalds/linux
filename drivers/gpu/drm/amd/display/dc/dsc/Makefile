# SPDX-License-Identifier: MIT
#
# Makefile for the 'dsc' sub-component of DAL.

ifdef CONFIG_DRM_AMD_DC_FP

###############################################################################
# DCN20
###############################################################################
DSC_DCN20 = dcn20_dsc.o

AMD_DISPLAY_FILES += $(addprefix $(AMDDALPATH)/dc/dsc/dcn20/,$(DSC_DCN20))




###############################################################################
# DCN35
###############################################################################

DSC_DCN35 = dcn35_dsc.o

AMD_DISPLAY_FILES += $(addprefix $(AMDDALPATH)/dc/dsc/dcn35/,$(DSC_DCN35))



endif

DSC = dc_dsc.o rc_calc.o rc_calc_dpi.o

AMD_DAL_DSC = $(addprefix $(AMDDALPATH)/dc/dsc/,$(DSC))

AMD_DISPLAY_FILES += $(AMD_DAL_DSC)

