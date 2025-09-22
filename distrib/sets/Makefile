# $OpenBSD: Makefile,v 1.3 2016/11/08 19:33:58 tb Exp $

DB = /usr/lib/locate/src.db

makedb:
	/bin/sh ${.CURDIR}/makelocatedb ${OSrev} >${DESTDIR}${DB}
	chown ${BINOWN}:${BINGRP} ${DESTDIR}${DB}
	chmod 644 ${DESTDIR}${DB}

.PHONY: makedb

.include <bsd.own.mk>
