#!/bin/bash

# Please source me!

git config --global user.email "you@example.com"
git config --global user.name "Your Name"

export PATH=$(echo $PATH | tr : "\n"| grep -v ^/opt | tr "\n" :)

export CI_TRIPLE=riscv64-linux-gnu
