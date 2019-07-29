#
# Makefile for DCN.

DCN20 = dcn20_resource.o dcn20_hwseq.o dcn20_dpp.o dcn20_dpp_cm.o dcn20_hubp.o \
		dcn20_mpc.o dcn20_opp.o dcn20_hubbub.o dcn20_optc.o dcn20_mmhubbub.o \
		dcn20_stream_encoder.o dcn20_link_encoder.o dcn20_dccg.o \
		dcn20_vmid.o dcn20_dwb.o dcn20_dwb_scl.o

ifdef CONFIG_DRM_AMD_DC_DSC_SUPPORT
DCN20 += dcn20_dsc.o
endif

ifneq ($(call cc-option, -mpreferred-stack-boundary=4),)
	cc_stack_align := -mpreferred-stack-boundary=4
else ifneq ($(call cc-option, -mstack-alignment=16),)
	cc_stack_align := -mstack-alignment=16
endif

CFLAGS_dcn20_resource.o := -mhard-float -msse $(cc_stack_align)

AMD_DAL_DCN20 = $(addprefix $(AMDDALPATH)/dc/dcn20/,$(DCN20))

AMD_DISPLAY_FILES += $(AMD_DAL_DCN20)
