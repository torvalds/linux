
%define apuver 1

Summary: Apache Portable Runtime Utility library
Name: apr-util
Version: 1.5.4
Release: 1
License: Apache Software License
Group: System Environment/Libraries
URL: http://apr.apache.org/
Source0: http://www.apache.org/dist/apr/%{name}-%{version}.tar.bz2
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-buildroot
BuildRequires: autoconf, libtool, doxygen, apr-devel >= 1.4.0
BuildRequires: expat-devel, libuuid-devel

%description
The mission of the Apache Portable Runtime (APR) is to provide a
free library of C data structures and routines.  This library
contains additional utility interfaces for APR; including support
for XML, LDAP, database interfaces, URI parsing and more.

%package devel
Group: Development/Libraries
Summary: APR utility library development kit
Requires: apr-util = %{version}-%{release}, apr-devel
Requires: db4-devel, expat-devel

%description devel
This package provides the support files which can be used to 
build applications using the APR utility library.  The mission 
of the Apache Portable Runtime (APR) is to provide a free 
library of C data structures and routines.

%package dbm
Group: Development/Libraries 
Summary: APR utility library DBM driver
BuildRequires: db4-devel
Requires: apr-util = %{version}-%{release}

%description dbm
This package provides the DBM driver for the apr-util.

%package pgsql
Group: Development/Libraries
Summary: APR utility library PostgreSQL DBD driver
BuildRequires: postgresql-devel
Requires: apr-util = %{version}-%{release}

%description pgsql
This package provides the PostgreSQL driver for the apr-util
DBD (database abstraction) interface.

%package mysql
Group: Development/Libraries
Summary: APR utility library MySQL DBD driver
BuildRequires: mysql-devel
Requires: apr-util = %{version}-%{release}

%description mysql
This package provides the MySQL driver for the apr-util DBD
(database abstraction) interface.

%package sqlite
Group: Development/Libraries
Summary: APR utility library SQLite DBD driver
BuildRequires: sqlite-devel >= 3.0.0
Requires: apr-util = %{version}-%{release}

%description sqlite
This package provides the SQLite driver for the apr-util DBD
(database abstraction) interface.

%package freetds
Group: Development/Libraries
Summary: APR utility library FreeTDS DBD driver
BuildRequires: freetds-devel
Requires: apr-util = %{version}-%{release}

%description freetds
This package provides the FreeTDS driver for the apr-util DBD
(database abstraction) interface.

%package odbc
Group: Development/Libraries
Summary: APR utility library ODBC DBD driver
BuildRequires: unixODBC-devel
Requires: apr-util = %{version}-%{release}

%description odbc
This package provides the ODBC driver for the apr-util DBD
(database abstraction) interface.

%package ldap
Group: Development/Libraries
Summary: APR utility library LDAP support
BuildRequires: openldap-devel
Requires: apr-util = %{version}-%{release}

%description ldap
This package provides the LDAP support for the apr-util.

%package openssl
Group: Development/Libraries
Summary: APR utility library OpenSSL crypto support
BuildRequires: openssl-devel
Requires: apr-util = %{version}-%{release}

%description openssl
This package provides crypto support for apr-util based on OpenSSL.

%package nss
Group: Development/Libraries
Summary: APR utility library NSS crypto support
BuildRequires: nss-devel
Requires: apr-util = %{version}-%{release}

%description nss
This package provides crypto support for apr-util based on Mozilla NSS.

%prep
%setup -q

%build
%configure --with-apr=%{_prefix} \
        --includedir=%{_includedir}/apr-%{apuver} \
        --with-ldap --without-gdbm \
        --with-sqlite3 --with-pgsql --with-mysql --with-freetds --with-odbc \
        --with-berkeley-db \
        --with-crypto --with-openssl --with-nss \
        --without-sqlite2
make %{?_smp_mflags} && make dox

%check
# Run non-interactive tests
pushd test
make %{?_smp_mflags} all CFLAGS=-fno-strict-aliasing
make check || exit 1
popd

%install
rm -rf $RPM_BUILD_ROOT
make install DESTDIR=$RPM_BUILD_ROOT

# Documentation
mv docs/dox/html html

# Unpackaged files
rm -f $RPM_BUILD_ROOT%{_libdir}/aprutil.exp

%clean
rm -rf $RPM_BUILD_ROOT

%post -p /sbin/ldconfig

%postun -p /sbin/ldconfig

%files
%defattr(-,root,root,-)
%doc CHANGES LICENSE NOTICE
%{_libdir}/libaprutil-%{apuver}.so.*
%dir %{_libdir}/apr-util-%{apuver}

%files dbm
%defattr(-,root,root,-)
%{_libdir}/apr-util-%{apuver}/apr_dbm_db*

%files pgsql
%defattr(-,root,root,-)
%{_libdir}/apr-util-%{apuver}/apr_dbd_pgsql*

%files mysql
%defattr(-,root,root,-)
%{_libdir}/apr-util-%{apuver}/apr_dbd_mysql*

%files sqlite
%defattr(-,root,root,-)
%{_libdir}/apr-util-%{apuver}/apr_dbd_sqlite*

%files freetds
%defattr(-,root,root,-)
%{_libdir}/apr-util-%{apuver}/apr_dbd_freetds*

%files odbc
%defattr(-,root,root,-)
%{_libdir}/apr-util-%{apuver}/apr_dbd_odbc*

%files ldap
%defattr(-,root,root,-)
%{_libdir}/apr-util-%{apuver}/apr_ldap*

%files openssl
%defattr(-,root,root,-)
%{_libdir}/apr-util-%{apuver}/apr_crypto_openssl*

%files nss
%defattr(-,root,root,-)
%{_libdir}/apr-util-%{apuver}/apr_crypto_nss*

%files devel
%defattr(-,root,root,-)
%{_bindir}/apu-%{apuver}-config
%{_libdir}/libaprutil-%{apuver}.*a
%{_libdir}/libaprutil-%{apuver}.so
%{_libdir}/pkgconfig/apr-util-%{apuver}.pc
%{_includedir}/apr-%{apuver}/*.h
%doc --parents html

%changelog
* Tue Jun 22 2004 Graham Leggett <minfrin@sharp.fm> 1.0.0-1
- update to support v1.0.0 of APR
                                                                                
* Tue Jun 22 2004 Graham Leggett <minfrin@sharp.fm> 1.0.0-1
- derived from Fedora Core apr.spec

