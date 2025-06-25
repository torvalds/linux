#!/usr/bin/env bash

make O=~/build/early-complete/ -j$(nproc)

sudo make O=~/build/early-complete/ install

sudo reboot
