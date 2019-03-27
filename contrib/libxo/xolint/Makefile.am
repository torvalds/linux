#
# Copyright 2014, Juniper Networks, Inc.
# All rights reserved.
# This SOFTWARE is licensed under the LICENSE provided in the
# ../Copyright file. By downloading, installing, copying, or otherwise
# using the SOFTWARE, you agree to be bound by the terms of that
# LICENSE.

man_MANS = xolint.1

EXTRA_DIST = xolint.1 xolint.pl

install-exec-hook:
	install ${srcdir}/xolint.pl ${DESTDIR}${bindir}/xolint

uninstall-hook:
	rm -f ${DESTDIR}${bindir}/xolint

install-data-hook:
	for file in ${man_MANS}; do \
	    cat ../libxo/add.man >> ${DESTDIR}${man1dir}/$$file ; done
