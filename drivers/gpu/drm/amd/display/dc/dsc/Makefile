#
# Makefile for the 'dsc' sub-component of DAL.

dsc_ccflags := -mhard-float -msse

ifdef CONFIG_CC_IS_GCC
dsc_ccflags += -mpreferred-stack-boundary=4
endif

ifdef CONFIG_CC_IS_CLANG
dsc_ccflags += -msse2
endif

CFLAGS_$(AMDDALPATH)/dc/dsc/rc_calc.o := $(dsc_ccflags)
CFLAGS_$(AMDDALPATH)/dc/dsc/rc_calc_dpi.o := $(dsc_ccflags)
CFLAGS_$(AMDDALPATH)/dc/dsc/dc_dsc.o := $(dsc_ccflags)

DSC = dc_dsc.o rc_calc.o rc_calc_dpi.o

AMD_DAL_DSC = $(addprefix $(AMDDALPATH)/dc/dsc/,$(DSC))

AMD_DISPLAY_FILES += $(AMD_DAL_DSC)
