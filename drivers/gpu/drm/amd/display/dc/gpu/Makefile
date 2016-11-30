#
# Makefile for the 'gpu' sub-component of DAL.
# It provides the control and status of HW adapter resources,
# that are global for the ASIC and sharable between pipes.

GPU = divider_range.o

AMD_DAL_GPU = $(addprefix $(AMDDALPATH)/dc/gpu/,$(GPU))

AMD_DISPLAY_FILES += $(AMD_DAL_GPU)

###############################################################################
# DCE 80 family
###############################################################################
GPU_DCE80 = display_clock_dce80.o

AMD_DAL_GPU_DCE80 = $(addprefix $(AMDDALPATH)/dc/gpu/dce80/,$(GPU_DCE80))

AMD_DISPLAY_FILES += $(AMD_DAL_GPU_DCE80)


###############################################################################
# DCE 110 family
###############################################################################
GPU_DCE110 = display_clock_dce110.o

AMD_DAL_GPU_DCE110 = $(addprefix $(AMDDALPATH)/dc/gpu/dce110/,$(GPU_DCE110))

AMD_DISPLAY_FILES += $(AMD_DAL_GPU_DCE110)

GPU_DCE112 = display_clock_dce112.o

AMD_DAL_GPU_DCE112 = $(addprefix $(AMDDALPATH)/dc/gpu/dce112/,$(GPU_DCE112))

AMD_DISPLAY_FILES += $(AMD_DAL_GPU_DCE110) $(AMD_DAL_GPU_DCE112)

