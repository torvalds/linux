#
# Makefile for the linux kernel.
#

obj-y		:= setup.o sysirq_mask.o

obj-$(CONFIG_SOC_AT91SAM9)	+= sam9_smc.o

# CPU-specific support
obj-$(CONFIG_SOC_AT91RM9200)	+= at91rm9200.o at91rm9200_time.o
obj-$(CONFIG_SOC_AT91SAM9260)	+= at91sam9260.o
obj-$(CONFIG_SOC_AT91SAM9261)	+= at91sam9261.o
obj-$(CONFIG_SOC_AT91SAM9263)	+= at91sam9263.o
obj-$(CONFIG_SOC_AT91SAM9G45)	+= at91sam9g45.o
obj-$(CONFIG_SOC_AT91SAM9N12)	+= at91sam9n12.o
obj-$(CONFIG_SOC_AT91SAM9X5)	+= at91sam9x5.o
obj-$(CONFIG_SOC_AT91SAM9RL)	+= at91sam9rl.o
obj-$(CONFIG_SOC_SAMA5D3)	+= sama5d3.o
obj-$(CONFIG_SOC_SAMA5D4)	+= sama5d4.o

# AT91SAM board with device-tree
obj-$(CONFIG_MACH_AT91RM9200_DT) += board-dt-rm9200.o
obj-$(CONFIG_MACH_AT91SAM9_DT) += board-dt-sam9.o

# SAMA5 board with device-tree
obj-$(CONFIG_SOC_SAMA5)		+= board-dt-sama5.o

# Power Management
obj-$(CONFIG_PM)		+= pm.o
obj-$(CONFIG_AT91_SLOW_CLOCK)	+= pm_slowclock.o

ifeq ($(CONFIG_PM_DEBUG),y)
CFLAGS_pm.o += -DDEBUG
endif
