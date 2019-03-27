# $FreeBSD$

ntest=1

confdir=${dir:-$(dirname "$0")}
maindir=${dir:-$(dirname "$0")}
while [ ! -r "$confdir/conf" -a "$confdir" != / ]; do
	confdir=$(cd $confdir/..; pwd)
done
while [ "$maindir" != / ]; do
	if [ -f "$maindir/pjdfstest" -a -x "$maindir/pjdfstest" ]; then
		break
	fi
	maindir=$(cd $maindir/../; pwd)
done
fstest="${maindir}/pjdfstest"
if ! . ${confdir}/conf; then
	echo "not ok - could not source configuration file"
	exit 1
fi
if [ ! -x $fstest ]; then
	echo "not ok - could not find pjdfstest app"
	exit 1
fi

requires_root()
{
	case "$(id -u)" in
	0)
		return 0
		;;
	*)
		echo "not ok ${ntest} not root"
		return 1
		;;
	esac
}

expect()
{
	e="${1}"
	shift
	r=`${fstest} $* 2>/dev/null | tail -1`
	echo "${r}" | ${GREP} -Eq '^'${e}'$'
	if [ $? -eq 0 ]; then
		if [ -z "${todomsg}" ]; then
			echo "ok ${ntest}"
		else
			echo "ok ${ntest} # TODO ${todomsg}"
		fi
	else
		if [ -z "${todomsg}" ]; then
			echo "not ok ${ntest} - tried '$*', expected ${e}, got ${r}"
		else
			echo "not ok ${ntest} # TODO ${todomsg}"
		fi
	fi
	todomsg=""
	ntest=$((ntest+1))
}

jexpect()
{
	s="${1}"
	d="${2}"
	e="${3}"

	shift 3
	r=`jail -s ${s} / pjdfstest 127.0.0.1 /bin/sh -c "cd ${d} && ${fstest} $* 2>/dev/null" 2>/dev/null | tail -1`
	echo "${r}" | ${GREP} -Eq '^'${e}'$'
	if [ $? -eq 0 ]; then
		if [ -z "${todomsg}" ]; then
			echo "ok ${ntest}"
		else
			echo "ok ${ntest} # TODO ${todomsg}"
		fi
	else
		if [ -z "${todomsg}" ]; then
			echo "not ok ${ntest} - tried '$*', expected ${e}, got ${r}"
		else
			echo "not ok ${ntest} # TODO ${todomsg}"
		fi
	fi
	todomsg=""
	ntest=$((ntest+1))
}

test_check()
{
	if [ $* ]; then
		if [ -z "${todomsg}" ]; then
			echo "ok ${ntest}"
		else
			echo "ok ${ntest} # TODO ${todomsg}"
		fi
	else
		if [ -z "${todomsg}" ]; then
			echo "not ok ${ntest}"
		else
			echo "not ok ${ntest} # TODO ${todomsg}"
		fi
	fi
	todomsg=""
	ntest=$((ntest+1))
}

todo()
{
	if [ "${os}" = "${1}" -o "${os}:${fs}" = "${1}" ]; then
		todomsg="${2}"
	fi
}

namegen()
{
	echo "pjdfstest_`dd if=/dev/urandom bs=1k count=1 2>/dev/null | openssl md5 | awk '{print $NF}'`"
}

namegen_len()
{
	len="${1}"

	name=""
	while :; do
		namepart="`dd if=/dev/urandom bs=64 count=1 2>/dev/null | openssl md5 | awk '{print $NF}'`"
		name="${name}${namepart}"
		curlen=`printf "%s" "${name}" | wc -c`
		[ ${curlen} -lt ${len} ] || break
	done
	name=`echo "${name}" | cut -b -${len}`
	printf "%s" "${name}"
}

# POSIX:
# {NAME_MAX}
#     Maximum number of bytes in a filename (not including terminating null).
namegen_max()
{
	name_max=`${fstest} pathconf . _PC_NAME_MAX`
	namegen_len ${name_max}
}

# POSIX:
# {PATH_MAX}
#     Maximum number of bytes in a pathname, including the terminating null character.
dirgen_max()
{
	name_max=`${fstest} pathconf . _PC_NAME_MAX`
	complen=$((name_max/2))
	path_max=`${fstest} pathconf . _PC_PATH_MAX`
	# "...including the terminating null character."
	path_max=$((path_max-1))

	name=""
	while :; do
		name="${name}`namegen_len ${complen}`/"
		curlen=`printf "%s" "${name}" | wc -c`
		[ ${curlen} -lt ${path_max} ] || break
	done
	name=`echo "${name}" | cut -b -${path_max}`
	name=`echo "${name}" | sed -E 's@/$@x@'`
	printf "%s" "${name}"
}

