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
# Makefile for dcn314.

DCN314 = dcn314_resource.o dcn314_hwseq.o dcn314_init.o \
		dcn314_dio_stream_encoder.o dcn314_dccg.o dcn314_optc.o

ifdef CONFIG_X86
CFLAGS_$(AMDDALPATH)/dc/dcn314/dcn314_resource.o := -mhard-float -msse
endif

ifdef CONFIG_PPC64
CFLAGS_$(AMDDALPATH)/dc/dcn314/dcn314_resource.o := -mhard-float -maltivec
endif

ifdef CONFIG_CC_IS_GCC
ifeq ($(call cc-ifversion, -lt, 0701, y), y)
IS_OLD_GCC = 1
endif
endif

ifdef CONFIG_X86
ifdef IS_OLD_GCC
# Stack alignment mismatch, proceed with caution.
# GCC < 7.1 cannot compile code using `double` and -mpreferred-stack-boundary=3
# (8B stack alignment).
CFLAGS_$(AMDDALPATH)/dc/dcn314/dcn314_resource.o += -mpreferred-stack-boundary=4
else
CFLAGS_$(AMDDALPATH)/dc/dcn314/dcn314_resource.o += -msse2
endif
endif

AMD_DAL_DCN314 = $(addprefix $(AMDDALPATH)/dc/dcn314/,$(DCN314))

AMD_DISPLAY_FILES += $(AMD_DAL_DCN314)
