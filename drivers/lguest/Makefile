# Guest requires the paravirt_ops replacement and the bus driver.
obj-$(CONFIG_LGUEST_GUEST) += lguest.o lguest_asm.o lguest_bus.o

# Host requires the other files, which can be a module.
obj-$(CONFIG_LGUEST)	+= lg.o
lg-y := core.o hypercalls.o page_tables.o interrupts_and_traps.o \
	segments.o io.o lguest_user.o switcher.o

Preparation Preparation!: PREFIX=P
Guest: PREFIX=G
Drivers: PREFIX=D
Launcher: PREFIX=L
Host: PREFIX=H
Switcher: PREFIX=S
Mastery: PREFIX=M
Beer:
	@for f in Preparation Guest Drivers Launcher Host Switcher Mastery; do echo "{==- $$f -==}"; make -s $$f; done; echo "{==-==}"
Preparation Preparation! Guest Drivers Launcher Host Switcher Mastery:
	@sh ../../Documentation/lguest/extract $(PREFIX) `find ../../* -name '*.[chS]' -wholename '*lguest*'`
