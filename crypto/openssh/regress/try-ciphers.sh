#	$OpenBSD: try-ciphers.sh,v 1.26 2017/04/30 23:34:55 djm Exp $
#	Placed in the Public Domain.

tid="try ciphers"

cp $OBJ/sshd_proxy $OBJ/sshd_proxy_bak

for c in `${SSH} -Q cipher`; do
	n=0
	for m in `${SSH} -Q mac`; do
		trace "cipher $c mac $m"
		verbose "test $tid: cipher $c mac $m"
		cp $OBJ/sshd_proxy_bak $OBJ/sshd_proxy
		echo "Ciphers=$c" >> $OBJ/sshd_proxy
		echo "MACs=$m" >> $OBJ/sshd_proxy
		${SSH} -F $OBJ/ssh_proxy -m $m -c $c somehost true
		if [ $? -ne 0 ]; then
			fail "ssh failed with mac $m cipher $c"
		fi
		# No point trying all MACs for AEAD ciphers since they
		# are ignored.
		if ${SSH} -Q cipher-auth | grep "^${c}\$" >/dev/null 2>&1 ; then
			break
		fi
		n=`expr $n + 1`
	done
done

