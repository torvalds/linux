#
# Checker for paravirtualizations of privileged operations.
#
s/ssm.*psr\.ic.*/.warning \"ssm psr.ic should not be used directly\"/g
s/rsm.*psr\.ic.*/.warning \"rsm psr.ic should not be used directly\"/g
s/ssm.*psr\.i.*/.warning \"ssm psr.i should not be used directly\"/g
s/rsm.*psr\.i.*/.warning \"rsm psr.i should not be used directly\"/g
s/ssm.*psr\.dt.*/.warning \"ssm psr.dt should not be used directly\"/g
s/rsm.*psr\.dt.*/.warning \"rsm psr.dt should not be used directly\"/g
s/mov.*=.*cr\.ifa/.warning \"cr.ifa should not used directly\"/g
s/mov.*=.*cr\.itir/.warning \"cr.itir should not used directly\"/g
s/mov.*=.*cr\.isr/.warning \"cr.isr should not used directly\"/g
s/mov.*=.*cr\.iha/.warning \"cr.iha should not used directly\"/g
s/mov.*=.*cr\.ipsr/.warning \"cr.ipsr should not used directly\"/g
s/mov.*=.*cr\.iim/.warning \"cr.iim should not used directly\"/g
s/mov.*=.*cr\.iip/.warning \"cr.iip should not used directly\"/g
s/mov.*=.*cr\.ivr/.warning \"cr.ivr should not used directly\"/g
s/mov.*=[^\.]*psr/.warning \"psr should not used directly\"/g	# avoid ar.fpsr
s/mov.*=.*ar\.eflags/.warning \"ar.eflags should not used directly\"/g
s/mov.*cr\.ifa.*=.*/.warning \"cr.ifa should not used directly\"/g
s/mov.*cr\.itir.*=.*/.warning \"cr.itir should not used directly\"/g
s/mov.*cr\.iha.*=.*/.warning \"cr.iha should not used directly\"/g
s/mov.*cr\.ipsr.*=.*/.warning \"cr.ipsr should not used directly\"/g
s/mov.*cr\.ifs.*=.*/.warning \"cr.ifs should not used directly\"/g
s/mov.*cr\.iip.*=.*/.warning \"cr.iip should not used directly\"/g
s/mov.*cr\.kr.*=.*/.warning \"cr.kr should not used directly\"/g
s/mov.*ar\.eflags.*=.*/.warning \"ar.eflags should not used directly\"/g
s/itc\.i.*/.warning \"itc.i should not be used directly.\"/g
s/itc\.d.*/.warning \"itc.d should not be used directly.\"/g
s/bsw\.0/.warning \"bsw.0 should not be used directly.\"/g
s/bsw\.1/.warning \"bsw.1 should not be used directly.\"/g
s/ptc\.ga.*/.warning \"ptc.ga should not be used directly.\"/g
