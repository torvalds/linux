# kbuild trick to avoid linker error. Can be omitted if a module is built.
obj- := dummy.o

# List of programs to build
hostprogs-y := slabinfo

# Tell kbuild to always build the programs
always := $(hostprogs-y)
