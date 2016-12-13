#
# Makefile for the 'utils' sub-component of DAL.
# It provides the general basic services required by other DAL
# subcomponents.

BASICS = conversion.o fixpt31_32.o fixpt32_32.o grph_object_id.o \
	logger.o log_helpers.o signal_types.o vector.o

AMD_DAL_BASICS = $(addprefix $(AMDDALPATH)/dc/basics/,$(BASICS))

AMD_DISPLAY_FILES += $(AMD_DAL_BASICS)
