#!/bin/sh
#
# buildbff.sh: Create AIX SMIT-installable OpenSSH packages
#
# Author: Darren Tucker (dtucker at zip dot com dot au)
# This file is placed in the public domain and comes with absolutely
# no warranty.
#
# Based originally on Ben Lindstrom's buildpkg.sh for Solaris
#

#
# Tunable configuration settings
# 	create a "config.local" in your build directory or set
#	environment variables to override these.
#
[ -z "$PERMIT_ROOT_LOGIN" ] && PERMIT_ROOT_LOGIN=no
[ -z "$X11_FORWARDING" ] && X11_FORWARDING=no
[ -z "$AIX_SRC" ] && AIX_SRC=no

umask 022

startdir=`pwd`

perl -v >/dev/null || (echo perl required; exit 1)

# Path to inventory.sh: same place as buildbff.sh
if  echo $0 | egrep '^/'
then
	inventory=`dirname $0`/inventory.sh		# absolute path
else
	inventory=`pwd`/`dirname $0`/inventory.sh	# relative path
fi

#
# We still support running from contrib/aix, but this is deprecated
#
if pwd | egrep 'contrib/aix$'
then
	echo "Changing directory to `pwd`/../.."
	echo "Please run buildbff.sh from your build directory in future."
	cd ../..
	contribaix=1
fi

if [ ! -f Makefile ]
then
	echo "Makefile not found (did you run configure?)"
	exit 1
fi

#
# Directories used during build:
# current dir = $objdir		directory you ran ./configure in.
# $objdir/$PKGDIR/ 		directory package files are constructed in
# $objdir/$PKGDIR/root/		package root ($FAKE_ROOT)
#
objdir=`pwd`
PKGNAME=openssh
PKGDIR=package

#
# Collect local configuration settings to override defaults
#
if [ -s ./config.local ]
then
	echo Reading local settings from config.local
	. ./config.local
fi

#
# Fill in some details from Makefile, like prefix and sysconfdir
#	the eval also expands variables like sysconfdir=${prefix}/etc
#	provided they are eval'ed in the correct order
#
for confvar in prefix exec_prefix bindir sbindir libexecdir datadir mandir mansubdir sysconfdir piddir srcdir
do
	eval $confvar=`grep "^$confvar=" $objdir/Makefile | cut -d = -f 2`
done

#
# Collect values of privsep user and privsep path
#	currently only found in config.h
#
for confvar in SSH_PRIVSEP_USER PRIVSEP_PATH
do
	eval $confvar=`awk '/#define[ \t]'$confvar'/{print $3}' $objdir/config.h`
done

# Set privsep defaults if not defined
if [ -z "$SSH_PRIVSEP_USER" ]
then
	SSH_PRIVSEP_USER=sshd
fi
if [ -z "$PRIVSEP_PATH" ]
then
	PRIVSEP_PATH=/var/empty
fi

# Clean package build directory
rm -rf $objdir/$PKGDIR
FAKE_ROOT=$objdir/$PKGDIR/root
mkdir -p $FAKE_ROOT

# Start by faking root install
echo "Faking root install..."
cd $objdir
make install-nokeys DESTDIR=$FAKE_ROOT

if [ $? -gt 0 ]
then
	echo "Fake root install failed, stopping."
	exit 1
fi

#
# Copy informational files to include in package
#
cp $srcdir/LICENCE $objdir/$PKGDIR/
cp $srcdir/README* $objdir/$PKGDIR/

#
# Extract common info requires for the 'info' part of the package.
#	AIX requires 4-part version numbers
#
VERSION=`./ssh -V 2>&1 | cut -f 1 -d , | cut -f 2 -d _`
MAJOR=`echo $VERSION | cut -f 1 -d p | cut -f 1 -d .`
MINOR=`echo $VERSION | cut -f 1 -d p | cut -f 2 -d .`
PATCH=`echo $VERSION | cut -f 1 -d p | cut -f 3 -d .`
PORTABLE=`echo $VERSION | awk 'BEGIN{FS="p"}{print $2}'`
[ "$PATCH" = "" ] && PATCH=0
[ "$PORTABLE" = "" ] && PORTABLE=0
BFFVERSION=`printf "%d.%d.%d.%d" $MAJOR $MINOR $PATCH $PORTABLE`

