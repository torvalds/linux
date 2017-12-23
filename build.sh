#!/bin/bash
if [[ $UID -eq 0 ]];
then
  echo "do not run as root!"
  exit 1;
fi

if [[ -z "$(which arm-linux-gnueabihf-gcc)" ]];then echo "please install gcc-arm-linux-gnueabihf";exit 1;fi
if [[ -z "$(which mkimage)" ]];then echo "please install u-boot-tools";exit 1;fi

if [[ -d drivers ]];
then
  action=$1

  #git pull
  #git reset --hard v4.14
  CFLAGS=-j$(grep ^processor /proc/cpuinfo  | wc -l)
  export INSTALL_MOD_PATH=$(dirname $(pwd))/mod/;export ARCH=arm;export CROSS_COMPILE=arm-linux-gnueabihf-
  if [[ ! -z ${#INSTALL_MOD_PATH}  ]]; then
    rm -r $INSTALL_MOD_PATH/lib/modules
    #echo $INSTALL_MOD_PATH
  fi

  if [[ "$action" == "reset" ]];
  then
    git reset --hard v4.14
    action=importconfig
  fi

  if [[ "$action" == "update" ]];
  then
    git pull
  fi

  if [[ "$action" == "importconfig" ]];
  then
#    cp ../mt7623n_evb_bpi_defconfig arch/arm/configs/
#    make mt7623n_evb_bpi_defconfig
#    cp ../mt7623n_evb_ryderlee_defconfig arch/arm/configs/
#	make mt7623n_evb_ryderlee_defconfig
#    cp ../mt7623n_evb_fwu_defconfig arch/arm/configs/
	make mt7623n_evb_fwu_defconfig
  fi

  if [[ "$action" == "config" ]];
  then
    make menuconfig
  fi

  if [[ -z "$action" ]];
  then
 #   set -x
#    make --debug && make modules_install
    make ${CFLAGS} && make modules_install
    ret=$?
#    set +x
    if [[ $ret == 0 ]];
    then
      cat arch/arm/boot/zImage arch/arm/boot/dts/mt7623n-bananapi-bpi-r2.dtb > arch/arm/boot/zImage-dtb
      mkimage -A arm -O linux -T kernel -C none -a 80008000 -e 80008000 -n "Linux Kernel $(git describe --tags)" -d arch/arm/boot/zImage-dtb ./uImage
      echo "==========================================="
      echo -e "1) pack\n2) install to SD-Card"
      read -n1 -p "choice [12]:" choice
      echo
      if [[ "$choice" == "1" ]]; then
        mkdir -p ../SD
        echo "cleanup..."
        rm -r ../SD/BPI-BOOT/
        rm -r ../SD/BPI-ROOT/
        mkdir -p ../SD/BPI-BOOT/bananapi/bpi-r2/linux/
        mkdir -p ../SD/BPI-ROOT/lib/modules
        mkdir -p ../SD/BPI-ROOT/etc/firmware
        mkdir -p ../SD/BPI-ROOT/usr/bin
        mkdir -p ../SD/BPI-ROOT/system/etc/firmware
        echo "copy..."
        cp ./uImage ../SD/BPI-BOOT/bananapi/bpi-r2/linux/uImage
        cp -r ../mod/lib/modules ../SD/BPI-ROOT/lib/

        cp utils/wmt/config/* ../SD/BPI-ROOT/system/etc/firmware/
        cp utils/wmt/src/{wmt_loader,wmt_loopback,stp_uart_launcher} ../SD/BPI-ROOT/usr/bin/
        cp -r utils/wmt/firmware/* ../SD/BPI-ROOT/etc/firmware/

        echo "pack..."
        kernver=$(make kernelversion)
        gitbranch=$(git rev-parse --abbrev-ref HEAD)
        olddir=$(pwd)
        cd ../SD
        fname=bpi-r2_${kernver}_${gitbranch}.tar.gz
        tar -czf $fname BPI-BOOT BPI-ROOT
        ls -lh $(pwd)"/"$fname
        cd $olddir
      elif [[ "$choice" == "2" ]];then
        read -p "Press [enter] to copy data to SD-Card..."
        if  [[ -d /media/$USER/BPI-BOOT ]];
        then
		  kernelfile=/media/$USER/BPI-BOOT/bananapi/bpi-r2/linux/uImage
          if [[ -e $kernelfile ]];then
		    echo "backup of kernel: $kernelfile.bak"
		    cp $kernelfile $kernelfile.bak
		  fi
		  echo "copy new kernel"
          cp ./uImage /media/$USER/BPI-BOOT/bananapi/bpi-r2/linux/uImage
		  echo "copy modules (root needed because of ext-fs permission)"
          sudo cp -r ../mod/lib/modules /media/$USER/BPI-ROOT/lib/
          sync
        else
          echo "SD-Card not found!"
        fi
      else
        echo "wrong option: $choice"
      fi
    fi
  fi
fi
