#
# Copyright 2017 Advanced Micro Devices, Inc.
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
# THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
# OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
# ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
# OTHER DEALINGS IN THE SOFTWARE.
#
#
# Makefile for the 'utils' sub-component of DAL.
# It provides the general basic services required by other DAL
# subcomponents.

ifneq ($(call cc-option, -mpreferred-stack-boundary=4),)
	cc_stack_align := -mpreferred-stack-boundary=4
else ifneq ($(call cc-option, -mstack-alignment=16),)
	cc_stack_align := -mstack-alignment=16
endif

dml_ccflags := -mhard-float -msse $(cc_stack_align)

ifdef CONFIG_CC_IS_CLANG
dml_ccflags += -msse2
endif

CFLAGS_display_mode_lib.o := $(dml_ccflags)

ifdef CONFIG_DRM_AMD_DC_DCN2_0
CFLAGS_display_mode_vba.o := $(dml_ccflags)
CFLAGS_display_mode_vba_20.o := $(dml_ccflags)
CFLAGS_display_rq_dlg_calc_20.o := $(dml_ccflags)
CFLAGS_display_mode_vba_20v2.o := $(dml_ccflags)
CFLAGS_display_rq_dlg_calc_20v2.o := $(dml_ccflags)
endif
ifdef CONFIG_DRM_AMD_DC_DCN2_1
CFLAGS_display_mode_vba_21.o := $(dml_ccflags)
CFLAGS_display_rq_dlg_calc_21.o := $(dml_ccflags)
endif
ifdef CONFIG_DRM_AMD_DCN3AG
CFLAGS_display_mode_vba_3ag.o := $(dml_ccflags)
endif
CFLAGS_dml1_display_rq_dlg_calc.o := $(dml_ccflags)
CFLAGS_display_rq_dlg_helpers.o := $(dml_ccflags)
CFLAGS_dml_common_defs.o := $(dml_ccflags)

DML = display_mode_lib.o display_rq_dlg_helpers.o dml1_display_rq_dlg_calc.o \
	dml_common_defs.o

ifdef CONFIG_DRM_AMD_DC_DCN2_0
DML += display_mode_vba.o dcn20/display_rq_dlg_calc_20.o dcn20/display_mode_vba_20.o
DML += dcn20/display_rq_dlg_calc_20v2.o dcn20/display_mode_vba_20v2.o
endif
ifdef CONFIG_DRM_AMD_DC_DCN2_1
DML += dcn21/display_rq_dlg_calc_21.o dcn21/display_mode_vba_21.o
endif


AMD_DAL_DML = $(addprefix $(AMDDALPATH)/dc/dml/,$(DML))

AMD_DISPLAY_FILES += $(AMD_DAL_DML)
