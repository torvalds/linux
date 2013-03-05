#
# MAKEFILE FOR THE MIPS LINUX BOOTLOADER AND ROM DEBUGGER
#
# i-data Networks
#
# Author: Thomas Horsten <thh@i-data.com>
#

ifndef Version
 Version = "$(USER)-test"
endif

MKLASATIMG = mklasatimg
MKLASATIMG_ARCH = mq2,mqpro,sp100,sp200
KERNEL_IMAGE = vmlinux
KERNEL_START = $(shell $(NM) $(KERNEL_IMAGE) | grep " _text" | cut -f1 -d\ )
KERNEL_ENTRY = $(shell $(NM) $(KERNEL_IMAGE) | grep kernel_entry | cut -f1 -d\ )

LDSCRIPT= -L$(srctree)/$(src) -Tromscript.normal

HEAD_DEFINES := -D_kernel_start=0x$(KERNEL_START) \
		-D_kernel_entry=0x$(KERNEL_ENTRY) \
		-D VERSION="\"$(Version)\"" \
		-D TIMESTAMP=$(shell date +%s)

$(obj)/head.o: $(obj)/head.S $(KERNEL_IMAGE)
	$(CC) -fno-pic $(HEAD_DEFINES) $(LINUXINCLUDE) -c -o $@ $<

OBJECTS = head.o kImage.o

rom.sw: $(obj)/rom.sw
rom.bin:	$(obj)/rom.bin

$(obj)/rom.sw:	$(obj)/rom.bin
	$(MKLASATIMG) -o $@ -k $^ -m $(MKLASATIMG_ARCH)

$(obj)/rom.bin: $(obj)/rom
	$(OBJCOPY) -O binary -S $^ $@

# Rule to make the bootloader
$(obj)/rom: $(addprefix $(obj)/,$(OBJECTS))
	$(LD) $(LDFLAGS) $(LDSCRIPT) -o $@ $^

$(obj)/%.o: $(obj)/%.gz
	$(LD) -r -o $@ -b binary $<

$(obj)/%.gz: $(obj)/%.bin
	gzip -cf -9 $< > $@

$(obj)/kImage.bin: $(KERNEL_IMAGE)
	$(OBJCOPY) -O binary -S $^ $@

clean:
	rm -f rom rom.bin rom.sw kImage.bin kImage.o
