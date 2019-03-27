# Default values for additional components
%define build_x11_askpass	1

# Define the UID/GID to use for privilege separation
%define sshd_gid	65
%define sshd_uid	71

# The version of x11-ssh-askpass to use
%define xversion	1.2.4.1

# Allow the ability to override defaults with -D skip_xxx=1
%{?skip_x11_askpass:%define build_x11_askpass 0}

Summary:	OpenSSH, a free Secure Shell (SSH) protocol implementation
Name:		openssh
Version:	7.8p1
URL:		https://www.openssh.com/
Release:	1
Source0:	openssh-%{version}.tar.gz
Source1:	x11-ssh-askpass-%{xversion}.tar.gz
License:	BSD
Group:		Productivity/Networking/SSH
BuildRoot:	%{_tmppath}/openssh-%{version}-buildroot
PreReq:		openssl
Obsoletes:	ssh
Provides:	ssh
#
# (Build[ing] Prereq[uisites] only work for RPM 2.95 and newer.)
# building prerequisites -- stuff for
#   OpenSSL (openssl-devel),
#   and Gnome (glibdev, gtkdev, and gnlibsd)
#
BuildPrereq:	openssl
BuildPrereq:	zlib-devel
#BuildPrereq:	glibdev
#BuildPrereq:	gtkdev
#BuildPrereq:	gnlibsd

%package	askpass
Summary:	A passphrase dialog for OpenSSH and the X window System.
Group:		Productivity/Networking/SSH
Requires:	openssh = %{version}
Obsoletes:	ssh-extras
Provides:	openssh:${_libdir}/ssh/ssh-askpass

%if %{build_x11_askpass}
BuildPrereq:	XFree86-devel
%endif

%description
Ssh (Secure Shell) is a program for logging into a remote machine and for
executing commands in a remote machine.  It is intended to replace
rlogin and rsh, and provide secure encrypted communications between
two untrusted hosts over an insecure network.  X11 connections and
arbitrary TCP/IP ports can also be forwarded over the secure channel.

OpenSSH is OpenBSD's rework of the last free version of SSH, bringing it
up to date in terms of security and features, as well as removing all
patented algorithms to separate libraries (OpenSSL).

This package includes all files necessary for both the OpenSSH
client and server.

%description askpass
Ssh (Secure Shell) is a program for logging into a remote machine and for
executing commands in a remote machine.  It is intended to replace
rlogin and rsh, and provide secure encrypted communications between
two untrusted hosts over an insecure network.  X11 connections and
arbitrary TCP/IP ports can also be forwarded over the secure channel.

OpenSSH is OpenBSD's rework of the last free version of SSH, bringing it
up to date in terms of security and features, as well as removing all
patented algorithms to separate libraries (OpenSSL).

This package contains an X Window System passphrase dialog for OpenSSH.

%changelog
* Wed Oct 26 2005 Iain Morgan <imorgan@nas.nasa.gov>
- Removed accidental inclusion of --without-zlib-version-check
* Tue Oct 25 2005 Iain Morgan <imorgan@nas.nasa.gov>
- Overhaul to deal with newer versions of SuSE and OpenSSH
* Mon Jun 12 2000 Damien Miller <djm@mindrot.org>
- Glob manpages to catch compressed files
* Wed Mar 15 2000 Damien Miller <djm@ibs.com.au>
- Updated for new location
- Updated for new gnome-ssh-askpass build
* Sun Dec 26 1999 Chris Saia <csaia@wtower.com>
- Made symlink to gnome-ssh-askpass called ssh-askpass
* Wed Nov 24 1999 Chris Saia <csaia@wtower.com>
- Removed patches that included /etc/pam.d/sshd, /sbin/init.d/rc.sshd, and
  /var/adm/fillup-templates/rc.config.sshd, since Damien merged these into
  his released tarfile
- Changed permissions on ssh_config in the install procedure to 644 from 600
  even though it was correct in the %files section and thus right in the RPMs
- Postinstall script for the server now only prints "Generating SSH host
  key..." if we need to actually do this, in order to eliminate a confusing
  message if an SSH host key is already in place
- Marked all manual pages as %doc(umentation)
* Mon Nov 22 1999 Chris Saia <csaia@wtower.com>
- Added flag to configure daemon with TCP Wrappers support
- Added building prerequisites (works in RPM 3.0 and newer)
* Thu Nov 18 1999 Chris Saia <csaia@wtower.com>
- Made this package correct for SuSE.
- Changed instances of pam_pwdb.so to pam_unix.so, since it works more properly
  with SuSE, and lib_pwdb.so isn't installed by default.
* Mon Nov 15 1999 Damien Miller <djm@mindrot.org>
- Split subpackages further based on patch from jim knoble <jmknoble@pobox.com>
* Sat Nov 13 1999 Damien Miller <djm@mindrot.org>
- Added 'Obsoletes' directives
* Tue Nov 09 1999 Damien Miller <djm@ibs.com.au>
- Use make install
- Subpackages
* Mon Nov 08 1999 Damien Miller <djm@ibs.com.au>
- Added links for slogin
- Fixed perms on manpages
* Sat Oct 30 1999 Damien Miller <djm@ibs.com.au>
- Renamed init script
* Fri Oct 29 1999 Damien Miller <djm@ibs.com.au>
- Back to old binary names
* Thu Oct 28 1999 Damien Miller <djm@ibs.com.au>
- Use autoconf
- New binary names
* Wed Oct 27 1999 Damien Miller <djm@ibs.com.au>
- Initial RPMification, based on Jan "Yenya" Kasprzak's <kas@fi.muni.cz> spec.

%prep

%if %{build_x11_askpass}
%setup -q -a 1
%else
%setup -q
%endif

