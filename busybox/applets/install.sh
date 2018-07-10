#!/bin/sh

export LC_ALL=POSIX
export LC_CTYPE=POSIX

prefix=$1
if [ -z "$prefix" ]; then
	echo "usage: applets/install.sh DESTINATION TYPE [OPTS ...]"
	echo "  TYPE is one of: --symlinks --hardlinks --binaries --scriptwrapper --none"
	echo "  OPTS is one or more of: --cleanup --noclobber"
	exit 1
fi
shift # Keep only remaining options

# Source the configuration
. ./.config

h=`sort busybox.links | uniq`

sharedlib_dir="0_lib"

linkopts=""
scriptwrapper="n"
binaries="n"
cleanup="0"
noclobber="0"
while [ ${#} -gt 0 ]; do
	case "$1" in
		--hardlinks)     linkopts="-f";;
		--symlinks)      linkopts="-fs";;
		--binaries)      binaries="y";;
		--scriptwrapper) scriptwrapper="y"; swrapall="y";;
		--sw-sh-hard)    scriptwrapper="y"; linkopts="-f";;
		--sw-sh-sym)     scriptwrapper="y"; linkopts="-fs";;
		--cleanup)       cleanup="1";;
		--noclobber)     noclobber="1";;
		--none)          h="";;
		*)               echo "Unknown install option: $1"; exit 1;;
	esac
	shift
done

if [ -n "$DO_INSTALL_LIBS" ] && [ x"$DO_INSTALL_LIBS" != x"n" ]; then
	# get the target dir for the libs
	# assume it starts with lib
	libdir=$($CC -print-file-name=libc.so | \
		 sed -n 's%^.*\(/lib[^\/]*\)/libc.so%\1%p')
	if test -z "$libdir"; then
		libdir=/lib
	fi

	mkdir -p "$prefix/$libdir" || exit 1
	for i in $DO_INSTALL_LIBS; do
		rm -f "$prefix/$libdir/$i" || exit 1
		if [ -f "$i" ]; then
			echo "   Installing $i to the target at $prefix/$libdir/"
			cp -pPR "$i" "$prefix/$libdir/" || exit 1
			chmod 0644 "$prefix/$libdir/`basename $i`" || exit 1
		fi
	done
fi

if [ x"$cleanup" = x"1" ] && [ -e "$prefix/bin/busybox" ]; then
	inode=`ls -i "$prefix/bin/busybox" | awk '{print $1}'`
	sub_shell_it=`
		cd "$prefix"
		for d in usr/sbin usr/bin sbin bin; do
			pd=$PWD
			if [ -d "$d" ]; then
				cd "$d"
				ls -iL . | grep "^ *$inode" | awk '{print $2}' | env -i xargs rm -f
			fi
			cd "$pd"
		done
		`
	exit 0
fi

rm -f "$prefix/bin/busybox" || exit 1
mkdir -p "$prefix/bin" || exit 1
install -m 755 busybox "$prefix/bin/busybox" || exit 1

for i in $h; do
	appdir=`dirname "$i"`
	app=`basename "$i"`
	if [ x"$noclobber" = x"1" ] && [ -e "$prefix/$i" ]; then
		echo "  $prefix/$i already exists"
		continue
	fi
	mkdir -p "$prefix/$appdir" || exit 1
	if [ x"$scriptwrapper" = x"y" ]; then
		if [ x"$swrapall" != x"y" ] && [ x"$i" = x"/bin/sh" ]; then
			ln $linkopts busybox "$prefix/$i" || exit 1
		else
			rm -f "$prefix/$i"
			echo "#!/bin/busybox" >"$prefix/$i"
			chmod +x "$prefix/$i"
		fi
		echo "	$prefix/$i"
	elif [ x"$binaries" = x"y" ]; then
		# Copy the binary over rather
		if [ -e "$sharedlib_dir/$app" ]; then
			echo "   Copying $sharedlib_dir/$app to $prefix/$i"
			cp -pPR "$sharedlib_dir/$app" "$prefix/$i" || exit 1
		else
			echo "Error: Could not find $sharedlib_dir/$app"
			exit 1
		fi
	else
		if [ x"$linkopts" = x"-f" ]; then
			bb_path="$prefix/bin/busybox"
		else
			case "$appdir" in
			/)
				bb_path="bin/busybox"
			;;
			/bin)
				bb_path="busybox"
			;;
			/sbin)
				bb_path="../bin/busybox"
			;;
			/usr/bin | /usr/sbin)
				bb_path="../../bin/busybox"
			;;
			*)
				echo "Unknown installation directory: $appdir"
				exit 1
			;;
			esac
		fi
		echo "  $prefix/$i -> $bb_path"
		ln $linkopts "$bb_path" "$prefix/$i" || exit 1
	fi
done

exit 0
