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
# Makefile for DCN35.

DCN35 = dcn35_dio_stream_encoder.o \
	dcn35_dio_link_encoder.o \
	dcn35_mmhubbub.o dcn35_opp.o dcn35_pg_cntl.o dcn35_dwb.o

AMD_DAL_DCN35 = $(addprefix $(AMDDALPATH)/dc/dcn35/,$(DCN35))

AMD_DISPLAY_FILES += $(AMD_DAL_DCN35)
