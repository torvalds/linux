#
# Makefile for common 'dce' logic
# HW object file under this folder follow similar pattern for HW programming
#   - register offset and/or shift + mask stored in the dec_hw struct
#   - register programming through common macros that look up register 
#     offset/shift/mask stored in dce_hw struct

DCE = dce_audio.o dce_stream_encoder.o dce_link_encoder.o dce_hwseq.o \
dce_mem_input.o dce_clock_source.o dce_scl_filters.o dce_transform.o \
dce_clocks.o dce_opp.o dce_dmcu.o dce_abm.o dce_ipp.o


AMD_DAL_DCE = $(addprefix $(AMDDALPATH)/dc/dce/,$(DCE))

AMD_DISPLAY_FILES += $(AMD_DAL_DCE)
