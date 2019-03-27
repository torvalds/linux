#!/usr/bin/make
#
# If the configure script does not work, then this Makefile is available
# as a backup.  Manually configure the variables below.
#
# Note:  This makefile works out-of-the-box on MacOS 10.2 (Jaguar)
#
CC = gcc
CFLAGS = -O0 -I.
LIBS = -lz
COPTS += -D_BSD_SOURCE
COPTS += -DSQLITE_ENABLE_LOCKING_STYLE=0
COPTS += -DSQLITE_THREADSAFE=0
COPTS += -DSQLITE_OMIT_LOAD_EXTENSION
COPTS += -DSQLITE_WITHOUT_ZONEMALLOC
COPTS += -DSQLITE_ENABLE_RTREE

sqlite3:	shell.c sqlite3.c
	$(CC) $(CFLAGS) $(COPTS) -o sqlite3 shell.c sqlite3.c $(LIBS)
