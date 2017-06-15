#
# Makefile for the 'audio' sub-component of DAL.
# It provides the control and status of HW adapter resources,
# that are global for the ASIC and sharable between pipes.

IRQ = irq_service.o

AMD_DAL_IRQ = $(addprefix $(AMDDALPATH)/dc/irq/,$(IRQ))

AMD_DISPLAY_FILES += $(AMD_DAL_IRQ)

###############################################################################
# DCE 8x
###############################################################################
IRQ_DCE80 = irq_service_dce80.o

AMD_DAL_IRQ_DCE80 = $(addprefix $(AMDDALPATH)/dc/irq/dce80/,$(IRQ_DCE80))

AMD_DISPLAY_FILES += $(AMD_DAL_IRQ_DCE80)

###############################################################################
# DCE 11x
###############################################################################
IRQ_DCE11 = irq_service_dce110.o

AMD_DAL_IRQ_DCE11 = $(addprefix $(AMDDALPATH)/dc/irq/dce110/,$(IRQ_DCE11))

AMD_DISPLAY_FILES += $(AMD_DAL_IRQ_DCE11)

###############################################################################
# DCE 12x
###############################################################################
IRQ_DCE12 = irq_service_dce120.o

AMD_DAL_IRQ_DCE12 = $(addprefix $(AMDDALPATH)/dc/irq/dce120/,$(IRQ_DCE12))

AMD_DISPLAY_FILES += $(AMD_DAL_IRQ_DCE12)

###############################################################################
# DCN 1x
###############################################################################
ifdef CONFIG_DRM_AMD_DC_DCN1_0
IRQ_DCN1 = irq_service_dcn10.o

AMD_DAL_IRQ_DCN1 = $(addprefix $(AMDDALPATH)/dc/irq/dcn10/,$(IRQ_DCN1))

AMD_DISPLAY_FILES += $(AMD_DAL_IRQ_DCN1)
endif
