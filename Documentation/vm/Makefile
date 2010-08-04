# kbuild trick to avoid linker error. Can be omitted if a module is built.
obj- := dummy.o

# List of programs to build
hostprogs-y := slabinfo page-types hugepage-mmap hugepage-shm map_hugetlb

# Tell kbuild to always build the programs
always := $(hostprogs-y)
