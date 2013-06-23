always	:= standby.bin
targets := standby.elf

standby-y := common.o standby.o standby_clock.o standby_ir.o standby_key.o \
	standby_power.o standby_tmr.o standby_twi.o standby_usb.o \
	standby_delay.o dram.o dram_init.o \
	pm_debug.o mem_delay.o mem_mmu_pc_asm.o mem_serial.o mem_printk.o \
	mem_divlibc.o mem_int.o	mem_timing.o mem_divlib.o

targets	+= $(standby-y)
STANDBY_OBJS = $(addprefix $(obj)/,$(standby-y))

LD_FILE = standby.xn

KBUILD_CFLAGS := $(LINUXINCLUDE) -I$(srctree)/arch/arm/mach-sun7i/pm -Os -g -c
KBUILD_CFLAGS += -nostdlib -march=armv7-a -marm -mlittle-endian -D__STANDBY_MODULE__
KBUILD_CFLAGS += -fno-unwind-tables -fno-asynchronous-unwind-tables
KBUILD_AFLAGS := $(KBUILD_CFLAGS) -D__ASSEMBLY__

LDFLAGS_standby.elf := -T
OBJCOPYFLAGS_standby.bin := -O binary

$(obj)/standby.elf: $(src)/$(LD_FILE) $(STANDBY_OBJS) FORCE
	$(call if_changed,ld)

$(obj)/standby.bin: $(obj)/standby.elf FORCE
	$(call if_changed,objcopy)
