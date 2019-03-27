# @(#)Makefile	8.7 (Berkeley) 8/18/96

ROFF=	groff

all: vi.0 vi.0.ps

vi.0: vi.1
	${ROFF} -man -Tascii < vi.1 > $@
	chmod 444 $@

vi.0.ps: vi.1
	${ROFF} -man < vi.1 > $@
	chmod 444 $@

clean:
	rm -f vi.0 vi.0.ps