echo "Building BFF for $PKGNAME $VERSION (package version $BFFVERSION)"

#
# Set ssh and sshd parameters as per config.local
#
if [ "${PERMIT_ROOT_LOGIN}" = no ]
then
	perl -p -i -e "s/#PermitRootLogin yes/PermitRootLogin no/" \
		$FAKE_ROOT/${sysconfdir}/sshd_config
fi
if [ "${X11_FORWARDING}" = yes ]
then
	perl -p -i -e "s/#X11Forwarding no/X11Forwarding yes/" \
		$FAKE_ROOT/${sysconfdir}/sshd_config
fi


# Rename config files; postinstall script will copy them if necessary
for cfgfile in ssh_config sshd_config
do
	mv $FAKE_ROOT/$sysconfdir/$cfgfile $FAKE_ROOT/$sysconfdir/$cfgfile.default
done

#
# Generate lpp control files.
#	working dir is $FAKE_ROOT but files are generated in dir above
#	and moved into place just before creation of .bff
#
cd $FAKE_ROOT
echo Generating LPP control files
find . ! -name . -print >../openssh.al
$inventory >../openssh.inventory

cat <<EOD >../openssh.copyright
This software is distributed under a BSD-style license.
For the full text of the license, see /usr/lpp/openssh/LICENCE
EOD

#
# openssh.size file allows filesystem expansion as required
# generate list of directories containing files
# then calculate disk usage for each directory and store in openssh.size
#
files=`find . -type f -print`
dirs=`for file in $files; do dirname $file; done | sort -u`
for dir in $dirs
do
	du $dir
done > ../openssh.size

#
# Create postinstall script
#
cat <<EOF >>../openssh.post_i
#!/bin/sh

echo Creating configs from defaults if necessary.
for cfgfile in ssh_config sshd_config
do
	if [ ! -f $sysconfdir/\$cfgfile ]
	then
		echo "Creating \$cfgfile from default"
		cp $sysconfdir/\$cfgfile.default $sysconfdir/\$cfgfile
	else
		echo "\$cfgfile already exists."
	fi
done
echo

# Create PrivilegeSeparation user and group if not present
echo Checking for PrivilegeSeparation user and group.
if cut -f1 -d: /etc/group | egrep '^'$SSH_PRIVSEP_USER'\$' >/dev/null
then
	echo "PrivSep group $SSH_PRIVSEP_USER already exists."
else
	echo "Creating PrivSep group $SSH_PRIVSEP_USER."
	mkgroup -A $SSH_PRIVSEP_USER
fi

# Create user if required
if lsuser "$SSH_PRIVSEP_USER" >/dev/null
then
	echo "PrivSep user $SSH_PRIVSEP_USER already exists."
else
	echo "Creating PrivSep user $SSH_PRIVSEP_USER."
	mkuser gecos='SSHD PrivSep User' login=false rlogin=false account_locked=true pgrp=$SSH_PRIVSEP_USER $SSH_PRIVSEP_USER
fi

if egrep '^[ \t]*UsePrivilegeSeparation[ \t]+no' $sysconfdir/sshd_config >/dev/null
then
	echo UsePrivilegeSeparation not enabled, privsep directory not required.
else
	# create chroot directory if required
	if [ -d $PRIVSEP_PATH ]
	then
		echo "PrivSep chroot directory $PRIVSEP_PATH already exists."
	else
		echo "Creating PrivSep chroot directory $PRIVSEP_PATH."
		mkdir $PRIVSEP_PATH
		chown 0 $PRIVSEP_PATH
		chgrp 0 $PRIVSEP_PATH
		chmod 755 $PRIVSEP_PATH
	fi
fi
echo

