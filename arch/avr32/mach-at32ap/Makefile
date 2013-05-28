obj-y				+= pdc.o clock.o intc.o extint.o pio.o hsmc.o
obj-y				+= hmatrix.o
obj-$(CONFIG_CPU_AT32AP700X)	+= at32ap700x.o pm-at32ap700x.o
obj-$(CONFIG_PM)		+= pm.o

ifeq ($(CONFIG_PM_DEBUG),y)
CFLAGS_pm.o	+= -DDEBUG
endif
