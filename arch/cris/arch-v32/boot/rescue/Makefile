#
# Makefile for rescue (bootstrap) code
#

CC = gcc-cris -mlinux -march=v32 $(LINUXINCLUDE)
ccflags-y += -O2 -I $(srctree)/include/asm/arch/mach/ \
		-I $(srctree)/include/asm/arch
asflags-y += -I $(srctree)/include/asm/arch/mach/ -I $(srctree)/include/asm/arch
LD = gcc-cris -mlinux -march=v32 -nostdlib
ldflags-y += -T $(obj)/rescue.ld
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
