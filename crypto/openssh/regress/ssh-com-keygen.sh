#	$OpenBSD: ssh-com-keygen.sh,v 1.4 2004/02/24 17:06:52 markus Exp $
#	Placed in the Public Domain.

tid="ssh.com key import"

#TEST_COMBASE=/path/to/ssh/com/binaries
if [ "X${TEST_COMBASE}" = "X" ]; then
	fatal '$TEST_COMBASE is not set'
fi

VERSIONS="
	2.0.10
	2.0.12
	2.0.13
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

COMPRV=${OBJ}/comkey
COMPUB=${COMPRV}.pub
OPENSSHPRV=${OBJ}/opensshkey
OPENSSHPUB=${OPENSSHPRV}.pub

# go for it
for v in ${VERSIONS}; do
	keygen=${TEST_COMBASE}/${v}/ssh-keygen2
	if [ ! -x ${keygen} ]; then
		continue
	fi
	types="dss"
        case $v in
        2.3.1|3.*)
                types="$types rsa"
                ;;
        esac
	for t in $types; do
		verbose "ssh-keygen $v/$t"
		rm -f $COMPRV $COMPUB $OPENSSHPRV $OPENSSHPUB
		${keygen} -q -P -t $t ${COMPRV} > /dev/null 2>&1
		if [ $? -ne 0 ]; then
			fail "${keygen} -t $t failed"
			continue
		fi
		${SSHKEYGEN} -if ${COMPUB} > ${OPENSSHPUB}
		if [ $? -ne 0 ]; then
			fail "import public key ($v/$t) failed"
			continue
		fi
		${SSHKEYGEN} -if ${COMPRV} > ${OPENSSHPRV}
		if [ $? -ne 0 ]; then
			fail "import private key ($v/$t) failed"
			continue
		fi
		chmod 600 ${OPENSSHPRV}
		${SSHKEYGEN} -yf ${OPENSSHPRV} |\
			diff - ${OPENSSHPUB}
		if [ $? -ne 0 ]; then
			fail "public keys ($v/$t) differ"
		fi
	done
done

rm -f $COMPRV $COMPUB $OPENSSHPRV $OPENSSHPUB
