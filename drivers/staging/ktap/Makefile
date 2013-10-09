
# Do not instrument the tracer itself:
ifdef CONFIG_FUNCTION_TRACER
ORIG_CFLAGS := $(KBUILD_CFLAGS)
KBUILD_CFLAGS = $(subst -pg,,$(ORIG_CFLAGS))
endif

all: mod ktap

INTP = interpreter

LIBDIR = $(INTP)/library

LIB_OBJS += $(LIBDIR)/baselib.o $(LIBDIR)/kdebug.o $(LIBDIR)/timer.o \
		$(LIBDIR)/ansilib.o

INTP_OBJS += $(INTP)/ktap.o $(INTP)/loader.o $(INTP)/object.o \
		$(INTP)/tstring.o $(INTP)/table.o $(INTP)/vm.o \
		$(INTP)/opcode.o $(INTP)/strfmt.o $(INTP)/transport.o \
		$(LIB_OBJS)

obj-m		+= ktapvm.o
ktapvm-y	:= $(INTP_OBJS)

KVERSION ?= $(shell uname -r)
KERNEL_SRC ?= /lib/modules/$(KVERSION)/build
mod:
	$(MAKE) -C $(KERNEL_SRC) M=$(PWD) modules

modules_install:
	$(MAKE) -C $(KERNEL_SRC) M=$(PWD) modules_install

KTAPC_CFLAGS = -Wall -O2

UDIR = userspace

$(UDIR)/lex.o: $(UDIR)/lex.c
	$(QUIET_CC)$(CC) $(DEBUGINFO_FLAG) $(KTAPC_CFLAGS) -o $@ -c $<
$(UDIR)/parser.o: $(UDIR)/parser.c
	$(QUIET_CC)$(CC) $(DEBUGINFO_FLAG) $(KTAPC_CFLAGS) -o $@ -c $<
$(UDIR)/code.o: $(UDIR)/code.c
	$(QUIET_CC)$(CC) $(DEBUGINFO_FLAG) $(KTAPC_CFLAGS) -o $@ -c $<
$(UDIR)/dump.o: $(UDIR)/dump.c
	$(QUIET_CC)$(CC) $(DEBUGINFO_FLAG) $(KTAPC_CFLAGS) -o $@ -c $<
$(UDIR)/main.o: $(UDIR)/main.c
	$(QUIET_CC)$(CC) $(DEBUGINFO_FLAG) $(KTAPC_CFLAGS) -o $@ -c $<
$(UDIR)/util.o: $(UDIR)/util.c
	$(QUIET_CC)$(CC) $(DEBUGINFO_FLAG) $(KTAPC_CFLAGS) -o $@ -c $<
$(UDIR)/ktapio.o: $(UDIR)/ktapio.c
	$(QUIET_CC)$(CC) $(DEBUGINFO_FLAG) $(KTAPC_CFLAGS) -o $@ -c $<
$(UDIR)/eventdef.o: $(UDIR)/eventdef.c
	$(QUIET_CC)$(CC) $(DEBUGINFO_FLAG) $(KTAPC_CFLAGS) -o $@ -c $<
$(UDIR)/opcode.o: $(INTP)/opcode.c
	$(QUIET_CC)$(CC) $(DEBUGINFO_FLAG) $(KTAPC_CFLAGS) -o $@ -c $<
$(UDIR)/table.o: $(INTP)/table.c
	$(QUIET_CC)$(CC) $(DEBUGINFO_FLAG) $(KTAPC_CFLAGS) -o $@ -c $<
$(UDIR)/tstring.o: $(INTP)/tstring.c
	$(QUIET_CC)$(CC) $(DEBUGINFO_FLAG) $(KTAPC_CFLAGS) -o $@ -c $<
$(UDIR)/object.o: $(INTP)/object.c
	$(QUIET_CC)$(CC) $(DEBUGINFO_FLAG) $(KTAPC_CFLAGS) -o $@ -c $<

KTAPOBJS =
KTAPOBJS += $(UDIR)/lex.o
KTAPOBJS += $(UDIR)/parser.o
KTAPOBJS += $(UDIR)/code.o
KTAPOBJS += $(UDIR)/dump.o
KTAPOBJS += $(UDIR)/main.o
KTAPOBJS += $(UDIR)/util.o
KTAPOBJS += $(UDIR)/ktapio.o
KTAPOBJS += $(UDIR)/eventdef.o
KTAPOBJS += $(UDIR)/opcode.o
KTAPOBJS += $(UDIR)/table.o
KTAPOBJS += $(UDIR)/tstring.o
KTAPOBJS += $(UDIR)/object.o

ktap: $(KTAPOBJS)
	$(QUIET_LINK)$(CC) $(KTAPC_CFLAGS) -o $@ $(KTAPOBJS) -lpthread

KMISC := /lib/modules/$(KVERSION)/ktapvm/

install: mod ktap
	install -d $(KMISC)
	install -m 644 -c *.ko /lib/modules/$(KVERSION)/ktapvm/
	/sbin/depmod -a

load:
	insmod ktapvm.ko

unload:
	rmmod ktapvm

test: FORCE
	cd test; sh ./run_test.sh; cd -

clean:
	$(MAKE) -C $(KERNEL_SRC) M=$(PWD) clean
	$(RM) ktap

PHONY += FORCE
FORCE:

