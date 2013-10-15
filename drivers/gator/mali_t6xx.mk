# Defines for Mali-T6xx driver
EXTRA_CFLAGS += -DMALI_USE_UMP=1 \
                -DMALI_LICENSE_IS_GPL=1 \
                -DMALI_BASE_TRACK_MEMLEAK=0 \
                -DMALI_DEBUG=0 \
                -DMALI_ERROR_INJECT_ON=0 \
                -DMALI_CUSTOMER_RELEASE=1 \
                -DMALI_UNIT_TEST=0 \
                -DMALI_BACKEND_KERNEL=1 \
                -DMALI_NO_MALI=0

KBASE_DIR = $(DDK_DIR)/kernel/drivers/gpu/arm/t6xx/kbase
OSK_DIR = $(DDK_DIR)/kernel/drivers/gpu/arm/t6xx/kbase/osk
UMP_DIR = $(DDK_DIR)/kernel/include/linux

# Include directories in the DDK
EXTRA_CFLAGS += -I$(DDK_DIR) \
                -I$(KBASE_DIR)/.. \
                -I$(OSK_DIR)/.. \
                -I$(UMP_DIR)/.. \
                -I$(DDK_DIR)/kernel/include \
                -I$(KBASE_DIR)/osk/src/linux/include \
                -I$(KBASE_DIR)/platform_dummy \
                -I$(KBASE_DIR)/src

