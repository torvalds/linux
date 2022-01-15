# SPDX-License-Identifier: MIT
#
# Copyright (C) 2021 Advanced Micro Devices, Inc. All the rights reserved
#
#  Authors: AMD
#
# Makefile for dcn303.

DCN3_03 = dcn303_init.o dcn303_hwseq.o dcn303_resource.o

ifdef CONFIG_X86
CFLAGS_$(AMDDALPATH)/dc/dcn303/dcn303_resource.o := -msse
endif

ifdef CONFIG_PPC64
CFLAGS_$(AMDDALPATH)/dc/dcn303/dcn303_resource.o := -mhard-float -maltivec
endif

ifdef CONFIG_CC_IS_GCC
ifeq ($(call cc-ifversion, -lt, 0701, y), y)
IS_OLD_GCC = 1
endif
CFLAGS_$(AMDDALPATH)/dc/dcn303/dcn303_resource.o += -mhard-float
endif

ifdef CONFIG_X86
ifdef IS_OLD_GCC
# Stack alignment mismatch, proceed with caution.
# GCC < 7.1 cannot compile code using `double` and -mpreferred-stack-boundary=3
# (8B stack alignment).
CFLAGS_$(AMDDALPATH)/dc/dcn303/dcn303_resource.o += -mpreferred-stack-boundary=4
else
CFLAGS_$(AMDDALPATH)/dc/dcn303/dcn303_resource.o += -msse2
endif
endif

AMD_DAL_DCN3_03 = $(addprefix $(AMDDALPATH)/dc/dcn303/,$(DCN3_03))

AMD_DISPLAY_FILES += $(AMD_DAL_DCN3_03)
