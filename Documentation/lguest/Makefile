# This creates the demonstration utility "lguest" which runs a Linux guest.
CFLAGS:=-Wall -Wmissing-declarations -Wmissing-prototypes -O3 -I../../include -I../../arch/x86/include -U_FORTIFY_SOURCE
LDLIBS:=-lz

all: lguest

clean:
	rm -f lguest
