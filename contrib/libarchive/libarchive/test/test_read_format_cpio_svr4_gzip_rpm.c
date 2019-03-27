/*-
 * Copyright (c) 2003-2007 Tim Kientzle
 * Copyright (c) 2009 Michihiro NAKAJIMA
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include "test.h"

/*
Execute the following command to rebuild the data for this program:
   tail -n +32 test_read_format_cpio_svr4_gzip_rpm.c | /bin/sh

F=test_read_format_cpio_svr4_gzip_rpm.rpm
NAME=rpmsample
TMPRPM=/tmp/rpm
rm -rf ${TMPRPM}
mkdir -p ${TMPRPM}/BUILD
mkdir -p ${TMPRPM}/RPMS
mkdir -p ${TMPRPM}/SOURCES
mkdir -p ${TMPRPM}/SPECS
mkdir -p ${TMPRPM}/SRPMS
echo "hello" > ${TMPRPM}/BUILD/file1
echo "hello" > ${TMPRPM}/BUILD/file2
echo "hello" > ${TMPRPM}/BUILD/file3
cat > ${TMPRPM}/SPECS/${NAME}.spec <<END
##
%define _topdir ${TMPRPM}
%define _binary_payload w9.gzdio

Summary: Sample data of RPM filter of libarchive
Name: ${NAME}
Version: 1.0.0
Release: 1
License: BSD
URL: http://libarchive.github.com/
BuildArch: noarch
BuildRoot: %{_tmppath}/%{name}-%{version}-root

%install
rm -rf \$RPM_BUILD_ROOT

mkdir -p \$RPM_BUILD_ROOT%{_sysconfdir}
install -m 644 file1 \$RPM_BUILD_ROOT%{_sysconfdir}/file1
install -m 644 file2 \$RPM_BUILD_ROOT%{_sysconfdir}/file2
install -m 644 file3 \$RPM_BUILD_ROOT%{_sysconfdir}/file3
TZ=utc touch -afm -t 197001020000.01 \$RPM_BUILD_ROOT%{_sysconfdir}/file1
TZ=utc touch -afm -t 197001020000.01 \$RPM_BUILD_ROOT%{_sysconfdir}/file2
TZ=utc touch -afm -t 197001020000.01 \$RPM_BUILD_ROOT%{_sysconfdir}/file3

%files
%{_sysconfdir}/file1
%{_sysconfdir}/file2
%{_sysconfdir}/file3

%description
Sample data.
END
#
rpmbuild -bb ${TMPRPM}/SPECS/${NAME}.spec
uuencode ${F} < ${TMPRPM}/RPMS/noarch/${NAME}-1.0.0-1.noarch.rpm > ${F}.uu

rm -rf ${TMPRPM}
exit 1
*/

DEFINE_TEST(test_read_format_cpio_svr4_gzip_rpm)
{
	struct archive_entry *ae;
	struct archive *a;
	const char *name = "test_read_format_cpio_svr4_gzip_rpm.rpm";
	int r;

	assert((a = archive_read_new()) != NULL);
        r = archive_read_support_filter_gzip(a);
	if (r == ARCHIVE_WARN) {
		skipping("gzip reading not fully supported on this platform");
		assertEqualInt(ARCHIVE_OK, archive_read_free(a));
		return;
        }
	assertEqualIntA(a, ARCHIVE_OK, r);
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_read_support_filter_rpm(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_support_format_all(a));
	extract_reference_file(name);
	assertEqualIntA(a, ARCHIVE_OK, archive_read_open_filename(a, name, 2));

	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualString("./etc/file1", archive_entry_pathname(ae));
	assertEqualInt(86401, archive_entry_mtime(ae));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualString("./etc/file2", archive_entry_pathname(ae));
	assertEqualInt(86401, archive_entry_mtime(ae));
	assertEqualIntA(a, ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualString("./etc/file3", archive_entry_pathname(ae));
	assertEqualInt(86401, archive_entry_mtime(ae));
	assertEqualInt(archive_entry_is_encrypted(ae), 0);
	assertEqualIntA(a, archive_read_has_encrypted_entries(a), ARCHIVE_READ_FORMAT_ENCRYPTION_UNSUPPORTED);

	/* Verify the end-of-archive. */
	assertEqualIntA(a, ARCHIVE_EOF, archive_read_next_header(a, &ae));
  
	/* Verify that the format detection worked. */
	assertEqualInt(archive_filter_code(a, 0), ARCHIVE_FILTER_GZIP);
	assertEqualString(archive_filter_name(a, 0), "gzip");
	assertEqualInt(archive_format(a), ARCHIVE_FORMAT_CPIO_SVR4_NOCRC);
 
	assertEqualInt(ARCHIVE_OK, archive_read_close(a));
	assertEqualInt(ARCHIVE_OK, archive_read_free(a));
}

