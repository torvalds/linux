#
# Makefile for the 'smu manager' sub-component of powerplay.
# It provides the smu management services for the driver.

SMU_MGR = smumgr.o cz_smumgr.o tonga_smumgr.o fiji_smumgr.o \
	  polaris10_smumgr.o iceland_smumgr.o \
	  smu7_smumgr.o vega10_smumgr.o rv_smumgr.o ci_smumgr.o

AMD_PP_SMUMGR = $(addprefix $(AMD_PP_PATH)/smumgr/,$(SMU_MGR))

AMD_POWERPLAY_FILES += $(AMD_PP_SMUMGR)
