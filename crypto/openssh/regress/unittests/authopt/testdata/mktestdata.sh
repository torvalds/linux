#/bin/sh

set -xe

rm -f ca_key ca_key.pub
rm -f user_key user_key.pub
rm -f *.cert

ssh-keygen -q -f ca_key -t ed25519 -C CA -N ''
ssh-keygen -q -f user_key -t ed25519 -C "user key" -N ''

sign() {
	output=$1
	shift
	set -xe
	ssh-keygen -q -s ca_key -I user -n user \
	    -V 19990101:19991231 -z 1 "$@" user_key.pub
	mv user_key-cert.pub "$output"
}

sign all_permit.cert -Opermit-agent-forwarding -Opermit-port-forwarding \
    -Opermit-pty -Opermit-user-rc -Opermit-X11-forwarding
sign no_permit.cert -Oclear

sign no_agentfwd.cert -Ono-agent-forwarding
sign no_portfwd.cert -Ono-port-forwarding
sign no_pty.cert -Ono-pty
sign no_user_rc.cert -Ono-user-rc
sign no_x11fwd.cert -Ono-X11-forwarding

sign only_agentfwd.cert -Oclear -Opermit-agent-forwarding
sign only_portfwd.cert -Oclear -Opermit-port-forwarding
sign only_pty.cert -Oclear -Opermit-pty
sign only_user_rc.cert -Oclear -Opermit-user-rc
sign only_x11fwd.cert -Oclear -Opermit-X11-forwarding

sign force_command.cert -Oforce-command="foo"
sign sourceaddr.cert -Osource-address="127.0.0.1/32,::1/128"

# ssh-keygen won't permit generation of certs with invalid source-address
# values, so we do it as a custom extension.
sign bad_sourceaddr.cert -Ocritical:source-address=xxxxx

sign unknown_critical.cert -Ocritical:blah=foo

sign host.cert -h

rm -f user_key ca_key user_key.pub ca_key.pub
