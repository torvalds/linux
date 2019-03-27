#	From: @(#)Makefile	8.1 (Berkeley) 5/31/93
# $FreeBSD$

.include <src.opts.mk>

SUBDIR= cat \
	chflags \
	chio \
	chmod \
	cp \
	date \
	dd \
	df \
	domainname \
	echo \
	ed \
	expr \
	freebsd-version \
	getfacl \
	hostname \
	kenv \
	kill \
	ln \
	ls \
	mkdir \
	mv \
	pax \
	pkill \
	ps \
	pwait \
	pwd \
	realpath \
	rm \
	rmdir \
	setfacl \
	sh \
	sleep \
	stty \
	sync \
	test \
	uuidgen

SUBDIR.${MK_SENDMAIL}+=	rmail
SUBDIR.${MK_TCSH}+=	csh
SUBDIR.${MK_TESTS}+=	tests

.include <bsd.arch.inc.mk>

SUBDIR_PARALLEL=

.include <bsd.subdir.mk>
