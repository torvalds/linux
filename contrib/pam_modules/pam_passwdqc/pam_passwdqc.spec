# $Id: pam_passwdqc.spec,v 1.11 2002/04/16 16:56:52 solar Exp $

Summary: Pluggable password "quality check".
Name: pam_passwdqc
Version: 0.5
Release: owl1
License: relaxed BSD and (L)GPL-compatible
Group: System Environment/Base
Source: pam_passwdqc-%version.tar.gz
BuildRoot: /override/%name-%version

%description
pam_passwdqc is a simple password strength checking module for
PAM-aware password changing programs, such as passwd(1).  In addition
to checking regular passwords, it offers support for passphrases and
can provide randomly generated passwords.  All features are optional
and can be (re-)configured without rebuilding.

%prep
%setup -q

%build
make CFLAGS="-c -Wall -fPIC -DHAVE_SHADOW -DLINUX_PAM $RPM_OPT_FLAGS"

%install
rm -rf $RPM_BUILD_ROOT
make install FAKEROOT=$RPM_BUILD_ROOT

%clean
rm -rf $RPM_BUILD_ROOT

%files
%defattr(-,root,root)
%doc LICENSE README
/lib/security/pam_passwdqc.so

%changelog
* Tue Apr 16 2002 Solar Designer <solar@owl.openwall.com>
- 0.5: preliminary OpenPAM (FreeBSD-current) support in the code and related
code cleanups (thanks to Dag-Erling Smorgrav).

* Thu Feb 07 2002 Michail Litvak <mci@owl.openwall.com>
- Enforce our new spec file conventions.

* Sun Nov 04 2001 Solar Designer <solar@owl.openwall.com>
- Updated to 0.4:
- Added "ask_oldauthtok" and "check_oldauthtok" as needed for stacking with
the Solaris pam_unix;
- Permit for stacking of more than one instance of this module (no statics).

* Tue Feb 13 2001 Solar Designer <solar@owl.openwall.com>
- Install the module as mode 755.

* Tue Dec 19 2000 Solar Designer <solar@owl.openwall.com>
- Added "-Wall -fPIC" to the CFLAGS.

* Mon Oct 30 2000 Solar Designer <solar@owl.openwall.com>
- 0.3: portability fixes (this might build on non-Linux-PAM now).

* Fri Sep 22 2000 Solar Designer <solar@owl.openwall.com>
- 0.2: added "use_authtok", added README.

* Fri Aug 18 2000 Solar Designer <solar@owl.openwall.com>
- 0.1, "retry_wanted" bugfix.

* Sun Jul 02 2000 Solar Designer <solar@owl.openwall.com>
- Initial version (non-public).
