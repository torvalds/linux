#
# Makefile for the 'i2c' sub-component of DAL.
# It provides the control and status of HW i2c engine of the adapter.

I2CAUX = aux_engine.o engine_base.o i2caux.o i2c_engine.o \
	 i2c_generic_hw_engine.o i2c_hw_engine.o i2c_sw_engine.o

AMD_DAL_I2CAUX = $(addprefix $(AMDDALPATH)/dc/i2caux/,$(I2CAUX))

AMD_DISPLAY_FILES += $(AMD_DAL_I2CAUX)

###############################################################################
# DCE 8x family
###############################################################################
I2CAUX_DCE80 = i2caux_dce80.o i2c_hw_engine_dce80.o \
	i2c_sw_engine_dce80.o

AMD_DAL_I2CAUX_DCE80 = $(addprefix $(AMDDALPATH)/dc/i2caux/dce80/,$(I2CAUX_DCE80))

AMD_DISPLAY_FILES += $(AMD_DAL_I2CAUX_DCE80)

###############################################################################
# DCE 100 family
###############################################################################
I2CAUX_DCE100 = i2caux_dce100.o

AMD_DAL_I2CAUX_DCE100 = $(addprefix $(AMDDALPATH)/dc/i2caux/dce100/,$(I2CAUX_DCE100))

AMD_DISPLAY_FILES += $(AMD_DAL_I2CAUX_DCE100)

###############################################################################
# DCE 110 family
###############################################################################
I2CAUX_DCE110 = i2caux_dce110.o i2c_sw_engine_dce110.o i2c_hw_engine_dce110.o \
	aux_engine_dce110.o

AMD_DAL_I2CAUX_DCE110 = $(addprefix $(AMDDALPATH)/dc/i2caux/dce110/,$(I2CAUX_DCE110))

AMD_DISPLAY_FILES += $(AMD_DAL_I2CAUX_DCE110)

###############################################################################
# DCE 112 family
###############################################################################
I2CAUX_DCE112 = i2caux_dce112.o

AMD_DAL_I2CAUX_DCE112 = $(addprefix $(AMDDALPATH)/dc/i2caux/dce112/,$(I2CAUX_DCE112))

AMD_DISPLAY_FILES += $(AMD_DAL_I2CAUX_DCE112)

###############################################################################
# DCE 120 family
###############################################################################
I2CAUX_DCE120 = i2caux_dce120.o

AMD_DAL_I2CAUX_DCE120 = $(addprefix $(AMDDALPATH)/dc/i2caux/dce120/,$(I2CAUX_DCE120))

AMD_DISPLAY_FILES += $(AMD_DAL_I2CAUX_DCE120)

###############################################################################
# Diagnostics on FPGA
###############################################################################
I2CAUX_DIAG = i2caux_diag.o

AMD_DAL_I2CAUX_DIAG = $(addprefix $(AMDDALPATH)/dc/i2caux/diagnostics/,$(I2CAUX_DIAG))

AMD_DISPLAY_FILES += $(AMD_DAL_I2CAUX_DIAG)

