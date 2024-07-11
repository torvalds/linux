#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only

# Copyright (C) 2006 Paul Mackerras, IBM Corporation <paulus@samba.org>

# This script takes a kernel binary and optionally an initrd image
# and/or a device-tree blob, and creates a bootable zImage for a
# given platform.

# Options:
# -o zImage	specify output file
# -p platform	specify platform (links in $platform.o)
# -i initrd	specify initrd file
# -d devtree	specify device-tree blob
# -s tree.dts	specify device-tree source file (needs dtc installed)
# -e esm_blob   specify ESM blob for secure images
# -c		cache $kernel.strip.gz (use if present & newer, else make)
# -C prefix	specify command prefix for cross-building tools
#		(strip, objcopy, ld)
# -D dir	specify directory containing data files used by script
#		(default ./arch/powerpc/boot)
# -W dir	specify working directory for temporary files (default .)
# -z		use gzip (legacy)
# -Z zsuffix    compression to use (gz, xz or none)

# Stop execution if any command fails
set -e

export LC_ALL=C

# Allow for verbose output
if [ "$V" = 1 ]; then
    set -x
    map="-Map wrapper.map"
fi

# defaults
kernel=
ofile=zImage
platform=of
initrd=
dtb=
dts=
esm_blob=
cacheit=
binary=
compression=.gz
uboot_comp=gzip
pie=
format=
notext=
rodynamic=

# cross-compilation prefix
CROSS=

# mkimage wrapper script
MKIMAGE=$srctree/scripts/mkuboot.sh

# directory for object and other files used by this script
object=arch/powerpc/boot
objbin=$object
dtc=scripts/dtc/dtc

# directory for working files
tmpdir=.

usage() {
    echo 'Usage: wrapper [-o output] [-p platform] [-i initrd]' >&2
    echo '       [-d devtree] [-s tree.dts] [-e esm_blob]' >&2
    echo '       [-c] [-C cross-prefix] [-D datadir] [-W workingdir]' >&2
    echo '       [-Z (gz|xz|none)] [--no-compression] [vmlinux]' >&2
    exit 1
}

run_cmd() {
    if [ "$V" = 1 ]; then
        $* 2>&1
    else
        local msg

        set +e
        msg=$($* 2>&1)

        if [ $? -ne "0" ]; then
                echo $msg
                exit 1
        fi
        set -e
    fi
}

while [ "$#" -gt 0 ]; do
    case "$1" in
    -o)
	shift
	[ "$#" -gt 0 ] || usage
	ofile="$1"
	;;
    -p)
	shift
	[ "$#" -gt 0 ] || usage
	platform="$1"
	;;
    -i)
	shift
	[ "$#" -gt 0 ] || usage
	initrd="$1"
	;;
    -d)
	shift
	[ "$#" -gt 0 ] || usage
	dtb="$1"
	;;
    -e)
	shift
	[ "$#" -gt 0 ] || usage
	esm_blob="$1"
	;;
    -s)
	shift
	[ "$#" -gt 0 ] || usage
	dts="$1"
	;;
    -c)
	cacheit=y
	;;
    -C)
	shift
	[ "$#" -gt 0 ] || usage
	CROSS="$1"
	;;
    -D)
	shift
	[ "$#" -gt 0 ] || usage
	object="$1"
	objbin="$1"
	;;
    -W)
	shift
	[ "$#" -gt 0 ] || usage
	tmpdir="$1"
	;;
    -z)
	compression=.gz
	uboot_comp=gzip
	;;
    -Z)
	shift
	[ "$#" -gt 0 ] || usage
        [ "$1" != "gz" -o "$1" != "xz" -o "$1" != "lzma" -o "$1" != "lzo" -o "$1" != "none" ] || usage

	compression=".$1"
	uboot_comp=$1

        if [ $compression = ".none" ]; then
                compression=
		uboot_comp=none
        fi
	if [ $uboot_comp = "gz" ]; then
		uboot_comp=gzip
	fi
	;;
    --no-gzip)
        # a "feature" of the wrapper script is that it can be used outside
        # the kernel tree. So keeping this around for backwards compatibility.
        compression=
	uboot_comp=none
        ;;
    -?)
	usage
	;;
    *)
	[ -z "$kernel" ] || usage
	kernel="$1"
	;;
    esac
    shift
done


if [ -n "$dts" ]; then
    if [ ! -r "$dts" -a -r "$object/dts/$dts" ]; then
	dts="$object/dts/$dts"
    fi
    if [ -z "$dtb" ]; then
	dtb="$platform.dtb"
    fi
    $dtc -O dtb -o "$dtb" -b 0 "$dts"
