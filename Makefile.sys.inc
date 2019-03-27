# $FreeBSD$
#
# This is included very early from share/mk/src.sys.env.mk, after
# /etc/src-env.conf but before /etc/make.conf, /etc/src.conf, or OBJDIR
# handling.
# - It is not safe to use .OBJDIR/OBJTOP/OBJROOT here.
# - __ENV_ONLY_OPTIONS have been parsed by now except for opporutunistic
#   MK_AUTO_OBJ.
#

.if ${MK_DIRDEPS_BUILD} == "no"
# For AUTO_OBJ many targets do not need object directories created at top-level
# for each visited directory.  Only when things are being built are they
# needed.  Having AUTO_OBJ disabled in a build target is fine as it should
# fallback to running 'make obj' as needed.  If a target is not in this list
# then it is ran with MK_AUTO_OBJ=no in environment.
# 'showconfig' is in the list to avoid forcing MK_AUTO_OBJ=no for it.
AUTO_OBJ_TGT_WHITELIST+= \
	_* all all-man build* depend everything *toolchain* includes \
	libraries obj objlink showconfig tags xdev xdev-build native-xtools \
	stage* create-packages* real-packages sign-packages package-pkg \
	tinderbox universe* kernel kernels world worlds bmake

# Only allow AUTO_OBJ for the whitelisted targets.  See AUTO_OBJ_TGT_WHITELIST
# above.  MK_AUTO_OBJ not checked here for "yes" as it may not yet be enabled
# since it is opportunistic.
.if empty(.MAKEOVERRIDES:MMK_AUTO_OBJ)
.for _tgt in ${AUTO_OBJ_TGT_WHITELIST}
.if make(${_tgt})
_CAN_USE_AUTO_OBJ?= yes
.endif
.endfor
.if !defined(_CAN_USE_AUTO_OBJ)
_MAKEARGS+=		MK_AUTO_OBJ=no
MK_AUTO_OBJ=		no
# This will prevent src.sys.obj.mk from opportunistically enabling AUTO_OBJ
# in this make execution and for sub-makes.  For all of these targets we
# just want to read any existing OBJDIR but we don't care if we can create
# or write to them.
.MAKEOVERRIDES+=	MK_AUTO_OBJ
.else
# For top-level we always assume the MAKEOBJDIRPREFIX is writable rather than
# falling back to .CURDIR.
__objdir_writable= yes
.endif
.endif	# empty(.MAKEOVERRIDES:MMK_AUTO_OBJ)

.endif	# ${MK_DIRDEPS_BUILD} == "no"
