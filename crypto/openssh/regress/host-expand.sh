#	$OpenBSD: host-expand.sh,v 1.5 2017/04/30 23:34:55 djm Exp $
#	Placed in the Public Domain.

tid="expand %h and %n"

echo 'PermitLocalCommand yes' >> $OBJ/ssh_proxy
printf 'LocalCommand printf "%%%%s\\n" "%%n" "%%h"\n' >> $OBJ/ssh_proxy

cat >$OBJ/expect <<EOE
somehost
127.0.0.1
EOE

${SSH} -F $OBJ/ssh_proxy somehost true >$OBJ/actual
diff $OBJ/expect $OBJ/actual || fail "$tid"

