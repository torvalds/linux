# kbuild trick to avoid linker error. Can be omitted if a module is built.
obj- := dummy.o

# List of programs to build
hostprogs-y := crc32hash

# Tell kbuild to always build the programs
always := $(hostprogs-y)

HOSTCFLAGS_crc32hash.o += -I$(objtree)/usr/include
