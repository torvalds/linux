# kbuild trick to avoid linker error. Can be omitted if a module is built.
obj- := dummy.o

# List of programs to build
hostprogs-y := ifenslave

HOSTCFLAGS_ifenslave.o += -I$(objtree)/usr/include

# Tell kbuild to always build the programs
always := $(hostprogs-y)

obj-m := timestamping/
