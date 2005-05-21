hostprogs-y		:= mk_ptregs
always			:= $(hostprogs-y)

mk_ptregs-objs := mk_ptregs-$(SUBARCH).o
HOSTCFLAGS_mk_ptregs-$(SUBARCH).o := -I$(objtree)/arch/um
