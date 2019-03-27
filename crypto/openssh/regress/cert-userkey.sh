#	$OpenBSD: cert-userkey.sh,v 1.19 2018/03/12 00:54:04 djm Exp $
#	Placed in the Public Domain.

tid="certified user keys"

rm -f $OBJ/authorized_keys_$USER $OBJ/user_ca_key* $OBJ/cert_user_key*
cp $OBJ/sshd_proxy $OBJ/sshd_proxy_bak
cp $OBJ/ssh_proxy $OBJ/ssh_proxy_bak

PLAIN_TYPES=`$SSH -Q key-plain | sed 's/^ssh-dss/ssh-dsa/;s/^ssh-//'`
EXTRA_TYPES=""

if echo "$PLAIN_TYPES" | grep '^rsa$' >/dev/null 2>&1 ; then
	PLAIN_TYPES="$PLAIN_TYPES rsa-sha2-256 rsa-sha2-512"
fi

kname() {
	case $ktype in
	rsa-sha2-*) n="$ktype" ;;
	# subshell because some seds will add a newline
	*) n=$(echo $1 | sed 's/^dsa/ssh-dss/;s/^rsa/ssh-rsa/;s/^ed/ssh-ed/') ;;
	esac
	echo "$n*,ssh-rsa*,ssh-ed25519*"
}

# Create a CA key
${SSHKEYGEN} -q -N '' -t rsa  -f $OBJ/user_ca_key ||\
	fail "ssh-keygen of user_ca_key failed"

# Generate and sign user keys
for ktype in $PLAIN_TYPES $EXTRA_TYPES ; do
	verbose "$tid: sign user ${ktype} cert"
	${SSHKEYGEN} -q -N '' -t ${ktype} \
	    -f $OBJ/cert_user_key_${ktype} || \
		fatal "ssh-keygen of cert_user_key_${ktype} failed"
	# Generate RSA/SHA2 certs for rsa-sha2* keys.
	case $ktype in
	rsa-sha2-*)	tflag="-t $ktype" ;;
	*)		tflag="" ;;
	esac
	${SSHKEYGEN} -q -s $OBJ/user_ca_key -z $$ \
	    -I "regress user key for $USER" \
	    -n ${USER},mekmitasdigoat $tflag $OBJ/cert_user_key_${ktype} || \
		fatal "couldn't sign cert_user_key_${ktype}"
done

