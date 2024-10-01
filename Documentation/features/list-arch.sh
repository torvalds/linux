# SPDX-License-Identifier: GPL-2.0
#
# Small script that visualizes the kernel feature support status
# of an architecture.
#
# (If no arguments are given then it will print the host architecture's status.)
#

ARCH=${1:-$(uname -m | sed 's/x86_64/x86/' | sed 's/i386/x86/')}

$(dirname $0)/../../scripts/get_feat.pl list --arch $ARCH
