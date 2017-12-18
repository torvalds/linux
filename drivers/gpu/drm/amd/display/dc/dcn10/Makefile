#
# Makefile for DCN.

DCN10 = dcn10_resource.o dcn10_ipp.o dcn10_hw_sequencer.o \
		dcn10_dpp.o dcn10_opp.o dcn10_timing_generator.o \
		dcn10_hubp.o dcn10_mpc.o \
		dcn10_dpp_dscl.o dcn10_dpp_cm.o dcn10_cm_common.o \
		dcn10_hubbub.o

AMD_DAL_DCN10 = $(addprefix $(AMDDALPATH)/dc/dcn10/,$(DCN10))

AMD_DISPLAY_FILES += $(AMD_DAL_DCN10)
