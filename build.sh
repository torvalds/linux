#!/bin/bash
if [[ $UID -eq 0 ]];
then
  echo "do not run as root!"
  exit 1;
fi

#Check Crosscompile
crosscompile=0
if [[ -z $(cat /proc/cpuinfo | grep -i 'model name.*ArmV7') ]]; then
	if [[ -z "$(which arm-linux-gnueabihf-gcc)" ]];then echo "please install gcc-arm-linux-gnueabihf";exit 1;fi

	export ARCH=arm;export CROSS_COMPILE=arm-linux-gnueabihf-
	crosscompile=1
fi;

#Check Dependencies
PACKAGE_Error=0
for package in "u-boot-tools" "bc" "make" "gcc" "libc6-dev" "libncurses5-dev"; do
	if [[ -z "$(dpkg -l |grep ${package})" ]];then echo "please install ${package}";PACKAGE_Error=1;fi
done
if [ ${PACKAGE_Error} == 1 ]; then exit 1; fi

kernver=$(make kernelversion)
gitbranch=$(git rev-parse --abbrev-ref HEAD)

function pack {
	prepare_SD
	echo "pack..."
	olddir=$(pwd)
	cd ../SD
	fname=bpi-r2_${kernver}_${gitbranch}.tar.gz
	tar -cz --owner=root --group=root -f $fname BPI-BOOT BPI-ROOT
	md5sum $fname > $fname.md5
	ls -lh $(pwd)"/"$fname
	cd $olddir
}

function installToSD {
	if [[ $crosscompile -eq 0 ]]; then
		kernelfile=/boot/bananapi/bpi-r2/linux/uImage
		if [[ -e $kernelfile ]];then
			echo "backup of kernel: $kernelfile.bak"
			cp $kernelfile $kernelfile.bak
			cp ./uImage $kernelfile
			sudo make modules_install
		else
			echo "actual Kernel not found...is /boot mounted?"
		fi
	else
		read -p "Press [enter] to copy data to SD-Card..."
		if  [[ -d /media/$USER/BPI-BOOT ]]; then
			kernelfile=/media/$USER/BPI-BOOT/bananapi/bpi-r2/linux/uImage
			if [[ -e $kernelfile ]];then
				echo "backup of kernel: $kernelfile.bak"
				cp $kernelfile $kernelfile.bak
			fi
			echo "copy new kernel"
			cp ./uImage /media/$USER/BPI-BOOT/bananapi/bpi-r2/linux/uImage
			echo "copy modules (root needed because of ext-fs permission)"
			export INSTALL_MOD_PATH=/media/$USER/BPI-ROOT/;
			echo "INSTALL_MOD_PATH: $INSTALL_MOD_PATH"
			sudo make ARCH=$ARCH INSTALL_MOD_PATH=$INSTALL_MOD_PATH modules_install
			#sudo cp -r ../mod/lib/modules /media/$USER/BPI-ROOT/lib/
			if [[ -n "$(grep 'CONFIG_MT76=' .config)" ]];then
				echo "MT76 set,don't forget the firmware-files...";
			fi
			sync
		else
			echo "SD-Card not found!"
		fi
	fi
}

function build {
	if [ -e ".config" ]; then
		echo Cleanup Kernel Build
		rm arch/arm/boot/zImage-dtb 2>/dev/null
		rm ./uImage 2>/dev/null

		exec 3> >(tee build.log)
		export LOCALVERSION="-${gitbranch}"
		make ${CFLAGS} 2>&3 #&& make modules_install 2>&3
		ret=$?
		exec 3>&-

		if [[ $ret == 0 ]]; then
			cat arch/arm/boot/zImage arch/arm/boot/dts/mt7623n-bananapi-bpi-r2.dtb > arch/arm/boot/zImage-dtb
			mkimage -A arm -O linux -T kernel -C none -a 80008000 -e 80008000 -n "Linux Kernel $kernver-$gitbranch" -d arch/arm/boot/zImage-dtb ./uImage
		fi
	else
		echo "No Configfile found, Please Configure Kernel"
	fi
}

function prepare_SD {
	SD=../SD
	cd $(dirname $0)
	mkdir -p ../SD >/dev/null 2>/dev/null

	echo "cleanup..."
	for toDel in "$SD/BPI-BOOT/" "$SD/BPI-ROOT/"; do
		rm -r ${toDel} 2>/dev/null
	done
	for createDir in "$SD/BPI-BOOT/bananapi/bpi-r2/linux/" "$SD/BPI-ROOT/lib/modules" "$SD/BPI-ROOT/etc/firmware" "$SD/BPI-ROOT/usr/bin" "$SD/BPI-ROOT/system/etc/firmware" "$SD/BPI-ROOT/lib/firmware"; do
		mkdir -p ${createDir} >/dev/null 2>/dev/null
	done

	echo "copy..."
	export INSTALL_MOD_PATH=$SD/BPI-ROOT/;
	echo "INSTALL_MOD_PATH: $INSTALL_MOD_PATH"
	cp ./uImage $SD/BPI-BOOT/bananapi/bpi-r2/linux/uImage
	make modules_install

	cp utils/wmt/config/* $SD/BPI-ROOT/system/etc/firmware/
	cp utils/wmt/src/{wmt_loader,wmt_loopback,stp_uart_launcher} $SD/BPI-ROOT/usr/bin/
	cp -r utils/wmt/firmware/* $SD/BPI-ROOT/etc/firmware/

	if [[ -n "$(grep 'CONFIG_MT76=' .config)" ]];then
		echo "MT76 set, including the firmware-files...";
		cp drivers/net/wireless/mediatek/mt76/firmware/* $SD/BPI-ROOT/lib/firmware/
	fi
}

#Test if the Kernel is there
if [ -n "$(make kernelversion)" ]; then
	action=$1
	LANG=C
	CFLAGS=-j$(grep ^processor /proc/cpuinfo  | wc -l)

	case "$action" in
		"reset")
			echo "Reset Git"
			##Reset Git
	    		git reset --hard HEAD
			#call self and Import Config
    			$0 importconfig
			;;

		"update")
			echo "Update Git Repo"
    			git pull
			;;

  		"umount")
			echo "Umount SD Media"
			umount /media/$USER/BPI-BOOT
			umount /media/$USER/BPI-ROOT
			;;

		"defconfig")
			echo "Edit def config"
			nano arch/arm/configs/mt7623n_evb_fwu_defconfig
			;;

		"importconfig")
			echo "Importiere config"
			make mt7623n_evb_fwu_defconfig
			;;

	  	"config")
			make menuconfig
			;;

		"pack")
			echo "Pack Kernel to Archive"
			pack
			;;

		"installToSD")
			echo "Install Kernel to SD Card"
			installToSD
			;;

		"build")
			echo "Build Kernel"
			build
			;;

		"all-pack")
			echo "Update Repo, Create Kernel & Build Archive"
			$0 update
			$0 importconfig
			$0 build
			$0 pack
			;;

		*)
			$0 build
			if [ -e "arch/arm/boot/zImage-dtb" ] && [ -e "./uImage" ]; then
				echo "==========================================="
				echo "1) pack"
				if [[ $crosscompile -eq 0 ]];then
					echo "2) install to System"
				else
					echo "2) install to SD-Card"
				fi;
				read -n1 -p "choice [12]:" choice
				echo
				if [[ "$choice" == "1" ]]; then
					$0 pack
				elif [[ "$choice" == "2" ]];then
					$0 installToSD
				else
					echo "wrong option: $choice"
				fi
			fi
 			;;
	esac
fi
