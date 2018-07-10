#!/bin/bash
# The list of all applet config symbols
test -f include/applets.h || { echo "No include/applets.h file"; exit 1; }
apps="`
grep ^IF_ include/applets.h \
| grep -v ^IF_FEATURE_ \
| sed 's/IF_\([A-Z0-9._-]*\)(.*/\1/' \
| sort | uniq
`"

test $# = 0 && set -- $apps

mintext=999999999
for app; do
	b="busybox_${app}"
	test -f "$b" || continue
	text=`size "$b" | tail -1 | sed -e's/\t/ /g' -e's/^ *//' -e's/ .*//'`
	#echo "text from $app: $text"
	test x"${text//[0123456789]/}" = x"" || {
		echo "Can't get: size $b"
		exit 1
	}
	test $mintext -gt $text && {
		mintext=$text
		echo "# New mintext from $app: $mintext"
	}
	eval "text_${app}=$text"
done

for app; do
	b="busybox_${app}"
	test -f "$b" || continue
	eval "text=\$text_${app}"
	echo "# $app adds $((text-mintext))"
done

grep ^IF_ include/applets.h \
| grep -v ^IF_FEATURE_ \
| sed 's/, .*//' \
| sed 's/\t//g' \
| sed 's/ //g' \
| sed 's/(APPLET(/(/' \
| sed 's/(APPLET_[A-Z]*(/(/' \
| sed 's/(IF_[A-Z_]*(/(/' \
| sed 's/IF_\([A-Z0-9._-]*\)(\(.*\)/\1 \2/' \
| sort | uniq \
| while read app name; do
	b="busybox_${app}"
	test -f "$b" || continue

	file=`grep -lF "bool \"$name" $(find -name '*.c') | xargs`
	# so far all such items are in .c files; if need to check Config.* files:
	#test "$file" || file=`grep -lF "bool \"$name" $(find -name 'Config.*') |  xargs`
	test "$file" || continue
	#echo "FILE:'$file'"

	eval "text=\$text_${app}"
	sz=$((text-mintext))
	sz_kb=$((sz/1000))
	sz_frac=$(( (sz - sz_kb*1000) ))
	sz_f=$((sz_frac / 100))

	echo -n "sed 's/bool \"$name"'[" ](*[0-9tinykbytes .]*)*"*$/'
	if test "$sz_kb" -ge 10; then
		echo -n "bool \"$name (${sz_kb} kb)\""
	elif test "$sz_kb" -gt 0 -a "$sz_f" = 0; then
		echo -n "bool \"$name (${sz_kb} kb)\""
	elif test "$sz_kb" -gt 0; then
		echo -n "bool \"$name ($sz_kb.${sz_f} kb)\""
	elif test "$sz" -ge 200; then
		echo -n "bool \"$name ($sz bytes)\""
	else
		echo -n "bool \"$name (tiny)\""
	fi
	echo "/' -i $file"
done
