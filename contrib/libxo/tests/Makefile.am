#
# Copyright 2014, Juniper Networks, Inc.
# All rights reserved.
# This SOFTWARE is licensed under the LICENSE provided in the
# ../Copyright file. By downloading, installing, copying, or otherwise
# using the SOFTWARE, you agree to be bound by the terms of that
# LICENSE.

SUBDIRS = core xo

if HAVE_GETTEXT
SUBDIRS += gettext
endif

test tests:
	@(cur=`pwd` ; for dir in $(SUBDIRS) ; do \
		cd $$dir ; \
		$(MAKE) tests ; \
		cd $$cur ; \
	done)

accept:
	@(cur=`pwd` ; for dir in $(SUBDIRS) ; do \
		cd $$dir ; \
		$(MAKE) accept ; \
		cd $$cur ; \
	done)

valgrind:
	@echo '## Running the regression tests under Valgrind'
	@echo '## Go get a cup of coffee it is gonna take a while ...'
	${MAKE} VALGRIND='valgrind -q' tests
