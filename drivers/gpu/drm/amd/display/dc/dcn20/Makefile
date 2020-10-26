# SPDX-License-Identifier: MIT
#
# Makefile for DCN.

DCN20 = dcn20_resource.o dcn20_init.o dcn20_hwseq.o dcn20_dpp.o dcn20_dpp_cm.o dcn20_hubp.o \
		dcn20_mpc.o dcn20_opp.o dcn20_hubbub.o dcn20_optc.o dcn20_mmhubbub.o \
		dcn20_stream_encoder.o dcn20_link_encoder.o dcn20_dccg.o \
		dcn20_vmid.o dcn20_dwb.o dcn20_dwb_scl.o

DCN20 += dcn20_dsc.o

ifdef CONFIG_X86
CFLAGS_$(AMDDALPATH)/dc/dcn20/dcn20_resource.o := -mhard-float -msse
endif

ifdef CONFIG_PPC64
CFLAGS_$(AMDDALPATH)/dc/dcn20/dcn20_resource.o := -mhard-float -maltivec
endif

ifdef CONFIG_ARM64
CFLAGS_REMOVE_$(AMDDALPATH)/dc/dcn20/dcn20_resource.o := -mgeneral-regs-only
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
CFLAGS_$(AMDDALPATH)/dc/dcn20/dcn20_resource.o += -mpreferred-stack-boundary=4
else
CFLAGS_$(AMDDALPATH)/dc/dcn20/dcn20_resource.o += -msse2
endif
endif

AMD_DAL_DCN20 = $(addprefix $(AMDDALPATH)/dc/dcn20/,$(DCN20))

AMD_DISPLAY_FILES += $(AMD_DAL_DCN20)
