# List of programs to build
hostprogs-y := timestamping hwtstamp_config

# Tell kbuild to always build the programs
always := $(hostprogs-y)

HOSTCFLAGS_timestamping.o += -I$(objtree)/usr/include
HOSTCFLAGS_hwtstamp_config.o += -I$(objtree)/usr/include

clean:
	rm -f timestamping hwtstamp_config
