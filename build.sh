#!/usr/bin/env bash

make O=/usr/src/linux-5.15-SPDIO -j$(nproc) # 변경분만 재컴파일
# sudo make modules_install
sudo make O=/usr/src/linux-5.15-SPDIO install # 모듈 → /lib/modules, vmlinuz → /boot
# sudo update-grub                        # 배포판에 따라 자동 호출될 수도 있음
sudo reboot
