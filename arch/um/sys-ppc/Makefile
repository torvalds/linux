OBJ = built-in.o

.S.o:
	$(CC) $(KBUILD_AFLAGS) -D__ASSEMBLY__ -D__UM_PPC__ -c $< -o $*.o

OBJS = ptrace.o sigcontext.o checksum.o miscthings.o misc.o \
	ptrace_user.o sysrq.o

EXTRA_AFLAGS := -DCONFIG_PPC32 -I. -I$(srctree)/arch/ppc/kernel

all: $(OBJ)

$(OBJ): $(OBJS)
	rm -f $@
	$(LD) $(LINKFLAGS) --start-group $^ --end-group -o $@

ptrace_user.o: ptrace_user.c
	$(CC) -D__KERNEL__ $(USER_CFLAGS) $(EXTRA_CFLAGS) -c -o $@ $<

sigcontext.o: sigcontext.c
	$(CC) $(USER_CFLAGS) $(EXTRA_CFLAGS) -c -o $@ $<

checksum.S:
	rm -f $@
	ln -s $(srctree)/arch/ppc/lib/$@ $@

mk_defs.c:
	rm -f $@
	ln -s $(srctree)/arch/ppc/kernel/$@ $@

ppc_defs.head:
	rm -f $@
	ln -s $(srctree)/arch/ppc/kernel/$@ $@

ppc_defs.h: mk_defs.c ppc_defs.head \
		$(srctree)/include/asm-ppc/mmu.h \
		$(srctree)/include/asm-ppc/processor.h \
		$(srctree)/include/asm-ppc/pgtable.h \
		$(srctree)/include/asm-ppc/ptrace.h
#	$(CC) $(CFLAGS) -S mk_defs.c
	cp ppc_defs.head ppc_defs.h
# for bk, this way we can write to the file even if it's not checked out
	echo '#define THREAD 608' >> ppc_defs.h
	echo '#define PT_REGS 8' >> ppc_defs.h
	echo '#define CLONE_VM 256' >> ppc_defs.h
#	chmod u+w ppc_defs.h
#	grep '^#define' mk_defs.s >> ppc_defs.h
#	rm mk_defs.s

# the asm link is horrible, and breaks the other targets.  This is also
# not going to work with parallel makes.

checksum.o: checksum.S
	rm -f asm
	ln -s $(srctree)/include/asm-ppc asm
	$(CC) $(EXTRA_AFLAGS) $(KBUILD_AFLAGS) -D__ASSEMBLY__ -D__UM_PPC__ -c $< -o $*.o
	rm -f asm

misc.o: misc.S ppc_defs.h
	rm -f asm
	ln -s $(srctree)/include/asm-ppc asm
	$(CC) $(EXTRA_AFLAGS) $(KBUILD_AFLAGS) -D__ASSEMBLY__ -D__UM_PPC__ -c $< -o $*.o
	rm -f asm

clean-files := $(OBJS) ppc_defs.h checksum.S mk_defs.c
