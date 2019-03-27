#	$OpenBSD: ssh-com-client.sh,v 1.7 2013/05/17 04:29:14 dtucker Exp $
#	Placed in the Public Domain.

tid="connect with ssh.com client"

#TEST_COMBASE=/path/to/ssh/com/binaries
if [ "X${TEST_COMBASE}" = "X" ]; then
	fatal '$TEST_COMBASE is not set'
fi

VERSIONS="
	2.1.0
	2.2.0
	2.3.0
	2.3.1
	2.4.0
	3.0.0
	3.1.0
	3.2.0
	3.2.2
	3.2.3
	3.2.5
	3.2.9
	3.2.9.1
	3.3.0"

# 2.0.10 2.0.12 2.0.13 don't like the test setup

# setup authorized keys
SRC=`dirname ${SCRIPT}`
cp ${SRC}/dsa_ssh2.prv ${OBJ}/id.com
chmod 600 ${OBJ}/id.com
${SSHKEYGEN} -i -f ${OBJ}/id.com	> $OBJ/id.openssh
chmod 600 ${OBJ}/id.openssh
${SSHKEYGEN} -y -f ${OBJ}/id.openssh	> $OBJ/authorized_keys_$USER
${SSHKEYGEN} -e -f ${OBJ}/id.openssh	> $OBJ/id.com.pub
echo IdKey ${OBJ}/id.com > ${OBJ}/id.list

# we need a DSA host key
t=dsa
rm -f                             ${OBJ}/$t ${OBJ}/$t.pub
${SSHKEYGEN} -q -N '' -t $t -f	  ${OBJ}/$t
$SUDO cp $OBJ/$t $OBJ/host.$t
echo HostKey $OBJ/host.$t >> $OBJ/sshd_config

# add hostkeys to known hosts
mkdir -p ${OBJ}/${USER}/hostkeys
HK=${OBJ}/${USER}/hostkeys/key_${PORT}_127.0.0.1
${SSHKEYGEN} -e -f ${OBJ}/rsa.pub > ${HK}.ssh-rsa.pub
${SSHKEYGEN} -e -f ${OBJ}/dsa.pub > ${HK}.ssh-dss.pub

cat > ${OBJ}/ssh2_config << EOF
*:
	QuietMode			yes
	StrictHostKeyChecking		yes
	Port				${PORT}
	User				${USER}
	Host				127.0.0.1
	IdentityFile			${OBJ}/id.list
	RandomSeedFile			${OBJ}/random_seed
        UserConfigDirectory             ${OBJ}/%U
	AuthenticationSuccessMsg	no
	BatchMode			yes
	ForwardX11			no
EOF

# we need a real server (no ProxyConnect option)
start_sshd

# go for it
for v in ${VERSIONS}; do
	ssh2=${TEST_COMBASE}/${v}/ssh2
	if [ ! -x ${ssh2} ]; then
		continue
	fi
	verbose "ssh2 ${v}"
	key=ssh-dss
	skipcat=0
        case $v in
        2.1.*|2.3.0)
                skipcat=1
                ;;
        3.0.*)
                key=ssh-rsa
                ;;
        esac
	cp ${HK}.$key.pub ${HK}.pub

	# check exit status
	${ssh2} -q -F ${OBJ}/ssh2_config somehost exit 42
	r=$?
        if [ $r -ne 42 ]; then
                fail "ssh2 ${v} exit code test failed (got $r, expected 42)"
        fi

	# data transfer
	rm -f ${COPY}
	${ssh2} -F ${OBJ}/ssh2_config somehost cat ${DATA} > ${COPY}
        if [ $? -ne 0 ]; then
                fail "ssh2 ${v} cat test (receive) failed"
        fi
	cmp ${DATA} ${COPY}	|| fail "ssh2 ${v} cat test (receive) data mismatch"

	# data transfer, again
	if [ $skipcat -eq 0 ]; then
		rm -f ${COPY}
		cat ${DATA} | \
			${ssh2} -F ${OBJ}/ssh2_config host "cat > ${COPY}"
		if [ $? -ne 0 ]; then
			fail "ssh2 ${v} cat test (send) failed"
		fi
		cmp ${DATA} ${COPY}	|| \
			fail "ssh2 ${v} cat test (send) data mismatch"
	fi

	# no stderr after eof
	rm -f ${COPY}
	${ssh2} -F ${OBJ}/ssh2_config somehost \
		exec sh -c \'"exec > /dev/null; sleep 1; echo bla 1>&2; exit 0"\' \
		2> /dev/null
        if [ $? -ne 0 ]; then
                fail "ssh2 ${v} stderr test failed"
        fi
done

rm -rf ${OBJ}/${USER}
for i in ssh2_config random_seed dsa.pub dsa host.dsa \
    id.list id.com id.com.pub id.openssh; do
	rm -f ${OBJ}/$i
done
