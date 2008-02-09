ifeq ($(CONFIG_X86_64),y)
        obj-$(CONFIG_PM)		+= suspend_64.o
        obj-$(CONFIG_HIBERNATION)	+= hibernate_asm_64.o
else
	obj-$(CONFIG_PM)		+= cpu.o
	obj-$(CONFIG_HIBERNATION)	+= swsusp.o suspend.o
endif
