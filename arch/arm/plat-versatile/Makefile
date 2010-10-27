obj-y	:= clock.o
obj-$(CONFIG_ARM_TIMER_SP804) += timer-sp.o
obj-$(CONFIG_ARCH_REALVIEW) += sched-clock.o
obj-$(CONFIG_ARCH_VERSATILE) += sched-clock.o
ifeq ($(CONFIG_LEDS_CLASS),y)
obj-$(CONFIG_ARCH_REALVIEW) += leds.o
obj-$(CONFIG_ARCH_VERSATILE) += leds.o
endif
