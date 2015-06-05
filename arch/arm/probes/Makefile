obj-$(CONFIG_UPROBES)		+= decode.o decode-arm.o uprobes/
obj-$(CONFIG_KPROBES)		+= decode.o kprobes/
ifdef CONFIG_THUMB2_KERNEL
obj-$(CONFIG_KPROBES)		+= decode-thumb.o
else
obj-$(CONFIG_KPROBES)		+= decode-arm.o
endif
