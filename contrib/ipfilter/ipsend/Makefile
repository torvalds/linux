#
# Copyright (C) 2012 by Darren Reed.
#
# See the IPFILTER.LICENCE file for details on licencing.
#
IPFT=ipft_ef.o ipft_hx.o ipft_pc.o ipft_sn.o ipft_td.o ipft_tx.o opt.o
OBJS=ipsend.o ip.o ipsopt.o y.tab.o lex.yy.o
ROBJS=ipresend.o ip.o resend.o $(IPFT)
TOBJS=iptest.o iptests.o ip.o
BPF=sbpf.o
NIT=snit.o
SUNOS4=sock.o arp.o inet_addr.o
BSD=sock.o 44arp.o
LINUX=lsock.o slinux.o larp.o
LINUXK=
TOP=..
SUNOS5=dlcommon.o sdlpi.o arp.o inet_addr.o
ULTRIX=ultrix.o sock.o arp.o inet_addr.o
HPUX=hpux.o sock.o arp.o inet_addr.o

#CC=gcc
DEBUG=-g
CFLAGS=$(DEBUG) -I. -Iipf
#
MFLAGS="BINDEST=$(BINDEST)" "SBINDEST=$(SBINDEST)" "MANDIR=$(MANDIR)" \
	"IPFLKM=$(IPFLKM)" \
	"IPFLOG=$(IPFLOG)" "LOGFAC=$(LOGFAC)" "POLICY=$(POLICY)" \
	"SOLARIS2=$(SOLARIS2)" "DEBUG=$(DEBUG)" "DCPU=$(CPU)" \
	"CPUDIR=$(CPUDIR)"
#
all:
	@echo "Use one of these targets:"
	@echo "	sunos4-nit (standard SunOS 4.1.x)"
	@echo "	sunos4-bpf (SunOS4.1.x with BPF in the kernel)"
	@echo "	bsd-bpf (4.4BSD variant with BPF in the kernel)"
	@echo "	linux10 (Linux 1.0 kernels)"
	@echo "	linux12 (Linux 1.2 kernels)"
	@echo "	linux20 (Linux 2.0 kernels)"
	@echo "	sunos5 (Solaris 2.x)"

ipf:
	-if [ ! -d iplang ] ; then ln -s ../iplang iplang; fi
	-if [ ! -d netinet ] ; then ln -s ../netinet netinet; fi
	-if [ ! -d ipf ] ; then ln -s .. ipf; fi

y.tab.o: iplang/iplang_y.y
	-if [ -h iplang ] ; then \
		(cd iplang; ${MAKE} $(MFLAGS) 'DESTDIR=../ipsend' ) \
	else \
		(cd iplang; ${MAKE} $(MFLAGS) 'DESTDIR=..' ) \
	fi

lex.yy.o: iplang/iplang_l.l
	-if [ -h iplang ] ; then \
		(cd iplang; ${MAKE} $(MFLAGS) 'DESTDIR=../ipsend' ) \
	else \
		(cd iplang; ${MAKE} $(MFLAGS) 'DESTDIR=..' ) \
	fi

.c.o:
	$(CC) $(CFLAGS) $(LINUXK) -c $< -o $@

install:
	-$(INSTALL) -cs -g wheel -m 755 -o root ipsend ipresend iptest $(BINDEST)

bpf sunos4-bpf :
	make ipsend "OBJS=$(OBJS)" "UNIXOBJS=$(BPF) $(SUNOS4)" "CC=$(CC)" \
		"CFLAGS=$(CFLAGS) -DDOSOCKET -DIPSEND" "LLIB=-ll"
	make ipresend "ROBJS=$(ROBJS)" "UNIXOBJS=$(BPF) $(SUNOS4)" "CC=$(CC)" \
		"CFLAGS=$(CFLAGS) -DDOSOCKET"
	make iptest "TOBJS=$(TOBJS)" "UNIXOBJS=$(BPF) $(SUNOS4)" "CC=$(CC)" \
		"CFLAGS=$(CFLAGS) -DDOSOCKET"

