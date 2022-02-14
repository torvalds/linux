#
# (c) Copyright 2020 Advanced Micro Devices, Inc. All the rights reserved
#
#  Authors: AMD
#
# Makefile for dcn302.

DCN3_02 = dcn302_init.o dcn302_hwseq.o dcn302_resource.o

ifdef CONFIG_X86
CFLAGS_$(AMDDALPATH)/dc/dcn302/dcn302_resource.o := -msse
endif

ifdef CONFIG_PPC64
CFLAGS_$(AMDDALPATH)/dc/dcn302/dcn302_resource.o := -mhard-float -maltivec
endif

ifdef CONFIG_X86
ifdef IS_OLD_GCC
# Stack alignment mismatch, proceed with caution.
# GCC < 7.1 cannot compile code using `double` and -mpreferred-stack-boundary=3
# (8B stack alignment).
CFLAGS_$(AMDDALPATH)/dc/dcn302/dcn302_resource.o += -mpreferred-stack-boundary=4
else
CFLAGS_$(AMDDALPATH)/dc/dcn302/dcn302_resource.o += -msse2
endif
endif

AMD_DAL_DCN3_02 = $(addprefix $(AMDDALPATH)/dc/dcn302/,$(DCN3_02))

AMD_DISPLAY_FILES += $(AMD_DAL_DCN3_02)
