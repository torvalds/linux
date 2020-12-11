# SPDX-License-Identifier: MIT
#
# Makefile for the 'dsc' sub-component of DAL.

ifdef CONFIG_X86
dsc_ccflags := -mhard-float -msse
endif

ifdef CONFIG_PPC64
dsc_ccflags := -mhard-float -maltivec
endif

ifdef CONFIG_ARM64
dsc_rcflags := -mgeneral-regs-only
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
dsc_ccflags += -mpreferred-stack-boundary=4
else
dsc_ccflags += -msse2
endif
endif

CFLAGS_$(AMDDALPATH)/dc/dsc/rc_calc.o := $(dsc_ccflags)
CFLAGS_REMOVE_$(AMDDALPATH)/dc/dsc/rc_calc.o := $(dsc_rcflags)

DSC = dc_dsc.o rc_calc.o rc_calc_dpi.o

AMD_DAL_DSC = $(addprefix $(AMDDALPATH)/dc/dsc/,$(DSC))

AMD_DISPLAY_FILES += $(AMD_DAL_DSC)
