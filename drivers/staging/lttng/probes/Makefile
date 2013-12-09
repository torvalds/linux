#
# Makefile for the LTT probes.
# Only build from the package top-level directory. Never use with make directly.

ifneq ($(KERNELRELEASE),)
ifneq ($(CONFIG_TRACEPOINTS),)

ccflags-y += -I$(PWD)/probes
obj-m += lttng-types.o

obj-m += lttng-probe-sched.o
obj-m += lttng-probe-irq.o
obj-m += lttng-probe-timer.o
obj-m += lttng-probe-kmem.o
obj-m += lttng-probe-module.o
obj-m += lttng-probe-power.o

obj-m += lttng-probe-statedump.o

ifneq ($(CONFIG_KVM),)
obj-m += lttng-probe-kvm.o
ifneq ($(CONFIG_X86),)
kvm_dep = $(srctree)/virt/kvm/iodev.h
ifneq ($(wildcard $(kvm_dep)),)
CFLAGS_lttng-probe-kvm-x86.o += -I$(srctree)/virt/kvm
CFLAGS_lttng-probe-kvm-x86-mmu.o += -I$(srctree)/virt/kvm
obj-m += lttng-probe-kvm-x86.o
obj-m += lttng-probe-kvm-x86-mmu.o
else
$(warning File $(kvm_dep) not found. Probe "kvm" x86-specific is disabled. Use full kernel source tree to enable it.)
endif
endif
endif

obj-m +=  $(shell \
	if [ $(VERSION) -ge 3 \
		-o \( $(VERSION) -eq 2 -a $(PATCHLEVEL) -ge 6 -a $(SUBLEVEL) -ge 33 \) ] ; then \
		echo "lttng-probe-signal.o" ; fi;)

ifneq ($(CONFIG_BLOCK),)
ifneq ($(CONFIG_EVENT_TRACING),)	# need blk_cmd_buf_len
obj-m += lttng-probe-block.o
endif
endif

ifneq ($(CONFIG_NET),)
obj-m += lttng-probe-napi.o
obj-m += lttng-probe-skb.o
obj-m +=  $(shell \
	if [ $(VERSION) -ge 3 \
		-o \( $(VERSION) -eq 2 -a $(PATCHLEVEL) -ge 6 -a $(SUBLEVEL) -ge 37 \) ] ; then \
		echo "lttng-probe-net.o" ; fi;)
obj-m +=  $(shell \
	if [ $(VERSION) -ge 3 -a $(PATCHLEVEL) -ge 1 ] ; then \
		echo "lttng-probe-sock.o" ; fi;)
obj-m +=  $(shell \
	if [ $(VERSION) -ge 3 -a $(PATCHLEVEL) -ge 1 ] ; then \
		echo "lttng-probe-udp.o" ; fi;)
endif

ifneq ($(CONFIG_SND_SOC),)
obj-m +=  $(shell \
	if [ $(VERSION) -ge 3 \
		-o \( $(VERSION) -eq 2 -a $(PATCHLEVEL) -ge 6 -a $(SUBLEVEL) -ge 38 \) ] ; then \
		echo "lttng-probe-asoc.o" ; fi;)
endif

