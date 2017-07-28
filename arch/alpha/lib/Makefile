#
# Makefile for alpha-specific library files..
#

asflags-y := $(KBUILD_CFLAGS)
ccflags-y := -Werror

# Many of these routines have implementations tuned for ev6.
# Choose them iff we're targeting ev6 specifically.
ev6-$(CONFIG_ALPHA_EV6) := ev6-

# Several make use of the cttz instruction introduced in ev67.
ev67-$(CONFIG_ALPHA_EV67) := ev67-

lib-y =	__divqu.o __remqu.o __divlu.o __remlu.o \
	udelay.o \
	$(ev6-y)memset.o \
	$(ev6-y)memcpy.o \
	memmove.o \
	checksum.o \
	csum_partial_copy.o \
	$(ev67-y)strlen.o \
	$(ev67-y)strcat.o \
	strcpy.o \
	$(ev67-y)strncat.o \
	strncpy.o \
	$(ev6-y)stxcpy.o \
	$(ev6-y)stxncpy.o \
	$(ev67-y)strchr.o \
	$(ev67-y)strrchr.o \
	$(ev6-y)memchr.o \
	$(ev6-y)copy_user.o \
	$(ev6-y)clear_user.o \
	$(ev6-y)csum_ipv6_magic.o \
	$(ev6-y)clear_page.o \
	$(ev6-y)copy_page.o \
	fpreg.o \
	callback_srm.o srm_puts.o srm_printk.o \
	fls.o

lib-$(CONFIG_SMP) += dec_and_lock.o

# The division routines are built from single source, with different defines.
AFLAGS___divqu.o = -DDIV
AFLAGS___remqu.o =       -DREM
AFLAGS___divlu.o = -DDIV       -DINTSIZE
AFLAGS___remlu.o =       -DREM -DINTSIZE

$(addprefix $(obj)/,__divqu.o __remqu.o __divlu.o __remlu.o): \
						$(src)/$(ev6-y)divide.S FORCE
	$(call if_changed_rule,as_o_S)
