#!/bin/sh
# Called from top-level directory a-la
#
# scripts/generate_BUFSIZ.sh include/common_bufsiz.h

. ./.config || exit 1

debug=false
#debug=true

postcompile=false
test x"$1" = x"--post" && { postcompile=true; shift; }

common_bufsiz_h=$1

test x"$NM" = x"" && NM="${CONFIG_CROSS_COMPILER_PREFIX}nm"
test x"$CC" = x"" && CC="${CONFIG_CROSS_COMPILER_PREFIX}gcc"

exitcmd="exit 0"

regenerate() {
	cat >"$1.$$"
	test -f "$1" && diff "$1.$$" "$1" >/dev/null && rm "$1.$$" && return
	mv "$1.$$" "$1"
}

generate_std_and_exit() {
	$debug && echo "Configuring: bb_common_bufsiz1[] in bss"
	{
	echo "enum { COMMON_BUFSIZE = 1024 };"
	echo "extern char bb_common_bufsiz1[];"
	echo "#define setup_common_bufsiz() ((void)0)"
	} | regenerate "$common_bufsiz_h"
	echo "std" >"$common_bufsiz_h.method"
	$exitcmd
}

generate_big_and_exit() {
	$debug && echo "Configuring: bb_common_bufsiz1[] in bss, COMMON_BUFSIZE = $1"
	{
	echo "enum { COMMON_BUFSIZE = $1 };"
	echo "extern char bb_common_bufsiz1[];"
	echo "#define setup_common_bufsiz() ((void)0)"
	} | regenerate "$common_bufsiz_h"
	echo "$2" >"$common_bufsiz_h.method"
	$exitcmd
}

generate_1k_and_exit() {
	generate_big_and_exit 1024 "1k"
}

round_down_COMMON_BUFSIZE() {
	COMMON_BUFSIZE=1024
	test "$1" -le 32 && return
	COMMON_BUFSIZE=$(( ($1-32) & 0x0ffffff0 ))
	COMMON_BUFSIZE=$(( COMMON_BUFSIZE < 1024 ? 1024 : COMMON_BUFSIZE ))
}

# User does not want any funky stuff?
test x"$CONFIG_FEATURE_USE_BSS_TAIL" = x"y" || generate_std_and_exit

# The script is run two times: before compilation, when it needs to
# (re)generate $common_bufsiz_h, and directly after successful build,
# when it needs to assess whether the build is ok to use at all (not buggy),
# and (re)generate $common_bufsiz_h for a future build.

if $postcompile; then
	# Postcompile needs to create/delete OK/FAIL files

	test -f busybox_unstripped || exit 1
	test -f "$common_bufsiz_h.method" || exit 1

	# How the build was done?
	method=`cat -- "$common_bufsiz_h.method"`

	# Get _end address
	END=`$NM busybox_unstripped | grep ' . _end$'| cut -d' ' -f1`
	test x"$END" = x"" && generate_std_and_exit
	$debug && echo "END:0x$END $((0x$END))"
	END=$((0x$END))

	# Get PAGE_SIZE
	{
	echo "#include <sys/user.h>"
	echo "#if defined(PAGE_SIZE) && PAGE_SIZE > 0"
	echo "char page_size[PAGE_SIZE];"
	echo "#endif"
	} >page_size_$$.c
	$CC -c "page_size_$$.c" || exit 1
	PAGE_SIZE=`$NM --size-sort "page_size_$$.o" | cut -d' ' -f1`
	rm "page_size_$$.c" "page_size_$$.o"
	test x"$PAGE_SIZE" = x"" && exit 1
	$debug && echo "PAGE_SIZE:0x$PAGE_SIZE $((0x$PAGE_SIZE))"
	PAGE_SIZE=$((0x$PAGE_SIZE))
	test $PAGE_SIZE -lt 1024 && exit 1

	# How much space between _end[] and next page?
	PAGE_MASK=$((PAGE_SIZE-1))
	TAIL_SIZE=$(( (-END) & PAGE_MASK ))
	$debug && echo "TAIL_SIZE:$TAIL_SIZE bytes"

	if test x"$method" = x"1k"; then
		{
		echo $TAIL_SIZE
		md5sum <.config | cut -d' ' -f1
		stat -c "%Y" .config
		} >"$common_bufsiz_h.1k.OK"
		round_down_COMMON_BUFSIZE $((1024 + TAIL_SIZE))
		# emit message only if COMMON_BUFSIZE is indeed larger
		test $COMMON_BUFSIZE -gt 1024 \
			&& echo "Rerun make to use larger COMMON_BUFSIZE ($COMMON_BUFSIZE)"
		test $COMMON_BUFSIZE = 1024 && generate_1k_and_exit
		generate_big_and_exit $COMMON_BUFSIZE "big"
	fi
fi

# Based on past success/fail of 1k build, decide next build type

if test -f "$common_bufsiz_h.1k.OK"; then
	# previous 1k build succeeded
	oldcfg=`tail -n2 -- "$common_bufsiz_h.1k.OK"`
	curcfg=`md5sum <.config | cut -d' ' -f1; stat -c "%Y" .config`
	# config did not change
	if test x"$oldcfg" = x"$curcfg"; then
		# Try bigger COMMON_BUFSIZE if possible
		TAIL_SIZE=`head -n1 -- "$common_bufsiz_h.1k.OK"`
		round_down_COMMON_BUFSIZE $((1024 + TAIL_SIZE))
		test $COMMON_BUFSIZE = 1024 && generate_1k_and_exit
		generate_big_and_exit $COMMON_BUFSIZE "big"
	fi
	# config did change
	rm -rf -- "$common_bufsiz_h.1k.OK"
fi

# There was no 1k build yet. Try it.
generate_1k_and_exit
