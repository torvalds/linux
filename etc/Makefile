#	$OpenBSD: Makefile,v 1.492 2025/06/17 01:36:55 dlg Exp $

.include <bsd.own.mk>

TZDIR=		/usr/share/zoneinfo
LOCALTIME=	Canada/Mountain
MTREEDIR=	/etc/mtree

NOOBJ=

KERNELS = GENERIC bsd

.if exists(etc.${MACHINE}/Makefile.inc)
.include "etc.${MACHINE}/Makefile.inc"
.endif

.for CONF K in ${KERNELS}
.  if !target($K)
$K:
	cd ../sys/arch/${MACHINE}/compile/${CONF} && \
		{ [[ -n "${KEEPKERNELS}" ]] || ${MAKE} cleandir; } && \
		${MAKE} config && ${MAKE} clean && exec ${MAKE};
.  endif
ALL_KERNELS += $K
.endfor

kernels: ${ALL_KERNELS}
.for CONF K in ${KERNELS}
	cp ../sys/arch/${MACHINE}/compile/${CONF}/obj/bsd ${RELEASEDIR}/$K
	chmod a+r ${RELEASEDIR}/$K
.endfor
	cd ${.CURDIR}/../sys/arch/${MACHINE}/compile/ && \
		tar -chzf ${DESTDIR}/usr/share/relink/kernel.tgz -s ',/obj/,/,' \
		GENERIC*/obj/*.o GENERIC*/obj/Makefile \
		GENERIC*/obj/ld.script GENERIC*/obj/makegap.sh
	chown root:wheel ${DESTDIR}/usr/share/relink/kernel.tgz
	chmod 644 ${DESTDIR}/usr/share/relink/kernel.tgz

# -rw-r--r--
BINOWN= root
BINGRP= wheel
MUTABLE=changelist daily etc.${MACHINE}/disktab \
	etc.${MACHINE}/login.conf ftpusers gettytab group ksh.kshrc locate.rc \
	mailer.conf moduli monthly netstart newsyslog.conf ntpd.conf \
	pf.os protocols rc rc.conf rpc services shells syslog.conf weekly

# -rw-r--r--
EXAMPLES=acme-client.conf chio.conf dhcpd.conf exports \
	httpd.conf ifstated.conf inetd.conf man.conf mixerctl.conf \
	mrouted.conf ntpd.conf printcap rad.conf rbootd.conf \
	remote sensorsd.conf wsconsctl.conf

# -rw-------
EXAMPLES_600=bgpd.conf doas.conf dvmrpd.conf eigrpd.conf hostapd.conf \
	iked.conf ipsec.conf ldapd.conf ldpd.conf login_ldap.conf \
	ospf6d.conf ospfd.conf pf.conf radiusd.conf rc.local \
	rc.securelevel rc.shutdown relayd.conf ripd.conf sasyncd.conf \
	snmpd.conf vm.conf ypldap.conf

# -r-xr-xr-x
RCDAEMONS=amd apmd bgpd bgplgd bootparamd bpflogd cron \
	dhcpd dhcpleased dhcp6leased dhcrelay dhcrelay6 \
	dvmrpd eigrpd ftpd ftpproxy ftpproxy6 hostapd \
	hotplugd httpd identd ifstated iked inetd isakmpd iscsid ldapd \
	ldattach ldomd ldpd lockd lpd mopd mountd mrouted nfsd npppd nsd ntpd \
	ospf6d ospfd pflogd portmap rad radiusd rarpd rbootd relayd resolvd \
	ripd route6d sasyncd sensorsd slowcgi slaacd smtpd sndiod snmpd spamd \
	spamlogd sshd statd syslogd tftpd tftpproxy unbound \
	unwind vmd watchdogd wsmoused xenodm ypbind ypldap ypserv

MISETS=	base${OSrev}.tgz comp${OSrev}.tgz man${OSrev}.tgz game${OSrev}.tgz

all clean cleandir etc install:

install-mtree:
	${INSTALL} -c -o root -g wheel -m 600 mtree/special \
	    ${DESTDIR}${MTREEDIR}
	${INSTALL} -c -o root -g wheel -m 444 mtree/4.4BSD.dist \
	    ${DESTDIR}${MTREEDIR}
	${INSTALL} -c -o root -g wheel -m 444 mtree/BSD.x11.dist \
	    ${DESTDIR}${MTREEDIR}

.ifndef DESTDIR
distribution-etc-root-var distribution distrib-dirs release:
	@echo setenv DESTDIR before doing that!
	@false