fi

if [ -z "$kernel" ]; then
    kernel=vmlinux
fi

LC_ALL=C elfformat="`${CROSS}objdump -p "$kernel" | grep 'file format' | awk '{print $4}'`"
case "$elfformat" in
    elf64-powerpcle)	format=elf64lppc	;;
    elf64-powerpc)	format=elf32ppc	;;
    elf32-powerpc)	format=elf32ppc	;;
esac

ld_version()
{
    # Poached from scripts/ld-version.sh, but we don't want to call that because
    # this script (wrapper) is distributed separately from the kernel source.
    # Extract linker version number from stdin and turn into single number.
    awk '{
	gsub(".*\\)", "");
	gsub(".*version ", "");
	gsub("-.*", "");
	split($1,a, ".");
	if( length(a[3]) == "8" )
		# a[3] is probably a date of format yyyymmdd used for release snapshots. We
		# can assume it to be zero as it does not signify a new version as such.
		a[3] = 0;
	print a[1]*100000000 + a[2]*1000000 + a[3]*10000;
	exit
    }'
}

ld_is_lld()
{
	${CROSS}ld -V 2>&1 | grep -q LLD
}

# Do not include PT_INTERP segment when linking pie. Non-pie linking
# just ignores this option.
LD_VERSION=$(${CROSS}ld --version | ld_version)
LD_NO_DL_MIN_VERSION=$(echo 2.26 | ld_version)
if [ "$LD_VERSION" -ge "$LD_NO_DL_MIN_VERSION" ] ; then
	nodl="--no-dynamic-linker"
fi

# suppress some warnings in recent ld versions
nowarn="-z noexecstack"
if ! ld_is_lld; then
	if [ "$LD_VERSION" -ge "$(echo 2.39 | ld_version)" ]; then
		nowarn="$nowarn --no-warn-rwx-segments"
	fi
fi

platformo=$object/"$platform".o
lds=$object/zImage.lds
ext=strip
objflags=-S
tmp=$tmpdir/zImage.$$.o
ksection=.kernel:vmlinux.strip
isection=.kernel:initrd
esection=.kernel:esm_blob
link_address='0x400000'
make_space=y


if [ -n "$esm_blob" -a "$platform" != "pseries" ]; then
    echo "ESM blob not support on non-pseries platforms" >&2
    exit 1
fi

case "$platform" in
of)
    platformo="$object/of.o $object/epapr.o"
    make_space=n
    ;;
pseries)
    platformo="$object/pseries-head.o $object/of.o $object/epapr.o"
    link_address='0x4000000'
    if [ "$format" != "elf32ppc" ]; then
	link_address=
	pie=-pie
    fi
    make_space=n
    ;;
maple)
    platformo="$object/of.o $object/epapr.o"
    link_address='0x400000'
    make_space=n
    ;;
pmac|chrp)
    platformo="$object/of.o $object/epapr.o"
    make_space=n
    ;;
coff)
    platformo="$object/crt0.o $object/of.o $object/epapr.o"
    lds=$object/zImage.coff.lds
    link_address='0x500000'
    make_space=n
    pie=
    ;;
miboot|uboot*)
    # miboot and U-boot want just the bare bits, not an ELF binary
    ext=bin
    objflags="-O binary"
    tmp="$ofile"
    ksection=image
    isection=initrd
    ;;
cuboot*)
    binary=y
    compression=
    case "$platform" in
    *-mpc866ads|*-mpc885ads|*-adder875*|*-ep88xc)
        platformo=$object/cuboot-8xx.o
        ;;
    *5200*|*-motionpro)
        platformo=$object/cuboot-52xx.o
        ;;
    *-pq2fads|*-ep8248e|*-mpc8272*|*-storcenter)
        platformo=$object/cuboot-pq2.o
        ;;
    *-mpc824*)
        platformo=$object/cuboot-824x.o
        ;;
    *-mpc83*|*-asp834x*)
        platformo=$object/cuboot-83xx.o
        ;;
    *-tqm8541|*-mpc8560*|*-tqm8560|*-tqm8555|*-ksi8560*)
        platformo=$object/cuboot-85xx-cpm2.o
        ;;
    *-mpc85*|*-tqm85*)
        platformo=$object/cuboot-85xx.o
        ;;
    *-amigaone)
        link_address='0x800000'
        ;;
    esac
    ;;
ps3)
    platformo="$object/ps3-head.o $object/ps3-hvcall.o $object/ps3.o"
    lds=$object/zImage.ps3.lds
    compression=
    ext=bin
    objflags="-O binary --set-section-flags=.bss=contents,alloc,load,data"
    ksection=.kernel:vmlinux.bin
    isection=.kernel:initrd
    link_address=''
    make_space=n
    pie=
    ;;
