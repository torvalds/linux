#
# Makefile for the LTTng modules.
#

obj-m += lttng-ring-buffer-client-discard.o
obj-m += lttng-ring-buffer-client-overwrite.o
obj-m += lttng-ring-buffer-metadata-client.o
obj-m += lttng-ring-buffer-client-mmap-discard.o
obj-m += lttng-ring-buffer-client-mmap-overwrite.o
obj-m += lttng-ring-buffer-metadata-mmap-client.o

obj-m += lttng-tracer.o
lttng-tracer-objs :=  lttng-events.o lttng-abi.o \
			lttng-probes.o lttng-context.o \
			lttng-context-pid.o lttng-context-procname.o \
			lttng-context-prio.o lttng-context-nice.o \
			lttng-context-vpid.o lttng-context-tid.o \
			lttng-context-vtid.o lttng-context-ppid.o \
			lttng-context-vppid.o lttng-calibrate.o \
			lttng-context-hostname.o wrapper/random.o

obj-m += lttng-statedump.o
lttng-statedump-objs := lttng-statedump-impl.o wrapper/irqdesc.o \
			wrapper/fdtable.o

ifneq ($(CONFIG_HAVE_SYSCALL_TRACEPOINTS),)
lttng-tracer-objs += lttng-syscalls.o probes/lttng-probe-user.o
endif

ifneq ($(CONFIG_PERF_EVENTS),)
lttng-tracer-objs += $(shell \
	if [ $(VERSION) -ge 3 \
		-o \( $(VERSION) -eq 2 -a $(PATCHLEVEL) -ge 6 -a $(SUBLEVEL) -ge 33 \) ] ; then \
		echo "lttng-context-perf-counters.o" ; fi;)
endif

obj-m += probes/
obj-m += lib/
