# SPDX-License-Identifier: MIT
#
# Makefile for DCN.
DCN201 = dcn201_init.o dcn201_resource.o dcn201_hwseq.o \
	dcn201_hubbub.o\
	dcn201_mpc.o dcn201_hubp.o dcn201_opp.o dcn201_optc.o dcn201_dpp.o \
	dcn201_dccg.o dcn201_link_encoder.o

ifdef CONFIG_X86
CFLAGS_$(AMDDALPATH)/dc/dcn201/dcn201_resource.o := -mhard-float -msse
endif

ifdef CONFIG_PPC64
CFLAGS_$(AMDDALPATH)/dc/dcn201/dcn201_resource.o := -mhard-float -maltivec
endif

ifdef CONFIG_CC_IS_GCC
ifeq ($(call cc-ifversion, -lt, 0701, y), y)
IS_OLD_GCC = 1
endif
CFLAGS_$(AMDDALPATH)/dc/dcn201/dcn201_resource.o += -mhard-float
endif

ifdef CONFIG_X86
ifdef IS_OLD_GCC
# Stack alignment mismatch, proceed with caution.
# GCC < 7.1 cannot compile code using `double` and -mpreferred-stack-boundary=3
# (8B stack alignment).
CFLAGS_$(AMDDALPATH)/dc/dcn201/dcn201_resource.o += -mpreferred-stack-boundary=4
else
CFLAGS_$(AMDDALPATH)/dc/dcn201/dcn201_resource.o += -msse2
endif
endif
AMD_DAL_DCN201 = $(addprefix $(AMDDALPATH)/dc/dcn201/,$(DCN201))

AMD_DISPLAY_FILES += $(AMD_DAL_DCN201)
