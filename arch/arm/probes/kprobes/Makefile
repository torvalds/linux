obj-$(CONFIG_KPROBES)		+= core.o actions-common.o
obj-$(CONFIG_ARM_KPROBES_TEST)	+= test-kprobes.o
test-kprobes-objs		:= test-core.o

ifdef CONFIG_THUMB2_KERNEL
obj-$(CONFIG_KPROBES)		+= actions-thumb.o
test-kprobes-objs		+= test-thumb.o
else
obj-$(CONFIG_KPROBES)		+= actions-arm.o
test-kprobes-objs		+= test-arm.o
endif