.else
distribution-etc-root-var: distrib-dirs
	${INSTALL} -c -o ${BINOWN} -g ${BINGRP} -m 644 ${MUTABLE} ${DESTDIR}/etc
	sh ttys.pty | cat etc.${MACHINE}/ttys - > ${DESTDIR}/etc/ttys && \
	    chown ${BINOWN}:${BINGRP} ${DESTDIR}/etc/ttys && \
	    chmod 644 ${DESTDIR}/etc/ttys
	cat examples/sysctl.conf etc.${MACHINE}/sysctl.conf > \
	    ${DESTDIR}/etc/examples/sysctl.conf && \
	    chown ${BINOWN}:${BINGRP} ${DESTDIR}/etc/examples/sysctl.conf && \
	    chmod 644 ${DESTDIR}/etc/examples/sysctl.conf
	cat fbtab.head etc.${MACHINE}/fbtab fbtab.tail > ${DESTDIR}/etc/fbtab && \
	    chown ${BINOWN}:${BINGRP} ${DESTDIR}/etc/fbtab && \
	    chmod 644 ${DESTDIR}/etc/fbtab
	${INSTALL} -c -o ${BINOWN} -g operator -m 664 motd ${DESTDIR}/etc
	${INSTALL} -c -o root -g crontab -m 600 crontab ${DESTDIR}/var/cron/tabs/root
	${INSTALL} -c -o root -g wheel -m 600 master.passwd ${DESTDIR}/etc
	pwd_mkdb -p -d ${DESTDIR}/etc /etc/master.passwd
	chown root:wheel ${DESTDIR}/etc/passwd ${DESTDIR}/etc/pwd.db
	chown root:_shadow ${DESTDIR}/etc/spwd.db
	${INSTALL} -c -o root -g wheel -m 600 pf.conf ${DESTDIR}/etc
	${INSTALL} -c -o root -g _nsd -m 640 nsd.conf ${DESTDIR}/var/nsd/etc
	${INSTALL} -c -o root -g wheel -m 644 unbound.conf ${DESTDIR}/var/unbound/etc
	${INSTALL} -c -o ${BINOWN} -g ${BINGRP} -m 555 \
	    etc.${MACHINE}/MAKEDEV ${DESTDIR}/dev
	cd root; \
		${INSTALL} -c -o root -g wheel -m 644 dot.cshrc \
		    ${DESTDIR}/root/.cshrc; \
		${INSTALL} -c -o root -g wheel -m 644 dot.login \
		    ${DESTDIR}/root/.login; \
		${INSTALL} -c -o root -g wheel -m 644 dot.profile \
		    ${DESTDIR}/root/.profile; \
		${INSTALL} -c -o root -g wheel -m 644 dot.Xdefaults \
		    ${DESTDIR}/root/.Xdefaults; \
		${INSTALL} -c -o root -g wheel -m 644 dot.cvsrc \
		    ${DESTDIR}/root/.cvsrc; \
		${INSTALL} -c -o root -g wheel -m 600 /dev/null \
		    ${DESTDIR}/root/.ssh/authorized_keys; \
		rm -f ${DESTDIR}/.cshrc ${DESTDIR}/.profile; \
		${INSTALL} -c -o root -g wheel -m 644 dot.cshrc \
		    ${DESTDIR}/.cshrc; \
		${INSTALL} -c -o root -g wheel -m 644 dot.profile \
		    ${DESTDIR}/.profile
	cd skel; \
		${INSTALL} -c -o root -g wheel -m 644 dot.cshrc \
		    ${DESTDIR}/etc/skel/.cshrc; \
		${INSTALL} -c -o root -g wheel -m 644 dot.login \
		    ${DESTDIR}/etc/skel/.login; \
		${INSTALL} -c -o root -g wheel -m 644 dot.mailrc \
		    ${DESTDIR}/etc/skel/.mailrc; \
		${INSTALL} -c -o root -g wheel -m 644 dot.profile \
		    ${DESTDIR}/etc/skel/.profile; \
		${INSTALL} -c -o root -g wheel -m 644 dot.Xdefaults \
		    ${DESTDIR}/etc/skel/.Xdefaults; \
		${INSTALL} -c -o root -g wheel -m 644 dot.cvsrc \
		    ${DESTDIR}/etc/skel/.cvsrc; \
		${INSTALL} -c -o root -g wheel -m 600 /dev/null \
		    ${DESTDIR}/etc/skel/.ssh/authorized_keys
	cd amd; \
		${INSTALL} -c -o root -g wheel -m 644 master.sample \
		    ${DESTDIR}/etc/amd
	cd ppp; \
		${INSTALL} -c -o root -g wheel -m 600 chap-secrets \
		    ${DESTDIR}/etc/ppp; \
		${INSTALL} -c -o root -g wheel -m 600 options \
		    ${DESTDIR}/etc/ppp; \
		${INSTALL} -c -o root -g wheel -m 600 options.sample \
		    ${DESTDIR}/etc/ppp; \
		${INSTALL} -c -o root -g wheel -m 600 chatscript.sample \
		    ${DESTDIR}/etc/ppp; \
		${INSTALL} -c -o root -g wheel -m 600 pap-secrets \
		    ${DESTDIR}/etc/ppp
	cd rpki; \
		${INSTALL} -c -o root -g wheel -m 644 \
		    afrinic.tal apnic.tal arin.tal lacnic.tal ripe.tal \
		    afrinic.constraints apnic.constraints arin.constraints \
		    lacnic.constraints ripe.constraints \
		    ${DESTDIR}/etc/rpki
	cd examples; \
		${INSTALL} -c -o root -g wheel -m 644 ${EXAMPLES} \
		    ${DESTDIR}/etc/examples; \
		${INSTALL} -c -o root -g wheel -m 600 ${EXAMPLES_600} \
		    ${DESTDIR}/etc/examples; \
		chgrp operator ${DESTDIR}/etc/examples/chio.conf
		chgrp auth ${DESTDIR}/etc/examples/login_ldap.conf
		chmod 0640 ${DESTDIR}/etc/examples/login_ldap.conf
	cd signify; \
		${INSTALL} -c -o root -g wheel -m 644 *.pub \
		    ${DESTDIR}/etc/signify
	ln -fs ${TZDIR}/${LOCALTIME} ${DESTDIR}/etc/localtime
	chown -h root:wheel ${DESTDIR}/etc/localtime
	ln -fs /usr/sbin/rmt ${DESTDIR}/etc/rmt
	chown -h root:wheel ${DESTDIR}/etc/rmt
	${INSTALL} -c -o root -g wheel -m 644 /dev/null \
	    ${DESTDIR}/var/account/acct
	${INSTALL} -c -o root -g wheel -m 644 minfree \
	    ${DESTDIR}/var/crash
	${INSTALL} -c -o ${BINOWN} -g operator -m 664 /dev/null \
	    ${DESTDIR}/etc/dumpdates
	${INSTALL} -c -o root -g crontab -m 660 /dev/null \
	    ${DESTDIR}/var/cron/at.deny
	${INSTALL} -c -o root -g crontab -m 660 /dev/null \
	    ${DESTDIR}/var/cron/cron.deny
	${INSTALL} -c -o root -g wheel -m 600 /dev/null \
	    ${DESTDIR}/var/cron/log
	${INSTALL} -c -o root -g wheel -m 444 /dev/null \
	    ${DESTDIR}/var/db/locate.database
	${INSTALL} -c -o _rpki-client -g wheel -m 644 /dev/null \
	    ${DESTDIR}/var/db/rpki-client/openbgpd
	${INSTALL} -c -o ${BINOWN} -g wheel -m 640 /dev/null \
	    ${DESTDIR}/var/log/authlog
	${INSTALL} -c -o ${BINOWN} -g wheel -m 640 /dev/null \
	    ${DESTDIR}/var/log/daemon
	${INSTALL} -c -o ${BINOWN} -g wheel -m 600 /dev/null \
	    ${DESTDIR}/var/log/failedlogin
	${INSTALL} -c -o ${BINOWN} -g wheel -m 640 /dev/null \
	    ${DESTDIR}/var/log/ftpd
	${INSTALL} -c -o ${BINOWN} -g wheel -m 644 /dev/null \
	    ${DESTDIR}/var/log/lastlog
	${INSTALL} -c -o ${BINOWN} -g wheel -m 640 /dev/null \
	    ${DESTDIR}/var/log/lpd-errs
	${INSTALL} -c -o ${BINOWN} -g wheel -m 640 /dev/null \
	    ${DESTDIR}/var/log/maillog
	${INSTALL} -c -o ${BINOWN} -g wheel -m 644 /dev/null \
	    ${DESTDIR}/var/log/messages
	${INSTALL} -c -o ${BINOWN} -g wheel -m 600 /dev/null \
	    ${DESTDIR}/var/log/secure
	${INSTALL} -c -o ${BINOWN} -g wheel -m 644 /dev/null \
	    ${DESTDIR}/var/log/wtmp
	${INSTALL} -c -o ${BINOWN} -g wheel -m 640 /dev/null \
	    ${DESTDIR}/var/log/xferlog
	cd ../usr.sbin/ypserv/ypinit && exec ${MAKE} distribution
	cd ../usr.bin/ssh && exec ${MAKE} distribution
	cd ../lib/libcrypto && exec ${MAKE} distribution
	cd ../usr.bin/bgplg && exec ${MAKE} distribution
	cd ../usr.bin/mail && exec ${MAKE} distribution
	cd ../usr.sbin/ikectl && exec ${MAKE} distribution
	cd ../usr.sbin/ldapd && exec ${MAKE} distribution
	cd ../usr.sbin/npppd && exec ${MAKE} distribution
	cd mail && exec ${MAKE} distribution
	${INSTALL} -c -o root -g wheel -m 600 root/root.mail \
	    ${DESTDIR}/var/mail/root
	cd rc.d; \
		${INSTALL} -c -o root -g wheel -m 644 rc.subr \
		    ${DESTDIR}/etc/rc.d && \
		${INSTALL} -c -o ${BINOWN} -g ${BINGRP} -m 555 ${RCDAEMONS} \
		    ${DESTDIR}/etc/rc.d
	cd ${DESTDIR}/var; ln -fs ../tmp
	chown -h root:wheel ${DESTDIR}/var/tmp
	touch ${DESTDIR}/var/sysmerge/etcsum
	chown root:wheel ${DESTDIR}/var/sysmerge/etcsum
	chmod 644 ${DESTDIR}/var/sysmerge/etcsum
	cd ${DESTDIR}/ && \
		sort ${.CURDIR}/../distrib/sets/lists/etc/{mi,md.${MACHINE}} | \
		xargs sha256 -h ${DESTDIR}/var/sysmerge/etcsum || true
	cd ${DESTDIR}/ && \
		sort ${.CURDIR}/../distrib/sets/lists/etc/{mi,md.${MACHINE}} | \
		pax -w -d | gzip -9 > ${DESTDIR}/var/sysmerge/etc.tgz
	chown root:wheel ${DESTDIR}/var/sysmerge/etc.tgz
	chmod 644 ${DESTDIR}/var/sysmerge/etc.tgz

