dtb-$(CONFIG_CAVIUM_OCTEON_SOC)	+= octeon_3xxx.dtb octeon_68xx.dtb

obj-y				+= $(patsubst %.dtb, %.dtb.o, $(dtb-y))

# Force kbuild to make empty built-in.o if necessary
obj-				+= dummy.o

always				:= $(dtb-y)
clean-files			:= *.dtb *.dtb.S
