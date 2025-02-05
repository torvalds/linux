# SPDX-License-Identifier: MIT
# Copyright © 2019-2024 Advanced Micro Devices, Inc. All rights reserved.

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

###############################################################################
# DCN401
###############################################################################

DSC_DCN401 += dcn401_dsc.o

AMD_DISPLAY_FILES += $(addprefix $(AMDDALPATH)/dc/dsc/dcn401/,$(DSC_DCN401))

endif

DSC = dc_dsc.o rc_calc.o rc_calc_dpi.o

AMD_DAL_DSC = $(addprefix $(AMDDALPATH)/dc/dsc/,$(DSC))

AMD_DISPLAY_FILES += $(AMD_DAL_DSC)