%build
CFLAGS="$RPM_OPT_FLAGS" \
%configure	--prefix=/usr \
		--sysconfdir=%{_sysconfdir}/ssh \
		--mandir=%{_mandir} \
		--with-privsep-path=/var/lib/empty \
		--with-pam \
		--libexecdir=%{_libdir}/ssh
make

%if %{build_x11_askpass}
cd x11-ssh-askpass-%{xversion}
%configure	--mandir=/usr/X11R6/man \
		--libexecdir=%{_libdir}/ssh
xmkmf -a
make
cd ..
%endif

%install
rm -rf $RPM_BUILD_ROOT
make install DESTDIR=$RPM_BUILD_ROOT/
install -d $RPM_BUILD_ROOT/etc/pam.d/
install -d $RPM_BUILD_ROOT/etc/init.d/
install -d $RPM_BUILD_ROOT/var/adm/fillup-templates
install -m644 contrib/sshd.pam.generic $RPM_BUILD_ROOT/etc/pam.d/sshd
install -m744 contrib/suse/rc.sshd $RPM_BUILD_ROOT/etc/init.d/sshd
install -m744 contrib/suse/sysconfig.ssh \
   $RPM_BUILD_ROOT/var/adm/fillup-templates

%if %{build_x11_askpass}
cd x11-ssh-askpass-%{xversion}
make install install.man BINDIR=%{_libdir}/ssh DESTDIR=$RPM_BUILD_ROOT/
rm -f $RPM_BUILD_ROOT/usr/share/Ssh.bin
%endif

%clean
rm -rf $RPM_BUILD_ROOT

%pre
/usr/sbin/groupadd -g %{sshd_gid} -o -r sshd 2> /dev/null || :
/usr/sbin/useradd -r -o -g sshd -u %{sshd_uid} -s /bin/false -c "SSH Privilege Separation User" -d /var/lib/sshd sshd 2> /dev/null || :

%post
/usr/bin/ssh-keygen -A
%{fillup_and_insserv -n -y ssh sshd}
%run_permissions

%verifyscript
%verify_permissions -e /etc/ssh/sshd_config -e /etc/ssh/ssh_config -e /usr/bin/ssh

%preun
%stop_on_removal sshd

%postun
%restart_on_update sshd
%{insserv_cleanup}

%files
%defattr(-,root,root)
%doc ChangeLog OVERVIEW README* PROTOCOL*
%doc TODO CREDITS LICENCE
%attr(0755,root,root) %dir %{_sysconfdir}/ssh
%attr(0644,root,root) %config(noreplace) %{_sysconfdir}/ssh/ssh_config
%attr(0600,root,root) %config(noreplace) %{_sysconfdir}/ssh/sshd_config
%attr(0600,root,root) %config(noreplace) %{_sysconfdir}/ssh/moduli
%attr(0644,root,root) %config(noreplace) /etc/pam.d/sshd
%attr(0755,root,root) %config /etc/init.d/sshd
%attr(0755,root,root) %{_bindir}/ssh-keygen
%attr(0755,root,root) %{_bindir}/scp
%attr(0755,root,root) %{_bindir}/ssh
%attr(0755,root,root) %{_bindir}/ssh-agent
%attr(0755,root,root) %{_bindir}/ssh-add
%attr(0755,root,root) %{_bindir}/ssh-keyscan
%attr(0755,root,root) %{_bindir}/sftp
%attr(0755,root,root) %{_sbindir}/sshd
%attr(0755,root,root) %dir %{_libdir}/ssh
%attr(0755,root,root) %{_libdir}/ssh/sftp-server
%attr(4711,root,root) %{_libdir}/ssh/ssh-keysign
%attr(0755,root,root) %{_libdir}/ssh/ssh-pkcs11-helper
%attr(0644,root,root) %doc %{_mandir}/man1/scp.1*
%attr(0644,root,root) %doc %{_mandir}/man1/sftp.1*
%attr(0644,root,root) %doc %{_mandir}/man1/ssh.1*
%attr(0644,root,root) %doc %{_mandir}/man1/ssh-add.1*
%attr(0644,root,root) %doc %{_mandir}/man1/ssh-agent.1*
%attr(0644,root,root) %doc %{_mandir}/man1/ssh-keygen.1*
%attr(0644,root,root) %doc %{_mandir}/man1/ssh-keyscan.1*
%attr(0644,root,root) %doc %{_mandir}/man5/moduli.5*
%attr(0644,root,root) %doc %{_mandir}/man5/ssh_config.5*
%attr(0644,root,root) %doc %{_mandir}/man5/sshd_config.5*
%attr(0644,root,root) %doc %{_mandir}/man8/sftp-server.8*
%attr(0644,root,root) %doc %{_mandir}/man8/ssh-keysign.8*
%attr(0644,root,root) %doc %{_mandir}/man8/ssh-pkcs11-helper.8*
%attr(0644,root,root) %doc %{_mandir}/man8/sshd.8*
%attr(0644,root,root) /var/adm/fillup-templates/sysconfig.ssh

%if %{build_x11_askpass}
%files askpass
%defattr(-,root,root)
%doc x11-ssh-askpass-%{xversion}/README
%doc x11-ssh-askpass-%{xversion}/ChangeLog
%doc x11-ssh-askpass-%{xversion}/SshAskpass*.ad
%attr(0755,root,root) %{_libdir}/ssh/ssh-askpass
%attr(0755,root,root) %{_libdir}/ssh/x11-ssh-askpass
%attr(0644,root,root) %doc /usr/X11R6/man/man1/ssh-askpass.1x*
%attr(0644,root,root) %doc /usr/X11R6/man/man1/x11-ssh-askpass.1x*
%attr(0644,root,root) %config /usr/X11R6/lib/X11/app-defaults/SshAskpass
%endif
