#	$OpenBSD: limit-keytype.sh,v 1.5 2018/03/12 00:52:57 djm Exp $
#	Placed in the Public Domain.

tid="restrict pubkey type"

rm -f $OBJ/authorized_keys_$USER $OBJ/user_ca_key* $OBJ/user_key*
rm -f $OBJ/authorized_principals_$USER $OBJ/cert_user_key*

mv $OBJ/sshd_proxy $OBJ/sshd_proxy.orig
mv $OBJ/ssh_proxy $OBJ/ssh_proxy.orig

# Create a CA key
${SSHKEYGEN} -q -N '' -t ed25519  -f $OBJ/user_ca_key ||\
	fatal "ssh-keygen failed"

# Make some keys and a certificate.
${SSHKEYGEN} -q -N '' -t ed25519 -f $OBJ/user_key1 || \
	fatal "ssh-keygen failed"
${SSHKEYGEN} -q -N '' -t rsa -f $OBJ/user_key2 || \
	fatal "ssh-keygen failed"
${SSHKEYGEN} -q -N '' -t rsa -f $OBJ/user_key3 || \
	fatal "ssh-keygen failed"
${SSHKEYGEN} -q -N '' -t dsa -f $OBJ/user_key4 || \
	fatal "ssh-keygen failed"
${SSHKEYGEN} -q -s $OBJ/user_ca_key -I "regress user key for $USER" \
	-z $$ -n ${USER},mekmitasdigoat $OBJ/user_key3 ||
		fatal "couldn't sign user_key1"
# Copy the private key alongside the cert to allow better control of when
# it is offered.
mv $OBJ/user_key3-cert.pub $OBJ/cert_user_key3.pub

grep -v IdentityFile $OBJ/ssh_proxy.orig > $OBJ/ssh_proxy

opts="-oProtocol=2 -F $OBJ/ssh_proxy -oIdentitiesOnly=yes"
certopts="$opts -i $OBJ/user_key3 -oCertificateFile=$OBJ/cert_user_key3.pub"

echo mekmitasdigoat > $OBJ/authorized_principals_$USER
cat $OBJ/user_key1.pub > $OBJ/authorized_keys_$USER
cat $OBJ/user_key2.pub >> $OBJ/authorized_keys_$USER

prepare_config() {
	(
		grep -v "Protocol"  $OBJ/sshd_proxy.orig
		echo "Protocol 2"
		echo "AuthenticationMethods publickey"
		echo "TrustedUserCAKeys $OBJ/user_ca_key.pub"
		echo "AuthorizedPrincipalsFile $OBJ/authorized_principals_%u"
		for x in "$@" ; do
			echo "$x"
		done
 	) > $OBJ/sshd_proxy
}

prepare_config

# Check we can log in with all key types.
${SSH} $certopts proxy true || fatal "cert failed"
${SSH} $opts -i $OBJ/user_key1 proxy true || fatal "key1 failed"
${SSH} $opts -i $OBJ/user_key2 proxy true || fatal "key2 failed"

# Allow plain Ed25519 and RSA. The certificate should fail.
verbose "allow rsa,ed25519"
prepare_config \
	"PubkeyAcceptedKeyTypes rsa-sha2-256,rsa-sha2-512,ssh-rsa,ssh-ed25519"
${SSH} $certopts proxy true && fatal "cert succeeded"
${SSH} $opts -i $OBJ/user_key1 proxy true || fatal "key1 failed"
${SSH} $opts -i $OBJ/user_key2 proxy true || fatal "key2 failed"

# Allow Ed25519 only.
verbose "allow ed25519"
prepare_config "PubkeyAcceptedKeyTypes ssh-ed25519"
${SSH} $certopts proxy true && fatal "cert succeeded"
${SSH} $opts -i $OBJ/user_key1 proxy true || fatal "key1 failed"
${SSH} $opts -i $OBJ/user_key2 proxy true && fatal "key2 succeeded"

# Allow all certs. Plain keys should fail.
verbose "allow cert only"
prepare_config "PubkeyAcceptedKeyTypes *-cert-v01@openssh.com"
${SSH} $certopts proxy true || fatal "cert failed"
${SSH} $opts -i $OBJ/user_key1 proxy true && fatal "key1 succeeded"
${SSH} $opts -i $OBJ/user_key2 proxy true && fatal "key2 succeeded"

# Allow RSA in main config, Ed25519 for non-existent user.
verbose "match w/ no match"
prepare_config "PubkeyAcceptedKeyTypes rsa-sha2-256,rsa-sha2-512,ssh-rsa" \
	"Match user x$USER" "PubkeyAcceptedKeyTypes +ssh-ed25519"
${SSH} $certopts proxy true && fatal "cert succeeded"
${SSH} $opts -i $OBJ/user_key1 proxy true && fatal "key1 succeeded"
${SSH} $opts -i $OBJ/user_key2 proxy true || fatal "key2 failed"

# Allow only DSA in main config, Ed25519 for user.
verbose "match w/ matching"
prepare_config "PubkeyAcceptedKeyTypes ssh-dss" \
	"Match user $USER" "PubkeyAcceptedKeyTypes +ssh-ed25519"
${SSH} $certopts proxy true || fatal "cert failed"
${SSH} $opts -i $OBJ/user_key1 proxy true || fatal "key1 failed"
${SSH} $opts -i $OBJ/user_key4 proxy true && fatal "key4 succeeded"

