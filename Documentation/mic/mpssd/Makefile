ifndef CROSS_COMPILE
# List of programs to build
hostprogs-$(CONFIG_X86_64) := mpssd

mpssd-objs := mpssd.o sysfs.o

# Tell kbuild to always build the programs
always := $(hostprogs-y)

HOSTCFLAGS += -I$(objtree)/usr/include -I$(srctree)/tools/include

ifdef DEBUG
HOSTCFLAGS += -DDEBUG=$(DEBUG)
endif

HOSTLOADLIBES_mpssd := -lpthread

install:
	install mpssd /usr/sbin/mpssd
	install micctrl /usr/sbin/micctrl
endif
