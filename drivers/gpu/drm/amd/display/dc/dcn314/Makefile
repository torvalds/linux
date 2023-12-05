#
# (c) Copyright 2022 Advanced Micro Devices, Inc. All the rights reserved
#
#  All rights reserved.  This notice is intended as a precaution against
#  inadvertent publication and does not imply publication or any waiver
#  of confidentiality.  The year included in the foregoing notice is the
#  year of creation of the work.
#
#  Authors: AMD
#
# Makefile for dcn314.

DCN314 = dcn314_init.o \
		dcn314_dio_stream_encoder.o dcn314_dccg.o

AMD_DAL_DCN314 = $(addprefix $(AMDDALPATH)/dc/dcn314/,$(DCN314))

AMD_DISPLAY_FILES += $(AMD_DAL_DCN314)
