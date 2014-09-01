# To compile, from the source root
#
#    make headers_install
#    make M=documentation

# kbuild trick to avoid linker error. Can be omitted if a module is built.
obj- := dummy.o

# List of programs to build
hostprogs-y := timestamping txtimestamp hwtstamp_config

# Tell kbuild to always build the programs
always := $(hostprogs-y)

HOSTCFLAGS_timestamping.o += -I$(objtree)/usr/include
HOSTCFLAGS_txtimestamp.o += -I$(objtree)/usr/include
HOSTCFLAGS_hwtstamp_config.o += -I$(objtree)/usr/include

clean:
	rm -f timestamping txtimestamp hwtstamp_config
