#
# $Id$
#
# Copyright 2014, Juniper Networks, Inc.
# All rights reserved.
# This SOFTWARE is licensed under the LICENSE provided in the
# ../Copyright file. By downloading, installing, copying, or otherwise
# using the SOFTWARE, you agree to be bound by the terms of that
# LICENSE.

ACLOCAL_AMFLAGS = -I m4

SUBDIRS = libxo xo xopo xolint xohtml tests doc encoder
bin_SCRIPTS=libxo-config
dist_doc_DATA = Copyright

EXTRA_DIST = \
    libxo-config.in \
    warnings.mk \
    README.md \
    INSTALL.md \
    packaging/libxo.spec

.PHONY: test tests

test tests:
	@(cd tests ; ${MAKE} test)

errors:
	@(cd tests/errors ; ${MAKE} test)

docs:
	@(cd doc ; ${MAKE} docs)


DIST_FILES_DIR = ~/Dropbox/dist-files/
GH_PAGES_DIR = gh-pages/
GH_PAGES_DIR_VER = gh-pages/${PACKAGE_VERSION}
PACKAGE_FILE = ${PACKAGE_TARNAME}-${PACKAGE_VERSION}.tar.gz

XOHTML_FILES = \
    ${top_srcdir}/xohtml/xohtml.css \
    ${top_srcdir}/xohtml/xohtml.js \
    ${top_srcdir}/xohtml/external/jquery.js \
    ${top_srcdir}/xohtml/external/jquery.qtip.css \
    ${top_srcdir}/xohtml/external/jquery.qtip.js

upload: dist upload-docs upload-xohtml-files
	@echo "Remember to run:"
	@echo "        gt tag ${PACKAGE_VERSION}"

upload-docs: docs
	@echo "Uploading libxo-manual.html ... "
	@-[ -d ${GH_PAGES_DIR} ] \
		&& echo "Updating manual on gh-pages ..." \
		&& mkdir -p ${GH_PAGES_DIR_VER} \
		&& cp doc/libxo-manual.html ${GH_PAGES_DIR} \
		&& cp doc/libxo-manual.html ${GH_PAGES_DIR_VER} \
		&& (cd ${GH_PAGES_DIR} \
			&& git add ${PACKAGE_VERSION} \
			&& git add libxo-manual.html \
			&& git commit -m 'new docs' \
                               libxo-manual.html ${PACKAGE_VERSION} \
			&& git push origin gh-pages ) ; true

upload-xohtml-files:
	@echo "Uploading xohtml files ... "
	@-[ -d ${GH_PAGES_DIR} ] \
		&& echo "Updating xohtml files on gh-pages ..." \
		&& mkdir -p ${GH_PAGES_DIR_VER}/xohtml \
		&& cp ${XOHTML_FILES} ${GH_PAGES_DIR_VER}/xohtml \
		&& (cd ${GH_PAGES_DIR} \
			&& git add ${PACKAGE_VERSION}/xohtml \
			&& git commit -m 'new xohtml files' \
                               ${PACKAGE_VERSION}/xohtml \
			&& git push origin gh-pages ) ; true

pkgconfigdir=$(libdir)/pkgconfig
pkgconfig_DATA = packaging/${PACKAGE_NAME}.pc

get-wiki:
	git clone https://github.com/Juniper/${PACKAGE_NAME}.wiki.git wiki

get-gh-pages:
	git clone https://github.com/Juniper/${PACKAGE_NAME}.git \
		gh-pages -b gh-pages

UPDATE_PACKAGE_FILE = \
    -e "s;__SHA1__;$$SHA1;" \
    -e "s;__SHA256__;SHA256 (textproc/${PACKAGE_FILE}) = $$SHA256;" \
    -e "s;__SIZE__;SIZE (textproc/${PACKAGE_FILE}) = $$SIZE;"

GH_PACKAGING_DIR = ${PACKAGE_VERSION}/packaging
GH_PAGES_PACKAGE_DIR = ${GH_PAGES_DIR}/${GH_PACKAGING_DIR}

packages:
	@-[ -d ${GH_PAGES_DIR} ] && set -x \
		&& echo "Updating packages on gh-pages ..." \
                && mkdir -p ${GH_PAGES_DIR}/${GH_PACKAGING_DIR} \
		&& SHA1="`openssl sha1 ${PACKAGE_FILE} | awk '{print $$2}'`" \
		&& SHA256="`openssl sha256 ${PACKAGE_FILE} | awk '{print $$2}'`" \
		&& SIZE="`ls -l ${PACKAGE_FILE} | awk '{print $$5}'`" \
		&& echo "... ${GH_PAGES_PACKAGE_DIR}/${PACKAGE_NAME}.rb ..." \
		&& sed ${UPDATE_PACKAGE_FILE} \
			packaging/${PACKAGE_NAME}.rb.base \
			> ${GH_PAGES_PACKAGE_DIR}/${PACKAGE_NAME}.rb \
		&& echo "... ${GH_PAGES_PACKAGE_DIR}/${PACKAGE_NAME}.spec ..." \
		&& cp packaging/${PACKAGE_NAME}.spec \
			${GH_PAGES_PACKAGE_DIR}/${PACKAGE_NAME}.spec \
		&& (cd ${GH_PAGES_DIR} \
			&& git add ${GH_PACKAGING_DIR} \
			&& git add ${GH_PACKAGING_DIR}/libxo.rb \
                                   ${GH_PACKAGING_DIR}/libxo.spec \
			&& git commit -m 'new packaging data' \
                               ${GH_PACKAGING_DIR} \
			&& git push origin gh-pages ) ; true

ANALYZE_DIR = ~/trash/libxo
ANALYZE_CMD = scan-build-mp-3.6

analyze:
	${MAKE} clean
	${ANALYZE_CMD} -o ${ANALYZE_DIR} ${MAKE}

SANIFLAGS=-fno-omit-frame-pointer -g -O2

sanitize-address:
	${MAKE} clean
	${MAKE} CFLAGS="-fsanitize=address ${SANIFLAGS}"
	${MAKE} install
	${MAKE} test

sanitize-undefined:
	${MAKE} clean
	${MAKE} CFLAGS="-fsanitize=undefined ${SANIFLAGS}"
	${MAKE} install
	${MAKE} test

sanitize-memory:
	${MAKE} clean
	${MAKE} CFLAGS="-fsanitize=memory ${SANIFLAGS}"
	${MAKE} install
	${MAKE} test
