obj-$(CONFIG_FB_INTEL) += intelfb.o

intelfb-objs := intelfbdrv.o intelfbhw.o

ifdef CONFIG_FB_INTEL_DEBUG
#EXTRA_CFLAGS += -DDEBUG -DVERBOSE -DREGDUMP
EXTRA_CFLAGS += -DDEBUG -DREGDUMP
endif
