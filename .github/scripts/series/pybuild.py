#!/usr/bin/env python3
# -*- coding: utf-8 -*-
import os
import sys
import logging
import argparse
import subprocess
import time
import re

from datetime import datetime

def main():
    # List of commands to run
    cmd = [["/ci/.github/scripts/series/kernel_builder_new.sh", "rv64", "nommu_k210_sdcard_defconfig", "plain", "gcc"]]
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv64", "nommu_k210_sdcard_defconfig", "plain", "llvm"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv64", "nommu_k210_defconfig", "plain", "gcc"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv64", "nommu_k210_defconfig", "plain", "llvm"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv32", "nommu_virt_defconfig", "plain", "gcc"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv32", "nommu_virt_defconfig", "plain", "llvm"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv64", "nommu_virt_defconfig", "plain", "gcc"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv64", "nommu_virt_defconfig", "plain", "llvm"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv32", "defconfig", "plain", "gcc"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv32", "defconfig", "plain", "llvm"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv32", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/randomize_base", "gcc"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv32", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/randomize_base", "llvm"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv32", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/flatmem", "gcc"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv32", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/flatmem", "llvm"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv32", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/vmap_stack", "gcc"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv32", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/vmap_stack", "llvm"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv32", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/preempt", "gcc"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv32", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/preempt", "llvm"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv32", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/kasan", "gcc"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv32", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/kasan", "llvm"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv32", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/strict_rwx", "gcc"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv32", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/strict_rwx", "llvm"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv32", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/medany", "gcc"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv32", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/medany", "llvm"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv32", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/medlow", "gcc"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv32", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/medlow", "llvm"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv32", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/kasan_sparsemem_vmemmmap", "gcc"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv32", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/kasan_sparsemem_vmemmmap", "llvm"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv32", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/kfence", "gcc"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv32", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/kfence", "llvm"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv32", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/ticket_spinlock", "gcc"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv32", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/ticket_spinlock", "llvm"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv32", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/kasan_sparsemem_novmemmmap", "gcc"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv32", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/kasan_sparsemem_novmemmmap", "llvm"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv32", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/nosmp", "gcc"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv32", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/nosmp", "llvm"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv32", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/size", "gcc"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv32", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/size", "llvm"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv32", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/legacy_sbi", "gcc"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv32", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/legacy_sbi", "llvm"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv32", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/pmu", "gcc"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv32", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/pmu", "llvm"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv32", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/rseq", "gcc"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv32", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/rseq", "llvm"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv32", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/early_boot_alternative", "gcc"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv32", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/early_boot_alternative", "llvm"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv32", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/kasan_vmalloc", "gcc"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv32", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/kasan_vmalloc", "llvm"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv32", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/sparsemem", "gcc"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv32", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/sparsemem", "llvm"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv32", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/rseq_debug", "gcc"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv32", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/rseq_debug", "llvm"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv32", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/preempt_rt", "gcc"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv32", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/preempt_rt", "llvm"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv32", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/noc", "gcc"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv32", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/noc", "llvm"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv32", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/early_boot_alternative_reloc", "gcc"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv32", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/early_boot_alternative_reloc", "llvm"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv32", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/spinwait", "gcc"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv32", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/spinwait", "llvm"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv32", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/svnapot", "gcc"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv32", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/svnapot", "llvm", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "15:05:48", "[13/77]"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv32", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/qspinlock", "gcc"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv32", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/qspinlock", "llvm"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv32", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/lockdep", "gcc"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv32", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/lockdep", "llvm"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv32", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/kasan_inline", "gcc"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv32", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/kasan_inline", "llvm"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv32", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/hardened_usercopy_slub", "gcc"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv32", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/hardened_usercopy_slub", "llvm"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv64", "defconfig", "plain", "gcc"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv64", "defconfig", "plain", "llvm"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv64", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/randomize_base", "gcc"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv64", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/randomize_base", "llvm"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv64", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/flatmem", "gcc"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv64", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/flatmem", "llvm"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv64", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/vmap_stack", "gcc"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv64", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/vmap_stack", "llvm"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv64", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/preempt", "gcc"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv64", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/preempt", "llvm"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv64", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/kasan", "gcc"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv64", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/kasan", "llvm"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv64", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/strict_rwx", "gcc"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv64", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/strict_rwx", "llvm"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv64", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/medany", "gcc"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv64", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/medany", "llvm"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv64", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/medlow", "gcc"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv64", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/medlow", "llvm"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv64", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/kasan_sparsemem_vmemmmap", "gcc"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv64", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/kasan_sparsemem_vmemmmap", "llvm"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv64", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/kfence", "gcc"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv64", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/kfence", "llvm"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv64", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/ticket_spinlock", "gcc"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv64", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/ticket_spinlock", "llvm"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv64", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/kasan_sparsemem_novmemmmap", "gcc"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv64", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/kasan_sparsemem_novmemmmap", "llvm"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv64", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/nosmp", "gcc"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv64", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/nosmp", "llvm"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv64", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/size", "gcc"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv64", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/size", "llvm"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv64", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/legacy_sbi", "gcc"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv64", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/legacy_sbi", "llvm"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv64", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/pmu", "gcc"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv64", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/pmu", "llvm"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv64", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/rseq", "gcc"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv64", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/rseq", "llvm"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv64", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/early_boot_alternative", "gcc"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv64", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/early_boot_alternative", "llvm"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv64", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/kasan_vmalloc", "gcc"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv64", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/kasan_vmalloc", "llvm"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv64", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/sparsemem", "gcc"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv64", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/sparsemem", "llvm"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv64", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/rseq_debug", "gcc"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv64", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/rseq_debug", "llvm"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv64", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/preempt_rt", "gcc"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv64", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/preempt_rt", "llvm"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv64", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/noc", "gcc"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv64", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/noc", "llvm"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv64", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/early_boot_alternative_reloc", "gcc"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv64", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/early_boot_alternative_reloc", "llvm"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv64", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/spinwait", "gcc"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv64", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/spinwait", "llvm"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv64", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/svnapot", "gcc"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv64", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/svnapot", "llvm"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv64", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/qspinlock", "gcc"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv64", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/qspinlock", "llvm"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv64", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/lockdep", "gcc"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv64", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/lockdep", "llvm"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv64", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/kasan_inline", "gcc"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv64", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/kasan_inline", "llvm"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv64", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/hardened_usercopy_slub", "gcc"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv64", "defconfig", "/ci/.github/scripts/series/kconfigs/defconfig/hardened_usercopy_slub", "llvm"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv32", "allmodconfig", "plain", "gcc"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv32", "allmodconfig", "plain", "llvm"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv64", "allmodconfig", "plain", "gcc"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv64", "allmodconfig", "plain", "llvm"])
    cmd.append(["/ci/.github/scripts/series/kernel_builder_new.sh", "rv64", "allmodconfig", "plain", "gcc-old"])

    concurrent = 79
    processes = []
    done = []

    while True:
        for i, process in enumerate(processes):
            if process.poll() is not None:
                # Process has completed
                output, error = process.communicate()
                print(f"Process completed {output.decode()}")
                done.append(process)

        processes = list(set(processes) - set(done))
                
        # Can we add more?
        if len(cmd) > 0 and len(processes) < concurrent:
            space = concurrent - len(processes)
            if space >= len(cmd):
                conc = space / len(cmd)
                for i in cmd:
                    print(f"Turbo {conc}")
                    i.append(str(conc))
                    processes.append(subprocess.Popen(i, stdout=subprocess.PIPE, stderr=subprocess.PIPE))
                cmd = []
            else:
                start = cmd[:space]
                cmd = cmd[space:]
                for i in start:
                    i.append(str(1))
                    processes.append(subprocess.Popen(i, stdout=subprocess.PIPE, stderr=subprocess.PIPE))
            
        if len(processes) == 0:
            break
        time.sleep(0.1)

if __name__ == "__main__":
    main()