nit sunos4 sunos4-nit :
	make ipsend "OBJS=$(OBJS)" "UNIXOBJS=$(NIT) $(SUNOS4)" "CC=$(CC)" \
		"CFLAGS=$(CFLAGS) -DDOSOCKET -DIPSEND" "LLIB=-ll"
	make ipresend "ROBJS=$(ROBJS)" "UNIXOBJS=$(NIT) $(SUNOS4)" "CC=$(CC)" \
		"CFLAGS=$(CFLAGS) -DDOSOCKET"
	make iptest "TOBJS=$(TOBJS)" "UNIXOBJS=$(NIT) $(SUNOS4)" "CC=$(CC)" \
		"CFLAGS=$(CFLAGS) -DDOSOCKET"

dlpi sunos5 :
	make ipsend "OBJS=$(OBJS)" "UNIXOBJS=$(SUNOS5)" "CC=$(CC)" \
		CFLAGS="$(CFLAGS) -Dsolaris -DIPSEND" "LIBS=-lsocket -lnsl" \
		"LLIB=-ll"
	make ipresend "ROBJS=$(ROBJS)" "UNIXOBJS=$(SUNOS5)" "CC=$(CC)" \
		CFLAGS="$(CFLAGS) -Dsolaris" "LIBS=-lsocket -lnsl"
	make iptest "TOBJS=$(TOBJS)" "UNIXOBJS=$(SUNOS5)" "CC=$(CC)" \
		CFLAGS="$(CFLAGS) -Dsolaris" "LIBS=-lsocket -lnsl"

bsd-bpf :
	make ipsend "OBJS=$(OBJS)" "UNIXOBJS=$(BPF) $(BSD)" "CC=$(CC)" \
		"CFLAGS=$(CFLAGS) -DDOSOCKET -DIPSEND" "LLIB=-ll"
	make ipresend "ROBJS=$(ROBJS)" "UNIXOBJS=$(BPF) $(BSD)" "CC=$(CC)" \
		"CFLAGS=$(CFLAGS) -DDOSOCKET"
	make iptest "TOBJS=$(TOBJS)" "UNIXOBJS=$(BPF) $(BSD)" "CC=$(CC)" \
		"CFLAGS=$(CFLAGS) -DDOSOCKET"

linuxrev :
	make ipsend "OBJS=$(OBJS)" "UNIXOBJS=$(LINUX)" "CC=$(CC)" \
		CFLAGS="$(CFLAGS) $(INC) -DDOSOCKET -DIPSEND" $(LINUXK)
	make ipresend "ROBJS=$(ROBJS)" "UNIXOBJS=$(LINUX)" "CC=$(CC)" \
		CFLAGS="$(CFLAGS) $(INC) -DDOSOCKET" $(LINUXK)
	make iptest "TOBJS=$(TOBJS)" "UNIXOBJS=$(LINUX)" "CC=$(CC)" \
		CFLAGS="$(CFLAGS) $(INC) -DDOSOCKET" $(LINUXK)

linux10:
	make linuxrev 'LINUXK="LINUXK=-DLINUX=0100"' \
		"INC=-I/usr/src/linux/include" "LLIB=-lfl"

linux12:
	make linuxrev 'LINUXK="LINUXK=-DLINUX=0102"' "INC=-I/usr/src/linux" \
		"LLIB=-lfl"

linux20:
	make linuxrev 'LINUXK="LINUXK=-DLINUX=0200"' \
		"INC=-I/usr/src/linux/include" "LLIB=-lfl" "ELIB=-lelf"

ultrix :
	make ipsend "OBJS=$(OBJS)" "UNIXOBJS=$(ULTRIX)" "CC=$(CC)" \
		CFLAGS="$(CFLAGS) -DIPSEND" "LIBS=" "LLIB=-ll"
	make ipresend "ROBJS=$(ROBJS)" "UNIXOBJS=$(ULTRIX)" "CC=$(CC)" \
		CFLAGS="$(CFLAGS)" "LIBS="
	make iptest "TOBJS=$(TOBJS)" "UNIXOBJS=$(ULTRIX)" "CC=$(CC)" \
		CFLAGS="$(CFLAGS)" "LIBS="

