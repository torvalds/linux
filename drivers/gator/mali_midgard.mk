# Defines for Mali-Midgard driver
EXTRA_CFLAGS += -DMALI_USE_UMP=1 \
                -DMALI_LICENSE_IS_GPL=1 \
                -DMALI_BASE_TRACK_MEMLEAK=0 \
                -DMALI_DEBUG=0 \
                -DMALI_ERROR_INJECT_ON=0 \
                -DMALI_CUSTOMER_RELEASE=1 \
                -DMALI_UNIT_TEST=0 \
                -DMALI_BACKEND_KERNEL=1 \
                -DMALI_NO_MALI=0

DDK_DIR ?= .
ifneq ($(wildcard $(DDK_DIR)/drivers/gpu/arm/t6xx),)
KBASE_DIR = $(DDK_DIR)/drivers/gpu/arm/t6xx/kbase
OSK_DIR = $(DDK_DIR)/drivers/gpu/arm/t6xx/kbase/osk
endif

ifneq ($(wildcard $(DDK_DIR)/drivers/gpu/arm/midgard),)
KBASE_DIR = $(DDK_DIR)/drivers/gpu/arm/midgard
OSK_DIR = $(DDK_DIR)/drivers/gpu/arm/midgard/osk
EXTRA_CFLAGS += -DMALI_DIR_MIDGARD=1
endif

ifneq ($(wildcard $(DDK_DIR)/drivers/gpu/arm/midgard/mali_kbase_gator_api.h),)
EXTRA_CFLAGS += -DMALI_SIMPLE_API=1
EXTRA_CFLAGS += -I$(DDK_DIR)/drivers/gpu/arm/midgard
endif

UMP_DIR = $(DDK_DIR)/include/linux

# Include directories in the DDK
EXTRA_CFLAGS += -I$(KBASE_DIR)/ \
                -I$(KBASE_DIR)/.. \
                -I$(OSK_DIR)/.. \
                -I$(UMP_DIR)/.. \
                -I$(DDK_DIR)/include \
                -I$(KBASE_DIR)/osk/src/linux/include \
                -I$(KBASE_DIR)/platform_dummy \
                -I$(KBASE_DIR)/src

