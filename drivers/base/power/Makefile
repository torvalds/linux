obj-$(CONFIG_PM)	+= sysfs.o
obj-$(CONFIG_PM_SLEEP)	+= main.o wakeup.o
obj-$(CONFIG_PM_RUNTIME)	+= runtime.o
obj-$(CONFIG_PM_OPS)	+= generic_ops.o
obj-$(CONFIG_PM_TRACE_RTC)	+= trace.o
obj-$(CONFIG_PM_OPP)	+= opp.o

ccflags-$(CONFIG_DEBUG_DRIVER) := -DDEBUG
ccflags-$(CONFIG_PM_VERBOSE)   += -DDEBUG
