# For Solaris
#LIBS=-lsocket -lnsl

all: l4check

l4check: l4check.c
	$(CC) -g -I.. $(CFLAGS) $(LIBS) l4check.c -o $@

clean:
	/bin/rm -f l4check
