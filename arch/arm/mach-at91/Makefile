#
# Makefile for the linux kernel.
#

obj-y		:= setup.o

obj-$(CONFIG_SOC_AT91SAM9)	+= sam9_smc.o

# CPU-specific support
obj-$(CONFIG_SOC_AT91RM9200)	+= at91rm9200.o at91rm9200_time.o
obj-$(CONFIG_SOC_AT91SAM9)	+= at91sam9.o
obj-$(CONFIG_SOC_SAMA5)		+= sama5.o

# Power Management
obj-$(CONFIG_PM)		+= pm.o
obj-$(CONFIG_AT91_SLOW_CLOCK)	+= pm_slowclock.o

ifeq ($(CONFIG_PM_DEBUG),y)
CFLAGS_pm.o += -DDEBUG
endif
