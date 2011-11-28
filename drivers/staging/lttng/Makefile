#
# Makefile for the LTTng modules.
#

obj-m += ltt-ring-buffer-client-discard.o
obj-m += ltt-ring-buffer-client-overwrite.o
obj-m += ltt-ring-buffer-metadata-client.o
obj-m += ltt-ring-buffer-client-mmap-discard.o
obj-m += ltt-ring-buffer-client-mmap-overwrite.o
obj-m += ltt-ring-buffer-metadata-mmap-client.o

obj-m += ltt-relay.o
ltt-relay-objs :=  ltt-events.o ltt-debugfs-abi.o \
			ltt-probes.o ltt-context.o \
			lttng-context-pid.o lttng-context-procname.o \
			lttng-context-prio.o lttng-context-nice.o \
			lttng-context-vpid.o lttng-context-tid.o \
			lttng-context-vtid.o lttng-context-ppid.o \
			lttng-context-vppid.o lttng-calibrate.o

ifneq ($(CONFIG_HAVE_SYSCALL_TRACEPOINTS),)
ltt-relay-objs += lttng-syscalls.o
endif

ifneq ($(CONFIG_PERF_EVENTS),)
ltt-relay-objs += $(shell \
	if [ $(VERSION) -ge 3 \
		-o \( $(VERSION) -eq 2 -a $(PATCHLEVEL) -ge 6 -a $(SUBLEVEL) -ge 33 \) ] ; then \
		echo "lttng-context-perf-counters.o" ; fi;)
endif

obj-m += probes/
obj-m += lib/
