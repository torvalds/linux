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

	export ARCH=arm;export CROSS_COMPILE='ccache arm-linux-gnueabihf-'
	crosscompile=1
fi;

#Check Dependencies
PACKAGE_Error=0
for package in "u-boot-tools" "bc" "make" "gcc" "libc6-dev" "libncurses5-dev" "libssl-dev" "fakeroot" "ccache"; do
	TESTPKG=$(dpkg -l |grep "\s${package}")
	if [[ -z "${TESTPKG}" ]];then echo "please install ${package}";PACKAGE_Error=1;fi
done
if [ ${PACKAGE_Error} == 1 ]; then exit 1; fi

kernver=$(make kernelversion)
kernbranch=$(git rev-parse --abbrev-ref HEAD)
gitbranch=$(git rev-parse --abbrev-ref HEAD|sed 's/^4\.[0-9]\+-//')

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

function install {
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
			echo "syncing sd-card...this will take a while"
			sync

			kernelname=$(ls -1t $INSTALL_MOD_PATH"/lib/modules" | head -n 1)
			EXTRA_MODULE_PATH=$INSTALL_MOD_PATH"/lib/modules/"$kernelname"/kernel/extras"
			#echo $kernelname" - "${EXTRA_MODULE_PATH}
			CRYPTODEV="cryptodev/cryptodev-linux/cryptodev.ko"
			if [ -e "${CRYPTODEV}" ]; then
				echo Copy CryptoDev
				sudo mkdir -p "${EXTRA_MODULE_PATH}"
				sudo cp "${CRYPTODEV}" "${EXTRA_MODULE_PATH}"
	        	#Build Module Dependencies
				sudo /sbin/depmod -b $INSTALL_MOD_PATH ${kernelname}
			fi

			#sudo cp -r ../mod/lib/modules /media/$USER/BPI-ROOT/lib/
			if [[ -n "$(grep 'CONFIG_MT76=' .config)" ]];then
				echo "MT76 set,don't forget the firmware-files...";
			fi
		else
			echo "SD-Card not found!"
		fi
	fi
}