distribution:
	exec ${MAKE} distribution-etc-root-var
	cd .. && exec ${MAKE} install

distrib-dirs:
	if [ ! -d ${DESTDIR}/. ]; then \
		${INSTALL} -d -o root -g wheel -m 755 ${DESTDIR}; \
	fi
	mtree -qdef mtree/4.4BSD.dist -p ${DESTDIR}/ -U
	cd ${DESTDIR}/; ln -fhs usr/src/sys sys
	chown -h root:wheel ${DESTDIR}/sys

.ifndef RELEASEDIR
release:
	@echo setenv RELEASEDIR before building a release.
	@false
.else
release:
	umask ${WOBJUMASK}; exec ${MAKE} do-release

do-release:
	@if [[ `id -u` -ne 0 ]]; then \
		echo $@ must be called by root >&2; \
		false; \
	fi
	@destmp=`df -P ${DESTDIR} | awk 'END { print $$6 }'`; \
	if ! mount | grep -q " $${destmp} .*noperm"; then \
		echo ${DESTDIR} must be on a noperm filesystem >&2; \
		false; \
	fi; \
	if [[ `stat -f '%Su %Lp' $${destmp}` != '${BUILDUSER} 700' ]]; then \
		echo $${destmp} must have owner BUILDUSER and mode 700 >&2; \
		false; \
	fi
	${MAKE} release-sets
	${MAKE} distrib
	su ${BUILDUSER} -c 'exec ${MAKE} sha'
	-cd ${RELEASEDIR} && chmod g-w,a+rX *

