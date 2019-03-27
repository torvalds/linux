%{?!with_python:      %global with_python      1}

%if %{with_python}
%{!?python_sitelib: %global python_sitelib %(%{__python} -c "from distutils.sysconfig import get_python_lib; print get_python_lib()")}
%{!?python_sitearch: %global python_sitearch %(%{__python} -c "from distutils.sysconfig import get_python_lib; print get_python_lib(1)")}
%endif

Summary: Lowlevel DNS(SEC) library with API
Name: ldns
Version: 1.6.13
Release: 1%{?dist}
License: BSD
Url: http://www.nlnetlabs.nl/%{name}/
Source: http://www.nlnetlabs.nl/downloads/%{%name}/%{name}-%{version}.tar.gz
Group: System Environment/Libraries
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)
BuildRequires: perl, libpcap-devel, openssl-devel , gcc-c++, doxygen,
# Only needed for builds from svn snapshot
# BuildRequires: libtool, autoconf, automake

%if %{with_python}
BuildRequires:  python-devel, swig
%endif

%description
ldns is a library with the aim to simplify DNS programing in C. All
lowlevel DNS/DNSSEC operations are supported. We also define a higher
level API which allows a programmer to (for instance) create or sign
packets.

%package devel
Summary: Development package that includes the ldns header files
Group: Development/Libraries
Requires: %{name} = %{version}-%{release}

%description devel
The devel package contains the ldns library and the include files

%if %{with_python}
%package python
Summary: Python extensions for ldns
Group: Applications/System
Requires: %{name} = %{version}-%{release}

%description python
Python extensions for ldns
%endif

%prep
%setup -q 
# To built svn snapshots
# rm config.guess config.sub ltmain.sh
# aclocal
# libtoolize -c 
# autoreconf 

%build
%configure --disable-rpath --disable-static --with-sha2 --disable-gost \
%if %{with_python}
 --with-pyldns
%endif

(cd drill ; %configure --disable-rpath --disable-static --with-sha2 --disable-gost --with-ldns=%{buildroot}/lib/ )
(cd examples ; %configure --disable-rpath --disable-static --with-sha2 --disable-gost --with-ldns=%{buildroot}/lib/ )

make %{?_smp_mflags} 
( cd drill ; make %{?_smp_mflags} )
( cd examples ; make %{?_smp_mflags} )
make %{?_smp_mflags} doc

%install
rm -rf %{buildroot}

make DESTDIR=%{buildroot} INSTALL="%{__install} -p" install 
make DESTDIR=%{buildroot} INSTALL="%{__install} -p" install-doc

# don't install another set of man pages from doc/
rm -rf doc/man/

# don't package building script for install-doc in doc section
rm doc/doxyparse.pl

# remove .la files
rm -rf %{buildroot}%{_libdir}/*.la %{buildroot}%{python_sitearch}/*.la
(cd drill ; make DESTDIR=%{buildroot} install)
(cd examples; make DESTDIR=%{buildroot} install)

%clean
rm -rf %{buildroot}

%files 
%defattr(-,root,root)
%{_libdir}/libldns*so.*
%{_bindir}/drill
%{_bindir}/ldnsd
%{_bindir}/ldns-chaos
%{_bindir}/ldns-compare-zones
%{_bindir}/ldns-[d-z]*
%doc README LICENSE
%{_mandir}/*/*

