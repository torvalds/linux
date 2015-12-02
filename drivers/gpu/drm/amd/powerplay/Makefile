
subdir-ccflags-y += -Iinclude/drm  \
		-Idrivers/gpu/drm/amd/powerplay/inc/  \
		-Idrivers/gpu/drm/amd/include/asic_reg  \
		-Idrivers/gpu/drm/amd/include

AMD_PP_PATH = ../powerplay

include $(AMD_POWERPLAY)

POWER_MGR = amd_powerplay.o

AMD_PP_POWER = $(addprefix $(AMD_PP_PATH)/,$(POWER_MGR))

AMD_POWERPLAY_FILES += $(AMD_PP_POWER)
