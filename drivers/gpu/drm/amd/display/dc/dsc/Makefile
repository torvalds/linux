#
# Makefile for the 'dsc' sub-component of DAL.

ifneq ($(call cc-option, -mpreferred-stack-boundary=4),)
	cc_stack_align := -mpreferred-stack-boundary=4
else ifneq ($(call cc-option, -mstack-alignment=16),)
	cc_stack_align := -mstack-alignment=16
endif

dsc_ccflags := -mhard-float -msse $(cc_stack_align)

ifdef CONFIG_CC_IS_CLANG
dsc_ccflags += -msse2
endif

CFLAGS_rc_calc.o := $(dsc_ccflags)
CFLAGS_rc_calc_dpi.o := $(dsc_ccflags)
CFLAGS_codec_main_amd.o := $(dsc_ccflags)
CFLAGS_dc_dsc.o := $(dsc_ccflags)

DSC = dc_dsc.o rc_calc.o rc_calc_dpi.o

AMD_DAL_DSC = $(addprefix $(AMDDALPATH)/dc/dsc/,$(DSC))

AMD_DISPLAY_FILES += $(AMD_DAL_DSC)
