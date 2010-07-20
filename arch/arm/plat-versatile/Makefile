obj-y	:= clock.o
obj-$(CONFIG_ARM_TIMER_SP804) += timer-sp.o
# For all but the Integrator, compile these
ifeq ($(CONFIG_ARCH_INTEGRATOR),)
obj-y += sched-clock.o
obj-$(CONFIG_LEDS_CLASS) += leds.o
endif
