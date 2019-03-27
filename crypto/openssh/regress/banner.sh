#	$OpenBSD: banner.sh,v 1.3 2017/04/30 23:34:55 djm Exp $
#	Placed in the Public Domain.

tid="banner"
echo "Banner $OBJ/banner.in" >> $OBJ/sshd_proxy

rm -f $OBJ/banner.out $OBJ/banner.in $OBJ/empty.in
touch $OBJ/empty.in

trace "test missing banner file"
verbose "test $tid: missing banner file"
( ${SSH} -F $OBJ/ssh_proxy otherhost true 2>$OBJ/banner.out && \
	cmp $OBJ/empty.in $OBJ/banner.out ) || \
	fail "missing banner file"

for s in 0 10 100 1000 10000 100000 ; do
	if [ "$s" = "0" ]; then
		# create empty banner
		touch $OBJ/banner.in
	elif [ "$s" = "10" ]; then
		# create 10-byte banner file
		echo "abcdefghi" >$OBJ/banner.in
	else
		# increase size 10x
		cp $OBJ/banner.in $OBJ/banner.out
		for i in 0 1 2 3 4 5 6 7 8 ; do
			cat $OBJ/banner.out >> $OBJ/banner.in
		done
	fi

	trace "test banner size $s"
	verbose "test $tid: size $s"
	( ${SSH} -F $OBJ/ssh_proxy otherhost true 2>$OBJ/banner.out && \
		cmp $OBJ/banner.in $OBJ/banner.out ) || \
		fail "banner size $s mismatch"
done

trace "test suppress banner (-q)"
verbose "test $tid: suppress banner (-q)"
( ${SSH} -q -F $OBJ/ssh_proxy otherhost true 2>$OBJ/banner.out && \
	cmp $OBJ/empty.in $OBJ/banner.out ) || \
	fail "suppress banner (-q)"

rm -f $OBJ/banner.out $OBJ/banner.in $OBJ/empty.in
