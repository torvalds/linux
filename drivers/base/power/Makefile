obj-$(CONFIG_PM)	+= sysfs.o
obj-$(CONFIG_PM_SLEEP)	+= main.o
obj-$(CONFIG_PM_TRACE)	+= trace.o

ifeq ($(CONFIG_DEBUG_DRIVER),y)
EXTRA_CFLAGS += -DDEBUG
endif
ifeq ($(CONFIG_PM_VERBOSE),y)
EXTRA_CFLAGS += -DDEBUG
endif
