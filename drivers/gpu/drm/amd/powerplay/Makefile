
subdir-ccflags-y += -Iinclude/drm  \
		-I$(FULL_AMD_PATH)/powerplay/inc/  \
		-I$(FULL_AMD_PATH)/include/asic_reg  \
		-I$(FULL_AMD_PATH)/include  \
		-I$(FULL_AMD_PATH)/powerplay/smumgr\
		-I$(FULL_AMD_PATH)/powerplay/hwmgr \
		-I$(FULL_AMD_PATH)/powerplay/eventmgr

AMD_PP_PATH = ../powerplay

PP_LIBS = smumgr hwmgr eventmgr

AMD_POWERPLAY = $(addsuffix /Makefile,$(addprefix $(FULL_AMD_PATH)/powerplay/,$(PP_LIBS)))

include $(AMD_POWERPLAY)

POWER_MGR = amd_powerplay.o

AMD_PP_POWER = $(addprefix $(AMD_PP_PATH)/,$(POWER_MGR))

AMD_POWERPLAY_FILES += $(AMD_PP_POWER)
