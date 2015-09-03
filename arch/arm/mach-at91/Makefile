#
# Makefile for the linux kernel.
#
obj-y		:= soc.o

# CPU-specific support
obj-$(CONFIG_SOC_AT91RM9200)	+= at91rm9200.o
obj-$(CONFIG_SOC_AT91SAM9)	+= at91sam9.o
obj-$(CONFIG_SOC_SAMA5)		+= sama5.o

# Power Management
obj-$(CONFIG_PM)		+= pm.o
obj-$(CONFIG_PM)		+= pm_suspend.o

ifeq ($(CONFIG_CPU_V7),y)
AFLAGS_pm_suspend.o := -march=armv7-a
endif
ifeq ($(CONFIG_PM_DEBUG),y)
CFLAGS_pm.o += -DDEBUG
endif
