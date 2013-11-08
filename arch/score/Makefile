#
# arch/score/Makefile
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#

KBUILD_DEFCONFIG := spct6600_defconfig
CROSS_COMPILE := score-linux-

#
# CPU-dependent compiler/assembler options for optimization.
#
cflags-y += -G0 -pipe -mel -mnhwloop -D__SCOREEL__ \
	-D__linux__ -ffunction-sections -ffreestanding

#
# Board-dependent options and extra files
#
KBUILD_AFLAGS += $(cflags-y)
KBUILD_CFLAGS += $(cflags-y)
KBUILD_AFLAGS_MODULE +=
KBUILD_CFLAGS_MODULE +=
LDFLAGS += --oformat elf32-littlescore
LDFLAGS_vmlinux	+= -G0 -static -nostdlib

head-y := arch/score/kernel/head.o
libs-y += arch/score/lib/
core-y += arch/score/kernel/ arch/score/mm/

boot := arch/score/boot

vmlinux.bin: vmlinux
	$(Q)$(MAKE) $(build)=$(boot) $(boot)/$@

archclean:
	@$(MAKE) $(clean)=$(boot)

define archhelp
	echo '  vmlinux.bin          - Raw binary boot image'
	echo
	echo '  These will be default as appropriate for a configured platform.'
endef
