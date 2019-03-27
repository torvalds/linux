#	$OpenBSD: cfginclude.sh,v 1.2 2016/05/03 15:30:46 dtucker Exp $
#	Placed in the Public Domain.

tid="config include"

# to appease StrictModes
umask 022

cat > $OBJ/ssh_config.i << _EOF
Match host a
	Hostname aa

Match host b
	Hostname bb
	Include $OBJ/ssh_config.i.*

Match host c
	Include $OBJ/ssh_config.i.*
	Hostname cc

Match host m
	Include $OBJ/ssh_config.i.*

Host d
	Hostname dd

Host e
	Hostname ee
	Include $OBJ/ssh_config.i.*

Host f
	Include $OBJ/ssh_config.i.*
	Hostname ff

Host n
	Include $OBJ/ssh_config.i.*
_EOF

cat > $OBJ/ssh_config.i.0 << _EOF
Match host xxxxxx
_EOF

cat > $OBJ/ssh_config.i.1 << _EOF
Match host a
	Hostname aaa

Match host b
	Hostname bbb

Match host c
	Hostname ccc

Host d
	Hostname ddd

Host e
	Hostname eee

Host f
	Hostname fff
_EOF

cat > $OBJ/ssh_config.i.2 << _EOF
Match host a
	Hostname aaaa

Match host b
	Hostname bbbb

Match host c
	Hostname cccc

Host d
	Hostname dddd

Host e
	Hostname eeee

Host f
	Hostname ffff

Match all
	Hostname xxxx
_EOF

trial() {
	_host="$1"
	_exp="$2"
	${REAL_SSH} -F $OBJ/ssh_config.i -G "$_host" > $OBJ/ssh_config.out ||
		fatal "ssh config parse failed"
	_got=`grep -i '^hostname ' $OBJ/ssh_config.out | awk '{print $2}'`
	if test "x$_exp" != "x$_got" ; then
		fail "host $_host include fail: expected $_exp got $_got"
	fi
}

trial a aa
trial b bb
trial c ccc
trial d dd
trial e ee
trial f fff
trial m xxxx
trial n xxxx
trial x x

# Prepare an included config with an error.

cat > $OBJ/ssh_config.i.3 << _EOF
Hostname xxxx
	Junk
_EOF

${REAL_SSH} -F $OBJ/ssh_config.i -G a 2>/dev/null && \
	fail "ssh include allowed invalid config"

${REAL_SSH} -F $OBJ/ssh_config.i -G x 2>/dev/null && \
	fail "ssh include allowed invalid config"

rm -f $OBJ/ssh_config.i.*

# Ensure that a missing include is not fatal.
cat > $OBJ/ssh_config.i << _EOF
Include $OBJ/ssh_config.i.*
Hostname aa
_EOF

trial a aa

# Ensure that Match/Host in an included config does not affect parent.
cat > $OBJ/ssh_config.i.x << _EOF
Match host x
_EOF

trial a aa

cat > $OBJ/ssh_config.i.x << _EOF
Host x
_EOF

trial a aa

# cleanup
rm -f $OBJ/ssh_config.i $OBJ/ssh_config.i.* $OBJ/ssh_config.out
#	$OpenBSD: cfginclude.sh,v 1.2 2016/05/03 15:30:46 dtucker Exp $
#	Placed in the Public Domain.

tid="config include"

cat > $OBJ/ssh_config.i << _EOF
Match host a
	Hostname aa

Match host b
	Hostname bb
	Include $OBJ/ssh_config.i.*

Match host c
	Include $OBJ/ssh_config.i.*
	Hostname cc

Match host m
	Include $OBJ/ssh_config.i.*

Host d
	Hostname dd

Host e
	Hostname ee
	Include $OBJ/ssh_config.i.*

Host f
	Include $OBJ/ssh_config.i.*
	Hostname ff

Host n
	Include $OBJ/ssh_config.i.*
_EOF

cat > $OBJ/ssh_config.i.0 << _EOF
Match host xxxxxx
_EOF

cat > $OBJ/ssh_config.i.1 << _EOF
Match host a
	Hostname aaa

Match host b
	Hostname bbb

Match host c
	Hostname ccc

Host d
	Hostname ddd

Host e
	Hostname eee

Host f
	Hostname fff
_EOF

cat > $OBJ/ssh_config.i.2 << _EOF
Match host a
	Hostname aaaa

Match host b
	Hostname bbbb

Match host c
	Hostname cccc

Host d
	Hostname dddd

Host e
	Hostname eeee

Host f
	Hostname ffff

Match all
	Hostname xxxx
_EOF

trial() {
	_host="$1"
	_exp="$2"
	${REAL_SSH} -F $OBJ/ssh_config.i -G "$_host" > $OBJ/ssh_config.out ||
		fatal "ssh config parse failed"
	_got=`grep -i '^hostname ' $OBJ/ssh_config.out | awk '{print $2}'`
	if test "x$_exp" != "x$_got" ; then
		fail "host $_host include fail: expected $_exp got $_got"
	fi
}

trial a aa
trial b bb
trial c ccc
trial d dd
trial e ee
trial f fff
trial m xxxx
trial n xxxx
trial x x

# Prepare an included config with an error.

cat > $OBJ/ssh_config.i.3 << _EOF
Hostname xxxx
	Junk
_EOF

${REAL_SSH} -F $OBJ/ssh_config.i -G a 2>/dev/null && \
	fail "ssh include allowed invalid config"

${REAL_SSH} -F $OBJ/ssh_config.i -G x 2>/dev/null && \
	fail "ssh include allowed invalid config"

rm -f $OBJ/ssh_config.i.*

# Ensure that a missing include is not fatal.
cat > $OBJ/ssh_config.i << _EOF
Include $OBJ/ssh_config.i.*
Hostname aa
_EOF

trial a aa

# Ensure that Match/Host in an included config does not affect parent.
cat > $OBJ/ssh_config.i.x << _EOF
Match host x
_EOF

trial a aa

cat > $OBJ/ssh_config.i.x << _EOF
Host x
_EOF

trial a aa

# Ensure that recursive includes are bounded.
cat > $OBJ/ssh_config.i << _EOF
Include $OBJ/ssh_config.i
_EOF

${REAL_SSH} -F $OBJ/ssh_config.i -G a 2>/dev/null && \
	fail "ssh include allowed infinite recursion?" # or hang...

# cleanup
rm -f $OBJ/ssh_config.i $OBJ/ssh_config.i.* $OBJ/ssh_config.out