ep88xc|ep8248e)
    platformo="$object/fixed-head.o $object/$platform.o"
    binary=y
    ;;
adder875-redboot)
    platformo="$object/fixed-head.o $object/redboot-8xx.o"
    binary=y
    ;;
simpleboot-*)
    platformo="$object/fixed-head.o $object/simpleboot.o"
    binary=y
    ;;
asp834x-redboot)
    platformo="$object/fixed-head.o $object/redboot-83xx.o"
    binary=y
    ;;
xpedite52*)
    link_address='0x1400000'
    platformo=$object/cuboot-85xx.o
    ;;
gamecube|wii)
    link_address='0x600000'
    platformo="$object/$platform-head.o $object/$platform.o"
    ;;
microwatt)
    link_address='0x500000'
    platformo="$object/fixed-head.o $object/$platform.o"
    binary=y
    ;;
treeboot-currituck)
    link_address='0x1000000'
    ;;
treeboot-akebono)
    link_address='0x1000000'
    ;;
treeboot-iss4xx-mpic)
    platformo="$object/treeboot-iss4xx.o"
    ;;
epapr)
    platformo="$object/pseries-head.o $object/epapr.o $object/epapr-wrapper.o"
    link_address='0x20000000'
    pie=-pie
    notext='-z notext'
    rodynamic=$(if ${CROSS}ld -V 2>&1 | grep -q LLD ; then echo "-z rodynamic"; fi)
    ;;
mvme5100)
    platformo="$object/fixed-head.o $object/mvme5100.o"
    binary=y
    ;;
mvme7100)
    platformo="$object/motload-head.o $object/mvme7100.o"
    link_address='0x4000000'
    binary=y
    ;;
esac

vmz="$tmpdir/`basename \"$kernel\"`.$ext"

# Calculate the vmlinux.strip size
${CROSS}objcopy $objflags "$kernel" "$vmz.$$"
strip_size=$(${CONFIG_SHELL} "${srctree}/scripts/file-size.sh" "$vmz.$$")

if [ -z "$cacheit" -o ! -f "$vmz$compression" -o "$vmz$compression" -ot "$kernel" ]; then
    # recompress the image if we need to
    case $compression in
    .xz)
        xz --check=crc32 -f -6 "$vmz.$$"
        ;;
    .gz)
        gzip -n -f -9 "$vmz.$$"
        ;;
    .lzma)
        xz --format=lzma -f -6 "$vmz.$$"
	;;
    .lzo)
        lzop -f -9 "$vmz.$$"
	;;
    *)
        # drop the compression suffix so the stripped vmlinux is used
        compression=
	uboot_comp=none
	;;
    esac

    if [ -n "$cacheit" ]; then
	mv -f "$vmz.$$$compression" "$vmz$compression"
    else
	vmz="$vmz.$$"
    fi
else
    rm -f $vmz.$$
fi

vmz="$vmz$compression"

if [ "$make_space" = "y" ]; then
	# Round the size to next higher MB limit
	round_size=$(((strip_size + 0xfffff) & 0xfff00000))

	round_size=0x$(printf "%x" $round_size)
	link_addr=$(printf "%d" $link_address)

	if [ $link_addr -lt $strip_size ]; then
	    echo "INFO: Uncompressed kernel (size 0x$(printf "%x\n" $strip_size))" \
			"overlaps the address of the wrapper($link_address)"
	    echo "INFO: Fixing the link_address of wrapper to ($round_size)"
	    link_address=$round_size
	fi
fi

# Extract kernel version information, some platforms want to include
# it in the image header
version=`${CROSS}strings "$kernel" | grep '^Linux version [-0-9.]' | \
    head -n1 | cut -d' ' -f3`
if [ -n "$version" ]; then
    uboot_version="-n Linux-$version"
fi

# physical offset of kernel image
membase=`${CROSS}objdump -p "$kernel" | grep -m 1 LOAD | awk '{print $7}'`

case "$platform" in
uboot)
    rm -f "$ofile"
    ${MKIMAGE} -A ppc -O linux -T kernel -C $uboot_comp -a $membase -e $membase \
	$uboot_version -d "$vmz" "$ofile"
    if [ -z "$cacheit" ]; then
	rm -f "$vmz"
    fi
    exit 0
    ;;
esac

addsec() {
    ${CROSS}objcopy $4 $1 \
	--add-section=$3="$2" \
	--set-section-flags=$3=contents,alloc,load,readonly,data
}

