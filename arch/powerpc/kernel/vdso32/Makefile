
# List of files in the vdso, has to be asm only for now

obj-vdso32 = sigtramp.o gettimeofday.o datapage.o cacheflush.o note.o

# Build rules

ifeq ($(CONFIG_PPC32),y)
CROSS32CC := $(CC)
endif

targets := $(obj-vdso32) vdso32.so
obj-vdso32 := $(addprefix $(obj)/, $(obj-vdso32))


EXTRA_CFLAGS := -shared -s -fno-common -fno-builtin
EXTRA_CFLAGS += -nostdlib -Wl,-soname=linux-vdso32.so.1
EXTRA_AFLAGS := -D__VDSO32__ -s

obj-y += vdso32_wrapper.o
extra-y += vdso32.lds
CPPFLAGS_vdso32.lds += -P -C -Upowerpc

# Force dependency (incbin is bad)
$(obj)/vdso32_wrapper.o : $(obj)/vdso32.so

# link rule for the .so file, .lds has to be first
$(obj)/vdso32.so: $(src)/vdso32.lds $(obj-vdso32)
	$(call if_changed,vdso32ld)

# assembly rules for the .S files
$(obj-vdso32): %.o: %.S
	$(call if_changed_dep,vdso32as)

# actual build commands
quiet_cmd_vdso32ld = VDSO32L $@
      cmd_vdso32ld = $(CROSS32CC) $(c_flags) -Wl,-T $^ -o $@
quiet_cmd_vdso32as = VDSO32A $@
      cmd_vdso32as = $(CROSS32CC) $(a_flags) -c -o $@ $<