%files devel
%defattr(-,root,root,-)
%{_libdir}/libldns*so
%{_bindir}/ldns-config
%dir %{_includedir}/ldns
%{_includedir}/ldns/*.h
%doc doc Changelog README

%if %{with_python}
%files python
%defattr(-,root,root)
%{python_sitearch}/*
%endif

%post -p /sbin/ldconfig

%postun -p /sbin/ldconfig

%changelog
* Thu Sep 22 2011 Paul Wouters <paul@xelerance.com> - 1.6.11-1
- Updated to 1.6.11
- Cleanup spec for
- Python goes into sitearch, not sitelib

* Wed Jun 08 2011 Paul Wouters <paul@xelerance.com> - 1.6.10-1
- Updated to 1.6.10
- commented out build dependancies for svn snapshots

* Sun Mar 27 2011 Paul Wouters <paul@xelerance.com> - 1.6.9-1
- Updated to 1.6.9

* Mon Jan 24 2011 Paul Wouters <paul@xelerance.com> - 1.6.8-1
- Updated to 1.6.8

* Thu Aug 26 2010 Paul Wouters <paul@xelerance.com> - 1.6.6-1
- Upgraded to 1.6.6

* Mon Apr 26 2010 Paul Wouters <paul@xelerance.com> - 1.6.4-4
- Disable a debug line that was added to find the LOC issue that causes
  unexpected output for automated tools using ldns-read-zone

* Thu Feb 11 2010 Paul Wouters <paul@xelerance.com> - 1.6.4-3
- Applied fix svn 3186 for LOC record parsing

* Fri Jan 22 2010 Paul Wouters <paul@xelerance.com> - 1.6.4-2
- libtool on EL-5 does not take --install as argument

* Fri Jan 22 2010 Paul Wouters <paul@xelerance.com> - 1.6.4-1
- Upgraded to 1.6.4
- Added ldns-python sub package
- Patch for installing ldns-python files
- Patch for rpath in ldns-python

* Sun Aug 16 2009 Paul Wouters <paul@xelerance.com> - 1.6.1-2
- Bump version, sources file was not updated.

* Sun Aug 16 2009 Paul Wouters <paul@xelerance.com> - 1.6.1-1
-Updated to 1.6.1

* Sat Jul 11 2009 Paul Wouters <paul@xelerance.com> - 1.6.0-1
- Updated to 1.6.0

* Thu Apr 16 2009 Paul Wouters <paul@xelerance.com> - 1.5.1-2
- Memory management bug when generating a sha256 key, see:
  https://bugzilla.redhat.com/show_bug.cgi?id=493953

* Fri Feb 13 2009 Paul Wouters <paul@xelerance.com> - 1.5.1-1
- Upgrade to 1.5.1 (1.5.0 was a dud release)

* Sun Nov  9 2008 Paul Wouters <paul@xelerance.com> - 1.4.0-2
- libldns.so was missing in files section.

* Sun Nov  9 2008 Paul Wouters <paul@xelerance.com> - 1.4.0-1
- Updated to 1.4.0
- enable SHA2 functionality

* Mon Jun 30 2008 Paul Wouters <paul@xelerance.com> - 1.3.0-1
- Updated to latest release

* Thu Nov 29 2007 Paul Wouters <paul@xelerance.com> - 1.2.2-1
- Upgraded to 1.2.2.

* Mon Sep 11 2006 Paul Wouters <paul@xelerance.com> 1.0.1-4
- Commented out 1.1.0 make targets, put make 1.0.1 targets.

* Mon Sep 11 2006 Paul Wouters <paul@xelerance.com> 1.0.1-3
- Fixed changelog typo in date
- Rebuild requested for PT_GNU_HASH support from gcc
- Did not upgrade to 1.1.0 due to compile issues on x86_64

* Fri Jan  6 2006 Paul Wouters <paul@xelerance.com> 1.0.1-1
- Upgraded to 1.0.1. Removed temporary clean hack from spec file.

* Sun Dec 18 2005 Paul Wouters <paul@xelerance.com> 1.0.0-8
- Cannot use make clean because there are no Makefiles. Use hardcoded rm.

* Sun Dec 18 2005 Paul Wouters <paul@xelerance.com> 1.0.0-7
- Patched 'make clean' target to get rid of object files shipped with 1.0.0

* Sun Dec 13 2005 Paul Wouters <paul@xelerance.com> 1.0.0-6
- added a make clean for 2.3.3 since .o files were left behind upstream,
  causing failure on ppc platform

* Sun Dec 11 2005 Tom "spot" Callaway <tcallawa@redhat.com> 1.0.0-5
- minor cleanups

* Wed Oct  5 2005 Paul Wouters <paul@xelerance.com> 0.70_1205
- reworked for svn version

* Sun Sep 25 2005 Paul Wouters <paul@xelerance.com> - 0.70
- Initial version
