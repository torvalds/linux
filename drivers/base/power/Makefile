obj-y			:= shutdown.o
obj-$(CONFIG_PM)	+= main.o suspend.o resume.o runtime.o sysfs.o
obj-$(CONFIG_PM_TRACE)	+= trace.o

ifeq ($(CONFIG_DEBUG_DRIVER),y)
EXTRA_CFLAGS += -DDEBUG
endif
ifeq ($(CONFIG_PM_DEBUG),y)
EXTRA_CFLAGS += -DDEBUG
endif
