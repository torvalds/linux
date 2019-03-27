# $Id$

include $(top_srcdir)/Makefile.am.common

if LIBEDIT
dir_editline = libedit
endif
if OTP
dir_otp = otp
endif
if DCE
dir_dce = kdfs
endif
if COM_ERR
dir_com_err = com_err
endif
if !HAVE_OPENSSL
dir_hcrypto = hcrypto
endif
if !SQLITE3
dir_sqlite = sqlite
endif

SUBDIRS = \
	roken \
	vers \
	$(dir_editline) \
	$(dir_com_err) \
	sl \
	wind \
	asn1 \
	$(dir_sqlite) \
	$(dir_hcrypto) \
	ipc \
	hx509 \
	krb5 \
	ntlm \
	kafs \
	gssapi \
	hdb \
	kadm5 \
	$(dir_otp) \
	$(dir_dce)

EXTRA_DIST = NTMakefile heimdal
