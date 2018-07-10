#!/bin/sh
#
# makedev.sh - creates device files for a busybox boot floppy image


# we do our work in the dev/ directory
if [ -z "$1" ]; then
	echo "usage: `basename $0` path/to/dev/dir"
	exit 1
fi

cd $1


# miscellaneous one-of-a-kind stuff
mknod console c 5 1
mknod full c 1 7
mknod kmem c 1 2
mknod mem c 1 1
mknod null c 1 3
mknod port c 1 4
mknod random c 1 8
mknod urandom c 1 9
mknod zero c 1 5
ln -s /proc/kcore core

# IDE HD devs
# note: not going to bother creating all concievable partitions; you can do
# that yourself as you need 'em.
mknod hda b 3 0
mknod hdb b 3 64
mknod hdc b 22 0
mknod hdd b 22 64

# loop devs
for i in `seq 0 7`; do
	mknod loop$i b 7 $i
done

# ram devs
for i in `seq 0 9`; do
	mknod ram$i b 1 $i
done
ln -s ram1 ram

# ttys
mknod tty c 5 0
for i in `seq 0 9`; do
	mknod tty$i c 4 $i
done

# virtual console screen devs
for i in `seq 0 9`; do
	mknod vcs$i b 7 $i
done
ln -s vcs0 vcs

# virtual console screen w/ attributes devs
for i in `seq 0 9`; do
	mknod vcsa$i b 7 $((128 + i))
done
ln -s vcsa0 vcsa