ifneq ($(CONFIG_EXT3_FS),)
ext3_dep = $(srctree)/fs/ext3/*.h
ext3_dep_check = $(wildcard $(ext3_dep))
ext3 = $(shell \
	if [ $(VERSION) -ge 3 -a $(PATCHLEVEL) -ge 1 ] ; then \
		if [ $(VERSION) -ge 3 -a $(PATCHLEVEL) -ge 4 -a \
			-z "$(ext3_dep_check)" ] ; then \
			echo "warn" ; \
			exit ; \
		fi; \
		echo "lttng-probe-ext3.o" ; \
	fi;)
ifeq ($(ext3),warn)
$(warning Files $(ext3_dep) not found. Probe "ext3" is disabled. Use full kernel source tree to enable it.)
ext3 =
endif
obj-m += $(ext3)
endif

ifneq ($(CONFIG_GPIOLIB),)
obj-m +=  $(shell \
	if [ $(VERSION) -ge 3 ] ; then \
		echo "lttng-probe-gpio.o" ; fi;)
endif

ifneq ($(CONFIG_JBD2),)
obj-m += lttng-probe-jbd2.o
endif

ifneq ($(CONFIG_JBD),)
obj-m +=  $(shell \
	if [ $(VERSION) -ge 3 -a $(PATCHLEVEL) -ge 1 ] ; then \
		echo "lttng-probe-jbd.o" ; fi;)
endif

ifneq ($(CONFIG_REGULATOR),)
obj-m +=  $(shell \
	if [ $(VERSION) -ge 3 \
		-o \( $(VERSION) -eq 2 -a $(PATCHLEVEL) -ge 6 -a $(SUBLEVEL) -ge 38 \) ] ; then \
		echo "lttng-probe-regulator.o" ; fi;)
endif

ifneq ($(CONFIG_SCSI),)
obj-m +=  $(shell \
	if [ $(VERSION) -ge 3 \
		-o \( $(VERSION) -eq 2 -a $(PATCHLEVEL) -ge 6 -a $(SUBLEVEL) -ge 35 \) ] ; then \
		echo "lttng-probe-scsi.o" ; fi;)
endif

vmscan = $(shell \
	if [ $(VERSION) -ge 3 \
		-o \( $(VERSION) -eq 2 -a $(PATCHLEVEL) -ge 6 -a $(SUBLEVEL) -ge 36 \) ] ; then \
		echo "lttng-probe-vmscan.o" ; fi;)
ifneq ($(CONFIG_SWAP),)
	obj-m += $(vmscan)
else
ifneq ($(CONFIG_CGROUP_MEM_RES_CTLR),)
	obj-m += $(vmscan)
endif
endif

# lock probe does not work, so disabling it for now
#ifneq ($(CONFIG_LOCKDEP),)
#obj-m += lttng-probe-lock.o
#endif

ifneq ($(CONFIG_BTRFS_FS),)
btrfs_dep = $(srctree)/fs/btrfs/*.h
btrfs = $(shell \
	if [ $(VERSION) -ge 3 \
		-o \( $(VERSION) -eq 2 -a $(PATCHLEVEL) -ge 6 -a $(SUBLEVEL) -ge 39 \) ] ; then \
		echo "lttng-probe-btrfs.o" ; fi;)
ifneq ($(btrfs),)
ifeq ($(wildcard $(btrfs_dep)),)
$(warning Files $(btrfs_dep) not found. Probe "btrfs" is disabled. Use full kernel source tree to enable it.)
btrfs =
endif
endif
obj-m += $(btrfs)
endif

obj-m +=  $(shell \
	if [ $(VERSION) -ge 3 \
		-o \( $(VERSION) -eq 2 -a $(PATCHLEVEL) -ge 6 -a $(SUBLEVEL) -ge 38 \) ] ; then \
		echo "lttng-probe-compaction.o" ; fi;)

ifneq ($(CONFIG_EXT4_FS),)
ext4_dep = $(srctree)/fs/ext4/*.h
ext4 = lttng-probe-ext4.o
ifeq ($(wildcard $(ext4_dep)),)
$(warning Files $(ext4_dep) not found. Probe "ext4" is disabled. Use full kernel source tree to enable it.)
ext4 =
endif
obj-m += $(ext4)
endif

obj-m +=  $(shell \
	if [ $(VERSION) -ge 3 -a $(PATCHLEVEL) -ge 4 ] ; then \
		echo "lttng-probe-printk.o" ; fi;)
ifneq ($(CONFIG_FRAME_WARN),0)
CFLAGS_lttng-probe-printk.o += -Wframe-larger-than=2200
endif

obj-m +=  $(shell \
	if [ \( $(VERSION) -eq 3 -a $(PATCHLEVEL) -ge 6 \) \
		-o \( $(VERSION) -eq 3 -a $(PATCHLEVEL) -eq 5 -a $(SUBLEVEL) -ge 2 \) \
		-o \( $(VERSION) -eq 3 -a $(PATCHLEVEL) -eq 4 -a $(SUBLEVEL) -ge 9 \) \
		-o \( $(VERSION) -eq 3 -a $(PATCHLEVEL) -eq 0 -a $(SUBLEVEL) -ge 41 \) ] ; then \
		echo "lttng-probe-random.o" ; fi;)

obj-m +=  $(shell \
	if [ $(VERSION) -ge 3 -a $(PATCHLEVEL) -ge 2 ] ; then \
		echo "lttng-probe-rcu.o" ; fi;)

ifneq ($(CONFIG_REGMAP),)
obj-m +=  $(shell \
	if [ $(VERSION) -ge 3 -a $(PATCHLEVEL) -ge 2 ] ; then \
		echo "lttng-probe-regmap.o" ; fi;)
endif

ifneq ($(CONFIG_PM_RUNTIME),)
obj-m +=  $(shell \
	if [ $(VERSION) -ge 3 -a $(PATCHLEVEL) -ge 2 ] ; then \
		echo "lttng-probe-rpm.o" ; fi;)
endif

ifneq ($(CONFIG_SUNRPC),)
obj-m +=  $(shell \
	if [ $(VERSION) -ge 3 -a $(PATCHLEVEL) -ge 4 ] ; then \
		echo "lttng-probe-sunrpc.o" ; fi;)
endif

obj-m += lttng-probe-workqueue.o

ifneq ($(CONFIG_KALLSYMS_ALL),)
obj-m +=  $(shell \
	if [ $(VERSION) -ge 3 \
		-o \( $(VERSION) -eq 2 -a $(PATCHLEVEL) -ge 6 -a $(SUBLEVEL) -ge 36 \) ] ; then \
		echo "lttng-probe-writeback.o" ; fi;)
endif


ifneq ($(CONFIG_KPROBES),)
obj-m += lttng-kprobes.o
endif


ifneq ($(CONFIG_KRETPROBES),)
obj-m += lttng-kretprobes.o
endif

ifneq ($(CONFIG_DYNAMIC_FTRACE),)
obj-m += lttng-ftrace.o
endif

endif

else
	KERNELDIR ?= /lib/modules/$(shell uname -r)/build
	PWD := $(shell pwd)
	CFLAGS = $(EXTCFLAGS)

default:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules

modules_install:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules_install
	/sbin/depmod -a

clean:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) clean

endif
