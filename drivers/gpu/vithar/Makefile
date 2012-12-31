#############################################################################################
#
#   Vithar DDK Makefile
#
#############################################################################################

MALI_KBASE_SCHEDULING_POLICY=cfs
PLATFORM=dummy
MALI_BACKEND_KERNEL=1
MALI_NO_MALI=0
MALI_USE_UMP=1
MALI_DEBUG=0
MALI_ERROR_INJECT_ON=0
MALI_ANDROID=1
MALI_BASE_TRACK_MEMLEAK=0
MALI_KERNEL_TEST_API=0
MALI_UNIT_TEST=0
MALI_INFINITE_CACHE=0
MALI_LICENSE_IS_GPL=1
MALI_FAKE_PLATFORM_DEVICE=1
MALI_MOCK_TEST=0
MALI_HW_VERSION=r0p0_beta_eco1
MALI_UNCACHED=1
MALI_INSTRUMENTATION_LEVEL=0
MALI_CUSTOMER_RELEASE=0

MALI_PLATFORM_CONFIG=exynos5
MALI_GATOR_SUPPORT=0

#MALI_HW_TYPE=2 is disappeared in 12wk05

# define path
#ROOT=.
VITHAR_ROOT=drivers/gpu/vithar
#CONFIG_VITHAR = y

##############################################################################################
#OSK
##############################################################################################
ROOT=$(VITHAR_ROOT)
OSK_PATH=osk/src/linux
KBUILD_EXTMOD=$(VITHAR_ROOT)/$(OSK_PATH)

include $(ROOT)/$(OSK_PATH)/Makefile

#$(filter-out $(SRC:.c=.o),$(lib-y))

obj-$(CONFIG_VITHAR) += $(addprefix $(OSK_PATH)/,$(SRC:.c=.o))

##############################################################################################
#KBASE
##############################################################################################
ROOT=$(VITHAR_ROOT)
KBASE_PATH=kbase/src
KBUILD_EXTMOD=$(VITHAR_ROOT)/$(KBASE_PATH)

include $(ROOT)/$(KBASE_PATH)/Makefile

obj-$(CONFIG_VITHAR) += $(addprefix $(KBASE_PATH)/,$(SRC:.c=.o))

##############################################################################################
# UKK
##############################################################################################

ROOT=$(VITHAR_ROOT)
UKK_PATH=uk/src/ukk/linux
KBUILD_EXTMOD=$(VITHAR_ROOT)/$(UKK_PATH)

include $(ROOT)/$(UKK_PATH)/Makefile

obj-$(CONFIG_VITHAR) += $(addprefix $(UKK_PATH)/,$(SRC:.c=.o))


##############################################################################################
# UMP
##############################################################################################

ROOT=$(VITHAR_ROOT)
UMP_PATH=ump/src/devicedrv
UMP_FILE_PREFIX=
KBUILD_EXTMOD=$(VITHAR_ROOT)/$(UMP_PATH)

include $(ROOT)/$(UMP_PATH)/Makefile

obj-$(CONFIG_VITHAR) += $(addprefix $(UMP_PATH)/,$(SRC:.c=.o))


#=============================================================================================



