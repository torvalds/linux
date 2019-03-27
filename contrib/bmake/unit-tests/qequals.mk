# $Id: qequals.mk,v 1.1.1.1 2014/08/30 18:57:18 sjg Exp $

M= i386
V.i386= OK
V.$M ?= bug

all:
	@echo 'V.$M ?= ${V.$M}'
