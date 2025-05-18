#!/bin/bash
# SPDX-FileCopyrightText: 2024 Rivos Inc.
#
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

qemu_subtests=()

generate_qemu_subtests() {
    local xlen=$1
    local config=$2
    local fragment=$3
    local toolchain=$4
    local rootfs=$5

    local n=$(gen_kernel_name $xlen $config $fragment $toolchain)
    local cpu_sifive=0
    local fw_uefi=0
    local hw_acpi=0
    local kernel_config=$(find ${ci_root}/${n} -name 'config-*' 2>/dev/null || echo "/dev/null")

    qemu_subtests=()

    if [[ $xlen == "rv64" ]]; then
	if ls ${kernel_config} &> /dev/null; then
	    if grep -q 'CONFIG_RISCV_ALTERNATIVE_EARLY=y' ${kernel_config}; then
		cpu_sifive=1
	    fi
	    if grep -q 'CONFIG_EFI=y' ${kernel_config}; then
		fw_uefi=1
	    fi
	    if grep -q 'CONFIG_ACPI=y' ${kernel_config}; then
		hw_acpi=1
	    fi
	fi

	for cpu in default64 server64 max64 sifive; do
	    if [[ $cpu =~ sifive ]]; then
		    if ! (( ${cpu_sifive} )); then
			continue
		    fi
	    fi

	    for fw in no_uefi uboot_uefi; do
		if ! [[ $fw == no_uefi ]]; then
		    if ! (( ${fw_uefi} )); then
			continue
		    fi

		    if [[ $fw == edk2_uefi ]]; then
			continue
		    fi
		fi

		# For now, we're only doing selftest on DT.
		if [[ $config =~ ^kselftest ]]; then
			if [[ $cpu == server64 && $fw == uboot_uefi && $rootfs == ubuntu ]]; then
				if (( ${ci_test_selftests} )); then
					qemu_subtests+=( "$cpu $fw dt $config" )
				fi
			fi
			continue
		fi

		for hw in dt acpi; do
		    if [[ $hw == acpi ]]; then
			if [[ $fw == no_uefi ]]; then
			    continue
			fi

			if ! (( ${hw_acpi} )); then
			    continue
			fi
		    fi

		    qemu_subtests+=( "$cpu $fw $hw boot" )

		done
	    done
	done
    else
	qemu_subtests+=( "default32 no_uefi dt boot" )
    fi
    return 0
}

get_qemu_test_name() {
    local cpu=$1
    local fw=$2
    local hw=$3
    local tst=$4

    echo "${cpu}__${fw}__${hw}__${tst}"
}
