#!/bin/bash


if [ ! -f .dest ]; then
        echo -n "Prosim zadaj adresu kam sa maju kopirovat zdrojaky (NONE pre nikam): "
        read DEST
        echo $DEST > .dest
fi
DEST=`cat .dest`

minor=0
major=0
GRUB=1
REBOOT=1
MEDUSA_ONLY=1

[ -f .minor ] && minor=`cat .minor`
[ -f .major ] && major=`cat .major`
echo $(($minor + 1)) > .minor

for arg in "$@"; do
        if [[ "$arg" == '--delete' || "$arg" == '-delete' ]]; then
                sudo find security/ -name '*.o' -delete
                sudo find security/ -name '*.cmd' -delete
        elif [[ "$arg" == '--clean' || "$arg" == '-clean' ]]; then
                sudo make clean
                sudo rm -rf debian
        elif [[ "$arg" == '--nogdb' || "$arg" == '-nogdb' || "$arg" == '--nogrub' || "$arg" == '-nogrub' ]]; then
                GRUB=0
        elif [[ "$arg" == '--noreboot' || "$arg" == '-noreboot' ]]; then
                REBOOT=0
        elif [[ "$arg" == '--medusa-only' || "$arg" == '-medusa-only' ]]; then
                MEDUSA_ONLY=1
                GRUB=0
        elif [[ "$arg" == '-h' || "$arg" == '--help' || "$arg" == '-help' ]]; then
                echo "$0 [--help] [--delete] [--clean] [--nogrub] [--noreboot] [--medusa-only] [--nogdb]";
                exit 0
        else
                echo "Error unknown parameter"
        fi
done
sudo rm vmlinux 2> /dev/null

#make -j4

#[ $? -ne 0 ] && exit 1
PROCESSORS=`cat /proc/cpuinfo | grep processor | wc -l`

if [ $MEDUSA_ONLY -eq 1 ]; then
        sudo find security/medusa/ -name '*.o' -delete
        sudo find security/medusa/ -name '*.cmd' -delete
        sudo make -j `expr $PROCESSORS + 1`
        [ $? -ne 0 ] && exit 1
        sudo cp arch/x86/boot/bzImage /boot/vmlinuz-`uname -r`
        [ $? -ne 0 ] && exit 1

        sudo update-initramfs -u 
        [ $? -ne 0 ] && exit 1
else
        sudo rm -rf ../linux-image-*.deb
        
        export CONCURRENCY_LEVEL=`expr $PROCESSORS + 1`
        #export CLEAN_SOURCE=no
        #fakeroot make-kpkg --initrd --revision=1.2 --append_to_version medusa kernel_image
        fakeroot make-kpkg --initrd --revision=1.2 kernel_image

        [ $? -ne 0 ] && exit 1
fi

PID=0
if [ "$DEST" != "NONE" ]; then
        rsync -avz --exclude 'Documentation' --exclude '*.o' --exclude '.*' --exclude '*.cmd' --exclude '.git' --exclude '*.xz' -e ssh . $DEST 
fi

echo $(($major + 1)) > .major
echo 0 > .minor

CONTINUE=1
if [ $MEDUSA_ONLY -ne 1 ]; then
        while [ $CONTINUE -ne 0 ]; do
                sudo dpkg --force-all -i ../linux-image-*.deb
                CONTINUE=$?
                [ $CONTINUE -ne 0 ] && sleep 5;
        done
fi

# [ $? -ne 0 ] && exit 1
echo $major.$minor >> myversioning

temp=`mktemp XXXXXX`
sudo cat /boot/grub/grub.cfg | while read line; do
        if [[ "$line" = */boot/vmlinuz-*medusa* ]]; then
                if [ $GRUB == 1 ]; then
                        echo "$line" | sed -e 's/quiet/kgdboc=ttyS0,115200 kgdbwait/' >> $temp
                else
                        echo "$line" | sed -e 's/quiet/kgdboc=ttyS0,115200/' >> $temp
                fi
        else
                echo "$line" >> $temp
        fi
done

sudo mv $temp /boot/grub/grub.cfg

rm $temp 2> /dev/null

#if [ "$DEST" != "NONE" ]; then
#        wait $PID
#fi

[ $REBOOT == 1 ] && sudo reboot

