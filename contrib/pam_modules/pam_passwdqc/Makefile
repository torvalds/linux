#
# Copyright (c) 2000,2001 by Solar Designer. See LICENSE.
#

CC = gcc
LD = ld
RM = rm -f
MKDIR = mkdir -p
INSTALL = install
CFLAGS = -c -Wall -fPIC -DHAVE_SHADOW -O2
LDFLAGS = -s -lpam -lcrypt --shared
LDFLAGS_SUN = -s -lpam -lcrypt -G

TITLE = pam_passwdqc
LIBSHARED = $(TITLE).so
SHLIBMODE = 755
SECUREDIR = /lib/security
FAKEROOT =

PROJ = $(LIBSHARED)
OBJS = pam_passwdqc.o passwdqc_check.o passwdqc_random.o wordset_4k.o

all:
	if [ "`uname -s`" = "SunOS" ]; then \
		make LDFLAGS="$(LDFLAGS_SUN)" $(PROJ); \
	else \
		make $(PROJ); \
	fi

$(LIBSHARED): $(OBJS)
	$(LD) $(LDFLAGS) $(OBJS) -o $(LIBSHARED)

.c.o:
	$(CC) $(CFLAGS) $*.c

pam_passwdqc.o: passwdqc.h pam_macros.h
passwdqc_check.o: passwdqc.h
passwdqc_random.o: passwdqc.h

install:
	$(MKDIR) $(FAKEROOT)$(SECUREDIR)
	$(INSTALL) -m $(SHLIBMODE) $(LIBSHARED) $(FAKEROOT)$(SECUREDIR)

remove:
	$(RM) $(FAKEROOT)$(SECUREDIR)/$(TITLE).so

clean:
	$(RM) $(PROJ) *.o