release-sets:
	su ${BUILDUSER} -c 'exec ${MAKE} distribution'
	su ${BUILDUSER} -c 'exec ${MAKE} kernels'
	cp -p ${DESTDIR}/usr/share/relink/kernel.tgz /usr/share/relink/kernel.tgz
	${MAKE} bootblocks
	cd ${RELEASEDIR} && rm -f SHA256
	cd ../distrib/sets && exec su ${BUILDUSER} -c 'exec sh maketars ${OSrev}'

sha:
	-cd ${RELEASEDIR}; \
	    date -u "+Build date: %s - %+" > BUILDINFO; \
	    cksum -a sha256 BUILDINFO INSTALL.`arch -ks` \
	    ${ALL_KERNELS} ${MDEXT} ${MISETS} | sort > SHA256

.endif

.endif	# DESTDIR check

distrib:
	umask 022; cd ../distrib && \
	    ${MAKE} && exec ${MAKE} install

.PHONY: distribution-etc-root-var distribution distrib-dirs \
	release allarchs kernels release-sets m4 install-mtree \
	bootblocks do-release ${ALL_KERNELS}

SUBDIR+= etc.alpha etc.amd64 etc.arm64 etc.armv7
SUBDIR+= etc.hppa etc.i386 etc.landisk etc.loongson etc.luna88k
SUBDIR+= etc.macppc etc.octeon etc.powerpc64 etc.riscv64
SUBDIR+= etc.sparc64

.include <bsd.subdir.mk>
.include <bsd.prog.mk>
