# $Id: manifest.mk,v 1.2 2014/10/31 18:06:17 sjg Exp $
#
#	@(#) Copyright (c) 2014, Simon J. Gerraty
#
#	This file is provided in the hope that it will
#	be of use.  There is absolutely NO WARRANTY.
#	Permission to copy, redistribute or otherwise
#	use this file is hereby granted provided that 
#	the above copyright notice and this notice are
#	left intact. 
#      
#	Please send copies of changes and bug-fixes to:
#	sjg@crufty.net
#

# generate mtree style manifest supported by makefs in FreeBSD

# input looks like
# MANIFEST= my.mtree
# for each MANIFEST we have a list of dirs
# ${MANIFEST}.DIRS += bin sbin usr/bin ...
# for each dir we have a ${MANIFEST}.SRCS.$dir
# that provides the absolute path to the contents
# ${MANIFEST}.SRCS.bin += ${OBJTOP}/bin/sh/sh 
# ${MANIFEST}.SYMLINKS is a list of src target pairs
# for each file/dir there are a number of attributes
# UID GID MODE FLAGS
# which can be set per dir, per file or we use defaults
# eg.  
# MODE.sbin = 550
# MODE.usr/sbin = 550
# MODE.dirs = 555
# means that sbin and usr/sbin get 550 all other dirs get 555
# MODE.usr/bin/passwd = 4555
# MODE.usr/bin.files = 555
# MODE.usr/sbin.files = 500
# means passwd gets 4555 other files in usr/bin get 555 and
# files in usr/sbin get 500
# STORE defaults to basename of src and target directory
# but we can use 
# ${MANIFEST}.SRCS.sbin += ${OBJTOP}/bin/sh-static/sh-static
# STORE.sbin/sh-static = sbin/sh
#
# the above is a little overkill but means we can easily adapt to
# different formats

UID.dirs ?= 0
GID.dirs ?= 0
MODE.dirs ?= 775
FLAGS.dirs ?= 

UID.files ?= 0
GID.files ?= 0
MODE.files ?= 555

# a is attribute name d is dirname
M_DIR_ATTR = L:@a@$${$$a.$$d:U$${$$a.dirs}}@
# as above and s is set to the name we store f as
M_FILE_ATTR = L:@a@$${$$a.$$s:U$${$$a.$$d.files:U$${$$a.files}}}@

# this produces the body of the manifest
# there should typically be a header prefixed
_GEN_MTREE_MANIFEST_USE: .USE
	@(${${.TARGET}.DIRS:O:u:@d@echo '$d type=dir uid=${UID:${M_DIR_ATTR}} gid=${GID:${M_DIR_ATTR}} mode=${MODE:${M_DIR_ATTR}} ${FLAGS:${M_DIR_ATTR}}';@} \
	${${.TARGET}.DIRS:O:u:@d@${${.TARGET}.SRCS.$d:O:u:@f@echo '${s::=${STORE.$d/${f:T}:U$d/${f:T}}}$s contents="$f" type=file uid=${UID:${M_FILE_ATTR}} gid=${GID:${M_FILE_ATTR}} mode=${MODE:${M_FILE_ATTR}} ${FLAGS:${M_FILE_ATTR}}';@}@} \
	set ${${.TARGET}.SYMLINKS}; while test $$# -ge 2; do echo "$$2 type=link link=$$1"; shift 2; done) > ${.TARGET}
