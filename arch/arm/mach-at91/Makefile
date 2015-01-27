#
# Makefile for the linux kernel.
#

obj-y		:= setup.o

obj-$(CONFIG_SOC_AT91SAM9)	+= sam9_smc.o

# CPU-specific support
obj-$(CONFIG_SOC_AT91RM9200)	+= at91rm9200_time.o

# AT91SAM board with device-tree
obj-$(CONFIG_SOC_AT91RM9200) += board-dt-rm9200.o
obj-$(CONFIG_SOC_AT91SAM9) += board-dt-sam9.o

# SAMA5 board with device-tree
obj-$(CONFIG_SOC_SAMA5)		+= board-dt-sama5.o

# Power Management
obj-$(CONFIG_PM)		+= pm.o
obj-$(CONFIG_AT91_SLOW_CLOCK)	+= pm_slowclock.o

ifeq ($(CONFIG_PM_DEBUG),y)
CFLAGS_pm.o += -DDEBUG
endif
