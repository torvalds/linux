obj-y	:= clock.o
obj-$(CONFIG_ARM_TIMER_SP804) += timer-sp.o
ifneq ($(CONFIG_ARCH_INTEGRATOR),y)
obj-y	+= sched-clock.o
endif
ifeq ($(CONFIG_LEDS_CLASS),y)
obj-$(CONFIG_ARCH_REALVIEW) += leds.o
obj-$(CONFIG_ARCH_VERSATILE) += leds.o
endif
