#
# arch/xtensa/boot/dts/Makefile
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
#

BUILTIN_DTB := $(patsubst "%",%,$(CONFIG_BUILTIN_DTB)).dtb.o
ifneq ($(CONFIG_BUILTIN_DTB),"")
obj-$(CONFIG_OF) += $(BUILTIN_DTB)
endif

# for CONFIG_OF_ALL_DTBS test
dtstree	:= $(srctree)/$(src)
dtb-	:= $(patsubst $(dtstree)/%.dts,%.dtb, $(wildcard $(dtstree)/*.dts))
