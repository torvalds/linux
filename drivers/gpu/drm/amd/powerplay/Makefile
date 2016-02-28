
subdir-ccflags-y += -Iinclude/drm  \
		-Idrivers/gpu/drm/amd/powerplay/inc/  \
		-Idrivers/gpu/drm/amd/include/asic_reg  \
		-Idrivers/gpu/drm/amd/include  \
		-Idrivers/gpu/drm/amd/powerplay/smumgr\
		-Idrivers/gpu/drm/amd/powerplay/hwmgr \
		-Idrivers/gpu/drm/amd/powerplay/eventmgr

AMD_PP_PATH = ../powerplay

PP_LIBS = smumgr hwmgr eventmgr

AMD_POWERPLAY = $(addsuffix /Makefile,$(addprefix drivers/gpu/drm/amd/powerplay/,$(PP_LIBS)))

include $(AMD_POWERPLAY)

POWER_MGR = amd_powerplay.o

AMD_PP_POWER = $(addprefix $(AMD_PP_PATH)/,$(POWER_MGR))

AMD_POWERPLAY_FILES += $(AMD_PP_POWER)
