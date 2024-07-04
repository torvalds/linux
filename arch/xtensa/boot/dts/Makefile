#
# arch/xtensa/boot/dts/Makefile
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
#

obj-$(CONFIG_OF) += $(addsuffix .dtb.o, $(CONFIG_BUILTIN_DTB_SOURCE))

# for CONFIG_OF_ALL_DTBS test
dtb-	:= $(patsubst $(src)/%.dts,%.dtb, $(wildcard $(src)/*.dts))