# Test explicitly-specified principals
for ktype in $EXTRA_TYPES $PLAIN_TYPES ; do
	t=$(kname $ktype)
	for privsep in yes no ; do
		_prefix="${ktype} privsep $privsep"

		# Setup for AuthorizedPrincipalsFile
		rm -f $OBJ/authorized_keys_$USER
		(
			cat $OBJ/sshd_proxy_bak
			echo "UsePrivilegeSeparation $privsep"
			echo "AuthorizedPrincipalsFile " \
			    "$OBJ/authorized_principals_%u"
			echo "TrustedUserCAKeys $OBJ/user_ca_key.pub"
			echo "PubkeyAcceptedKeyTypes ${t}"
		) > $OBJ/sshd_proxy
		(
			cat $OBJ/ssh_proxy_bak
			echo "PubkeyAcceptedKeyTypes ${t}"
		) > $OBJ/ssh_proxy

		# Missing authorized_principals
		verbose "$tid: ${_prefix} missing authorized_principals"
		rm -f $OBJ/authorized_principals_$USER
		${SSH} -i $OBJ/cert_user_key_${ktype} \
		    -F $OBJ/ssh_proxy somehost true >/dev/null 2>&1
		if [ $? -eq 0 ]; then
			fail "ssh cert connect succeeded unexpectedly"
		fi

		# Empty authorized_principals
		verbose "$tid: ${_prefix} empty authorized_principals"
		echo > $OBJ/authorized_principals_$USER
		${SSH} -i $OBJ/cert_user_key_${ktype} \
		    -F $OBJ/ssh_proxy somehost true >/dev/null 2>&1
		if [ $? -eq 0 ]; then
			fail "ssh cert connect succeeded unexpectedly"
		fi

		# Wrong authorized_principals
		verbose "$tid: ${_prefix} wrong authorized_principals"
		echo gregorsamsa > $OBJ/authorized_principals_$USER
		${SSH} -i $OBJ/cert_user_key_${ktype} \
		    -F $OBJ/ssh_proxy somehost true >/dev/null 2>&1
		if [ $? -eq 0 ]; then
			fail "ssh cert connect succeeded unexpectedly"
		fi

		# Correct authorized_principals
		verbose "$tid: ${_prefix} correct authorized_principals"
		echo mekmitasdigoat > $OBJ/authorized_principals_$USER
		${SSH} -i $OBJ/cert_user_key_${ktype} \
		    -F $OBJ/ssh_proxy somehost true >/dev/null 2>&1
		if [ $? -ne 0 ]; then
			fail "ssh cert connect failed"
		fi

		# authorized_principals with bad key option
		verbose "$tid: ${_prefix} authorized_principals bad key opt"
		echo 'blah mekmitasdigoat' > $OBJ/authorized_principals_$USER
		${SSH} -i $OBJ/cert_user_key_${ktype} \
		    -F $OBJ/ssh_proxy somehost true >/dev/null 2>&1
		if [ $? -eq 0 ]; then
			fail "ssh cert connect succeeded unexpectedly"
		fi

		# authorized_principals with command=false
		verbose "$tid: ${_prefix} authorized_principals command=false"
		echo 'command="false" mekmitasdigoat' > \
		    $OBJ/authorized_principals_$USER
		${SSH} -i $OBJ/cert_user_key_${ktype} \
		    -F $OBJ/ssh_proxy somehost true >/dev/null 2>&1
		if [ $? -eq 0 ]; then
			fail "ssh cert connect succeeded unexpectedly"
		fi


		# authorized_principals with command=true
		verbose "$tid: ${_prefix} authorized_principals command=true"
		echo 'command="true" mekmitasdigoat' > \
		    $OBJ/authorized_principals_$USER
		${SSH} -i $OBJ/cert_user_key_${ktype} \
		    -F $OBJ/ssh_proxy somehost false >/dev/null 2>&1
		if [ $? -ne 0 ]; then
			fail "ssh cert connect failed"
		fi

		# Setup for principals= key option
		rm -f $OBJ/authorized_principals_$USER
		(
			cat $OBJ/sshd_proxy_bak
			echo "UsePrivilegeSeparation $privsep"
			echo "PubkeyAcceptedKeyTypes ${t}"
		) > $OBJ/sshd_proxy
		(
			cat $OBJ/ssh_proxy_bak
			echo "PubkeyAcceptedKeyTypes ${t}"
		) > $OBJ/ssh_proxy

		# Wrong principals list
		verbose "$tid: ${_prefix} wrong principals key option"
		(
			printf 'cert-authority,principals="gregorsamsa" '
			cat $OBJ/user_ca_key.pub
		) > $OBJ/authorized_keys_$USER
		${SSH} -i $OBJ/cert_user_key_${ktype} \
		    -F $OBJ/ssh_proxy somehost true >/dev/null 2>&1
		if [ $? -eq 0 ]; then
			fail "ssh cert connect succeeded unexpectedly"
		fi

		# Correct principals list
		verbose "$tid: ${_prefix} correct principals key option"
		(
			printf 'cert-authority,principals="mekmitasdigoat" '
			cat $OBJ/user_ca_key.pub
		) > $OBJ/authorized_keys_$USER
		${SSH} -i $OBJ/cert_user_key_${ktype} \
		    -F $OBJ/ssh_proxy somehost true >/dev/null 2>&1
		if [ $? -ne 0 ]; then
			fail "ssh cert connect failed"
		fi
	done
done

