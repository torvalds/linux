#
# (c) Copyright 2022 Advanced Micro Devices, Inc. All the rights reserved
#
#  All rights reserved.  This notice is intended as a precaution against
#  inadvertent publication and does not imply publication or any waiver
#  of confidentiality.  The year included in the foregoing notice is the
#  year of creation of the work.
#
#  Authors: AMD
#
# Makefile for dcn32.

DCN32 = dcn32_resource.o dcn32_hubbub.o dcn32_hwseq.o dcn32_init.o \
		dcn32_dccg.o dcn32_optc.o dcn32_mmhubbub.o dcn32_hubp.o dcn32_dpp.o \
		dcn32_dio_stream_encoder.o dcn32_dio_link_encoder.o dcn32_hpo_dp_link_encoder.o \
		dcn32_resource_helpers.o dcn32_mpc.o

ifdef CONFIG_X86
dcn32_ccflags := -mhard-float -msse
endif

ifdef CONFIG_PPC64
dcn32_ccflags := -mhard-float -maltivec
endif

ifdef CONFIG_CC_IS_GCC
ifeq ($(call cc-ifversion, -lt, 0701, y), y)
IS_OLD_GCC = 1
endif
dcn32_ccflags += -mhard-float
endif

ifdef CONFIG_X86
ifdef IS_OLD_GCC
# Stack alignment mismatch, proceed with caution.
# GCC < 7.1 cannot compile code using `double` and -mpreferred-stack-boundary=3
# (8B stack alignment).
dcn32_ccflags += -mpreferred-stack-boundary=4
else
dcn32_ccflags += -msse2
endif
endif

CFLAGS_$(AMDDALPATH)/dc/dcn32/dcn32_resource_helpers.o := $(dcn32_ccflags)
CFLAGS_$(AMDDALPATH)/dc/dcn32/dcn32_resource.o := $(dcn32_ccflags)

AMD_DAL_DCN32 = $(addprefix $(AMDDALPATH)/dc/dcn32/,$(DCN32))

AMD_DISPLAY_FILES += $(AMD_DAL_DCN32)
