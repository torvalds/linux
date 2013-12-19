ifneq ($(KERNELRELEASE),)

# Uncomment the following line to enable kernel stack unwinding within gator, or update gator_backtrace.c
# EXTRA_CFLAGS +=	-DGATOR_KERNEL_STACK_UNWINDING

CONFIG_GATOR ?= m
obj-$(CONFIG_GATOR) := gator.o

gator-y :=	gator_main.o \
		gator_events_irq.o \
		gator_events_sched.o \
		gator_events_net.o \
		gator_events_block.o \
		gator_events_meminfo.o \
		gator_events_perf_pmu.o \
		gator_events_mmapped.o \

# Convert the old GATOR_WITH_MALI_SUPPORT to the new kernel flags
ifneq ($(GATOR_WITH_MALI_SUPPORT),)
  CONFIG_GATOR_WITH_MALI_SUPPORT := y
  ifeq ($(GATOR_WITH_MALI_SUPPORT),MALI_T6xx)
    CONFIG_GATOR_MALI_4XXMP := n
    CONFIG_GATOR_MALI_T6XX := y
  else
    CONFIG_GATOR_MALI_4XXMP := y
    CONFIG_GATOR_MALI_T6XX := n
  endif
  EXTRA_CFLAGS += -DMALI_SUPPORT=$(GATOR_WITH_MALI_SUPPORT)
  ifneq ($(GATOR_MALI_INTERFACE_STYLE),)
    EXTRA_CFLAGS += -DGATOR_MALI_INTERFACE_STYLE=$(GATOR_MALI_INTERFACE_STYLE)
  endif
endif

ifeq ($(CONFIG_GATOR_WITH_MALI_SUPPORT),y)
  ifeq ($(CONFIG_GATOR_MALI_T6XX),y)
    gator-y +=	gator_events_mali_t6xx.o \
		gator_events_mali_t6xx_hw.o
    include $(src)/mali_t6xx.mk
  else
    gator-y +=	gator_events_mali_4xx.o
  endif
  gator-y +=	gator_events_mali_common.o

  ifneq ($(CONFIG_GATOR_MALI_PATH),)
    ccflags-y += -I$(CONFIG_GATOR_MALI_PATH)
  endif
  ccflags-$(CONFIG_GATOR_MALI_4XXMP) += -DMALI_SUPPORT=MALI_4xx
  ccflags-$(CONFIG_GATOR_MALI_T6XX) += -DMALI_SUPPORT=MALI_T6xx
endif

# GATOR_TEST controls whether to include (=1) or exclude (=0) test code. 
GATOR_TEST ?= 0
EXTRA_CFLAGS +=	-DGATOR_TEST=$(GATOR_TEST)

gator-$(CONFIG_ARM) +=	gator_events_armv6.o \
			gator_events_armv7.o \
			gator_events_ccn-504.o \
			gator_events_l2c-310.o \
			gator_events_scorpion.o

gator-$(CONFIG_ARM64) +=	gator_events_ccn-504.o

else

all:
	@echo
	@echo "usage:"
	@echo "      make -C <kernel_build_dir> M=\`pwd\` ARCH=arm CROSS_COMPILE=<...> modules"
	@echo
	$(error)

clean:
	rm -f *.o .*.cmd modules.order Module.symvers gator.ko gator.mod.c
	rm -rf .tmp_versions

endif
