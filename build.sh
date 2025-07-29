#!/usr/bin/env bash

sudo cp /boot/config-5.16.0 ~/build/linux-5.15-SPDIO/.config
make O=~/build/linux-5.15-SPDIO/ olddefconfig
make O=~/build/linux-5.15-SPDIO -j$(nproc) # 변경분만 재컴파일
sudo make O=~/build/linux-5.15-SPDIO headers_install INSTALL_HDR_DIR=/usr
sudo make O=~/build/linux-5.15-SPDIO modules_install
sudo make O=~/build/linux-5.15-SPDIO install # 모듈 → /lib/modules, vmlinuz → /boot
# sudo update-grub                        # 배포판에 따라 자동 호출될 수도 있음

sudo reboot
