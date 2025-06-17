#!/bin/bash
# SPDX-FileCopyrightText: 2024 Rivos Inc.
#
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

xlen=$1
config=$2
fragment=$3
toolchain=$4
rootfs=$5

rv64_cpus=(
    "rv64"
    "rv64,v=true,vlen=256,elen=64,h=true,zbkb=on,zbkc=on,zbkx=on,zkr=on,zkt=on,svinval=on,svnapot=on,svpbmt=on"
)

list_cpus+=( "sifive-u54" )


rv32_cpus=(
    "rv32"
)
