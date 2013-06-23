always	:= suspend.bin resume0.bin resume1.bin
targets := suspend.elf resume0.elf resume1.elf

KBUILD_CFLAGS := $(LINUXINCLUDE) -I$(srctree)/arch/arm/mach-sun7i/pm
KBUILD_CFLAGS += -I$(srctree)/arch/arm/mach-sun7i/pm/standby -Os -g -c
KBUILD_CFLAGS += -nostdlib -march=armv7-a -marm -mlittle-endian -D__STANDBY_MODULE__
KBUILD_CFLAGS += -fno-unwind-tables -fno-asynchronous-unwind-tables
KBUILD_AFLAGS := $(KBUILD_CFLAGS) -D__ASSEMBLY__


suspend-y := common.o super_tmr.o super_clock.o super_twi.o super_power.o \
	pm_debug.o suspend.o dram.o dram_init.o \
	mem_delay.o mem_cpu.o mem_mmu_pc.o mem_mmu_pc_asm.o mem_serial.o \
	mem_printk.o mem_divlib.o mem_divlibc.o mem_int.o mem_timing.o

targets	+= $(suspend-y)
SUSPEND_OBJS = $(addprefix $(obj)/,$(suspend-y))

SUSPEND_LD_FILE = suspend.xn

LDFLAGS_suspend.elf := -T
OBJCOPYFLAGS_suspend.bin := -O binary

$(obj)/suspend.elf: $(src)/$(SUSPEND_LD_FILE) $(SUSPEND_OBJS) FORCE
	$(call if_changed,ld)

$(obj)/suspend.bin: $(obj)/suspend.elf FORCE
	$(call if_changed,objcopy)


resume0-y := common.o super_clock.o super_twi.o pm_debug.o dram.o dram_init.o \
	resume0.o resume0_c_part.o resume1_asm.o resume_head.o \
	mem_delay.o mem_cpu.o mem_mmu_pc_asm.o mem_serial.o \
	mem_printk.o mem_divlib.o mem_divlibc.o mem_timing.o

targets	+= $(resume0-y)
RESUME0_OBJS = $(addprefix $(obj)/,$(resume0-y))

RESUME0_LD_FILE = resume0.xn

LDFLAGS_resume0.elf := -T
OBJCOPYFLAGS_resume0.bin := -O binary

$(obj)/resume1_asm.o: $(obj)/resume1.bin

$(obj)/resume0.elf: $(src)/$(RESUME0_LD_FILE) $(RESUME0_OBJS) FORCE
	$(call if_changed,ld)

$(obj)/resume0.bin: $(obj)/resume0.elf FORCE
	$(call if_changed,objcopy)


resume1-y := common.o super_tmr.o super_clock.o super_twi.o super_power.o \
	pm_debug.o resume1.o dram.o dram_init.o \
	mem_delay.o mem_cpu.o mem_mmu_pc.o mem_mmu_pc_asm.o mem_serial.o \
	mem_printk.o mem_divlib.o mem_divlibc.o mem_timing.o

targets	+= $(resume1-y)
RESUME1_OBJS = $(addprefix $(obj)/,$(resume1-y))

RESUME1_LD_FILE = resume1.xn

LDFLAGS_resume1.elf := -T
OBJCOPYFLAGS_resume1.bin := -O binary

$(obj)/resume1.elf: $(src)/$(RESUME1_LD_FILE) $(RESUME1_OBJS) FORCE
	$(call if_changed,ld)

$(obj)/resume1.bin: $(obj)/resume1.elf FORCE
	$(call if_changed,objcopy)
