#
#  Makefile for configuration files.
#
#	$Id: Makefile,v 8.60 2005-06-14 02:16:34 gshapiro Exp $
#

#
#  Create configuration files using "m4 ../m4/cf.m4 file.mc > file.cf";
#  this may be easier than tweaking the Makefile.  You do need to
#  have a fairly modern M4 available (GNU m4 works).  On SunOS, use
#  /usr/5bin/m4.
#

# name of source for sendmail.cf (without extension)
CF=	sendmail
# name of source for submit.cf (without extension)
SUBMIT=	submit
# directory for .cf files
MAILDIR=/etc/mail
M4=	m4
CFDIR=	..
SED=  sed
ECHO= echo
CHMOD=	chmod
ROMODE=	444
RM=	rm -f
# use our own install program; should be really confINSTALL
INSTALL=../../devtools/bin/install.sh
# CF file ownership/permissions
CFOWN=root
CFGRP=bin
CFMODE=0444


.SUFFIXES:  .mc .cf

.mc.cf:
	$(RM) $@
	$(M4) ${CFDIR}/m4/cf.m4 $*.mc > $@ || ( $(RM) $@ && exit 1 )
	$(ECHO) "### $*.mc ###" >>$@
	$(SED) -e 's/^/# /' $*.mc >>$@
	$(CHMOD) $(ROMODE) $@

GENERIC=generic-bsd4.4.cf generic-hpux9.cf generic-hpux10.cf \
		generic-linux.cf generic-mpeix.cf generic-nextstep3.3.cf \
		generic-osf1.cf generic-solaris.cf \
		generic-sunos4.1.cf generic-ultrix4.cf
BERKELEY=cs-hpux9.cf cs-hpux10.cf cs-osf1.cf cs-solaris.cf \
	cs-sunos4.1.cf cs-ultrix4.cf \
	s2k-osf1.cf s2k-ultrix4.cf \
	chez.cs.cf huginn.cs.cf mail.cs.cf mail.eecs.cf mailspool.cs.cf \
	python.cs.cf ucbarpa.cf ucbvax.cf vangogh.cs.cf
OTHER=	knecht.cf
ALL=	submit.cf $(GENERIC) $(OTHER)

all: $(ALL)

berkeley: $(BERKELEY)
generic: $(GENERIC)
other: $(OTHER)

clean cleandir:
	$(RM) $(ALL) core

install:
	@echo "Before installing the .cf files please make sure you have read the"
	@echo "instructions in the file ../../INSTALL.  You should have prepared the"
	@echo "files \"submit.mc\" (supplied) and \"sendmail.mc\". Then you can use"
	@echo ""
	@echo "	make install-cf"
	@echo ""
	@echo "If you use a different name than \"sendmail\" for your main .mc file"

	@echo "then you should use"
	@echo ""
	@echo "	make install-cf CF=config"
	@echo ""
	@echo "where \"config\" is the name of your main .mc file."

install-cf:  install-sendmail-cf install-submit-cf

install-sendmail-cf: $(CF).cf
	$(INSTALL) -c -o $(CFOWN) -g $(CFGRP) -m $(CFMODE) $(CF).cf ${DESTDIR}$(MAILDIR)/sendmail.cf

install-submit-cf: $(SUBMIT).cf
	$(INSTALL) -c -o $(CFOWN) -g $(CFGRP) -m $(CFMODE) $(SUBMIT).cf ${DESTDIR}$(MAILDIR)/submit.cf

depend:

# this is overkill, but....
M4FILES=\
	${CFDIR}/domain/Berkeley.EDU.m4 \
	${CFDIR}/domain/CS.Berkeley.EDU.m4 \
	${CFDIR}/domain/EECS.Berkeley.EDU.m4 \
	${CFDIR}/domain/S2K.Berkeley.EDU.m4 \
	${CFDIR}/domain/berkeley-only.m4 \
	${CFDIR}/domain/generic.m4 \
	${CFDIR}/feature/accept_unqualified_senders.m4 \
	${CFDIR}/feature/accept_unresolvable_domains.m4 \
	${CFDIR}/feature/access_db.m4 \
	${CFDIR}/feature/allmasquerade.m4 \
	${CFDIR}/feature/always_add_domain.m4 \
	${CFDIR}/feature/bcc.m4 \
	${CFDIR}/feature/bestmx_is_local.m4 \
	${CFDIR}/feature/bitdomain.m4 \
	${CFDIR}/feature/blacklist_recipients.m4 \
	${CFDIR}/feature/conncontrol.m4 \
	${CFDIR}/feature/dnsbl.m4 \
	${CFDIR}/feature/domaintable.m4 \
	${CFDIR}/feature/generics_entire_domain.m4 \
	${CFDIR}/feature/genericstable.m4 \
	${CFDIR}/feature/ldap_routing.m4 \
	${CFDIR}/feature/limited_masquerade.m4 \
	${CFDIR}/feature/local_lmtp.m4 \
	${CFDIR}/feature/local_procmail.m4 \
	${CFDIR}/feature/loose_relay_check.m4 \
	${CFDIR}/feature/mailertable.m4 \
	${CFDIR}/feature/masquerade_entire_domain.m4 \
	${CFDIR}/feature/masquerade_envelope.m4 \
	${CFDIR}/feature/no_default_msa.m4 \
	${CFDIR}/feature/nocanonify.m4 \
	${CFDIR}/feature/nopercenthack.m4 \
	${CFDIR}/feature/notsticky.m4 \
	${CFDIR}/feature/nouucp.m4 \
	${CFDIR}/feature/nullclient.m4 \
	${CFDIR}/feature/prefixmod.m4 \
	${CFDIR}/feature/promiscuous_relay.m4 \
	${CFDIR}/feature/redirect.m4 \
	${CFDIR}/feature/ratecontrol.m4 \
	${CFDIR}/feature/relay_based_on_MX.m4 \
	${CFDIR}/feature/relay_entire_domain.m4 \
	${CFDIR}/feature/relay_hosts_only.m4 \
	${CFDIR}/feature/relay_local_from.m4 \
	${CFDIR}/feature/relay_mail_from.m4 \
	${CFDIR}/feature/smrsh.m4 \
	${CFDIR}/feature/stickyhost.m4 \
	${CFDIR}/feature/tls_session_features.m4 \
	${CFDIR}/feature/use_ct_file.m4 \
	${CFDIR}/feature/use_cw_file.m4 \
	${CFDIR}/feature/uucpdomain.m4 \
	${CFDIR}/feature/virtuser_entire_domain.m4 \
	${CFDIR}/feature/virtusertable.m4 \
	${CFDIR}/hack/cssubdomain.m4 \
	${CFDIR}/hack/xconnect.m4 \
	${CFDIR}/m4/cf.m4 \
	${CFDIR}/m4/cfhead.m4 \
	${CFDIR}/m4/proto.m4 \
	${CFDIR}/m4/version.m4 \
	${CFDIR}/mailer/cyrus.m4 \
	${CFDIR}/mailer/fax.m4 \
	${CFDIR}/mailer/local.m4 \
	${CFDIR}/mailer/mail11.m4 \
	${CFDIR}/mailer/phquery.m4 \
	${CFDIR}/mailer/pop.m4 \
	${CFDIR}/mailer/procmail.m4 \
	${CFDIR}/mailer/qpage.m4 \
	${CFDIR}/mailer/smtp.m4 \
	${CFDIR}/mailer/usenet.m4 \
	${CFDIR}/mailer/uucp.m4 \
	${CFDIR}/ostype/aix3.m4 \
	${CFDIR}/ostype/aix4.m4 \
	${CFDIR}/ostype/altos.m4 \
	${CFDIR}/ostype/amdahl-uts.m4 \
	${CFDIR}/ostype/a-ux.m4 \
	${CFDIR}/ostype/bsd4.3.m4 \
	${CFDIR}/ostype/bsd4.4.m4 \
	${CFDIR}/ostype/bsdi.m4 \
	${CFDIR}/ostype/bsdi1.0.m4 \
	${CFDIR}/ostype/bsdi2.0.m4 \
	${CFDIR}/ostype/dgux.m4 \
	${CFDIR}/ostype/domainos.m4 \
	${CFDIR}/ostype/dragonfly.m4 \
	${CFDIR}/ostype/dynix3.2.m4 \
	${CFDIR}/ostype/freebsd4.m4 \
	${CFDIR}/ostype/freebsd5.m4 \
	${CFDIR}/ostype/freebsd6.m4 \
	${CFDIR}/ostype/gnu.m4 \
	${CFDIR}/ostype/hpux10.m4 \
	${CFDIR}/ostype/hpux11.m4 \
	${CFDIR}/ostype/hpux9.m4 \
	${CFDIR}/ostype/irix4.m4 \
	${CFDIR}/ostype/irix5.m4 \
	${CFDIR}/ostype/irix6.m4 \
	${CFDIR}/ostype/isc4.1.m4 \
	${CFDIR}/ostype/linux.m4 \
	${CFDIR}/ostype/maxion.m4 \
	${CFDIR}/ostype/mklinux.m4 \
	${CFDIR}/ostype/mpeix.m4 \
	${CFDIR}/ostype/nextstep.m4 \
	${CFDIR}/ostype/openbsd.m4 \
	${CFDIR}/ostype/osf1.m4 \
	${CFDIR}/ostype/powerux.m4 \
	${CFDIR}/ostype/ptx2.m4 \
	${CFDIR}/ostype/qnx.m4 \
	${CFDIR}/ostype/riscos4.5.m4 \
	${CFDIR}/ostype/sco-uw-2.1.m4 \
	${CFDIR}/ostype/sco3.2.m4 \
	${CFDIR}/ostype/sinix.m4 \
	${CFDIR}/ostype/solaris2.m4 \
	${CFDIR}/ostype/solaris2.ml.m4 \
	${CFDIR}/ostype/solaris2.pre5.m4 \
	${CFDIR}/ostype/solaris8.m4 \
	${CFDIR}/ostype/sunos3.5.m4 \
	${CFDIR}/ostype/sunos4.1.m4 \
	${CFDIR}/ostype/svr4.m4 \
	${CFDIR}/ostype/ultrix4.m4 \
	${CFDIR}/ostype/unixware7.m4 \
	${CFDIR}/ostype/unknown.m4 \
	${CFDIR}/ostype/uxpds.m4

$(ALL): $(M4FILES)
$(BERKELEY): $(M4FILES)
$(GENERIC): $(M4FILES)
$(OTHER): $(M4FILES)
