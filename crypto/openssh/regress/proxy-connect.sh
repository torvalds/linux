#	$OpenBSD: proxy-connect.sh,v 1.11 2017/09/26 22:39:25 dtucker Exp $
#	Placed in the Public Domain.

tid="proxy connect"

for c in no yes; do
	verbose "plain username comp=$c"
	opts="-oCompression=$c -F $OBJ/ssh_proxy"
	SSH_CONNECTION=`${SSH} $opts 999.999.999.999 'echo $SSH_CONNECTION'`
	if [ $? -ne 0 ]; then
		fail "ssh proxyconnect comp=$c failed"
	fi
	if [ "$SSH_CONNECTION" != "UNKNOWN 65535 UNKNOWN 65535" ]; then
		fail "bad SSH_CONNECTION comp=$c: " \
		    "$SSH_CONNECTION"
	fi
done

verbose "username with style"
${SSH} -F $OBJ/ssh_proxy ${USER}:style@999.999.999.999 true || \
	fail "ssh proxyconnect failed"
