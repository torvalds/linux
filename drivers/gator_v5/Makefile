ifeq ($(CONFIG_ARM_STREAMLINE_DEBUGGER),y)

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
gator-y +=	gator_events_mali_t6xx.o
else
gator-y +=	gator_events_mali.o
endif
EXTRA_CFLAGS +=	-DMALI_SUPPORT=$(GATOR_WITH_MALI_SUPPORT)
endif

gator-$(CONFIG_ARM) +=	gator_events_armv6.o \
			gator_events_armv7.o \
			gator_events_l2c-310.o \
			gator_events_scorpion.o

$(obj)/gator_main.o: gator_events.h

clean-files := gator_events.h

chk_events.h = :
quiet_chk_events.h = echo '  CHK     $@'
silent_chk_events.h = :
gator_events.h: FORCE
	@$($(quiet)chk_events.h)
	$(CONFIG_SHELL) $(obj)/gator_events.sh $@ $(obj)

endif
