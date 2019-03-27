#	$OpenBSD: localcommand.sh,v 1.4 2017/04/30 23:34:55 djm Exp $
#	Placed in the Public Domain.

tid="localcommand"

echo 'PermitLocalCommand yes' >> $OBJ/ssh_proxy
echo 'LocalCommand echo foo' >> $OBJ/ssh_proxy

verbose "test $tid: proto $p localcommand"
a=`${SSH} -F $OBJ/ssh_proxy somehost true`
if [ "$a" != "foo" ] ; then
	fail "$tid proto $p"
fi
