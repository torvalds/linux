#
# Makefile for the 'bios' sub-component of DAL.
# It provides the parsing and executing controls for atom bios image.

BIOS = bios_parser.o bios_parser_interface.o  bios_parser_helper.o command_table.o command_table_helper.o

BIOS += command_table2.o command_table_helper2.o bios_parser2.o

AMD_DAL_BIOS = $(addprefix $(AMDDALPATH)/dc/bios/,$(BIOS))

AMD_DISPLAY_FILES += $(AMD_DAL_BIOS)

###############################################################################
# DCE 8x
###############################################################################
# All DCE8.x are derived from DCE8.0, so 8.0 MUST be defined if ANY of
# DCE8.x is compiled.
AMD_DISPLAY_FILES += $(AMDDALPATH)/dc/bios/dce80/command_table_helper_dce80.o

###############################################################################
# DCE 11x
###############################################################################
AMD_DISPLAY_FILES += $(AMDDALPATH)/dc/bios/dce110/command_table_helper_dce110.o

AMD_DISPLAY_FILES += $(AMDDALPATH)/dc/bios/dce112/command_table_helper_dce112.o

AMD_DISPLAY_FILES += $(AMDDALPATH)/dc/bios/dce112/command_table_helper2_dce112.o
