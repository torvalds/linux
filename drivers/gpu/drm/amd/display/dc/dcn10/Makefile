#
# Makefile for DCN.

DCN10 = dcn10_resource.o dcn10_ipp.o dcn10_hw_sequencer.o \
		dcn10_dpp.o dcn10_opp.o dcn10_timing_generator.o \
		dcn10_mem_input.o dcn10_mpc.o dcn10_dwb.o

AMD_DAL_DCN10 = $(addprefix $(AMDDALPATH)/dc/dcn10/,$(DCN10))

AMD_DISPLAY_FILES += $(AMD_DAL_DCN10)
