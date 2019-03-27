# RCSid:
#	$Id: host-target.mk,v 1.11 2015/10/25 00:07:20 sjg Exp $

# Host platform information; may be overridden
.if !defined(_HOST_OSNAME)
_HOST_OSNAME !=	uname -s
.export _HOST_OSNAME
.endif
.if !defined(_HOST_OSREL)
_HOST_OSREL  !=	uname -r
.export _HOST_OSREL
.endif
.if !defined(_HOST_MACHINE)
_HOST_MACHINE != uname -m
.export _HOST_MACHINE
.endif
.if !defined(_HOST_ARCH)
# for NetBSD prefer $MACHINE (amd64 rather than x86_64)
.if ${_HOST_OSNAME:NNetBSD} == ""
_HOST_ARCH := ${_HOST_MACHINE}
.else
_HOST_ARCH != uname -p 2> /dev/null || uname -m
# uname -p may produce garbage on linux
.if ${_HOST_ARCH:[\#]} > 1 || ${_HOST_ARCH:Nunknown} == ""
_HOST_ARCH := ${_HOST_MACHINE}
.endif
.endif
.export _HOST_ARCH
.endif
.if !defined(HOST_MACHINE)
HOST_MACHINE := ${_HOST_MACHINE}
.export HOST_MACHINE
.endif

HOST_OSMAJOR := ${_HOST_OSREL:C/[^0-9].*//}
HOST_OSTYPE  :=	${_HOST_OSNAME:S,/,,g}-${_HOST_OSREL:C/\([^\)]*\)//}-${_HOST_ARCH}
HOST_OS      :=	${_HOST_OSNAME}
host_os      :=	${_HOST_OSNAME:tl}
HOST_TARGET  := ${host_os:S,/,,g}${HOST_OSMAJOR}-${_HOST_ARCH}

# tr is insanely non-portable, accommodate the lowest common denominator
TR ?= tr
toLower = ${TR} 'ABCDEFGHIJKLMNOPQRSTUVWXYZ' 'abcdefghijklmnopqrstuvwxyz'
toUpper = ${TR} 'abcdefghijklmnopqrstuvwxyz' 'ABCDEFGHIJKLMNOPQRSTUVWXYZ'
