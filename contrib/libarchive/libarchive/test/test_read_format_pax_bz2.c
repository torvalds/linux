/*-
 * Copyright (c) 2003-2007 Tim Kientzle
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
__FBSDID("$FreeBSD$");

static unsigned char archive[] = {
'B','Z','h','9','1','A','Y','&','S','Y',152,180,30,185,0,0,140,127,176,212,
144,0,' ','@',1,255,226,8,'d','H',' ',238,'/',159,'@',0,16,4,'@',0,8,'0',
0,216,'A',164,167,147,'Q',147,'!',180,'#',0,'L',153,162,'i',181,'?','P',192,
26,'h','h',209,136,200,6,128,13,12,18,132,202,'5','O',209,'5','=',26,'2',
154,7,168,12,2,'d',252,13,254,29,'4',247,181,'l','T','i',130,5,195,1,'2',
'@',146,18,251,245,'c','J',130,224,172,'$','l','4',235,170,186,'c','1',255,
179,'K',188,136,18,208,152,192,149,153,10,'{','|','0','8',166,3,6,9,128,172,
'(',164,220,244,149,6,' ',243,212,'B',25,17,'6',237,13,'I',152,'L',129,209,
'G','J','<',137,'Y',16,'b',21,18,'a','Y','l','t','r',160,128,147,'l','f',
'~',219,206,'=','?','S',233,'3',251,'L','~',17,176,169,'%',23,'_',225,'M',
'C','u','k',218,8,'q',216,'(',22,235,'K',131,136,146,136,147,202,0,158,134,
'F',23,160,184,'s','0','a',246,'*','P',7,2,238,'H',167,10,18,19,22,131,215,
' '};

DEFINE_TEST(test_read_format_pax_bz2)
{
	struct archive_entry *ae;
	struct archive *a;
	int r;

	assert((a = archive_read_new()) != NULL);
	r = archive_read_support_filter_bzip2(a);
	if (r != ARCHIVE_OK) {
		archive_read_close(a);
		skipping("Bzip2 unavailable");
		return;
	}
	assertEqualIntA(a,ARCHIVE_OK, archive_read_support_format_all(a));
	assertEqualIntA(a,ARCHIVE_OK,
	    archive_read_open_memory(a, archive, sizeof(archive)));
	assertEqualIntA(a,ARCHIVE_OK, archive_read_next_header(a, &ae));
	assertEqualInt(1, archive_file_count(a));
	assertEqualInt(archive_filter_code(a, 0), ARCHIVE_FILTER_BZIP2);
	assertEqualInt(archive_format(a), ARCHIVE_FORMAT_TAR_PAX_INTERCHANGE);
	assertEqualInt(archive_entry_is_encrypted(ae), 0);
	assertEqualIntA(a, archive_read_has_encrypted_entries(a), ARCHIVE_READ_FORMAT_ENCRYPTION_UNSUPPORTED);
	assertEqualIntA(a,ARCHIVE_OK, archive_read_close(a));
	assertEqualInt(ARCHIVE_OK, archive_read_free(a));
}


