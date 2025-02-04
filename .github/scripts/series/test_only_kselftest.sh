#!/bin/bash
# SPDX-FileCopyrightText: 2023 Rivos Inc.
#
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

d=$(dirname "${BASH_SOURCE[0]}")
. $d/utils.sh
. $d/kselftest_prep.sh

$d/unpack_fw.sh
rc=0

kselftest_subtests=("kselftest-bpf" "kselftest-net" "kselftest-ftrace" "kselftest-acct" "kselftest-cachestat" "kselftest-capabilities" "kselftest-cgroup" "kselftest-clone3" "kselftest-connector" "kselftest-core" "kselftest-cpu-hotplug" "kselftest-cpufreq" "kselftest-damon" "kselftest-devices" "kselftest-drivers-dma-buf" "kselftest-dt" "kselftest-efivarfs" "kselftest-exec" "kselftest-fchmodat2" "kselftest-filesystems" "kselftest-filesystems-binderfs" "kselftest-firmware" "kselftest-fpu" "kselftest-futex" "kselftest-gpio" "kselftest-hid" "kselftest-ipc" "kselftest-ir" "kselftest-iommu" "kselftest-kcmp" "kselftest-kexec" "kselftest-kvm" "kselftest-landlock" "kselftest-lib" "kselftest-livepatch" "kselftest-lsm" "kselftest-membarrier" "kselftest-memfd" "kselftest-memory-hotplug" "kselftest-mincore" "kselftest-mount" "kselftest-mqueue" "kselftest-nci" "kselftest-nsfs" "kselftest-openat2" "kselftest-pid_namespace" "kselftest-pidfd" "kselftest-proc" "kselftest-pstore" "kselftest-ptrace" "kselftest-resctrl" "kselftest-riscv" "kselftest-rlimits" "kselftest-rseq" "kselftest-rtc" "kselftest-rust" "kselftest-seccomp" "kselftest-sigaltstack" "kselftest-signal" "kselftest-size" "kselftest-splice" "kselftest-static_keys" "kselftest-sync" "kselftest-sysctl" "kselftest-tc-testing" "kselftest-tdx" "kselftest-timens" "kselftest-timers" "kselftest-tmpfs" "kselftest-tpm2" "kselftest-tty" "kselftest-uevent" "kselftest-user" "kselftest-zram")

parallel_log=$(mktemp -p ${ci_root})

for subtest in "${kselftest_subtests[@]}"; do
    echo "${d}/kernel_tester.sh rv64 ${subtest} plain gcc ubuntu"
done | parallel -j$(($(nproc)/8)) --colsep ' ' --joblog ${parallel_log} || true

cat ${parallel_log}
rm ${parallel_log}
