#
# Makefile for rescue (bootstrap) code
#

# CC = gcc-cris -mlinux -march=v32 $(LINUXINCLUDE)
# ccflags-$(CONFIG_ETRAX_ARCH_V32) += -I$(srctree)/include/asm/arch/mach/ \
# 				    -I$(srctree)/include/asm/arch
# asflags-y += -I $(srctree)/include/asm/arch/mach/ -I $(srctree)/include/asm/arch
# LD = gcc-cris -mlinux -march=v32 -nostdlib

ifdef CONFIG_ETRAX_AXISFLASHMAP

arch-$(CONFIG_ETRAX_ARCH_V10) = v10
arch-$(CONFIG_ETRAX_ARCH_V32) = v32

ldflags-y += -T $(srctree)/$(src)/rescue_$(arch-y).lds
OBJCOPYFLAGS = -O binary --remove-section=.bss
obj-$(CONFIG_ETRAX_ARCH_V32) = $(obj)/head_v32.o
obj-$(CONFIG_ETRAX_ARCH_V10) = $(obj)/head_v10.o
OBJECTS := $(obj-y)

targets := rescue.o rescue.bin

$(obj)/rescue.o: $(OBJECTS) FORCE
	$(call if_changed,ld)

$(obj)/rescue.bin: $(obj)/rescue.o FORCE
	$(call if_changed,objcopy)
	cp -p $(obj)/rescue.bin $(objtree)

else
$(obj)/rescue.bin:

endif

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

