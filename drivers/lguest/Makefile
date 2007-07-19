# Guest requires the paravirt_ops replacement and the bus driver.
obj-$(CONFIG_LGUEST_GUEST) += lguest.o lguest_asm.o lguest_bus.o

# Host requires the other files, which can be a module.
obj-$(CONFIG_LGUEST)	+= lg.o
lg-y := core.o hypercalls.o page_tables.o interrupts_and_traps.o \
	segments.o io.o lguest_user.o switcher.o
