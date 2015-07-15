# All DTBs
dtb-$(CONFIG_ATH79)			+= ar9132_tl_wr1043nd_v1.dtb

# Select a DTB to build in the kernel
obj-$(CONFIG_DTB_TL_WR1043ND_V1)	+= ar9132_tl_wr1043nd_v1.dtb.o

# Force kbuild to make empty built-in.o if necessary
obj-				+= dummy.o

always				:= $(dtb-y)
clean-files			:= *.dtb *.dtb.S