hpux9 :
	make ipsend "OBJS=$(OBJS)" "UNIXOBJS=$(HPUX)" "CC=$(CC)" \
		CFLAGS="$(CFLAGS) -DIPSEND" "LIBS="
	make ipresend "ROBJS=$(ROBJS)" "UNIXOBJS=$(HPUX)" "CC=$(CC)" \
		CFLAGS="$(CFLAGS)" "LIBS="
	make iptest "TOBJS=$(TOBJS)" "UNIXOBJS=$(HPUX)" "CC=$(CC)" \
		CFLAGS="$(CFLAGS)" "LIBS="

hpux11 :
	make ipsend "OBJS=$(OBJS)" "UNIXOBJS=$(HPUX)" "CC=$(CC)" \
		CFLAGS="$(CFLAGS) -DIPSEND" "LIBS="
	make ipresend "ROBJS=$(ROBJS)" "UNIXOBJS=$(HPUX)" "CC=$(CC)" \
		CFLAGS="$(CFLAGS)" "LIBS="
	make iptest "TOBJS=$(TOBJS)" "UNIXOBJS=$(HPUX)" "CC=$(CC)" \
		CFLAGS="$(CFLAGS)" "LIBS="

ipsend: ipf $(OBJS) $(UNIXOBJS)
	$(CC) $(OBJS) $(UNIXOBJS) -o $@ $(LIBS) $(LLIB) $(ELIB)

ipresend: $(ROBJS) $(UNIXOBJS)
	$(CC) $(ROBJS) $(UNIXOBJS) -o $@ $(LIBS) $(ELIB)

iptest: $(TOBJS) $(UNIXOBJS)
	$(CC) $(TOBJS) $(UNIXOBJS) -o $@ $(LIBS) $(ELIB)

ipft_ef.o: ipf/ipft_ef.c ipf/ipt.h ipf/ipf.h ipf/ip_compat.h
	$(CC) $(CFLAGS) $(LINUXK) -c ipf/ipft_ef.c -o $@

ipft_hx.o: ipf/ipft_hx.c ipf/ipt.h ipf/ipf.h ipf/ip_compat.h
	$(CC) $(CFLAGS) $(LINUXK) -c ipf/ipft_hx.c -o $@

ipft_pc.o: ipf/ipft_pc.c ipf/ipt.h ipf/ipf.h ipf/ip_compat.h
	$(CC) $(CFLAGS) $(LINUXK) -c ipf/ipft_pc.c -o $@

ipft_sn.o: ipf/ipft_sn.c ipf/ipt.h ipf/ipf.h ipf/ip_compat.h
	$(CC) $(CFLAGS) $(LINUXK) -c ipf/ipft_sn.c -o $@

ipft_td.o: ipf/ipft_td.c ipf/ipt.h ipf/ipf.h ipf/ip_compat.h
	$(CC) $(CFLAGS) $(LINUXK) -c ipf/ipft_td.c -o $@

ipft_tx.o: ipf/ipft_tx.c ipf/ipt.h ipf/ipf.h ipf/ip_compat.h
	$(CC) $(CFLAGS) $(LINUXK) -c ipf/ipft_tx.c -o $@

opt.o: ipf/opt.c ipf/ipt.h ipf/ipf.h ipf/ip_compat.h
	$(CC) $(CFLAGS) $(LINUXK) -c ipf/opt.c -o $@

inet_addr.o: ipf/inet_addr.c
	$(CC) $(CFLAGS) $(LINUXK) -c ipf/inet_addr.c -o $@

clean:
	rm -rf *.o *core a.out ipsend ipresend iptest
	if [ -d iplang ]; then (cd iplang; $(MAKE) $(MFLAGS) clean); fi
	if [ -d $(TOP)/iplang ]; then (cd $(TOP)/iplang; $(MAKE) $(MFLAGS) clean); fi

do-cvs:
	find . -type d -name CVS -print | xargs /bin/rm -rf
	find . -type f -name .cvsignore -print | xargs /bin/rm -f
