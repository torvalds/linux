obj-y	:= clock.o
ifneq ($(CONFIG_ARCH_INTEGRATOR),y)
obj-y	+= sched-clock.o
endif
ifeq ($(CONFIG_LEDS_CLASS),y)
obj-$(CONFIG_ARCH_REALVIEW) += leds.o
obj-$(CONFIG_ARCH_VERSATILE) += leds.o
endif
