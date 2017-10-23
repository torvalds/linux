#
# Makefile for the 'utils' sub-component of DAL.
# It provides the general basic services required by other DAL
# subcomponents.

CFLAGS_display_mode_vba.o := -mhard-float -msse -mpreferred-stack-boundary=4
CFLAGS_display_mode_lib.o := -mhard-float -msse -mpreferred-stack-boundary=4
CFLAGS_display_pipe_clocks.o := -mhard-float -msse -mpreferred-stack-boundary=4
CFLAGS_display_rq_dlg_calc.o := -mhard-float -msse -mpreferred-stack-boundary=4
CFLAGS_dml1_display_rq_dlg_calc.o := -mhard-float -msse -mpreferred-stack-boundary=4
CFLAGS_display_rq_dlg_helpers.o := -mhard-float -msse -mpreferred-stack-boundary=4
CFLAGS_soc_bounding_box.o := -mhard-float -msse -mpreferred-stack-boundary=4
CFLAGS_dml_common_defs.o := -mhard-float -msse -mpreferred-stack-boundary=4


DML = display_mode_lib.o display_rq_dlg_calc.o \
	  display_rq_dlg_helpers.o dml1_display_rq_dlg_calc.o \
	  soc_bounding_box.o dml_common_defs.o display_mode_vba.o

AMD_DAL_DML = $(addprefix $(AMDDALPATH)/dc/dml/,$(DML))

AMD_DISPLAY_FILES += $(AMD_DAL_DML)
