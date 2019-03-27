#	$OpenBSD: sshcfgparse.sh,v 1.4 2018/07/04 13:51:12 djm Exp $
#	Placed in the Public Domain.

tid="ssh config parse"

expect_result_present() {
	_str="$1" ; shift
	for _expect in "$@" ; do
		echo "$f" | tr ',' '\n' | grep "^$_expect\$" >/dev/null
		if test $? -ne 0 ; then
			fail "missing expected \"$_expect\" from \"$_str\""
		fi
	done
}
expect_result_absent() {
	_str="$1" ; shift
	for _expect in "$@" ; do
		echo "$f" | tr ',' '\n' | grep "^$_expect\$" >/dev/null
		if test $? -eq 0 ; then
			fail "unexpected \"$_expect\" present in \"$_str\""
		fi
	done
}

verbose "reparse minimal config"
(${SSH} -G -F $OBJ/ssh_config somehost >$OBJ/ssh_config.1 &&
 ${SSH} -G -F $OBJ/ssh_config.1 somehost >$OBJ/ssh_config.2 &&
 diff $OBJ/ssh_config.1 $OBJ/ssh_config.2) || fail "reparse minimal config"

verbose "ssh -W opts"
f=`${SSH} -GF $OBJ/ssh_config host | awk '/exitonforwardfailure/{print $2}'`
test "$f" = "no" || fail "exitonforwardfailure default"
f=`${SSH} -GF $OBJ/ssh_config -W a:1 h | awk '/exitonforwardfailure/{print $2}'`
test "$f" = "yes" || fail "exitonforwardfailure enable"
f=`${SSH} -GF $OBJ/ssh_config -W a:1 -o exitonforwardfailure=no h | \
    awk '/exitonforwardfailure/{print $2}'`
test "$f" = "no" || fail "exitonforwardfailure override"

f=`${SSH} -GF $OBJ/ssh_config host | awk '/clearallforwardings/{print $2}'`
test "$f" = "no" || fail "clearallforwardings default"
f=`${SSH} -GF $OBJ/ssh_config -W a:1 h | awk '/clearallforwardings/{print $2}'`
test "$f" = "yes" || fail "clearallforwardings enable"
f=`${SSH} -GF $OBJ/ssh_config -W a:1 -o clearallforwardings=no h | \
    awk '/clearallforwardings/{print $2}'`
test "$f" = "no" || fail "clearallforwardings override"

verbose "user first match"
user=`awk '$1=="User" {print $2}' $OBJ/ssh_config`
f=`${SSH} -GF $OBJ/ssh_config host | awk '/^user /{print $2}'`
test "$f" = "$user" || fail "user from config, expected '$user' got '$f'"
f=`${SSH} -GF $OBJ/ssh_config -o user=foo -l bar baz@host | awk '/^user /{print $2}'`
test "$f" = "foo" || fail "user first match -oUser, expected 'foo' got '$f' "
f=`${SSH} -GF $OBJ/ssh_config -lbar baz@host user=foo baz@host | awk '/^user /{print $2}'`
test "$f" = "bar" || fail "user first match -l, expected 'bar' got '$f'"
f=`${SSH} -GF $OBJ/ssh_config baz@host -o user=foo -l bar baz@host | awk '/^user /{print $2}'`
test "$f" = "baz" || fail "user first match user@host, expected 'baz' got '$f'"

verbose "pubkeyacceptedkeytypes"
# Default set
f=`${SSH} -GF none host | awk '/^pubkeyacceptedkeytypes /{print $2}'`
expect_result_present "$f" "ssh-ed25519" "ssh-ed25519-cert-v01.*"
expect_result_absent "$f" "ssh-dss"
# Explicit override
f=`${SSH} -GF none -opubkeyacceptedkeytypes=ssh-ed25519 host | \
    awk '/^pubkeyacceptedkeytypes /{print $2}'`
expect_result_present "$f" "ssh-ed25519"
expect_result_absent "$f" "ssh-ed25519-cert-v01.*" "ssh-dss"
# Removal from default set
f=`${SSH} -GF none -opubkeyacceptedkeytypes=-ssh-ed25519-cert* host | \
    awk '/^pubkeyacceptedkeytypes /{print $2}'`
expect_result_present "$f" "ssh-ed25519"
expect_result_absent "$f" "ssh-ed25519-cert-v01.*" "ssh-dss"
f=`${SSH} -GF none -opubkeyacceptedkeytypes=-ssh-ed25519 host | \
    awk '/^pubkeyacceptedkeytypes /{print $2}'`
expect_result_present "$f" "ssh-ed25519-cert-v01.*"
expect_result_absent "$f" "ssh-ed25519" "ssh-dss"
# Append to default set.
# XXX this will break for !WITH_OPENSSL
f=`${SSH} -GF none -opubkeyacceptedkeytypes=+ssh-dss-cert* host | \
    awk '/^pubkeyacceptedkeytypes /{print $2}'`
expect_result_present "$f" "ssh-ed25519" "ssh-dss-cert-v01.*"
expect_result_absent "$f" "ssh-dss"
f=`${SSH} -GF none -opubkeyacceptedkeytypes=+ssh-dss host | \
    awk '/^pubkeyacceptedkeytypes /{print $2}'`
expect_result_present "$f" "ssh-ed25519" "ssh-ed25519-cert-v01.*" "ssh-dss"
expect_result_absent "$f" "ssh-dss-cert-v01.*"

# cleanup
rm -f $OBJ/ssh_config.[012]
