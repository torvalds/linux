always	:= standby.bin
targets := standby.elf

standby-y := common.o standby.o stack.o standby_clock.o \
	     dram.o dram_init.o \
	     standby_int.o standby_ir.o standby_key.o standby_power.o \
	     standby_tmr.o standby_twi.o standby_usb.o standby_delay.o

targets	+= $(standby-y)
STANDBY_OBJS = $(addprefix $(obj)/,$(standby-y))

LD_FILE = standby.lds

KBUILD_CFLAGS := $(LINUXINCLUDE) -Os -g -march=armv7-a -marm -mlittle-endian -c
KBUILD_CFLAGS += -fno-unwind-tables -fno-asynchronous-unwind-tables
KBUILD_AFLAGS := $(KBUILD_CFLAGS) -D__ASSEMBLY__

LDFLAGS_standby.elf := -T
OBJCOPYFLAGS_standby.bin := -O binary

$(obj)/standby.elf: $(src)/$(LD_FILE) $(STANDBY_OBJS) FORCE
	$(call if_changed,ld)

$(obj)/standby.bin: $(obj)/standby.elf FORCE
	$(call if_changed,objcopy)
