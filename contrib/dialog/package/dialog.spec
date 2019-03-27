Summary: dialog - display dialog boxes from shell scripts
%define AppProgram dialog
%define AppVersion 1.3
%define AppRelease 20180621
%define ActualProg c%{AppProgram}
# $XTermId: dialog.spec,v 1.108 2018/06/21 09:19:45 tom Exp $
Name: %{ActualProg}
Version: %{AppVersion}
Release: %{AppRelease}
License: LGPL
Group: Applications/System
URL: ftp://ftp.invisible-island.net/%{AppProgram}
Source0: %{AppProgram}-%{AppVersion}-%{AppRelease}.tgz
Packager: Thomas Dickey <dickey@invisible-island.net>

%description
Dialog is a program that will let you present a variety of questions or
display messages using dialog boxes from a shell script.  These types
of dialog boxes are implemented (though not all are necessarily compiled
into dialog):

     buildlist, calendar, checklist, dselect, editbox, form, fselect,
     gauge, infobox, inputbox, inputmenu, menu, mixedform,
     mixedgauge, msgbox (message), passwordbox, passwordform, pause,
     prgbox, programbox, progressbox, radiolist, rangebox, tailbox,
     tailboxbg, textbox, timebox, treeview, and yesno (yes/no).

This package installs as "cdialog" to avoid conflict with other packages.
%prep

%define debug_package %{nil}

%setup -q -n %{AppProgram}-%{AppVersion}-%{AppRelease}

%build

cp -v package/dialog.map package/%{ActualProg}.map

INSTALL_PROGRAM='${INSTALL}' \
%configure \
        --target %{_target_platform} \
        --prefix=%{_prefix} \
        --bindir=%{_bindir} \
        --libdir=%{_libdir} \
        --mandir=%{_mandir} \
        --with-package=%{ActualProg} \
        --enable-header-subdir \
        --enable-nls \
        --enable-widec \
        --with-shared \
        --with-ncursesw \
        --with-versioned-syms \
        --disable-rpath-hack

make

%install
[ "$RPM_BUILD_ROOT" != "/" ] && rm -rf $RPM_BUILD_ROOT

make install                    DESTDIR=$RPM_BUILD_ROOT
make install-full               DESTDIR=$RPM_BUILD_ROOT

strip $RPM_BUILD_ROOT%{_bindir}/%{ActualProg}
chmod 755 $RPM_BUILD_ROOT%{_libdir}/lib%{ActualProg}.so.*

%clean
[ "$RPM_BUILD_ROOT" != "/" ] && rm -rf $RPM_BUILD_ROOT

%files
%defattr(-,root,root)
%{_bindir}/%{ActualProg}
%{_bindir}/%{ActualProg}-config
%{_mandir}/man1/%{ActualProg}.*
%{_mandir}/man3/%{ActualProg}.*
%{_includedir}/%{ActualProg}.h
%{_includedir}/%{ActualProg}/dlg_colors.h
%{_includedir}/%{ActualProg}/dlg_config.h
%{_includedir}/%{ActualProg}/dlg_keys.h
%{_libdir}/lib%{ActualProg}.*
%{_datadir}/locale/*/LC_MESSAGES/%{ActualProg}.mo 

%changelog
# each patch should add its ChangeLog entries here

* Sat Dec 09 2017 Thomas Dickey
- update ftp url

* Thu Apr 21 2016 Thomas Dickey
- remove stray call to libtool

* Tue Oct 18 2011 Thomas Dickey
- add executable permissions for shared libraries, discard ".la" file.

* Thu Dec 30 2010 Thomas Dickey
- initial version
