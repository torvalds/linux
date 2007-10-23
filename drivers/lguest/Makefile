# Guest requires the device configuration and probing code.
obj-$(CONFIG_LGUEST_GUEST) += lguest_device.o

# Host requires the other files, which can be a module.
obj-$(CONFIG_LGUEST)	+= lg.o
lg-y = core.o hypercalls.o page_tables.o interrupts_and_traps.o \
	segments.o lguest_user.o

lg-$(CONFIG_X86_32) += x86/switcher_32.o x86/core.o

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
