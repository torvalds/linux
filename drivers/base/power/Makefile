obj-y			:= shutdown.o
obj-$(CONFIG_PM)	+= main.o suspend.o resume.o runtime.o sysfs.o

ifeq ($(CONFIG_DEBUG_DRIVER),y)
EXTRA_CFLAGS += -DDEBUG
endif
