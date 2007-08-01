# This creates the demonstration utility "lguest" which runs a Linux guest.

# For those people that have a separate object dir, look there for .config
KBUILD_OUTPUT := ../..
ifdef O
  ifeq ("$(origin O)", "command line")
    KBUILD_OUTPUT := $(O)
  endif
endif
# We rely on CONFIG_PAGE_OFFSET to know where to put lguest binary.
include $(KBUILD_OUTPUT)/.config
LGUEST_GUEST_TOP := ($(CONFIG_PAGE_OFFSET) - 0x08000000)

CFLAGS:=-Wall -Wmissing-declarations -Wmissing-prototypes -O3 -Wl,-T,lguest.lds
LDLIBS:=-lz

all: lguest.lds lguest

# The linker script on x86 is so complex the only way of creating one
# which will link our binary in the right place is to mangle the
# default one.
lguest.lds:
	$(LD) --verbose | awk '/^==========/ { PRINT=1; next; } /SIZEOF_HEADERS/ { gsub(/0x[0-9A-F]*/, "$(LGUEST_GUEST_TOP)") } { if (PRINT) print $$0; }' > $@

clean:
	rm -f lguest.lds lguest
