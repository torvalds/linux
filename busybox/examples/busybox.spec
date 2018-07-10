Summary: Statically linked binary providing simplified versions of system commands
Name: busybox
Version: 1.15.1
Release: 1%{?dist}
Epoch: 1
License: GPLv2
Group: System Environment/Shells
Source: http://www.busybox.net/downloads/%{name}-%{version}.tar.bz2
Source1: busybox-static.config
Source2: busybox-petitboot.config
Source3: http://www.uclibc.org/downloads/uClibc-0.9.30.1.tar.bz2
Source4: uClibc.config
Patch16: busybox-1.10.1-hwclock.patch
# patch to avoid conflicts with getline() from stdio.h, already present in upstream VCS
Patch22: uClibc-0.9.30.1-getline.patch
Obsoletes: busybox-anaconda
URL: http://www.busybox.net
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)
BuildRequires: libselinux-devel >= 1.27.7-2
BuildRequires: libsepol-devel
BuildRequires: libselinux-static
BuildRequires: libsepol-static
BuildRequires: glibc-static

%define debug_package %{nil}

%package petitboot
Group: System Environment/Shells
Summary: Version of busybox configured for use with petitboot

%description
Busybox is a single binary which includes versions of a large number
of system commands, including a shell.  This package can be very
useful for recovering from certain types of system failures,
particularly those involving broken shared libraries.

%description petitboot
Busybox is a single binary which includes versions of a large number
of system commands, including a shell.  The version contained in this
package is a minimal configuration intended for use with the Petitboot
bootloader used on PlayStation 3. The busybox package provides a binary
better suited to normal use.

%prep
%setup -q -a3
%patch16 -b .ia64 -p1
cat %{SOURCE4} >uClibc-0.9.30.1/.config1
%patch22 -b .getline -p1

%build
# create static busybox - the executable is kept as busybox-static
# We use uclibc instead of system glibc, uclibc is several times
# smaller, this is important for static build.
# Build uclibc first.
cd uClibc-0.9.30.1
# fixme:
mkdir kernel-include
cp -a /usr/include/asm kernel-include
cp -a /usr/include/asm-generic kernel-include
cp -a /usr/include/linux kernel-include
# uclibc can't be built on ppc64,s390,ia64, we set $arch to "" in this case
arch=`uname -m | sed -e 's/i.86/i386/' -e 's/ppc/powerpc/' -e 's/ppc64//' -e 's/powerpc64//' -e 's/ia64//' -e 's/s390.*//'`
echo "TARGET_$arch=y" >.config
echo "TARGET_ARCH=\"$arch\"" >>.config
cat .config1 >>.config
if test "$arch"; then yes "" | make oldconfig; fi
if test "$arch"; then cat .config; fi
if test "$arch"; then make V=1; fi
if test "$arch"; then make install; fi
if test "$arch"; then make install_kernel_headers; fi
cd ..
# we are back in busybox-NN.MM dir now
cp %{SOURCE1} .config
# set all new options to defaults
yes "" | make oldconfig
# gcc needs to be convinced to use neither system headers, nor libs,
# nor startfiles (i.e. crtXXX.o files)
if test "$arch"; then \
    mv .config .config1 && \
    grep -v ^CONFIG_SELINUX .config1 >.config && \
    yes "" | make oldconfig && \
    cat .config && \
    make V=1 \
        EXTRA_CFLAGS="-isystem uClibc-0.9.30.1/installed/include" \
        CFLAGS_busybox="-static -nostartfiles -LuClibc-0.9.30.1/installed/lib uClibc-0.9.30.1/installed/lib/crt1.o uClibc-0.9.30.1/installed/lib/crti.o uClibc-0.9.30.1/installed/lib/crtn.o"; \
else \
    cat .config && \
    make V=1 CC="gcc $RPM_OPT_FLAGS"; \
fi
cp busybox busybox.static

# create busybox optimized for petitboot
make clean
# copy new configuration file
cp %{SOURCE2} .config
# set all new options to defaults
yes "" | make oldconfig
make V=1 CC="%__cc $RPM_OPT_FLAGS"
cp busybox busybox.petitboot

%install
rm -rf $RPM_BUILD_ROOT
mkdir -p $RPM_BUILD_ROOT/sbin
install -m 755 busybox.static $RPM_BUILD_ROOT/sbin/busybox
install -m 755 busybox.petitboot $RPM_BUILD_ROOT/sbin/busybox.petitboot

%clean
rm -rf $RPM_BUILD_ROOT

%files
%defattr(-,root,root,-)
%doc LICENSE docs/busybox.net/*.html
/sbin/busybox

%files petitboot
%defattr(-,root,root,-)
%doc LICENSE
/sbin/busybox.petitboot

%changelog
