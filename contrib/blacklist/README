# $NetBSD: README,v 1.8 2017/04/13 17:59:34 christos Exp $

This package contains library that can be used by network daemons to
communicate with a packet filter via a daemon to enforce opening and
closing ports dynamically based on policy.

The interface to the packet filter is in libexec/blacklistd-helper
(this is currently designed for npf) and the configuration file
(inspired from inetd.conf) is in etc/blacklistd.conf.

On NetBSD you can find an example npf.conf and blacklistd.conf in
/usr/share/examples/blacklistd; you need to adjust the interface
in npf.conf and copy both files to /etc; then you just enable
blacklistd=YES in /etc/rc.conf, start it up, and you are all set.

There is also a startup file in etc/rc.d/blacklistd

Patches to various daemons to add blacklisting capabilitiers are in the
"diff" directory:
    - OpenSSH: diff/ssh.diff [tcp socket example]
    - Bind: diff/named.diff [both tcp and udp]
    - ftpd: diff/ftpd.diff [tcp]

These patches have been applied to NetBSD-current.

The network daemon (for example sshd) communicates to blacklistd, via
a unix socket like syslog. The library calls are simple and everything
is handled by the library. In the simplest form the only thing the
daemon needs to do is to call:

	blacklist(action, acceptedfd, message);

Where:
	action = 0 -> successful login clear blacklist state
		 1 -> failed login, add to the failed count
	acceptedfd -> the file descriptor where the server is
		      connected to the remote client. It is used
		      to determine the listening socket, and the
		      remote address. This allows any program to
		      contact the blacklist daemon, since the verification
		      if the program has access to the listening
		      socket is done by virtue that the port
		      number is retrieved from the kernel.
	message    -> an optional string that is used in debugging logs.

Unfortunately there is no way to get information about the "peer"
from a udp socket, because there is no connection and that information
is kept with the server. In that case the daemon can provide the
peer information to blacklistd via:

	blacklist_sa(action, acceptedfd, sockaddr, sockaddr_len, message);

The configuration file contains entries of the form:

# Blacklist rule
# host/Port	type	protocol	owner	name	nfail	disable
192.168.1.1:ssh	stream	tcp		*	-int	10	1m
8.8.8.8:ssh	stream	tcp		*	-ext	6	60m
ssh		stream	tcp6		*	*	6	60m
http		stream	tcp		*	*	6	60m

Here note that owner is * because the connection is done from the
child ssh socket which runs with user privs. We treat ipv4 connections
differently by maintaining two different rules one for the external
interface and one from the internal We also register for both tcp
and tcp6 since those are different listening sockets and addresses;
we don't bother with ipv6 and separate rules. We use nfail = 6,
because ssh allows 3 password attempts per connection, and this
will let us have 2 connections before blocking. Finally we block
for an hour; we could block forever too by specifying * in the
duration column.

blacklistd and the library use syslog(3) to report errors. The
blacklist filter state is persisted automatically in /var/db/blacklistd.db
so that if the daemon is restarted, it remembers what connections
is currently handling. To start from a fresh state (if you restart
npf too for example), you can use -f. To watch the daemon at work,
you can use -d.

The current control file is designed for npf, and it uses the
dynamic rule feature. You need to create a dynamic rule in your
/etc/npf.conf on the group referring to the interface you want to block
called blacklistd as follows:

ext_if=bge0
int_if=sk0
	
group "external" on $ext_if {
	...
        ruleset "blacklistd-ext" 
        ruleset "blacklistd" 
	...
}

group "internal" on $int_if {
	...
        ruleset "blacklistd-int" 
	...
}

You can use 'blacklistctl dump -a' to list all the current entries
in the database; the ones that have nfail <c>/<t> where <c>urrent
>= <t>otal, should have an id assosiated with them; this means that
there is a packet filter rule added for that entry. For npf, you
can examine the packet filter dynamic rule entries using 'npfctl
rule <rulename> list'.  The number of current entries can exceed
the total. This happens because entering packet filter rules is
asynchronous; there could be other connection before the rule
becomes activated.

Enjoy,

christos
