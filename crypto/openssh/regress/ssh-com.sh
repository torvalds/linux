#	$OpenBSD: ssh-com.sh,v 1.10 2017/05/08 01:52:49 djm Exp $
#	Placed in the Public Domain.

tid="connect to ssh.com server"

#TEST_COMBASE=/path/to/ssh/com/binaries
if [ "X${TEST_COMBASE}" = "X" ]; then
	fatal '$TEST_COMBASE is not set'
fi

VERSIONS="
	2.0.12
	2.0.13
	2.1.0
	2.2.0
	2.3.0
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
# 2.0.10 does not support UserConfigDirectory
# 2.3.1 requires a config in $HOME/.ssh2

SRC=`dirname ${SCRIPT}`

# ssh.com
cat << EOF > $OBJ/sshd2_config
#*:
	# Port and ListenAddress are not used.
	QuietMode			yes
	Port				4343
	ListenAddress			127.0.0.1
	UserConfigDirectory		${OBJ}/%U
	Ciphers				AnyCipher
	PubKeyAuthentication		yes
	#AllowedAuthentications		publickey
	AuthorizationFile		authorization
	HostKeyFile			${SRC}/dsa_ssh2.prv
	PublicHostKeyFile		${SRC}/dsa_ssh2.pub
	RandomSeedFile			${OBJ}/random_seed
	MaxConnections			0
	PermitRootLogin			yes
	VerboseMode			no
	CheckMail			no
	Ssh1Compatibility		no
EOF

# create client config
sed "s/HostKeyAlias.*/HostKeyAlias ssh2-localhost-with-alias/" \
	< $OBJ/ssh_config > $OBJ/ssh_config_com

# we need a DSA key for
rm -f                             ${OBJ}/dsa ${OBJ}/dsa.pub
${SSHKEYGEN} -q -N '' -t dsa -f	  ${OBJ}/dsa

# setup userdir, try rsa first
mkdir -p ${OBJ}/${USER}
cp /dev/null ${OBJ}/${USER}/authorization
for t in rsa dsa; do
	${SSHKEYGEN} -e -f ${OBJ}/$t.pub	>  ${OBJ}/${USER}/$t.com
	echo Key $t.com			>> ${OBJ}/${USER}/authorization
	echo IdentityFile ${OBJ}/$t	>> ${OBJ}/ssh_config_com
done

# convert and append DSA hostkey
(
	printf 'ssh2-localhost-with-alias,127.0.0.1,::1 '
	${SSHKEYGEN} -if ${SRC}/dsa_ssh2.pub
) >> $OBJ/known_hosts

# go for it
for v in ${VERSIONS}; do
	sshd2=${TEST_COMBASE}/${v}/sshd2
	if [ ! -x ${sshd2} ]; then
		continue
	fi
	trace "sshd2 ${v}"
	PROXY="proxycommand ${sshd2} -qif ${OBJ}/sshd2_config 2> /dev/null"
	${SSH} -qF ${OBJ}/ssh_config_com -o "${PROXY}" dummy exit 0
        if [ $? -ne 0 ]; then
                fail "ssh connect to sshd2 ${v} failed"
        fi

	ciphers="3des-cbc"
	macs="hmac-md5"
	case $v in
	2.4.*)
		ciphers="$ciphers cast128-cbc"
		macs="$macs hmac-sha1 hmac-sha1-96 hmac-md5-96"
		;;
	3.*)
		ciphers="$ciphers aes128-cbc cast128-cbc"
		macs="$macs hmac-sha1 hmac-sha1-96 hmac-md5-96"
		;;
	esac
	#ciphers="3des-cbc"
	for m in $macs; do
	for c in $ciphers; do
		trace "sshd2 ${v} cipher $c mac $m"
		verbose "test ${tid}: sshd2 ${v} cipher $c mac $m"
		${SSH} -c $c -m $m -qF ${OBJ}/ssh_config_com -o "${PROXY}" dummy exit 0
		if [ $? -ne 0 ]; then
			fail "ssh connect to sshd2 ${v} with $c/$m failed"
		fi
	done
	done
done

rm -rf ${OBJ}/${USER}
for i in sshd_config_proxy ssh_config_proxy random_seed \
	sshd2_config dsa.pub dsa ssh_config_com; do
	rm -f ${OBJ}/$i
done
