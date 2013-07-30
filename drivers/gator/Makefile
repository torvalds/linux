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
		gator_events_perf_pmu.o

gator-y +=	gator_events_mmaped.o

ifeq ($(CONFIG_GATOR_WITH_MALI_SUPPORT),y)

ifeq ($(CONFIG_GATOR_MALI_T6XX),y)
gator-y +=	gator_events_mali_t6xx.o \
		gator_events_mali_t6xx_hw.o
include $(M)/mali_t6xx.mk
else
gator-y +=	gator_events_mali_400.o
endif
gator-y +=	gator_events_mali_common.o

ccflags-y += -I$(CONFIG_GATOR_MALI_PATH)
ccflags-$(CONFIG_GATOR_MALI_400MP) += -DMALI_SUPPORT=MALI_400
ccflags-$(CONFIG_GATOR_MALI_T6XX) += -DMALI_SUPPORT=MALI_T6xx
endif

# GATOR_TEST controls whether to include (=1) or exclude (=0) test code. 
GATOR_TEST ?= 0
EXTRA_CFLAGS +=	-DGATOR_TEST=$(GATOR_TEST)

gator-$(CONFIG_ARM) +=	gator_events_armv6.o \
			gator_events_armv7.o \
			gator_events_l2c-310.o \
			gator_events_scorpion.o

$(obj)/gator_main.o: $(obj)/gator_events.h

clean-files := gator_events.h

# Note, in the recipe below we use "cd $(srctree) && cd $(src)" rather than
# "cd $(srctree)/$(src)" because under DKMS  $(src) is an absolute path, and we
# can't just use $(src) because for normal kernel builds this is relative to
# $(srctree)

       chk_events.h = :
 quiet_chk_events.h = echo '  CHK     $@'
silent_chk_events.h = :
$(obj)/gator_events.h: FORCE
	@$($(quiet)chk_events.h)
	$(Q)cd $(srctree) && cd $(src) ; $(CONFIG_SHELL) gator_events.sh $(abspath $@)

else

all:
	@echo
	@echo "usage:"
	@echo "      make -C <kernel_build_dir> M=\`pwd\` ARCH=arm CROSS_COMPILE=<...> modules"
	@echo
	$(error)

clean:
	rm -f *.o .*.cmd gator_events.h modules.order Module.symvers gator.ko gator.mod.c
	rm -rf .tmp_versions

endif