quick_exit()
{
	echo "1..1"
	echo "ok 1"
	exit 0
}

supported()
{
	case "${1}" in
	lchmod)
		if [ "${os}" != "FreeBSD" ]; then
			return 1
		fi
		;;
	chflags)
		if [ "${os}" != "FreeBSD" ]; then
			return 1
		fi
		;;
	chflags_SF_SNAPSHOT)
		if [ "${os}" != "FreeBSD" -o "${fs}" != "UFS" ]; then
			return 1
		fi
		;;
	posix_fallocate)
		if [ "${os}" != "FreeBSD" ]; then
			return 1
		fi
		;;
	stat_st_birthtime)
		case "${os}" in
		Darwin|FreeBSD)
			;;
		*)
			return 1
			;;
		esac
		;;
	utimensat)
		case ${os} in
		Darwin)
			return 1
			;;
		esac
		;;
	esac
	return 0
}

require()
{
	if supported ${1}; then
		return
	fi
	quick_exit
}

if [ "${os}" = "FreeBSD" ]; then
mountpoint()
{
	df $1 | tail -1 | awk '{ print $6 }'
}

mount_options()
{
	mount -p | awk '$2 == "'$(mountpoint .)'" { print $4 }' | sed -e 's/,/ /g'
}

nfsv4acls()
{
	if mount_options | grep -q nfsv4acls; then
		return 0
	fi
	return 1
}

noexec()
{
	if mount_options | grep -q noexec; then
		return 0
	fi
	return 1
}

nosuid()
{
	if mount_options | grep -q nosuid; then
		return 0
	fi
	return 1
}
else
mountpoint()
{
	return 1
}
mount_options()
{
	return 1
}
nfsv4acls()
{
	return 1
}
noexec()
{
	return 1
}
nosuid()
{
	return 1
}
fi

# usage:
#	create_file <type> <name>
#	create_file <type> <name> <mode>
#	create_file <type> <name> <uid> <gid>
#	create_file <type> <name> <mode> <uid> <gid>
create_file() {
	type="${1}"
	name="${2}"

	case "${type}" in
	none)
		return
		;;
	regular)
		expect 0 create ${name} 0644
		;;
	dir)
		expect 0 mkdir ${name} 0755
		;;
	fifo)
		expect 0 mkfifo ${name} 0644
		;;
	block)
		expect 0 mknod ${name} b 0644 1 2
		;;
	char)
		expect 0 mknod ${name} c 0644 1 2
		;;
	socket)
		expect 0 bind ${name}
		;;
	symlink)
		expect 0 symlink test ${name}
		;;
	esac
	if [ -n "${3}" -a -n "${4}" -a -n "${5}" ]; then
		if [ "${type}" = symlink ]; then
			expect 0 lchmod ${name} ${3}
		else
			expect 0 chmod ${name} ${3}
		fi
		expect 0 lchown ${name} ${4} ${5}
	elif [ -n "${3}" -a -n "${4}" ]; then
		expect 0 lchown ${name} ${3} ${4}
	elif [ -n "${3}" ]; then
		if [ "${type}" = symlink ]; then
			expect 0 lchmod ${name} ${3}
		else
			expect 0 chmod ${name} ${3}
		fi
	fi
}

# Tests for whether or not a filesystem is mounted with a particular option
# with -o, e.g. `mount -o noexec`.
#
# Parameters:
# - mount_option - noatime, noexec, etc.
#
# Returns:
# - 0 if mounted with the option.
# - 1 otherwise.
has_mount_option()
{
	local IFS=,
	local mount_opt

	local mount_option_search=$1

	# XXX: mountpoint is defined in .../tests/sys/pjdfstest/tests/conf
	for mount_opt in $(mount -d -p | awk '$2 == "'$mountpoint'" { print $4 }'); do
		if [ "$mount_opt" = "$mount_option_search" ]; then
			return 0
		fi
	done
	return 1
}

# Filesystem must be mounted with -o exec
requires_exec()
{
	if has_mount_option noexec; then
		echo "1..0 # SKIP filesystem mounted with -o noexec"
		exit 0
	fi
}
