#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
set -euo pipefail

ln -svf "$(which clang++-${LLVM_VERSION})"      /usr/bin/clang++
ln -svf "$(which clang-${LLVM_VERSION})"        /usr/bin/clang
ln -svf "$(which ld.lld-${LLVM_VERSION})"       /usr/bin/ld.lld
ln -svf "$(which lld-${LLVM_VERSION})"          /usr/bin/lld
ln -svf "$(which llvm-ar-${LLVM_VERSION})"      /usr/bin/llvm-ar
ln -svf "$(which llvm-nm-${LLVM_VERSION})"      /usr/bin/llvm-nm
ln -svf "$(which llvm-objcopy-${LLVM_VERSION})" /usr/bin/llvm-objcopy
ln -svf "$(which llvm-readelf-${LLVM_VERSION})" /usr/bin/llvm-readelf
ln -svf "$(which llvm-strip-${LLVM_VERSION})"   /usr/bin/llvm-strip
