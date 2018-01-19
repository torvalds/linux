#
# Makefile for the 'controller' sub-component of DAL.
# It provides the control and status of HW CRTC block.

DCE110 = dce110_timing_generator.o \
dce110_compressor.o dce110_hw_sequencer.o dce110_resource.o \
dce110_opp_regamma_v.o dce110_opp_csc_v.o dce110_timing_generator_v.o \
dce110_mem_input_v.o dce110_opp_v.o dce110_transform_v.o

AMD_DAL_DCE110 = $(addprefix $(AMDDALPATH)/dc/dce110/,$(DCE110))

AMD_DISPLAY_FILES += $(AMD_DAL_DCE110)
