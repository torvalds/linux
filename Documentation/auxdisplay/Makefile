# kbuild trick to avoid linker error. Can be omitted if a module is built.
obj- := dummy.o

# List of programs to build
hostprogs-y := cfag12864b-example

# Tell kbuild to always build the programs
always := $(hostprogs-y)

HOSTCFLAGS_cfag12864b-example.o += -I$(objtree)/usr/include
