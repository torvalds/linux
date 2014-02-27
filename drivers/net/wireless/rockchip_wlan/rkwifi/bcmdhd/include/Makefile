#!/bin/bash
#
# This script serves following purpose:
#
# 1. It generates native version information by querying
#    automerger maintained database to see where src/include
#    came from
# 2. For select components, as listed in compvers.sh
#    it generates component version files
#
# Copyright 2005, Broadcom, Inc.
#
# $Id: Makefile 347587 2012-07-27 09:13:31Z $
#

export SRCBASE:=..

TARGETS := epivers.h

ifdef VERBOSE
export VERBOSE
endif

all release: epivers compvers

# Generate epivers.h for native branch url
epivers:
	bash epivers.sh

# Generate component versions based on component url
compvers:
	@if [ -s "compvers.sh" ]; then \
		echo "Generating component versions, if any"; \
		bash compvers.sh; \
	else \
		echo "Skipping component version generation"; \
	fi

# Generate epivers.h for native branch version
clean_compvers:
	@if [ -s "compvers.sh" ]; then \
		echo "bash compvers.sh clean"; \
		bash compvers.sh clean; \
	else \
		echo "Skipping component version clean"; \
	fi

clean:
	rm -f $(TARGETS) *.prev

clean_all: clean clean_compvers

.PHONY: all release clean epivers compvers clean_compvers
