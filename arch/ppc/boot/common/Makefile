#
# arch/ppc/boot/common/Makefile
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Tom Rini	January 2001
#

lib-y					:= string.o util.o misc-common.o \
						serial_stub.o bootinfo.o
lib-$(CONFIG_SERIAL_8250_CONSOLE)	+= ns16550.o
