#
# arch/xtensa/boot/dts/Makefile
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
#

dtb-$(CONFIG_OF) += $(addsuffix .dtb, $(CONFIG_BUILTIN_DTB_NAME))

# for CONFIG_OF_ALL_DTBS test
dtb-	:= $(patsubst $(src)/%.dts,%.dtb, $(wildcard $(src)/*.dts))
