# SMBFS build configuration
# copy this file to config.mk and edit as needed.
# If you want to disable an option just comment it with '#' char.
#
# $Id: config.mk.in,v 1.16 2001/04/16 04:34:26 bp Exp $

# Where your kernel source tree located (/usr/src/sys for example)
SYSDIR=/usr/src/sys

# Where the kernel module gets installed
KMODDIR=/modules

# Where all files get installed
PREFIX?=/usr/local

# Build shared smb library, or link all executables statically
USE_SHAREDLIBS=no

# Comment this to disable support for encrypted passwords (requires
# src/sys/crypto directory). By default, NT and Win* machines use encrypted
# passwords.
ENCRYPTED_PASSWD=yes

# Uncomment this option if kernel compiled with SMP suppport.
# SMP_SUPPORT=

# This turns on debug logging, be careful - it produces a lot of kernel
# messages.
#KDEBUG+= -DSMB_SOCKET_DEBUG
#KDEBUG+= -DSMB_SOCKETDATA_DEBUG
#KDEBUG+= -DSMB_VNODE_DEBUG

# Compile binaries with debugging symbols
#SMBGDB=yes

# Build kernel module (don't touch that)
SINGLEKLD=yes
