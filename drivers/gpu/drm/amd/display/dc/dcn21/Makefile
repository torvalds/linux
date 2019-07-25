#
# Makefile for DCN21.

DCN21 = dcn21_hubp.o

CFLAGS_dcn21_resource.o := -mhard-float -msse -mpreferred-stack-boundary=4

AMD_DAL_DCN21 = $(addprefix $(AMDDALPATH)/dc/dcn21/,$(DCN21))

AMD_DISPLAY_FILES += $(AMD_DAL_DCN21)
