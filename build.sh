#!/bin/bash
if [ $UID -eq 0 ];
then
  echo "This should not be run as root."
  echo "Hit enter key to force or press CTRL+C to abort."
  read -s
  echo "[ OK ] Proceeding now."
fi

clr_red=$'\e[1;31m'
clr_green=$'\e[1;32m'
clr_yellow=$'\e[1;33m'
clr_blue=$'\e[1;34m'
clr_reset=$'\e[0m'

#Check Crosscompile
crosscompile=0
if [[ -z $(cat /proc/cpuinfo | grep -i 'model name.*ArmV7') ]]; then
	if [[ -z "$(which arm-linux-gnueabihf-gcc)" ]];then echo "please install gcc-arm-linux-gnueabihf";exit 1;fi

	CCVER=$(arm-linux-gnueabihf-gcc --version |grep arm| sed -e 's/^.* \([0-9]\.[0-9-]\).*$/\1/')
#	if [[ $CCVER =~ ^7 ]]; then
#		echo "arm-linux-gnueabihf-gcc version 7 currently not supported";exit 1;
#	fi

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
#kernbranch=$(git rev-parse --abbrev-ref HEAD)
kernbranch=$(git branch --contains $(git log -n 1 --pretty='%h') | grep -v '(HEAD' | head -1 | sed 's/^..//')
gitbranch=$(echo $kernbranch|sed 's/^4\.[0-9]\+-//')

function increase_kernel {
        #echo $kernver
        old_IFS=$IFS
        IFS='.'
        read -ra KV <<< "$kernver"
        IFS=','
        newkernver=${KV[0]}"."${KV[1]}"."$(( ${KV[2]} +1 ))
        echo $newkernver
}

function update_kernel_source {
        changedfiles=$(git diff --name-only)
        if [[ -z "$changedfiles" ]]; then
        git fetch stable
        ret=$?
        if [[ $ret -eq 0 ]];then
                newkernver=$(increase_kernel)
                echo "newkernver:$newkernver"
                git merge v$newkernver
        elif [[ $ret -eq 128 ]];then
                #repo not found
                git remote add stable https://git.kernel.org/pub/scm/linux/kern$
        fi
        else
                echo "please first commit/stash modified files: $changedfiles"
        fi
}

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

	imagename="uImage_${kernver}-${gitbranch}"
	read -e -i $imagename -p "uImage-filename: " input
	imagename="${input:-$imagename}"

	echo "Name: $imagename"

	if [[ $crosscompile -eq 0 ]]; then
		kernelfile=/boot/bananapi/bpi-r2/linux/$imagename
		if [[ -e $kernelfile ]];then
			echo "backup of kernel: $kernelfile.bak"
			cp $kernelfile $kernelfile.bak
			cp ./uImage $kernelfile
			sudo make modules_install
		else
			echo "Actual kernel not found..."
			echo "is /boot mounted?"
		fi
	else
		echo "By default this kernel-file will be loaded (uEnv.txt):"
		grep '^kernel=' /media/${USER}/BPI-BOOT/bananapi/bpi-r2/linux/uEnv.txt|tail -1
		read -p "Press [enter] to copy data to SD-Card..."
		if  [[ -d /media/$USER/BPI-BOOT ]]; then
			kernelfile=/media/$USER/BPI-BOOT/bananapi/bpi-r2/linux/$imagename
			if [[ -e $kernelfile ]];then
				echo "backup of kernel: $kernelfile.bak"
				cp $kernelfile $kernelfile.bak
			fi
			echo "copy new kernel"
			cp ./uImage $kernelfile
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
  uimagename=uImage_${kernver}-${gitbranch}
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
    cp ./uImage debian/bananapi-r2-image/boot/bananapi/bpi-r2/linux/${uimagename}
#    pwd
    cp -r ../SD/BPI-ROOT/lib/modules/${ver} debian/bananapi-r2-image/lib/modules/
    #rm debian/bananapi-r2-image/lib/modules/${ver}/{build,source}
    #mkdir debian/bananapi-r2-image/lib/modules/${ver}/kernel/extras
    #cp cryptodev-linux/cryptodev.ko debian/bananapi-r2-image/lib/modules/${ver}/kernel/extras
	cat > debian/bananapi-r2-image/DEBIAN/preinst << EOF
#!/bin/bash
clr_red=\$'\e[1;31m'
clr_green=\$'\e[1;32m'
clr_yellow=\$'\e[1;33m'
clr_blue=\$'\e[1;34m'
clr_reset=\$'\e[0m'
m=\$(mount | grep '/boot[^/]')
if [[ -z "\$m" ]];
then
	echo "\${clr_red}/boot needs to be mountpoint for /dev/mmcblk0p1\${clr_reset}";
	exit 1;
fi
kernelfile=/boot/bananapi/bpi-r2/linux/${uimagename}
if [[ -e "\${kernelfile}" ]];then
	echo "\${clr_red}\${kernelfile} already exists\${clr_reset}"
	echo "\${clr_red}please remove/rename it or uninstall previous installed kernel-package\${clr_reset}"
	exit 2;
fi
EOF
	chmod +x debian/bananapi-r2-image/DEBIAN/preinst
	cat > debian/bananapi-r2-image/DEBIAN/postinst << EOF
