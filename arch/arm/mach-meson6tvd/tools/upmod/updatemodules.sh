#! /bin/bash

#
# Notice:
# 	This scripit will get and update all the modules code of kernel.
#


# cd the parent directory of common and hardware
cd ../../../../../../

if [ ! -d "common/" ]; then
	echo "Note: <work-directory> should be the parent directory of common"
	exit 1
fi


################################################################################
#
# mali
#
################################################################################

if [ ! -d "hardware/arm/gpu" ]; then
	mkdir -p hardware/arm/
	cd hardware/arm/
	git clone git://git-sc.amlogic.com/platform/hardware/arm/gpu.git
	cd gpu/
	git checkout -t origin/jb-mr1-amlogic-m6tvd -b jb-mr1-amlogic-m6tvd
else
	cd hardware/arm/gpu
	echo "updating hardware/arm/gpu"
	git pull
fi


# cd <work-directory>
cd ../../../
echo ""



################################################################################
#
# tvin
#
################################################################################

if [ ! -d "hardware/tvin" ]; then
	mkdir -p hardware/
	cd hardware/
	git clone ssh://gituser@git-sc.amlogic.com/linux/amlogic/tvin.git
	cd tvin/
	git checkout -t origin/amlogic-3.10-bringup -b amlogic-3.10-bringup
else
	cd hardware/tvin
	echo "updating hardware/tvin"
	git pull
fi


# cd <work-directory>
cd ../../
echo ""



################################################################################
#
# dvb
#
################################################################################


if [ ! -d "hardware/dvb/altobeam/drivers/atbm887x" ]; then
	mkdir -p hardware/dvb/altobeam/drivers/
	cd hardware/dvb/altobeam/drivers/
	git clone ssh://android@10.8.9.5/linux/dvb/altobeam/drivers/atbm887x.git
	cd atbm887x
	git checkout -t origin/amlogic-3.10 -b amlogic-3.10
else
	cd hardware/dvb/altobeam/drivers/atbm887x
	echo "updating hardware/dvb/altobeam/drivers/atbm887x"
	git pull
fi


# cd <work-directory>
cd ../../../../../
echo ""


if [ ! -d "hardware/dvb/silabs/drivers/si2177" ]; then
	mkdir -p hardware/dvb/silabs/drivers/
	cd hardware/dvb/silabs/drivers/
	git clone ssh://android@10.8.9.5/linux/dvb/silabs/drivers/si2177.git
	cd si2177
	git checkout -t origin/amlogic-3.10 -b amlogic-3.10
else
	cd hardware/dvb/silabs/drivers/si2177
	echo "updating hardware/dvb/silabs/drivers/si2177"
	git pull
fi


# cd <work-directory>
cd ../../../../../
echo ""


if [ ! -d "hardware/dvb/availink/drivers/avl6211" ]; then
	mkdir -p hardware/dvb/availink/drivers/
	cd hardware/dvb/availink/drivers/
	git clone ssh://android@10.8.9.5/linux/dvb/availink/drivers/avl6211.git
	cd avl6211/
	git checkout -t origin/amlogic-3.10 -b amlogic-3.10
else
	cd hardware/dvb/availink/drivers/avl6211
	echo "updating hardware/dvb/availink/drivers/avl6211"
	git pull
fi


# cd <work-directory>
cd ../../../../../
echo ""



################################################################################
#
# nand
#
################################################################################

if [ ! -d "hardware/amlogic/nand" ]; then
	mkdir -p hardware/amlogic
	cd hardware/amlogic
	git clone git://git-sc.amlogic.com/platform/hardware/amlogic/nand.git
	cd nand/
	git checkout -t origin/amlogic-nand -b amlogic-nand
else
	cd hardware/amlogic/nand
	echo "updating hardware/amlogic/nand"
	git pull
fi


# cd <work-directory>
cd ../../../
echo ""



################################################################################
#
# pmu
#
################################################################################

if [ ! -d "hardware/amlogic/pmu" ]; then
	mkdir -p hardware/amlogic
	cd hardware/amlogic
	git clone git://git-sc.amlogic.com/platform/hardware/amlogic/pmu.git
	cd pmu/
else
	cd hardware/amlogic/pmu
	echo "updating hardware/amlogic/pmu"
	git pull
fi


# cd <work-directory>
cd ../../../
echo ""


################################################################################
#
# wifi broadcom
#
################################################################################

if [ ! -d "hardware/wifi/broadcom/drivers/ap6xxx" ]; then
	mkdir -p hardware/wifi/broadcom/drivers/
	cd hardware/wifi/broadcom/drivers/
	git clone git://git-sc.amlogic.com/platform/hardware/wifi/broadcom/drivers/ap6xxx.git
	cd ap6xxx/
	git checkout -t origin/ap6xxx -b ap6xxx
else
	cd hardware/wifi/broadcom/drivers/ap6xxx
	echo "updating hardware/wifi/broadcom/drivers/ap6xxx"
	git pull
fi


# cd <work-directory>
cd ../../../../../
echo ""


if [ ! -d "hardware/wifi/broadcom/drivers/usi" ]; then
	mkdir -p hardware/wifi/broadcom/drivers/
	cd hardware/wifi/broadcom/drivers/
	git clone git://git-sc.amlogic.com/platform/hardware/wifi/broadcom/drivers/usi.git
	cd usi/
	git checkout -t origin/kk-amlogic -b kk-amlogic
