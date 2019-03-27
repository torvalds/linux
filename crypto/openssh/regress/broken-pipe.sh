#	$OpenBSD: broken-pipe.sh,v 1.6 2017/04/30 23:34:55 djm Exp $
#	Placed in the Public Domain.

tid="broken pipe test"

for i in 1 2 3 4; do
	${SSH} -F $OBJ/ssh_config_config nexthost echo $i 2> /dev/null | true
	r=$?
	if [ $r -ne 0 ]; then
		fail "broken pipe returns $r"
	fi
done