basic_tests() {
	auth=$1
	if test "x$auth" = "xauthorized_keys" ; then
		# Add CA to authorized_keys
		(
			printf 'cert-authority '
			cat $OBJ/user_ca_key.pub
		) > $OBJ/authorized_keys_$USER
	else
		echo > $OBJ/authorized_keys_$USER
		extra_sshd="TrustedUserCAKeys $OBJ/user_ca_key.pub"
	fi

	for ktype in $PLAIN_TYPES ; do
		t=$(kname $ktype)
		for privsep in yes no ; do
			_prefix="${ktype} privsep $privsep $auth"
			# Simple connect
			verbose "$tid: ${_prefix} connect"
			(
				cat $OBJ/sshd_proxy_bak
				echo "UsePrivilegeSeparation $privsep"
				echo "PubkeyAcceptedKeyTypes ${t}"
				echo "$extra_sshd"
			) > $OBJ/sshd_proxy
			(
				cat $OBJ/ssh_proxy_bak
				echo "PubkeyAcceptedKeyTypes ${t}"
			) > $OBJ/ssh_proxy

			${SSH} -i $OBJ/cert_user_key_${ktype} \
			    -F $OBJ/ssh_proxy somehost true
			if [ $? -ne 0 ]; then
				fail "ssh cert connect failed"
			fi

			# Revoked keys
			verbose "$tid: ${_prefix} revoked key"
			(
				cat $OBJ/sshd_proxy_bak
				echo "UsePrivilegeSeparation $privsep"
				echo "RevokedKeys $OBJ/cert_user_key_revoked"
				echo "PubkeyAcceptedKeyTypes ${t}"
				echo "$extra_sshd"
			) > $OBJ/sshd_proxy
			cp $OBJ/cert_user_key_${ktype}.pub \
			    $OBJ/cert_user_key_revoked
			${SSH} -i $OBJ/cert_user_key_${ktype} \
			    -F $OBJ/ssh_proxy somehost true >/dev/null 2>&1
			if [ $? -eq 0 ]; then
				fail "ssh cert connect succeeded unexpecedly"
			fi
			verbose "$tid: ${_prefix} revoked via KRL"
			rm $OBJ/cert_user_key_revoked
			${SSHKEYGEN} -kqf $OBJ/cert_user_key_revoked \
			    $OBJ/cert_user_key_${ktype}.pub
			${SSH} -i $OBJ/cert_user_key_${ktype} \
			    -F $OBJ/ssh_proxy somehost true >/dev/null 2>&1
			if [ $? -eq 0 ]; then
				fail "ssh cert connect succeeded unexpecedly"
			fi
			verbose "$tid: ${_prefix} empty KRL"
			${SSHKEYGEN} -kqf $OBJ/cert_user_key_revoked
			${SSH} -i $OBJ/cert_user_key_${ktype} \
			    -F $OBJ/ssh_proxy somehost true >/dev/null 2>&1
			if [ $? -ne 0 ]; then
				fail "ssh cert connect failed"
			fi
		done

		# Revoked CA
		verbose "$tid: ${ktype} $auth revoked CA key"
		(
			cat $OBJ/sshd_proxy_bak
			echo "RevokedKeys $OBJ/user_ca_key.pub"
			echo "PubkeyAcceptedKeyTypes ${t}"
			echo "$extra_sshd"
		) > $OBJ/sshd_proxy
		${SSH} -i $OBJ/cert_user_key_${ktype} -F $OBJ/ssh_proxy \
		    somehost true >/dev/null 2>&1
		if [ $? -eq 0 ]; then
			fail "ssh cert connect succeeded unexpecedly"
		fi
	done

	verbose "$tid: $auth CA does not authenticate"
	(
		cat $OBJ/sshd_proxy_bak
		echo "PubkeyAcceptedKeyTypes ${t}"
		echo "$extra_sshd"
	) > $OBJ/sshd_proxy
	verbose "$tid: ensure CA key does not authenticate user"
	${SSH} -i $OBJ/user_ca_key \
	    -F $OBJ/ssh_proxy somehost true >/dev/null 2>&1
	if [ $? -eq 0 ]; then
		fail "ssh cert connect with CA key succeeded unexpectedly"
	fi
}

basic_tests authorized_keys
basic_tests TrustedUserCAKeys

