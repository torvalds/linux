# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for HabanaLabs AI accelerators driver
#

obj-$(CONFIG_HABANA_AI) := habanalabs.o

include $(src)/common/Makefile
habanalabs-y += $(HL_COMMON_FILES)

include $(src)/goya/Makefile
habanalabs-y += $(HL_GOYA_FILES)

include $(src)/gaudi/Makefile
habanalabs-y += $(HL_GAUDI_FILES)

include $(src)/gaudi2/Makefile
habanalabs-y += $(HL_GAUDI2_FILES)

habanalabs-$(CONFIG_DEBUG_FS) += common/debugfs.o
