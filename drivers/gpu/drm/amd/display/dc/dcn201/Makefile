# SPDX-License-Identifier: MIT
#
# Makefile for DCN.
DCN201 = dcn201_init.o \
	dcn201_hubbub.o\
	dcn201_mpc.o dcn201_hubp.o dcn201_opp.o dcn201_dpp.o \
	dcn201_dccg.o dcn201_link_encoder.o

AMD_DAL_DCN201 = $(addprefix $(AMDDALPATH)/dc/dcn201/,$(DCN201))

AMD_DISPLAY_FILES += $(AMD_DAL_DCN201)
