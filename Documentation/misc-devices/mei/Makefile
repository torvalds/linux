# kbuild trick to avoid linker error. Can be omitted if a module is built.
obj- := dummy.o

# List of programs to build
hostprogs-y := mei-amt-version
HOSTCFLAGS_mei-amt-version.o += -I$(objtree)/usr/include
# Tell kbuild to always build the programs
always := $(hostprogs-y)