test_one() {
	ident=$1
	result=$2
	sign_opts=$3
	auth_choice=$4
	auth_opt=$5

	if test "x$auth_choice" = "x" ; then
		auth_choice="authorized_keys TrustedUserCAKeys"
	fi

	for auth in $auth_choice ; do
		for ktype in rsa ed25519 ; do
			cat $OBJ/sshd_proxy_bak > $OBJ/sshd_proxy
			if test "x$auth" = "xauthorized_keys" ; then
				# Add CA to authorized_keys
				(
					printf "cert-authority${auth_opt} "
					cat $OBJ/user_ca_key.pub
				) > $OBJ/authorized_keys_$USER
			else
				echo > $OBJ/authorized_keys_$USER
				echo "TrustedUserCAKeys $OBJ/user_ca_key.pub" \
				    >> $OBJ/sshd_proxy
				echo "PubkeyAcceptedKeyTypes ${t}*" \
				    >> $OBJ/sshd_proxy
				if test "x$auth_opt" != "x" ; then
					echo $auth_opt >> $OBJ/sshd_proxy
				fi
			fi

			verbose "$tid: $ident auth $auth expect $result $ktype"
			${SSHKEYGEN} -q -s $OBJ/user_ca_key \
			    -I "regress user key for $USER" \
			    $sign_opts $OBJ/cert_user_key_${ktype} ||
				fail "couldn't sign cert_user_key_${ktype}"

			${SSH} -i $OBJ/cert_user_key_${ktype} \
			    -F $OBJ/ssh_proxy somehost true >/dev/null 2>&1
			rc=$?
			if [ "x$result" = "xsuccess" ] ; then
				if [ $rc -ne 0 ]; then
					fail "$ident failed unexpectedly"
				fi
			else
				if [ $rc -eq 0 ]; then
					fail "$ident succeeded unexpectedly"
				fi
			fi
		done
	done
}

test_one "correct principal"	success "-n ${USER}"
test_one "host-certificate"	failure "-n ${USER} -h"
test_one "wrong principals"	failure "-n foo"
test_one "cert not yet valid"	failure "-n ${USER} -V20200101:20300101"
test_one "cert expired"		failure "-n ${USER} -V19800101:19900101"
test_one "cert valid interval"	success "-n ${USER} -V-1w:+2w"
test_one "wrong source-address"	failure "-n ${USER} -Osource-address=10.0.0.0/8"
test_one "force-command"	failure "-n ${USER} -Oforce-command=false"

# Behaviour is different here: TrustedUserCAKeys doesn't allow empty principals
test_one "empty principals"	success "" authorized_keys
test_one "empty principals"	failure "" TrustedUserCAKeys

# Check explicitly-specified principals: an empty principals list in the cert
# should always be refused.

# AuthorizedPrincipalsFile
rm -f $OBJ/authorized_keys_$USER
echo mekmitasdigoat > $OBJ/authorized_principals_$USER
test_one "AuthorizedPrincipalsFile principals" success "-n mekmitasdigoat" \
    TrustedUserCAKeys "AuthorizedPrincipalsFile $OBJ/authorized_principals_%u"
test_one "AuthorizedPrincipalsFile no principals" failure "" \
    TrustedUserCAKeys "AuthorizedPrincipalsFile $OBJ/authorized_principals_%u"

# principals= key option
rm -f $OBJ/authorized_principals_$USER
test_one "principals key option principals" success "-n mekmitasdigoat" \
    authorized_keys ',principals="mekmitasdigoat"'
test_one "principals key option no principals" failure "" \
    authorized_keys ',principals="mekmitasdigoat"'

# command= options vs. force-command in key
test_one "force-command match true" success \
    "-n ${USER} -Oforce-command=true" \
    authorized_keys ',command="true"'
test_one "force-command match true" failure \
    "-n ${USER} -Oforce-command=false" \
    authorized_keys ',command="false"'
test_one "force-command mismatch 1" failure \
    "-n ${USER} -Oforce-command=false" \
    authorized_keys ',command="true"'
test_one "force-command mismatch 2" failure \
    "-n ${USER} -Oforce-command=true" \
    authorized_keys ',command="false"'

# Wrong certificate
cat $OBJ/sshd_proxy_bak > $OBJ/sshd_proxy
for ktype in $PLAIN_TYPES ; do
	t=$(kname $ktype)
	# Self-sign
	${SSHKEYGEN} -q -s $OBJ/cert_user_key_${ktype} -I \
	    "regress user key for $USER" \
	    -n $USER $OBJ/cert_user_key_${ktype} ||
		fatal "couldn't sign cert_user_key_${ktype}"
	verbose "$tid: user ${ktype} connect wrong cert"
	${SSH} -i $OBJ/cert_user_key_${ktype} -F $OBJ/ssh_proxy \
	    somehost true >/dev/null 2>&1
	if [ $? -eq 0 ]; then
		fail "ssh cert connect $ident succeeded unexpectedly"
	fi
done

rm -f $OBJ/authorized_keys_$USER $OBJ/user_ca_key* $OBJ/cert_user_key*
rm -f $OBJ/authorized_principals_$USER

