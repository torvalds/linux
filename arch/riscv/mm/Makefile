
CFLAGS_init.o := -mcmodel=medany
ifdef CONFIG_FTRACE
CFLAGS_REMOVE_init.o = -pg
endif

obj-y += init.o
obj-y += fault.o
obj-y += extable.o
obj-y += ioremap.o
obj-y += cacheflush.o
