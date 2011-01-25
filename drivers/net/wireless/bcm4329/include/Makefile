#
# include/Makefile
#
# Copyright 2005, Broadcom, Inc.
#
# $Id: Makefile,v 13.5 2005/02/17 19:11:31 Exp $
#

SRCBASE	= ..

TARGETS	= epivers.h


all release:
	bash epivers.sh

clean:
	rm -rf ${TARGETS} *.prev


.PHONY: all release clean
