#	$OpenBSD: trimcerts.awk,v 1.2 2019/05/15 20:27:42 sthen Exp $
#
# Copyright (c) 2018 Stuart Henderson <sthen@openbsd.org>
#
# Permission to use, copy, modify, and distribute this software for any
# purpose with or without fee is hereby granted, provided that the above
# copyright notice and this permission notice appear in all copies.
#
# THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
# WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
# ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
# WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
# ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
# OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
#
#
#	read in a formatted list of X509 certificates with long decodes,
#	output only short comments plus the certificates themselves
#

BEGIN {
	if (ARGC != 3) {
		print "usage: awk -f trimcert.awk cert.pem outputfile";
		bad=1;
		exit 1;
	}
	ARGC=2;
	incert=0;
}

{
	if ($0 ~ /^-----BEGIN CERTIFICATE-----/) {
		incert=1;
	}
	if ($0 ~ /^#/ || incert) {
		print $0 > ARGV[2];
	}
	if ($0 ~ /^-----END CERTIFICATE-----/) {
		incert=0;
	}
}

END {
	if (!bad) {
		system("chmod 444 " ARGV[2]);
		system("chown root:bin " ARGV[2]);
	}
}
