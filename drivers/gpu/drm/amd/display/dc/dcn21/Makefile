# SPDX-License-Identifier: MIT
#
# Makefile for DCN21.

DCN21 = dcn21_init.o dcn21_hubp.o dcn21_hubbub.o \
	 dcn21_link_encoder.o dcn21_dccg.o

AMD_DAL_DCN21 = $(addprefix $(AMDDALPATH)/dc/dcn21/,$(DCN21))

AMD_DISPLAY_FILES += $(AMD_DAL_DCN21)
