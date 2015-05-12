ifneq ($(KERNELRELEASE),)

# Uncomment the following line to enable kernel stack unwinding within gator, or update gator_backtrace.c
# EXTRA_CFLAGS +=	-DGATOR_KERNEL_STACK_UNWINDING

CONFIG_GATOR ?= m
obj-$(CONFIG_GATOR) := gator.o

gator-y :=	gator_main.o \
		gator_events_block.o \
		gator_events_irq.o \
		gator_events_meminfo.o \
		gator_events_mmapped.o \
		gator_events_net.o \
		gator_events_perf_pmu.o \
		gator_events_sched.o \

# Convert the old GATOR_WITH_MALI_SUPPORT to the new kernel flags
ifneq ($(GATOR_WITH_MALI_SUPPORT),)
  CONFIG_GATOR_WITH_MALI_SUPPORT := y
  ifeq ($(GATOR_WITH_MALI_SUPPORT),MALI_MIDGARD)
    CONFIG_GATOR_MALI_4XXMP := n
    CONFIG_GATOR_MALI_MIDGARD := y
  else
    CONFIG_GATOR_MALI_4XXMP := y
    CONFIG_GATOR_MALI_MIDGARD := n
  endif
  EXTRA_CFLAGS += -DMALI_SUPPORT=$(GATOR_WITH_MALI_SUPPORT)
  ifneq ($(GATOR_MALI_INTERFACE_STYLE),)
    EXTRA_CFLAGS += -DGATOR_MALI_INTERFACE_STYLE=$(GATOR_MALI_INTERFACE_STYLE)
  endif
endif

ifeq ($(CONFIG_GATOR_WITH_MALI_SUPPORT),y)
  ifeq ($(CONFIG_GATOR_MALI_MIDGARD),y)
    gator-y +=	gator_events_mali_midgard.o \
		gator_events_mali_midgard_hw.o
    include $(src)/mali_midgard.mk
  else
    gator-y +=	gator_events_mali_4xx.o
  endif
  gator-y +=	gator_events_mali_common.o

  ifneq ($(CONFIG_GATOR_MALI_PATH),)
    ccflags-y += -I$(CONFIG_GATOR_MALI_PATH)
  endif
  ccflags-$(CONFIG_GATOR_MALI_4XXMP) += -DMALI_SUPPORT=MALI_4xx
  ccflags-$(CONFIG_GATOR_MALI_MIDGARD) += -DMALI_SUPPORT=MALI_MIDGARD
endif

# GATOR_TEST controls whether to include (=1) or exclude (=0) test code.
GATOR_TEST ?= 0
EXTRA_CFLAGS +=	-DGATOR_TEST=$(GATOR_TEST)

# Should the original or new block_rq_complete API be used?
OLD_BLOCK_RQ_COMPLETE := $(shell grep -A3 block_rq_complete $(srctree)/include/trace/events/block.h | grep nr_bytes -q; echo $$?)
EXTRA_CFLAGS += -DOLD_BLOCK_RQ_COMPLETE=$(OLD_BLOCK_RQ_COMPLETE)

gator-$(CONFIG_ARM) +=	gator_events_armv6.o \
			gator_events_armv7.o \
			gator_events_l2c-310.o \
			gator_events_scorpion.o

gator-$(CONFIG_ARM64) +=

$(obj)/gator_main.o: $(obj)/gator_src_md5.h

clean-files := gator_src_md5.h

# Note, in the recipe below we use "cd $(srctree) && cd $(src)" rather than
# "cd $(srctree)/$(src)" because under DKMS $(src) is an absolute path, and we
# can't just use $(src) because for normal kernel builds this is relative to
# $(srctree)

       chk_events.h = :
 quiet_chk_events.h = echo '  CHK     $@'
silent_chk_events.h = :
$(obj)/gator_src_md5.h: FORCE
	@$($(quiet)chk_events.h)
	$(Q)cd $(srctree) && cd $(src) ; $(CONFIG_SHELL) -c "echo 'static char *gator_src_md5 = \"'\`ls *.c *.h mali/*.h | grep -Ev '^(gator_src_md5\.c|gator\.mod\.c)$$' | LC_ALL=C sort | xargs cat | md5sum | cut -b 1-32\`'\";'" > $(abspath $@)

else

all:
	@echo
	@echo "usage:"
	@echo "      make -C <kernel_build_dir> M=\`pwd\` ARCH=arm CROSS_COMPILE=<...> modules"
	@echo
	$(error)

clean:
	rm -f *.o .*.cmd gator_src_md5.h modules.order Module.symvers gator.ko gator.mod.c
	rm -rf .tmp_versions

endif
