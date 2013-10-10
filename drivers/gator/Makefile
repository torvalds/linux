ifneq ($(KERNELRELEASE),)

# Uncomment the following line to enable kernel stack unwinding within gator, or update gator_backtrace.c
# EXTRA_CFLAGS +=	-DGATOR_KERNEL_STACK_UNWINDING

obj-m := gator.o

gator-y :=	gator_main.o \
		gator_events_irq.o \
		gator_events_sched.o \
		gator_events_net.o \
		gator_events_block.o \
		gator_events_meminfo.o \
		gator_events_perf_pmu.o

gator-y +=	gator_events_mmaped.o

ifneq ($(GATOR_WITH_MALI_SUPPORT),)
ifeq ($(GATOR_WITH_MALI_SUPPORT),MALI_T6xx)
gator-y +=	gator_events_mali_t6xx.o \
		gator_events_mali_t6xx_hw.o
include $(M)/mali_t6xx.mk
else
gator-y +=	gator_events_mali_4xx.o
endif
gator-y +=	gator_events_mali_common.o
EXTRA_CFLAGS +=	-DMALI_SUPPORT=$(GATOR_WITH_MALI_SUPPORT)
ifneq ($(GATOR_MALI_INTERFACE_STYLE),)
EXTRA_CFLAGS += -DGATOR_MALI_INTERFACE_STYLE=$(GATOR_MALI_INTERFACE_STYLE)
endif
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

$(obj)/gator_main.o: gator_events.h

clean-files := gator_events.h

       chk_events.h = :
 quiet_chk_events.h = echo '  CHK     $@'
silent_chk_events.h = :
gator_events.h: FORCE
	@$($(quiet)chk_events.h)
	$(Q)cd $(obj) ; $(CONFIG_SHELL) $(obj)/gator_events.sh $@

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