else
	cd hardware/wifi/broadcom/drivers/usi
	echo "updating hardware/wifi/broadcom/drivers/usi"
	git pull
fi


# cd <work-directory>
cd ../../../../../
echo ""


################################################################################
#
# wifi realtek
#
################################################################################

if [ ! -d "hardware/wifi/realtek/drivers/8188eu" ]; then
	mkdir -p hardware/wifi/realtek/drivers/
	cd hardware/wifi/realtek/drivers/
	git clone git://git-sc.amlogic.com/platform/hardware/wifi/realtek/drivers/8188eu.git
	cd 8188eu/
	git checkout -t origin/8188eu -b 8188eu
else
	cd hardware/wifi/realtek/drivers/8188eu
	echo "updating hardware/wifi/realtek/drivers/8188eu"
	git pull
fi


# cd <work-directory>
cd ../../../../../
echo ""



if [ ! -d "hardware/wifi/realtek/drivers/8192cu" ]; then
	mkdir -p hardware/wifi/realtek/drivers/
	cd hardware/wifi/realtek/drivers/
	git clone git://git-sc.amlogic.com/platform/hardware/wifi/realtek/drivers/8192cu.git
	cd 8192cu/
	git checkout -t origin/8192cu -b 8192cu
else
	cd hardware/wifi/realtek/drivers/8192cu
	echo "updating hardware/wifi/realtek/drivers/8192cu"
	git pull
fi


# cd <work-directory>
cd ../../../../../
echo ""


if [ ! -d "hardware/wifi/realtek/drivers/8192du" ]; then
	mkdir -p hardware/wifi/realtek/drivers/
	cd hardware/wifi/realtek/drivers/
	git clone git://git-sc.amlogic.com/platform/hardware/wifi/realtek/drivers/8192du.git
	cd 8192du/
	git checkout -t origin/8192du -b 8192du
else
	cd hardware/wifi/realtek/drivers/8192du
	echo "updating hardware/wifi/realtek/drivers/8192du"
	git pull
fi


# cd <work-directory>
cd ../../../../../
echo ""


if [ ! -d "hardware/wifi/realtek/drivers/8192eu" ]; then
	mkdir -p hardware/wifi/realtek/drivers/
	cd hardware/wifi/realtek/drivers/
	git clone git://git-sc.amlogic.com/platform/hardware/wifi/realtek/drivers/8192eu.git
	cd 8192eu/
	git checkout -t origin/8192eu -b 8192eu
else
	cd hardware/wifi/realtek/drivers/8192eu
	echo "updating hardware/wifi/realtek/drivers/8192eu"
	git pull
fi


# cd <work-directory>
cd ../../../../../
echo ""


if [ ! -d "hardware/wifi/realtek/drivers/8189es" ]; then
	mkdir -p hardware/wifi/realtek/drivers/
	cd hardware/wifi/realtek/drivers/
	git clone git://git-sc.amlogic.com/platform/hardware/wifi/realtek/drivers/8189es.git
	cd 8189es/
	git checkout -t origin/kk-amlogic -b kk-amlogic
else
	cd hardware/wifi/realtek/drivers/8189es
	echo "updating hardware/wifi/realtek/drivers/8189es"
	git pull
fi


# cd <work-directory>
cd ../../../../../
echo ""


if [ ! -d "hardware/wifi/realtek/drivers/8723bs" ]; then
	mkdir -p hardware/wifi/realtek/drivers/
	cd hardware/wifi/realtek/drivers/
	git clone git://git-sc.amlogic.com/platform/hardware/wifi/realtek/drivers/8723bs.git
	cd 8723bs/
	git checkout -t origin/kk-amlogic -b kk-amlogic
else
	cd hardware/wifi/realtek/drivers/8723bs
	echo "updating hardware/wifi/realtek/drivers/8723bs"
	git pull
fi


# cd <work-directory>
cd ../../../../../
echo ""


if [ ! -d "hardware/wifi/realtek/drivers/8723au" ]; then
	mkdir -p hardware/wifi/realtek/drivers
	cd hardware/wifi/realtek/drivers
	git clone git://git-sc.amlogic.com/platform/hardware/wifi/realtek/drivers/8723au.git
	cd 8723au
	git checkout -t origin/8723au -b 8723au
else
	cd hardware/wifi/realtek/drivers/8723au
	echo "updating hardware/wifi/realtek/drivers/8723au"
	git pull
fi


# cd <work-directory>
cd ../../../../../
echo ""


if [ ! -d "hardware/wifi/realtek/drivers/8811au" ]; then
	mkdir -p hardware/wifi/realtek/drivers
	cd hardware/wifi/realtek/drivers
	git clone git://git-sc.amlogic.com/platform/hardware/wifi/realtek/drivers/8811au.git
	cd 8811au
	git checkout -t origin/8811au -b 8811au
else
	cd hardware/wifi/realtek/drivers/8811au
	echo "updating hardware/wifi/realtek/drivers/8811au"
	git pull
fi



# cd <work-directory>
cd ../../../../../
echo ""

echo "done"

