#
# Makefile for rescue (bootstrap) code
#

CC = gcc-cris -mlinux $(LINUXINCLUDE)
ccflags-y += -O2
asflags-y += -traditional
LD = gcc-cris -mlinux -nostdlib
ldflags-y += -T $(obj)/rescue.ld
OBJCOPY = objcopy-cris
OBJCOPYFLAGS = -O binary --remove-section=.bss
obj-$(CONFIG_ETRAX_AXISFLASHMAP) = head.o
OBJECT := $(obj)/head.o

targets := rescue.o rescue.bin

$(obj)/rescue.o: $(OBJECT) FORCE
	$(call if_changed,ld)

$(obj)/rescue.bin: $(obj)/rescue.o FORCE
	$(call if_changed,objcopy)
	cp -p $(obj)/rescue.bin $(objtree)

$(obj)/testrescue.bin: $(obj)/testrescue.o
	$(OBJCOPY) $(OBJCOPYFLAGS) $(obj)/testrescue.o tr.bin
# Pad it to 784 bytes
	dd if=/dev/zero of=tmp2423 bs=1 count=784
	cat tr.bin tmp2423 >testrescue_tmp.bin
	dd if=testrescue_tmp.bin of=$(obj)/testrescue.bin bs=1 count=784
	rm tr.bin tmp2423 testrescue_tmp.bin

$(obj)/kimagerescue.bin: $(obj)/kimagerescue.o
	$(OBJCOPY) $(OBJCOPYFLAGS) $(obj)/kimagerescue.o ktr.bin
# Pad it to 784 bytes, that's what the rescue loader expects
	dd if=/dev/zero of=tmp2423 bs=1 count=784
	cat ktr.bin tmp2423 >kimagerescue_tmp.bin
	dd if=kimagerescue_tmp.bin of=$(obj)/kimagerescue.bin bs=1 count=784
	rm ktr.bin tmp2423 kimagerescue_tmp.bin