#!/bin/bash
clr_red=\$'\e[1;31m'
clr_green=\$'\e[1;32m'
clr_yellow=\$'\e[1;33m'
clr_blue=\$'\e[1;34m'
clr_reset=\$'\e[0m'
case "\$1" in
	configure)
	#install|upgrade)
		echo "kernel=${uimagename}">>/boot/bananapi/bpi-r2/linux/uEnv.txt

		#check for non-dsa-kernel (4.4.x)
		kernver=\$(uname -r)
		if [[ "\${kernver:0:3}" == "4.4" ]];
		then
			echo "\${clr_yellow}you are upgrading from kernel 4.4.\${clr_reset}";
			echo "\${clr_yellow}Please make sure your network-config (/etc/network/interfaces) matches dsa-driver\${clr_reset}";
			echo "\${clr_yellow}(bring cpu-ports ethx up, ip-configuration to wan/lanx)\${clr_reset}";
		fi
	;;
	*) echo "unhandled \$1 in postinst-script"
esac
EOF
	chmod +x debian/bananapi-r2-image/DEBIAN/postinst
	cat > debian/bananapi-r2-image/DEBIAN/postrm << EOF
#!/bin/bash
case "\$1" in
	abort-install)
		echo "installation aborted"
	;;
	remove|purge)
		cp /boot/bananapi/bpi-r2/linux/uEnv.txt /boot/bananapi/bpi-r2/linux/uEnv.txt.bak
		grep -v  ${uimagename} /boot/bananapi/bpi-r2/linux/uEnv.txt.bak > /boot/bananapi/bpi-r2/linux/uEnv.txt
	;;
esac
EOF
	chmod +x debian/bananapi-r2-image/DEBIAN/postrm
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
    debfile=debian/bananapi-r2-image-${kernbranch,,}_${kernver}-1_armhf.deb
    dpkg -c $debfile

	dpkg -I $debfile
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
		echo "No configfile found, please configure kernel first."
	fi
}

function prepare_SD {
	SD=../SD
	cd $(dirname $0)
	mkdir -p ../SD >/dev/null 2>/dev/null

	echo "Cleaning up..."
	for toDel in "$SD/BPI-BOOT/" "$SD/BPI-ROOT/"; do
		rm -r ${toDel} 2>/dev/null
	done
	for createDir in "$SD/BPI-BOOT/bananapi/bpi-r2/linux/" "$SD/BPI-ROOT/lib/modules" "$SD/BPI-ROOT/etc/firmware" "$SD/BPI-ROOT/usr/bin" "$SD/BPI-ROOT/system/etc/firmware" "$SD/BPI-ROOT/lib/firmware"; do
		mkdir -p ${createDir} >/dev/null 2>/dev/null
	done

	echo "Copying..."
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

function release
{
	lc=$(git log -n 1 --pretty=format:'%s')
	reltag="Release_${kernver}"
	if [[ ${lc} =~ ^Merge ]];
	then
		echo Merge;
	else
		echo "normal commit";
		reltag="${reltag}_${lc}"
	fi
	echo "RelTag:"$reltag
	if [[ -z "$(git tag -l $reltag)" ]]; then
	git tag $reltag
	git push origin $reltag
	else
		echo "Tag already used, please use another tag."
	fi
}

#Test if the Kernel is there
if [ -n "$kernver" ]; then
	action=$1
	file=$2
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

		"updatesrc")
			echo "Update kernel source"
			update_kernel_source
			;;

 		"umount")
			echo "umount SD Media"
			umount /media/$USER/BPI-BOOT
			umount /media/$USER/BPI-ROOT
			;;

 		"uenv")
			echo "edit uEnv.txt on sd-card"
			nano /media/$USER/BPI-BOOT/bananapi/bpi-r2/linux/uEnv.txt
			;;

		"defconfig")
			echo "edit def config"
			nano arch/arm/configs/mt7623n_evb_fwu_defconfig
			;;
		"deb")
			echo "deb-package (currently testing-state)"
			deb
			;;
		"dtsi")
			echo "edit mt7623.dtsi"
			nano arch/arm/boot/dts/mt7623.dtsi
			;;

		"dts")
			echo "edit mt7623n-bpi.dts"
			nano arch/arm/boot/dts/mt7623n-bananapi-bpi-r2.dts
			;;

		"importmylconfig")
			echo "import myl config"
			make mt7623n_myl_defconfig
			;;


		"importconfig")
			echo "import a defconfig file"
			if [[ -z "$file" ]];then
				echo "Import fwu config"
				make mt7623n_evb_fwu_defconfig
			else
				f=mt7623n_${file}_defconfig
				echo "Import config: $f"
				if [[ -e "arch/arm/configs/${f}" ]];then
					make ${f}
				else
					echo "file not found"
				fi
			fi
			;;

		"ic")
			echo "menu for multiple conf-files...currently in developement"
			files=();
			i=1;
			for f in $(cd arch/arm/configs/; ls mt7623n*defconfig)
			do
				echo "[$i] $f"
				files+=($f)
				i=$(($i+1))
			done
			read -n1 -p "choice [1..${#files[@]}]:" choice
			echo
			set -x
			make ${files[$(($choice-1))]}
			set +x
		;;
	  	"config")
			echo "change kernel-configuration (menuconfig)"
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

		"release")
			echo "create release tag for travis-ci build"
			release
			;;
		"all-pack")
			echo "update repo, create kernel & build archive"
			$0 update
			$0 importconfig
			$0 build
			$0 cryptodev
			$0 pack
			;;

		"help")
			echo "print help"
			sed -n -e '/case "$action" in/,/esac/{//!p}'  $0 | grep -A1 '")$' | sed -e 's/echo "\(.*\)"/\1/'
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
				echo "3) deb-package"
				read -n1 -p "choice [123]:" choice
				echo
				if [[ "$choice" == "1" ]]; then
					$0 pack
				elif [[ "$choice" == "2" ]];then
					$0 install
				elif [[ "$choice" == "3" ]];then
					$0 deb
				else
					echo "wrong option: $choice"
				fi
			fi
 			;;
	esac
fi
