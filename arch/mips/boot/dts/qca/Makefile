# All DTBs
dtb-$(CONFIG_ATH79)			+= ar9132_tl_wr1043nd_v1.dtb

# Force kbuild to make empty built-in.o if necessary
obj-				+= dummy.o

always				:= $(dtb-y)
clean-files			:= *.dtb *.dtb.S
