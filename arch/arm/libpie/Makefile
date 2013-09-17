#
# linux/arch/arm/libpie/Makefile
#
ccflags-y	:= -fpic -mno-single-pic-base -fno-builtin

obj-y		:= relocate.o empty.o
obj-y		+= lib1funcs.o ashldi3.o string.o

# string library code (-Os is enforced to keep it much smaller)
string = $(obj)/string.o
CFLAGS_string.o := -Os

$(obj)/string.c: $(srctree)/arch/$(SRCARCH)/boot/compressed/string.c
	$(call cmd,shipped)

# For __aeabi_uidivmod
lib1funcs = $(obj)/lib1funcs.o

$(obj)/lib1funcs.S: $(srctree)/arch/$(SRCARCH)/lib/lib1funcs.S
	$(call cmd,shipped)

# For __aeabi_llsl
ashldi3 = $(obj)/ashldi3.o

$(obj)/ashldi3.S: $(srctree)/arch/$(SRCARCH)/lib/ashldi3.S
	$(call cmd,shipped)

$(obj)/libpie.o: $(string) $(lib1funcs) $(ashldi3) $(addprefix $(obj)/,$(OBJS))
	$(call if_changed,ld)

# Make sure files are removed during clean
extra-y		+= string.c lib1funcs.S ashldi3.S
