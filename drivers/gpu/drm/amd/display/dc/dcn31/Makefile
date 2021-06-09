#
# (c) Copyright 2020 Advanced Micro Devices, Inc. All the rights reserved
#
#  All rights reserved.  This notice is intended as a precaution against
#  inadvertent publication and does not imply publication or any waiver
#  of confidentiality.  The year included in the foregoing notice is the
#  year of creation of the work.
#
#  Authors: AMD
#
# Makefile for dcn31.

DCN31 = dcn31_resource.o dcn31_hubbub.o dcn31_hwseq.o dcn31_init.o dcn31_hubp.o \
	dcn31_dccg.o dcn31_optc.o dcn31_dio_link_encoder.o dcn31_panel_cntl.o \
	dcn31_apg.o dcn31_hpo_dp_stream_encoder.o

ifdef CONFIG_X86
CFLAGS_$(AMDDALPATH)/dc/dcn31/dcn31_resource.o := -msse
endif

ifdef CONFIG_PPC64
CFLAGS_$(AMDDALPATH)/dc/dcn31/dcn31_resource.o := -mhard-float -maltivec
endif

ifdef CONFIG_CC_IS_GCC
ifeq ($(call cc-ifversion, -lt, 0701, y), y)
IS_OLD_GCC = 1
endif
CFLAGS_$(AMDDALPATH)/dc/dcn31/dcn31_resource.o += -mhard-float
endif

ifdef CONFIG_X86
ifdef IS_OLD_GCC
# Stack alignment mismatch, proceed with caution.
# GCC < 7.1 cannot compile code using `double` and -mpreferred-stack-boundary=3
# (8B stack alignment).
CFLAGS_$(AMDDALPATH)/dc/dcn31/dcn31_resource.o += -mpreferred-stack-boundary=4
else
CFLAGS_$(AMDDALPATH)/dc/dcn31/dcn31_resource.o += -msse2
endif
endif

AMD_DAL_DCN31 = $(addprefix $(AMDDALPATH)/dc/dcn31/,$(DCN31))

AMD_DISPLAY_FILES += $(AMD_DAL_DCN31)
