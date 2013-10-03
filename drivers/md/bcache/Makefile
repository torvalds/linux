
obj-$(CONFIG_BCACHE)	+= bcache.o

bcache-y		:= alloc.o btree.o bset.o io.o journal.o writeback.o\
	movinggc.o request.o super.o sysfs.o debug.o util.o trace.o stats.o closure.o

CFLAGS_request.o	+= -Iblock
