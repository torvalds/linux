#
# Makefile - Intel MIC User Space Tools.
# Copyright(c) 2013, Intel Corporation.
#
ifdef DEBUG
CFLAGS += $(USERWARNFLAGS) -I. -g -Wall -DDEBUG=$(DEBUG)
else
CFLAGS += $(USERWARNFLAGS) -I. -g -Wall
endif

mpssd: mpssd.o sysfs.o
	$(CC) $(CFLAGS) -o $@ $^ -lpthread

install:
	install mpssd /usr/sbin/mpssd
	install micctrl /usr/sbin/micctrl

clean:
	rm -f mpssd *.o
