#
# Copyright 2020 Advanced Micro Devices, Inc. All the rights reserved
#
#  All rights reserved.  This notice is intended as a precaution against
#  inadvertent publication and does not imply publication or any waiver
#  of confidentiality.  The year included in the foregoing notice is the
#  year of creation of the work.
#
#  Authors: AMD
#
# Makefile for dcn31.

DCN31 = dcn31_hubbub.o dcn31_hubp.o \
	dcn31_dccg.o dcn31_dio_link_encoder.o dcn31_panel_cntl.o \
	dcn31_apg.o dcn31_hpo_dp_stream_encoder.o dcn31_hpo_dp_link_encoder.o \
	dcn31_afmt.o dcn31_vpg.o

AMD_DAL_DCN31 = $(addprefix $(AMDDALPATH)/dc/dcn31/,$(DCN31))

AMD_DISPLAY_FILES += $(AMD_DAL_DCN31)
