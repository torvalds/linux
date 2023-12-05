# SPDX-License-Identifier: MIT
#
# Makefile for DCN.

DCN20 = dcn20_init.o dcn20_dpp.o dcn20_dpp_cm.o dcn20_hubp.o \
		dcn20_mpc.o dcn20_opp.o dcn20_hubbub.o dcn20_mmhubbub.o \
		dcn20_stream_encoder.o dcn20_link_encoder.o dcn20_dccg.o \
		dcn20_vmid.o dcn20_dwb.o dcn20_dwb_scl.o

AMD_DAL_DCN20 = $(addprefix $(AMDDALPATH)/dc/dcn20/,$(DCN20))

AMD_DISPLAY_FILES += $(AMD_DAL_DCN20)