addsec $tmp "$vmz" $ksection $object/empty.o
if [ -z "$cacheit" ]; then
    rm -f "$vmz"
fi

if [ -n "$initrd" ]; then
    addsec $tmp "$initrd" $isection
fi

if [ -n "$dtb" ]; then
    addsec $tmp "$dtb" .kernel:dtb
    if [ -n "$dts" ]; then
	rm $dtb
    fi
fi

if [ -n "$esm_blob" ]; then
    addsec $tmp "$esm_blob" $esection
fi

if [ "$platform" != "miboot" ]; then
    if [ -n "$link_address" ] ; then
        text_start="-Ttext $link_address"
    fi
#link everything
    ${CROSS}ld -m $format -T $lds $text_start $pie $nodl $nowarn $rodynamic $notext -o "$ofile" $map \
	$platformo $tmp $object/wrapper.a
    rm $tmp
fi

# Some platforms need the zImage's entry point and base address
base=0x`${CROSS}nm "$ofile" | grep ' _start$' | cut -d' ' -f1`
entry=`${CROSS}objdump -f "$ofile" | grep '^start address ' | cut -d' ' -f3`

if [ -n "$binary" ]; then
    mv "$ofile" "$ofile".elf
    ${CROSS}objcopy -O binary "$ofile".elf "$ofile"
fi

# post-processing needed for some platforms
case "$platform" in
pseries|chrp|maple)
    $objbin/addnote "$ofile"
    ;;
coff)
    ${CROSS}objcopy -O aixcoff-rs6000 --set-start "$entry" "$ofile"
    $objbin/hack-coff "$ofile"
    ;;
cuboot*)
    gzip -n -f -9 "$ofile"
    ${MKIMAGE} -A ppc -O linux -T kernel -C gzip -a "$base" -e "$entry" \
            $uboot_version -d "$ofile".gz "$ofile"
    ;;
treeboot*)
    mv "$ofile" "$ofile.elf"
    $objbin/mktree "$ofile.elf" "$ofile" "$base" "$entry"
    if [ -z "$cacheit" ]; then
	rm -f "$ofile.elf"
    fi
    exit 0
    ;;
ps3)
    # The ps3's loader supports loading a gzipped binary image from flash
    # rom to ram addr zero. The loader then enters the system reset
    # vector at addr 0x100.  A bootwrapper overlay is used to arrange for
    # a binary image of the kernel to be at addr zero, and yet have a
    # suitable bootwrapper entry at 0x100.  To construct the final rom
    # image 512 bytes from offset 0x100 is copied to the bootwrapper
    # place holder at symbol __system_reset_kernel.  The 512 bytes of the
    # bootwrapper entry code at symbol __system_reset_overlay is then
    # copied to offset 0x100.  At runtime the bootwrapper program copies
    # the data at __system_reset_kernel back to addr 0x100.

    system_reset_overlay=0x`${CROSS}nm "$ofile" \
        | grep ' __system_reset_overlay$'       \
        | cut -d' ' -f1`
    system_reset_overlay=`printf "%d" $system_reset_overlay`
    system_reset_kernel=0x`${CROSS}nm "$ofile" \
        | grep ' __system_reset_kernel$'       \
        | cut -d' ' -f1`
    system_reset_kernel=`printf "%d" $system_reset_kernel`
    overlay_dest="256"
    overlay_size="512"

    ${CROSS}objcopy -O binary "$ofile" "$ofile.bin"

    run_cmd dd if="$ofile.bin" of="$ofile.bin" conv=notrunc   \
        skip=$overlay_dest seek=$system_reset_kernel          \
        count=$overlay_size bs=1

    run_cmd dd if="$ofile.bin" of="$ofile.bin" conv=notrunc   \
        skip=$system_reset_overlay seek=$overlay_dest         \
        count=$overlay_size bs=1

    odir="$(dirname "$ofile.bin")"

    # The ps3's flash loader has a size limit of 16 MiB for the uncompressed
    # image.  If a compressed image that exceeded this limit is written to
    # flash the loader will decompress that image until the 16 MiB limit is
    # reached, then enter the system reset vector of the partially decompressed
    # image.  No warning is issued.
    rm -f "$odir"/{otheros,otheros-too-big}.bld
    size=$(${CROSS}nm --no-sort --radix=d "$ofile" | grep -E ' _end$' | cut -d' ' -f1)
    bld="otheros.bld"
    if [ $size -gt $((0x1000000)) ]; then
        bld="otheros-too-big.bld"
    fi
    gzip -n --force -9 --stdout "$ofile.bin" > "$odir/$bld"
    ;;
esac
