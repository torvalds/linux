#!/bin/sh -
# This script generates ed test scripts (.ed) from .t files
#	
# $FreeBSD$

PATH="/bin:/usr/bin:/usr/local/bin/:."
ED=$1
[ ! -x $ED ] && { echo "$ED: cannot execute"; exit 1; }

for i in *.t; do
#	base=${i%.*}
#	base=`echo $i | sed 's/\..*//'`
#	base=`expr $i : '\([^.]*\)'`
#	(
#	echo "#!/bin/sh -"
#	echo "$ED - <<\EOT"
#	echo "r $base.d"
#	cat $i
#	echo "w $base.o"
#	echo EOT
#	) >$base.ed
#	chmod +x $base.ed
# The following is pretty ugly way of doing the above, and not appropriate 
# use of ed  but the point is that it can be done...
	base=`$ED - \!"echo $i" <<-EOF
		s/\..*
	EOF`
	$ED - <<-EOF
		a
		#!/bin/sh -
		$ED - <<\EOT
		H
		r $base.d
		w $base.o
		EOT
		.
		-2r $i
		w $base.ed
		!chmod +x $base.ed
	EOF
done

for i in *.err; do
#	base=${i%.*}
#	base=`echo $i | sed 's/\..*//'`
#	base=`expr $i : '\([^.]*\)'`
#	(
#	echo "#!/bin/sh -"
#	echo "$ED - <<\EOT"
#	echo H
#	echo "r $base.err"
#	cat $i
#	echo "w $base.o"
#	echo EOT
#	) >$base-err.ed
#	chmod +x $base-err.ed
# The following is pretty ugly way of doing the above, and not appropriate 
# use of ed  but the point is that it can be done...
	base=`$ED - \!"echo $i" <<-EOF
		s/\..*
	EOF`
	$ED - <<-EOF
		a
		#!/bin/sh -
		$ED - <<\EOT
		H
		r $base.err
		w $base.o
		EOT
		.
		-2r $i
		w ${base}.red
		!chmod +x ${base}.red
	EOF
done