# Generate keys unless they already exist
echo Creating host keys if required.
$bindir/ssh-keygen -A
echo

# Set startup command depending on SRC support
if [ "$AIX_SRC" = "yes" ]
then
	echo Creating SRC sshd subsystem.
	rmssys -s sshd 2>&1 >/dev/null
	mkssys -s sshd -p "$sbindir/sshd" -a '-D' -u 0 -S -n 15 -f 9 -R -G tcpip
	startupcmd="start $sbindir/sshd \\\"\\\$src_running\\\""
	oldstartcmd="$sbindir/sshd"
else
	startupcmd="$sbindir/sshd"
	oldstartcmd="start $sbindir/sshd \\\"$src_running\\\""
fi

# If migrating to or from SRC, change previous startup command
# otherwise add to rc.tcpip
if egrep "^\$oldstartcmd" /etc/rc.tcpip >/dev/null
then
	if sed "s|^\$oldstartcmd|\$startupcmd|g" /etc/rc.tcpip >/etc/rc.tcpip.new
	then
		chmod 0755 /etc/rc.tcpip.new
		mv /etc/rc.tcpip /etc/rc.tcpip.old && \
		mv /etc/rc.tcpip.new /etc/rc.tcpip
	else
		echo "Updating /etc/rc.tcpip failed, please check."
	fi
else
	# Add to system startup if required
	if grep "^\$startupcmd" /etc/rc.tcpip >/dev/null
	then
		echo "sshd found in rc.tcpip, not adding."
	else
		echo "Adding sshd to rc.tcpip"
		echo >>/etc/rc.tcpip
		echo "# Start sshd" >>/etc/rc.tcpip
		echo "\$startupcmd" >>/etc/rc.tcpip
	fi
fi
EOF

#
# Create liblpp.a and move control files into it
#
echo Creating liblpp.a
(
	cd ..
	for i in openssh.al openssh.copyright openssh.inventory openssh.post_i openssh.size LICENCE README*
	do
		ar -r liblpp.a $i
		rm $i
	done
)

#
# Create lpp_name
#
# This will end up looking something like:
# 4 R I OpenSSH {
# OpenSSH 3.0.2.1 1 N U en_US OpenSSH 3.0.2p1 Portable for AIX
# [
# %
# /usr/local/bin 8073
# /usr/local/etc 189
# /usr/local/libexec 185
# /usr/local/man/man1 145
# /usr/local/man/man8 83
# /usr/local/sbin 2105
# /usr/local/share 3
# %
# ]
# }

echo Creating lpp_name
cat <<EOF >../lpp_name
4 R I $PKGNAME {
$PKGNAME $BFFVERSION 1 N U en_US OpenSSH $VERSION Portable for AIX
[
%
EOF

for i in $bindir $sysconfdir $libexecdir $mandir/${mansubdir}1 $mandir/${mansubdir}8 $sbindir $datadir /usr/lpp/openssh
do
	# get size in 512 byte blocks
	if [ -d $FAKE_ROOT/$i ]
	then
		size=`du $FAKE_ROOT/$i | awk '{print $1}'`
		echo "$i $size" >>../lpp_name
	fi
done

echo '%' >>../lpp_name
echo ']' >>../lpp_name
echo '}' >>../lpp_name

#
# Move pieces into place
#
mkdir -p usr/lpp/openssh
mv ../liblpp.a usr/lpp/openssh
mv ../lpp_name .

#
# Now invoke backup to create .bff file
#	note: lpp_name needs to be the first file so we generate the
#	file list on the fly and feed it to backup using -i
#
echo Creating $PKGNAME-$VERSION.bff with backup...
rm -f $PKGNAME-$VERSION.bff
(
	echo "./lpp_name"
	find . ! -name lpp_name -a ! -name . -print
) | backup  -i -q -f ../$PKGNAME-$VERSION.bff $filelist

#
# Move package into final location and clean up
#
mv ../$PKGNAME-$VERSION.bff $startdir
cd $startdir
rm -rf $objdir/$PKGDIR

echo $0: done.

