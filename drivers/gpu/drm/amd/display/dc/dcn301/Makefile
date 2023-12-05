#
# (c) Copyright 2020 Advanced Micro Devices, Inc. All the rights reserved
#
#  All rights reserved.  This notice is intended as a precaution against
#  inadvertent publication and does not imply publication or any waiver
#  of confidentiality.  The year included in the foregoing notice is the
#  year of creation of the work.
#
#  Authors: AMD
#
# Makefile for dcn30.

DCN301 = dcn301_init.o dcn301_dccg.o \
		dcn301_dio_link_encoder.o dcn301_panel_cntl.o dcn301_hubbub.o

AMD_DAL_DCN301 = $(addprefix $(AMDDALPATH)/dc/dcn301/,$(DCN301))

AMD_DISPLAY_FILES += $(AMD_DAL_DCN301)
