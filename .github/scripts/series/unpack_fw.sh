#!/bin/bash
# SPDX-FileCopyrightText: 2023 Rivos Inc.
#
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail
shopt -s extglob

d=$(dirname "${BASH_SOURCE[0]}")
. $d/utils.sh

firmware_dir=${ci_root}/firmware

fw_rv32_opensbi=$(echo ${ci_fw_root}/firmware_rv32_opensbi_+([a-f0-9]).tar.zst)
fw_rv64_opensbi=$(echo ${ci_fw_root}/firmware_rv64_opensbi_+([a-f0-9]).tar.zst)
fw_rv64_uboot=$(echo ${ci_fw_root}/firmware_rv64_uboot_+([a-f0-9]).tar.zst)
fw_rv64_uboot_acpi=$(echo ${ci_fw_root}/firmware_rv64_uboot_acpi_+([a-f0-9]).tar.zst)

mkdir -p ${firmware_dir}/rv32
mkdir -p ${firmware_dir}/rv64

tar -C ${firmware_dir}/rv32 -xf $fw_rv32_opensbi
tar -C ${firmware_dir}/rv64 -xf $fw_rv64_opensbi
tar -C ${firmware_dir}/rv64 -xf $fw_rv64_uboot
tar -C ${firmware_dir}/rv64 -xf $fw_rv64_uboot_acpi
