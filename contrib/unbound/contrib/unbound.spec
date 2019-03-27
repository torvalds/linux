Summary: Validating, recursive, and caching DNS resolver
Name: unbound
Version: 1.4.18
Release: 1%{?dist}
License: BSD
Url: http://www.nlnetlabs.nl/unbound/
Source: http://www.unbound.net/downloads/%{name}-%{version}.tar.gz
#Source1: unbound.init
Group: System Environment/Daemons
Requires: ldns
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)
BuildRequires: flex, openssl-devel, expat-devel, ldns-devel

%description
Unbound is a validating, recursive, and caching DNS resolver.

The C implementation of Unbound is developed and maintained by NLnet
Labs. It is based on ideas and algorithms taken from a java prototype
developed by Verisign labs, Nominet, Kirei and ep.net.

Unbound is designed as a set of modular components, so that also
DNSSEC (secure DNS) validation and stub-resolvers (that do not run
as a server, but are linked into an application) are easily possible.

The source code is under a BSD License.

%prep
%setup -q

# configure with /var/unbound/unbound.conf so that all default chroot, 
# pidfile and config file are in /var/unbound, ready for chroot jail set up.
%configure --with-conf-file=%{_localstatedir}/%{name}/unbound.conf --disable-rpath

%build
#%{__make} %{?_smp_mflags}
make

%install
rm -rf %{buildroot}
%{__make} DESTDIR=%{buildroot} install
install -d 0700 %{buildroot}%{_localstatedir}/%{name}
install -d 0755 %{buildroot}%{_initrddir}
install -m 0755 contrib/unbound.init %{buildroot}%{_initrddir}/unbound
# add symbolic link from /etc/unbound.conf -> /var/unbound/unbound.conf
ln -s %{_localstatedir}/unbound/unbound.conf %{buildroot}%{_sysconfdir}/unbound.conf 
# remove static library from install (fedora packaging guidelines)
rm -f %{buildroot}%{_libdir}/libunbound.a %{buildroot}%{_libdir}/libunbound.la

%clean
rm -rf ${RPM_BUILD_ROOT}

%files
%defattr(-,root,root,-)
%doc doc/README doc/CREDITS doc/LICENSE doc/FEATURES
%attr(0755,root,root) %{_initrddir}/%{name}
%attr(0700,%{name},%{name}) %dir %{_localstatedir}/%{name}
%attr(0644,%{name},%{name}) %config(noreplace) %{_localstatedir}/%{name}/unbound.conf
%attr(0644,%{name},%{name}) %config(noreplace) %{_sysconfdir}/unbound.conf
%{_sbindir}/*
%{_mandir}/*/*
%{_includedir}/*
%{_libdir}/libunbound*

%pre
getent group unbound >/dev/null || groupadd -r unbound
getent passwd unbound >/dev/null || \
useradd -r -g unbound -d /var/unbound -s /sbin/nologin \
    -c "unbound name daemon" unbound
exit 0

%post
# This adds the proper /etc/rc*.d links for the script
/sbin/chkconfig --add %{name}

%preun
if [ $1 -eq 0 ]; then
	/sbin/service %{name} stop >/dev/null 2>&1
	/sbin/chkconfig --del %{name}
	# remove root jail 
	rm -f /var/unbound/dev/log /var/unbound/dev/random /var/unbound/etc/localtime /var/unbound/etc/resolv.conf >/dev/null 2>&1
	rmdir /var/unbound/dev >/dev/null 2>&1 || :
	rmdir /var/unbound/etc >/dev/null 2>&1 || :
	rmdir /var/unbound >/dev/null 2>&1 || :
fi

%postun
if [ "$1" -ge "1" ]; then
	/sbin/service %{name} condrestart >/dev/null 2>&1 || :
fi

%changelog
* Thu Jul 13 2011 Wouter Wijngaards <wouter@nlnetlabs.nl> - 1.4.8
- ldns required and ldns-devel required for build, no more ldns-builtin.

* Thu Mar 17 2011 Wouter Wijngaards <wouter@nlnetlabs.nl> - 1.4.8
- removed --disable-gost, assume recent openssl on the destination platform.

* Wed Mar 16 2011 Harold Jones <hajones@verisign.com> - 1.4.8
- Bump version number to latest
- Add expat-devel to BuildRequires
- Added --disable-gost for building on CentOS 5.x
- Added --with-ldns-builtin for CentOS 5.x

* Thu May 22 2008 Wouter Wijngaards <wouter@nlnetlabs.nl> - 1.0.0
- contrib changes from Patrick Vande Walle.

* Thu Apr 25 2008 Wouter Wijngaards <wouter@nlnetlabs.nl> - 0.12
- Using parts from ports collection entry by Jaap Akkerhuis.
- Using Fedoraproject wiki guidelines.

* Wed Apr 23 2008 Wouter Wijngaards <wouter@nlnetlabs.nl> - 0.11
- Initial version.
