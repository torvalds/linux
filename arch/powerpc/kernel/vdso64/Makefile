# List of files in the vdso, has to be asm only for now

obj-vdso64 = sigtramp.o gettimeofday.o datapage.o cacheflush.o note.o

# Build rules

targets := $(obj-vdso64) vdso64.so
obj-vdso64 := $(addprefix $(obj)/, $(obj-vdso64))

EXTRA_CFLAGS := -shared -s -fno-common -fno-builtin
EXTRA_CFLAGS +=  -nostdlib -Wl,-soname=linux-vdso64.so.1
EXTRA_AFLAGS := -D__VDSO64__ -s

obj-y += vdso64_wrapper.o
extra-y += vdso64.lds
CPPFLAGS_vdso64.lds += -P -C -U$(ARCH)

# Force dependency (incbin is bad)
$(obj)/vdso64_wrapper.o : $(obj)/vdso64.so

# link rule for the .so file, .lds has to be first
$(obj)/vdso64.so: $(src)/vdso64.lds $(obj-vdso64)
	$(call if_changed,vdso64ld)

# assembly rules for the .S files
$(obj-vdso64): %.o: %.S
	$(call if_changed_dep,vdso64as)

# actual build commands
quiet_cmd_vdso64ld = VDSO64L $@
      cmd_vdso64ld = $(CC) $(c_flags) -Wl,-T $^ -o $@
quiet_cmd_vdso64as = VDSO64A $@
      cmd_vdso64as = $(CC) $(a_flags) -c -o $@ $<


