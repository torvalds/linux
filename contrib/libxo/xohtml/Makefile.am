#
# Copyright 2015, Juniper Networks, Inc.
# All rights reserved.
# This SOFTWARE is licensed under the LICENSE provided in the
# ../Copyright file. By downloading, installing, copying, or otherwise
# using the SOFTWARE, you agree to be bound by the terms of that
# LICENSE.

man_MANS = xohtml.1

EXTERNAL_FILES = \
    external/jquery.js \
    external/jquery.qtip.css \
    external/jquery.qtip.js

INTERNAL_FILES = \
    xohtml.js \
    xohtml.css

EXTRA_DIST = \
    xohtml.1 \
    xohtml.sh.in \
    ${INTERNAL_FILES} \
    ${EXTERNAL_FILES}

install-exec-hook:
	install xohtml.sh ${DESTDIR}${bindir}/xohtml
	mkdir -p ${DESTDIR}${XO_SHAREDIR}/external
	for file in ${INTERNAL_FILES}; do \
	    install ${srcdir}/$$file ${DESTDIR}${XO_SHAREDIR} ; done
	for file in ${EXTERNAL_FILES}; do \
	    install ${srcdir}/$$file ${DESTDIR}${XO_SHAREDIR}/external ; done

uninstall-hook:
	for file in ${INTERNAL_FILES} ${EXTERNAL_FILES}; do \
	    rm ${DESTDIR}${XO_SHAREDIR}/$$file ; done
	rmdir ${DESTDIR}${XO_SHAREDIR}/external
	rm -f ${DESTDIR}${bindir}/xohtml

install-data-hook:
	for file in ${man_MANS}; do \
	    cat ../libxo/add.man >> ${DESTDIR}${man1dir}/$$file ; done
