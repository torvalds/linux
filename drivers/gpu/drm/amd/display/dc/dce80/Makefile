#
# Makefile for the 'controller' sub-component of DAL.
# It provides the control and status of HW CRTC block.

DCE80 = dce80_timing_generator.o \
	dce80_compressor.o dce80_mem_input.o dce80_hw_sequencer.o \
	dce80_resource.o

AMD_DAL_DCE80 = $(addprefix $(AMDDALPATH)/dc/dce80/,$(DCE80))

AMD_DISPLAY_FILES += $(AMD_DAL_DCE80)



