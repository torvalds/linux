#!/bin/bash

PROGNAME=$0

if [ ! -f .dest ]; then
        echo -n "Prosim zadaj adresu kam sa maju kopirovat zdrojaky (NONE pre nikam): "
        read DEST
        echo $DEST > .dest
fi
DEST=`cat .dest`

minor=0
major=0

[ -f .minor ] && minor=`cat .minor`
[ -f .major ] && major=`cat .major`
echo $(($minor + 1)) > .minor

function do_exit {
        echo $2
        exit $1
}

function parse_argv {
        GRUB=1
        REBOOT=1
        MEDUSA_ONLY=0
        DELETE=0
        INSTALL=1

        for arg in "$@"; do
                if [[ "$arg" == '--delete' || "$arg" == '-delete' ]]; then
                        DELETE=1
                elif [[ "$arg" == '--clean' || "$arg" == '-clean' ]]; then
                        sudo make clean
                        sudo rm -rf debian
                elif [[ "$arg" == '--nogdb' || "$arg" == '-nogdb' || "$arg" == '--nogrub' || "$arg" == '-nogrub' ]]; then
                        GRUB=0
                elif [[ "$arg" == '--noreboot' || "$arg" == '-noreboot' ]]; then
                        REBOOT=0
                elif [[ "$arg" == '--medusa-only' || "$arg" == '-medusa-only' ]]; then
                        MEDUSA_ONLY=1
                        DELETE=1
                        GRUB=0
                elif [[ "$arg" == '--build-only' || "$arg" == '-build-only' ]]; then
                        INSTALL=0
                        GRUB=0
                        REBOOT=0
                elif [[ "$arg" == '-h' || "$arg" == '--help' || "$arg" == '-help' ]]; then
                        help
                else
                        echo "Error unknown parameter '$arg'"
                        help 
                fi
        done

        PROCESSORS=`cat /proc/cpuinfo | grep processor | wc -l`
}

function help {
        echo "$PROGNAME [--help] [--delete] [--clean] [--nogrub] [--noreboot] [--medusa-only] [--nogdb]";
        echo "    --help           - Prints this help"
        echo "    --delete         - Deletes the medusa object files (handy when changing"
        echo "                       header files or makefiles)"
        echo "    --clean          - Does make clean before the compilation (handy when"
        echo "                       changing kernel release)"
        echo "    --nogrub/--nogdb - Turns off the waiting for GDB connection during booting"
        echo "    --noreboot       - Does not reboot at the end"
        echo "    --medusa-only    - Rebuilds just medusa not the whole kernel"
        echo "    --build-only     - Just rebuid the kernel(modue) no reboot no installation"
        exit 0
}

function delete_medusa {
        sudo find security/medusa/ -name '*.o' -delete
        sudo find security/medusa/ -name '*.cmd' -delete
}

function medusa_only {
        if [[ `uname -a` != *medusa* ]]; then
                echo "Sorry you can use parameter --medusa-only only when running medusa kernel"
                help
                exit 0
        fi
        sudo make -j `expr $PROCESSORS + 1`

        [ $? -ne 0 ] && do_exit 1 "Medusa compilation failed"
}

function install_module {
        sudo cp arch/x86/boot/bzImage /boot/vmlinuz-`uname -r`
        [ $? -ne 0 ] && do_exit 1 "Copying of medusa module failed"

        sudo update-initramfs -u 
        [ $? -ne 0 ] && do_exit 1 "Update-initramfs failed"
}

function create_package {
        sudo rm -rf ../linux-image-*.deb
 
        export CONCURRENCY_LEVEL=`expr $PROCESSORS + 1`
        fakeroot make-kpkg --initrd --revision=1.2 kernel_image

        [ $? -ne 0 ] && do_exit 1 "Make-kpkg failed"
}

function rsync_repo {
        rsync -avz --exclude 'Documentation' --exclude '*.o' --exclude '.*' --exclude '*.cmd' --exclude '.git' --exclude '*.xz' --exclude '*ctags' -e ssh . $DEST 
}

function install_package {
        CONTINUE=1
        while [ $CONTINUE -ne 0 ]; do
                sudo DEBIAN_FRONTEND=noninteractive dpkg --force-all -i ../linux-image-*.deb
                CONTINUE=$?
                [ $CONTINUE -ne 0 ] && sleep 5;
        done
}

function update_grub {
        temp=`mktemp XXXXXX`

        sudo cat /boot/grub/grub.cfg | while read line; do
                if [[ "$line" = */boot/vmlinuz-*medusa* ]]; then
                        echo "$line" | sed -e 's/quiet/kgdboc=ttyS0,115200 kgdbwait/' >> $temp
                else
                        echo "$line" >> $temp
                fi
        done

        sudo mv $temp /boot/grub/grub.cfg

        rm $temp 2> /dev/null
}

parse_argv $@

[ -f vmlinux ] && sudo rm -f vmlinux 2> /dev/null

[ $DELETE -eq 1 ] && delete_medusa

if [ $MEDUSA_ONLY -eq 1 ]; then
        medusa_only
else
        create_package
fi

[ "$DEST" != "NONE" ] && rsync_repo

echo $(($major + 1)) > .major
echo 0 > .minor

if [ $INSTALL -eq 1 ]; then
        [ $MEDUSA_ONLY -eq 0 ] && install_package
        [ $MEDUSA_ONLY -ne 0 ] && install_module
fi

echo $major.$minor >> myversioning

[ $GRUB -eq 1 ] && update_grub

[ $REBOOT -eq 1 ] && sudo reboot

