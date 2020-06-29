#
# arch/x86/realmode/Makefile
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
#

# Sanitizer runtimes are unavailable and cannot be linked here.
KASAN_SANITIZE			:= n
KCSAN_SANITIZE			:= n
OBJECT_FILES_NON_STANDARD	:= y

subdir- := rm

obj-y += init.o
obj-y += rmpiggy.o

$(obj)/rmpiggy.o: $(obj)/rm/realmode.bin

$(obj)/rm/realmode.bin: FORCE
	$(Q)$(MAKE) $(build)=$(obj)/rm $@
