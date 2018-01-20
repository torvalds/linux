#!/bin/bash
if [[ $UID -eq 0 ]];
then
  echo "do not run as root!"
  exit 1;
fi

if [[ -z "$(which arm-linux-gnueabihf-gcc)" ]];then echo "please install gcc-arm-linux-gnueabihf";exit 1;fi
if [[ -z "$(which mkimage)" ]];then echo "please install u-boot-tools";exit 1;fi

function prepare_SD
{
  SD=../SD
  cd $(dirname $0)
  mkdir -p ../SD
  echo "cleanup..."
  rm -r $SD/BPI-BOOT/ 2>/dev/null
  rm -r $SD/BPI-ROOT/ 2>/dev/null
  mkdir -p $SD/BPI-BOOT/bananapi/bpi-r2/linux/
  mkdir -p $SD/BPI-ROOT/lib/modules
  mkdir -p $SD/BPI-ROOT/etc/firmware
  mkdir -p $SD/BPI-ROOT/usr/bin
  mkdir -p $SD/BPI-ROOT/system/etc/firmware
  echo "copy..."
  export INSTALL_MOD_PATH=$SD/BPI-ROOT/;
  echo "INSTALL_MOD_PATH: $INSTALL_MOD_PATH"
  cp ./uImage $SD/BPI-BOOT/bananapi/bpi-r2/linux/uImage
  make modules_install
  #cp -r ../mod/lib/modules $SD/BPI-ROOT/lib/

  cp utils/wmt/config/* $SD/BPI-ROOT/system/etc/firmware/
  cp utils/wmt/src/{wmt_loader,wmt_loopback,stp_uart_launcher} $SD/BPI-ROOT/usr/bin/
  cp -r utils/wmt/firmware/* $SD/BPI-ROOT/etc/firmware/

  if [[ -n "$(grep 'CONFIG_MT76=' .config)" ]];then
    echo "MT76 set, including the firmware-files...";
    mkdir $SD/BPI-ROOT/lib/firmware/
    cp drivers/net/wireless/mediatek/mt76/firmware/* $SD/BPI-ROOT/lib/firmware/
  fi
}

if [[ -d drivers ]];
then
  action=$1
  LANG=C
  #git pull
  #git reset --hard v4.14
  CFLAGS=-j$(grep ^processor /proc/cpuinfo  | wc -l)
  #export INSTALL_MOD_PATH=$(dirname $(pwd))/mod/;
  export ARCH=arm;export CROSS_COMPILE=arm-linux-gnueabihf-
#  if [[ ! -z ${#INSTALL_MOD_PATH}  ]]; then
#    rm -r $INSTALL_MOD_PATH/lib/modules 2>/dev/null
#    #echo $INSTALL_MOD_PATH
#  fi

  if [[ "$action" == "reset" ]];
  then
    git reset --hard v4.14
    action=importconfig
  fi

  if [[ "$action" == "update" ]];
  then
    git pull
  fi

  if [[ "$action" == "defconfig" ]];
  then
    nano arch/arm/configs/mt7623n_evb_fwu_defconfig
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
    exec 3> >(tee build.log)
	export LOCALVERSION=
#    make --debug && make modules_install
    make ${CFLAGS} 2>&3 #&& make modules_install 2>&3
    ret=$?
#    set +x
    exec 3>&-
    if [[ $ret == 0 ]];
    then
      kernver=$(make kernelversion)
      gitbranch=$(git rev-parse --abbrev-ref HEAD)
      cat arch/arm/boot/zImage arch/arm/boot/dts/mt7623n-bananapi-bpi-r2.dtb > arch/arm/boot/zImage-dtb
      mkimage -A arm -O linux -T kernel -C none -a 80008000 -e 80008000 -n "Linux Kernel $kernver-$gitbranch" -d arch/arm/boot/zImage-dtb ./uImage
      echo "==========================================="
      echo -e "1) pack\n2) install to SD-Card"
      read -n1 -p "choice [12]:" choice
      echo
      if [[ "$choice" == "1" ]]; then
        prepare_SD
        echo "pack..."
        olddir=$(pwd)
        cd ../SD
        fname=bpi-r2_${kernver}_${gitbranch}.tar.gz
        tar -czf $fname BPI-BOOT BPI-ROOT
		md5sum $fname > $fname.md5
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
      else
        echo "wrong option: $choice"
      fi
    fi
  fi
fi
