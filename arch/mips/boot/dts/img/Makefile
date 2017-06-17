dtb-$(CONFIG_MACH_PISTACHIO)	+= pistachio_marduk.dtb
obj-$(CONFIG_MACH_PISTACHIO)	+= pistachio_marduk.dtb.o

# Force kbuild to make empty built-in.o if necessary
obj-				+= dummy.o

always				:= $(dtb-y)
clean-files			:= *.dtb *.dtb.S
