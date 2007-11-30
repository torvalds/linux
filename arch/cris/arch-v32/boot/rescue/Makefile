#
# Makefile for rescue (bootstrap) code
#

CC = gcc-cris -mlinux -march=v32 $(LINUXINCLUDE)
EXTRA_CFLAGS = -O2
AFLAGS += -I $(TOPDIR)/include/asm/arch/mach/ -I $(TOPDIR)/include/asm/arch
EXTRA_CFLAGS += -I $(TOPDIR)/include/asm/arch/mach/ -I $(TOPDIR)/include/asm/arch
LD = gcc-cris -mlinux -march=v32 -nostdlib
LDFLAGS = -T $(obj)/rescue.ld
LDPOSTFLAGS = -lgcc
OBJCOPY = objcopy-cris
OBJCOPYFLAGS = -O binary --remove-section=.bss
obj-$(CONFIG_ETRAX_AXISFLASHMAP) = head.o
OBJECT := $(obj)/head.o

targets := rescue.o rescue.bin

quiet_cmd_ldlibgcc = LD      $@
cmd_ldlibgcc = $(LD) $(LDFLAGS) $(filter-out FORCE,$^) $(LDPOSTFLAGS) -o $@

$(obj)/rescue.o: $(OBJECTS) FORCE
	$(call if_changed,ldlibgcc)

$(obj)/rescue.bin: $(obj)/rescue.o FORCE
	$(call if_changed,objcopy)
	cp -p $(obj)/rescue.bin $(objtree)