function deb {
#set -x
  ver=${kernver}-bpi-r2-${gitbranch}
  echo "deb package ${ver}"
  prepare_SD

#    cd ../SD
#    fname=bpi-r2_${kernver}_${gitbranch}.tar.gz
#    tar -cz --owner=root --group=root -f $fname BPI-BOOT BPI-ROOT

  mkdir -p debian/bananapi-r2-image/boot/bananapi/bpi-r2/linux/
  mkdir -p debian/bananapi-r2-image/lib/modules/
  mkdir -p debian/bananapi-r2-image/DEBIAN/
  rm debian/bananapi-r2-image/boot/bananapi/bpi-r2/linux/*
  rm -rf debian/bananapi-r2-image/lib/modules/*

  #sudo mount --bind ../SD/BPI-ROOT/lib/modules debian/bananapi-r2-image/lib/modules/
  if test -e ./uImage && test -d ../SD/BPI-ROOT/lib/modules/${ver}; then
    cp ./uImage debian/bananapi-r2-image/boot/bananapi/bpi-r2/linux/uImage_${ver}
#    pwd
    cp -r ../SD/BPI-ROOT/lib/modules/${ver} debian/bananapi-r2-image/lib/modules/
    #rm debian/bananapi-r2-image/lib/modules/${ver}/{build,source}
    #mkdir debian/bananapi-r2-image/lib/modules/${ver}/kernel/extras
    #cp cryptodev-linux/cryptodev.ko debian/bananapi-r2-image/lib/modules/${ver}/kernel/extras
    cat > debian/bananapi-r2-image/DEBIAN/control << EOF
Package: bananapi-r2-image-${kernbranch}
Version: ${kernver}-1
Section: custom
Priority: optional
Architecture: armhf
Multi-Arch: no
Essential: no
Maintainer: Frank Wunderlich
Description: BPI-R2 linux image ${ver}
EOF
    cd debian
    fakeroot dpkg-deb --build bananapi-r2-image ../debian
    cd ..
    ls -lh debian/*.deb
    dpkg -c debian/bananapi-r2-image-${kernbranch,,}_${kernver}-1_armhf.deb
  else
    echo "First build kernel ${ver}"
    echo "eg: ./build"
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

	#Add CryptoDev Module if exists or Blacklist
	CRYPTODEV="cryptodev/cryptodev-linux/cryptodev.ko"
	mkdir -p "${INSTALL_MOD_PATH}/etc/modules-load.d"

	LOCALVERSION=$(find ../SD/BPI-ROOT/lib/modules/* -maxdepth 0 -type d |rev|cut -d"/" -f1 | rev)
	EXTRA_MODUL_PATH="${SD}/BPI-ROOT/lib/modules/${LOCALVERSION}/kernel/extras"
	if [ -e "${CRYPTODEV}" ]; then
		echo Copy CryptoDev
		mkdir -p "${EXTRA_MODUL_PATH}"
		cp "${CRYPTODEV}" "${EXTRA_MODUL_PATH}"
		#Load Cryptodev on BOOT
		echo  "cryptodev" >${INSTALL_MOD_PATH}/etc/modules-load.d/cryptodev.conf

		#Build Module Dependencies
		/sbin/depmod -b "${SD}/BPI-ROOT/" ${LOCALVERSION}
	else
		#Blacklist Cryptodev Module
		echo "blacklist cryptodev" >${INSTALL_MOD_PATH}/etc/modules-load.d/cryptodev.conf
	fi

	cp utils/wmt/config/* $SD/BPI-ROOT/system/etc/firmware/
	cp utils/wmt/src/{wmt_loader,wmt_loopback,stp_uart_launcher} $SD/BPI-ROOT/usr/bin/
	cp -r utils/wmt/firmware/* $SD/BPI-ROOT/etc/firmware/

	if [[ -n "$(grep 'CONFIG_MT76=' .config)" ]];then
		echo "MT76 set, including the firmware-files...";
		cp drivers/net/wireless/mediatek/mt76/firmware/* $SD/BPI-ROOT/lib/firmware/
	fi
}

#Test if the Kernel is there
if [ -n "$kernver" ]; then
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
		"deb")
			echo "deb-package (currently testing-state)"
			deb
			;;
		"dtsi")
			echo "Edit mt7623.dtsi"
			nano arch/arm/boot/dts/mt7623.dtsi
			;;

		"dts")
			echo "Edit mt7623n-bpi.dts"
			nano arch/arm/boot/dts/mt7623n-bananapi-bpi-r2.dts
			;;

		"importmylconfig")
			echo "Import myl config"
			make mt7623n_myl_defconfig
			;;


		"importconfig")
			echo "Import fwu config"
			make mt7623n_evb_fwu_defconfig
			;;

	  	"config")
			make menuconfig
			;;

		"pack")
			echo "Pack Kernel to Archive"
			pack
			;;

		"install")
			echo "Install Kernel to SD Card"
			install
			;;

		"build")
			echo "Build Kernel"
			build
			#$0 cryptodev
			;;

		"spidev")
			echo "Build SPIDEV-Test"
			(
				cd tools/spi;
				make #CROSS_COMPILE=arm-linux-gnueabihf-
			)
			;;

		"cryptodev")
			echo "Build CryptoDev"
			cryptodev/build.sh
			;;

		"utils")
			echo "Build utils"
			( cd utils; make )
			;;

		"all-pack")
			echo "Update Repo, Create Kernel & Build Archive"
			$0 update
			$0 importconfig
			$0 build
			$0 cryptodev
			$0 pack
			;;

		*)
			if [[ -n "$action" ]];then
				echo "unknown command $action";
				exit 1;
			fi;
			$0 build
			$0 cryptodev
			if [ -e "./uImage" ]; then
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
					$0 install
				else
					echo "wrong option: $choice"
				fi
			fi
 			;;
	esac
fi
